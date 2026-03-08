#include "include/monitor_parity_checker_tests.hpp"

#include "monitor/debug_probe_core.hpp"
#include "monitor/monitor_parity_checker.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QtTest/QtTest>

namespace {

QJsonObject make_protocol_message(
    const QString& family, const QString& session_id, const QJsonObject& payload
) {
    const debug_probe_core::protocol_identity identity {
        .app_name = QStringLiteral("cppr"),
        .process_id = 777,
        .session_id = session_id,
        .build_id = QStringLiteral("build-dev"),
        .protocol_version = debug_probe_core::protocol_version_string(),
        .debug_flags = QStringList() << QStringLiteral("debug_build"),
        .instrumentation_mode = QStringLiteral("realistic"),
    };
    return debug_probe_core::build_protocol_message_v1(
        family, identity, 10, payload
    );
}

QJsonObject make_embedded_snapshot_export(
    qint64 cache_accounted_ready_bytes, qint64 widget_local_estimated_bytes,
    qint64 process_rss_bytes
) {
    QJsonObject latest;
    latest.insert(
        QStringLiteral("cache_accounted_ready_bytes"),
        cache_accounted_ready_bytes
    );
    latest.insert(
        QStringLiteral("widget_local_display_bytes_estimated"),
        widget_local_estimated_bytes
    );
    latest.insert(
        QStringLiteral("process_memory_rss_bytes"), process_rss_bytes
    );
    latest.insert(
        QStringLiteral("measured_accounted_gap_bytes_derived"),
        process_rss_bytes - cache_accounted_ready_bytes
    );

    QJsonArray cache_timeline;
    cache_timeline.push_back(latest);

    QJsonObject root;
    root.insert(QStringLiteral("cache_timeline"), cache_timeline);
    return root;
}

QJsonObject make_sample_batch(
    qint64 cache_accounted_ready_bytes, qint64 widget_local_estimated_bytes,
    qint64 process_rss_bytes
) {
    QJsonArray samples;
    auto push_sample = [&samples](const QString& metric_id, qint64 value) {
        QJsonObject sample;
        sample.insert(QStringLiteral("metric_id"), metric_id);
        sample.insert(QStringLiteral("value"), value);
        samples.push_back(sample);
    };

    push_sample(
        QStringLiteral("cache_accounted_ready_bytes"),
        cache_accounted_ready_bytes
    );
    push_sample(
        QStringLiteral("widget_local_display_bytes_estimated"),
        widget_local_estimated_bytes
    );
    push_sample(QStringLiteral("process_memory_rss_bytes"), process_rss_bytes);
    push_sample(
        QStringLiteral("measured_accounted_gap_bytes_derived"),
        process_rss_bytes - cache_accounted_ready_bytes
    );

    QJsonObject payload;
    payload.insert(QStringLiteral("sample_count"), samples.size());
    payload.insert(QStringLiteral("samples"), samples);
    return payload;
}

QJsonObject make_capabilities_message(const QString& session_id) {
    QJsonObject payload;
    payload.insert(
        QStringLiteral("capabilities"),
        debug_probe_core::protocol_capabilities_v1()
    );
    return make_protocol_message(
        QStringLiteral("capabilities"), session_id, payload
    );
}

QJsonObject make_snapshot_message(
    const QString& session_id, qint64 cache_accounted_ready_bytes,
    qint64 widget_local_estimated_bytes, qint64 process_rss_bytes
) {
    QJsonObject snapshot;
    snapshot.insert(
        QStringLiteral("cache_accounted_ready_bytes"),
        cache_accounted_ready_bytes
    );
    snapshot.insert(
        QStringLiteral("widget_local_display_bytes_estimated"),
        widget_local_estimated_bytes
    );
    snapshot.insert(
        QStringLiteral("process_memory_rss_bytes"), process_rss_bytes
    );

    QJsonObject payload;
    payload.insert(QStringLiteral("snapshot"), snapshot);
    return make_protocol_message(
        QStringLiteral("snapshot"), session_id, payload
    );
}

} // namespace

void monitor_parity_checker_tests::
    aligned_embedded_and_external_payloads_have_no_warnings() {
    const qint64 cache_bytes = 80 * 1024 * 1024;
    const qint64 widget_bytes = 16 * 1024 * 1024;
    const qint64 rss_bytes = 180 * 1024 * 1024;
    const QString session_id = QStringLiteral("session-aligned");

    const QJsonObject embedded_snapshot
        = make_embedded_snapshot_export(cache_bytes, widget_bytes, rss_bytes);
    const QVector<QJsonObject> external_messages {
        make_capabilities_message(session_id),
        make_protocol_message(
            QStringLiteral("sample_batch"), session_id,
            make_sample_batch(cache_bytes, widget_bytes, rss_bytes)
        ),
        make_snapshot_message(session_id, cache_bytes, widget_bytes, rss_bytes),
    };

    const auto parity = monitor_parity_checker::
        compare_embedded_snapshot_and_external_messages(
            embedded_snapshot, external_messages
        );
    QVERIFY(parity.ok());
    QVERIFY(parity.warnings.isEmpty());
    QCOMPARE(parity.compared_message_count, qint64(3));
    QVERIFY(parity.compared_metric_count >= 4);
}

void monitor_parity_checker_tests::drifting_payloads_surface_warnings() {
    const qint64 embedded_cache_bytes = 64 * 1024 * 1024;
    const qint64 embedded_widget_bytes = 8 * 1024 * 1024;
    const qint64 embedded_rss_bytes = 160 * 1024 * 1024;
    const QString session_id = QStringLiteral("session-drift");

    const QJsonObject embedded_snapshot = make_embedded_snapshot_export(
        embedded_cache_bytes, embedded_widget_bytes, embedded_rss_bytes
    );
    const QVector<QJsonObject> external_messages {
        make_capabilities_message(session_id),
        make_protocol_message(
            QStringLiteral("sample_batch"), session_id,
            make_sample_batch(
                embedded_cache_bytes + (32 * 1024 * 1024),
                embedded_widget_bytes + (16 * 1024 * 1024),
                embedded_rss_bytes + (48 * 1024 * 1024)
            )
        ),
    };

    const auto parity = monitor_parity_checker::
        compare_embedded_snapshot_and_external_messages(
            embedded_snapshot, external_messages, 1 * 1024 * 1024
        );
    QVERIFY(!parity.ok());
    QVERIFY(!parity.warnings.isEmpty());
}
