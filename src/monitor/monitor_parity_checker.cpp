#include "monitor/monitor_parity_checker.hpp"

#include "monitor/debug_probe_core.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtGlobal>

#include <cmath>

namespace {

qint64 integer_like_value(const QJsonValue& value) {
    if (value.isDouble()) {
        return static_cast<qint64>(std::llround(value.toDouble()));
    }
    return value.toInteger();
}

void compare_metric_with_tolerance(
    const QString& metric_name, qint64 embedded_value, qint64 external_value,
    qint64 tolerance, monitor_parity_checker::parity_result* result
) {
    if (result == nullptr) {
        return;
    }
    if (embedded_value < 0 || external_value < 0) {
        result->warnings.push_back(
            QStringLiteral(
                "metric '%1' unavailable for parity comparison (embedded=%2 "
                "external=%3)"
            )
                .arg(metric_name)
                .arg(embedded_value)
                .arg(external_value)
        );
        return;
    }

    ++result->compared_metric_count;
    if (std::llabs(embedded_value - external_value) > tolerance) {
        result->warnings.push_back(
            QStringLiteral(
                "metric '%1' drift exceeds tolerance: embedded=%2 external=%3 "
                "tolerance=%4"
            )
                .arg(metric_name)
                .arg(embedded_value)
                .arg(external_value)
                .arg(tolerance)
        );
    }
}

} // namespace

monitor_parity_checker::parity_result
monitor_parity_checker::compare_embedded_snapshot_and_external_messages(
    const QJsonObject& embedded_snapshot_export,
    const QVector<QJsonObject>& external_messages, qint64 byte_tolerance
) {
    parity_result result;
    result.compared_message_count = external_messages.size();

    const QJsonArray cache_timeline
        = embedded_snapshot_export.value(QStringLiteral("cache_timeline"))
              .toArray();
    if (cache_timeline.isEmpty()) {
        result.warnings.push_back(
            QStringLiteral("embedded snapshot has no cache_timeline entries")
        );
        return result;
    }

    const QJsonObject embedded_latest
        = cache_timeline.at(cache_timeline.size() - 1).toObject();
    const qint64 embedded_cache_accounted = integer_like_value(
        embedded_latest.value(QStringLiteral("cache_accounted_ready_bytes"))
    );
    const qint64 embedded_widget_estimated
        = integer_like_value(embedded_latest.value(
            QStringLiteral("widget_local_display_bytes_estimated")
        ));
    const qint64 embedded_process_rss = integer_like_value(
        embedded_latest.value(QStringLiteral("process_memory_rss_bytes"))
    );
    const qint64 embedded_gap = integer_like_value(embedded_latest.value(
        QStringLiteral("measured_accounted_gap_bytes_derived")
    ));

    debug_probe_core::metric_point_v1 external_point;
    bool saw_capabilities = false;

    for (const QJsonObject& message : external_messages) {
        const QJsonObject protocol
            = message.value(QStringLiteral("protocol_v1")).toObject();
        const QString family
            = protocol.value(QStringLiteral("message_family")).toString();

        if (family == QStringLiteral("capabilities")) {
            saw_capabilities = true;
            const QJsonObject capabilities
                = message.value(QStringLiteral("capabilities")).toObject();
            const QJsonArray incoming_catalog
                = capabilities.value(QStringLiteral("metric_catalog"))
                      .toArray();

            QHash<QString, QJsonObject> incoming_by_id;
            for (const QJsonValue& entry : incoming_catalog) {
                const QJsonObject metric = entry.toObject();
                const QString id
                    = metric.value(QStringLiteral("id")).toString();
                if (!id.isEmpty()) {
                    incoming_by_id.insert(id, metric);
                }
            }

            const QJsonArray expected_catalog
                = debug_probe_core::protocol_metric_catalog_v1();
            for (const QJsonValue& expected_value : expected_catalog) {
                const QJsonObject expected = expected_value.toObject();
                const QString id
                    = expected.value(QStringLiteral("id")).toString();
                if (id.isEmpty()) {
                    continue;
                }
                if (!incoming_by_id.contains(id)) {
                    result.warnings.push_back(
                        QStringLiteral(
                            "capabilities missing expected metric '%1'"
                        )
                            .arg(id)
                    );
                    continue;
                }
                const QString incoming_provenance
                    = incoming_by_id.value(id)
                          .value(QStringLiteral("provenance"))
                          .toString();
                const QString expected_provenance
                    = expected.value(QStringLiteral("provenance")).toString();
                if (incoming_provenance != expected_provenance) {
                    result.warnings.push_back(
                        QStringLiteral(
                            "metric '%1' provenance drift: incoming='%2' "
                            "expected='%3'"
                        )
                            .arg(id, incoming_provenance, expected_provenance)
                    );
                }
            }
            continue;
        }

        if (family == QStringLiteral("sample_batch")) {
            const QJsonArray samples
                = message.value(QStringLiteral("samples")).toArray();
            debug_probe_core::merge_metric_point_v1(
                &external_point,
                debug_probe_core::metric_point_from_sample_batch_v1(samples)
            );
            continue;
        }

        if (family == QStringLiteral("snapshot")) {
            const QJsonObject snapshot
                = message.value(QStringLiteral("snapshot")).toObject();
            debug_probe_core::merge_metric_point_v1(
                &external_point,
                debug_probe_core::metric_point_from_snapshot_payload_v1(
                    snapshot
                )
            );
        }
    }

    if (!saw_capabilities) {
        result.warnings.push_back(QStringLiteral(
            "external trace did not include capabilities message"
        ));
    }

    compare_metric_with_tolerance(
        QStringLiteral("cache_accounted_ready_bytes"), embedded_cache_accounted,
        external_point.cache_accounted_ready_bytes, byte_tolerance, &result
    );
    compare_metric_with_tolerance(
        QStringLiteral("widget_local_display_bytes_estimated"),
        embedded_widget_estimated,
        external_point.widget_local_display_bytes_estimated, byte_tolerance,
        &result
    );
    compare_metric_with_tolerance(
        QStringLiteral("process_memory_rss_bytes"), embedded_process_rss,
        external_point.process_memory_rss_bytes, byte_tolerance, &result
    );
    compare_metric_with_tolerance(
        QStringLiteral("measured_accounted_gap_bytes_derived"), embedded_gap,
        external_point.measured_accounted_gap_bytes_derived, byte_tolerance,
        &result
    );

    return result;
}

QVector<QJsonObject> monitor_parity_checker::parse_external_history_jsonl(
    const QByteArray& jsonl_data
) {
    QVector<QJsonObject> messages;
    const QList<QByteArray> lines = jsonl_data.split('\n');
    for (const QByteArray& raw_line : lines) {
        const QByteArray line = raw_line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (document.isObject()) {
            messages.push_back(document.object());
        }
    }
    return messages;
}
