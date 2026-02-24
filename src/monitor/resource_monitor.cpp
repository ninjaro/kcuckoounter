#include "monitor/resource_monitor.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent>
#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace {

int size_to_int(qsizetype value) {
    return value > static_cast<qsizetype>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(value);
}

QString cadence_mode_to_string(resource_monitor::debug_cadence_mode mode) {
    switch (mode) {
    case resource_monitor::debug_cadence_mode::instrumented:
        return QStringLiteral("instrumented");
    case resource_monitor::debug_cadence_mode::realistic:
    default:
        return QStringLiteral("realistic");
    }
}

QString write_debug_snapshot_json(
    const QString& output_path,
    const resource_monitor::export_request_metadata& metadata,
    const QVector<resource_monitor::cache_timeline_entry>& cache_entries,
    const QVector<resource_monitor::event_timeline_entry>& event_entries,
    resource_monitor::debug_cadence_mode export_mode
) {
    QJsonObject root;
    root.insert(
        QStringLiteral("collector_sequence"), metadata.collector_sequence
    );
    root.insert(
        QStringLiteral("cache_timeline_size"), metadata.cache_timeline_size
    );
    root.insert(
        QStringLiteral("event_timeline_size"), metadata.event_timeline_size
    );
    root.insert(
        QStringLiteral("debug_cadence_mode"),
        cadence_mode_to_string(export_mode)
    );

    QJsonArray cache_array;
    for (const auto& entry : cache_entries) {
        QJsonObject object;
        object.insert(
            QStringLiteral("collector_sequence"), entry.collector_sequence
        );
        object.insert(
            QStringLiteral("snapshot_sequence"),
            entry.cache_snapshot.snapshot_sequence
        );
        object.insert(
            QStringLiteral("ready_entries"), entry.cache_snapshot.ready_entries
        );
        object.insert(
            QStringLiteral("ready_bytes"),
            static_cast<qint64>(entry.cache_snapshot.ready_bytes)
        );
        object.insert(
            QStringLiteral("ready_images"), entry.cache_snapshot.ready_images
        );
        object.insert(
            QStringLiteral("displayed_ready_entries"),
            entry.cache_snapshot.displayed_ready_entries
        );
        object.insert(
            QStringLiteral("cached_only_ready_entries"),
            entry.cache_snapshot.cached_only_ready_entries
        );
        object.insert(
            QStringLiteral("displayed_ready_images"),
            entry.cache_snapshot.displayed_ready_images
        );
        object.insert(
            QStringLiteral("cached_only_ready_images"),
            entry.cache_snapshot.cached_only_ready_images
        );
        object.insert(
            QStringLiteral("displayed_entry_window_ms"),
            static_cast<qint64>(entry.cache_snapshot.displayed_entry_window_ms)
        );
        object.insert(
            QStringLiteral("displayed_entry_coverage_percent"),
            entry.cache_snapshot.displayed_entry_coverage_percent
        );

        QJsonArray bucket_array;
        for (const auto& bucket : entry.cache_snapshot.size_buckets) {
            QJsonObject bucket_object;
            bucket_object.insert(
                QStringLiteral("target_bucket_px"), bucket.target_bucket_px
            );
            bucket_object.insert(
                QStringLiteral("entry_count"), bucket.entry_count
            );
            bucket_object.insert(
                QStringLiteral("total_bytes"),
                static_cast<qint64>(bucket.total_bytes)
            );
            bucket_array.push_back(bucket_object);
        }
        object.insert(
            QStringLiteral("unique_size_buckets"),
            entry.cache_snapshot.unique_size_buckets
        );
        object.insert(QStringLiteral("size_buckets"), bucket_array);

        QJsonArray largest_array;
        for (const auto& largest : entry.cache_snapshot.largest_entries) {
            QJsonObject largest_object;
            largest_object.insert(
                QStringLiteral("target_bucket_px"), largest.target_bucket_px
            );
            largest_object.insert(
                QStringLiteral("estimated_bytes"),
                static_cast<qint64>(largest.estimated_bytes)
            );
            largest_array.push_back(largest_object);
        }
        object.insert(QStringLiteral("largest_entries"), largest_array);

        QJsonArray requested_array;
        for (const auto& requested :
             entry.cache_snapshot.top_requested_entries) {
            QJsonObject requested_object;
            requested_object.insert(
                QStringLiteral("source_id"), requested.source_id
            );
            requested_object.insert(
                QStringLiteral("render_scope"), requested.render_scope
            );
            requested_object.insert(
                QStringLiteral("target_bucket_px"), requested.target_bucket_px
            );
            requested_object.insert(
                QStringLiteral("request_count"), requested.request_count
            );
            requested_array.push_back(requested_object);
        }
        object.insert(QStringLiteral("top_requested_entries"), requested_array);

        QJsonArray expensive_array;
        for (const auto& expensive : entry.cache_snapshot.top_expensive_tasks) {
            QJsonObject expensive_object;
            expensive_object.insert(
                QStringLiteral("stage"),
                expensive.stage
                        == raster_cache::debug_snapshot::timing_stage::
                            coalesced_wait
                    ? QStringLiteral("coalesced_wait")
                    : QStringLiteral("raster_lifecycle")
            );
            expensive_object.insert(
                QStringLiteral("source_id"), expensive.source_id
            );
            expensive_object.insert(
                QStringLiteral("render_scope"), expensive.render_scope
            );
            expensive_object.insert(
                QStringLiteral("target_bucket_px"), expensive.target_bucket_px
            );
            expensive_object.insert(
                QStringLiteral("completed_samples"), expensive.completed_samples
            );
            expensive_object.insert(
                QStringLiteral("avg_elapsed_ms"),
                static_cast<qint64>(expensive.avg_elapsed_ms)
            );
            expensive_object.insert(
                QStringLiteral("max_elapsed_ms"),
                static_cast<qint64>(expensive.max_elapsed_ms)
            );
            expensive_array.push_back(expensive_object);
        }
        object.insert(QStringLiteral("top_expensive_tasks"), expensive_array);
        cache_array.push_back(object);
    }
    root.insert(QStringLiteral("cache_timeline"), cache_array);

    QJsonArray event_array;
    for (const auto& entry : event_entries) {
        QJsonObject object;
        object.insert(
            QStringLiteral("collector_sequence"), entry.collector_sequence
        );
        object.insert(QStringLiteral("timestamp_ms"), entry.timestamp_ms);
        object.insert(
            QStringLiteral("kind"),
            entry.kind
                    == resource_monitor::event_timeline_entry::event_kind::
                        cache_snapshot
                ? QStringLiteral("cache_snapshot")
                : QStringLiteral("manual_marker")
        );
        object.insert(QStringLiteral("label"), entry.label);
        event_array.push_back(object);
    }
    root.insert(QStringLiteral("event_timeline"), event_array);

    const QJsonDocument document(root);
    QFile file(output_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QStringLiteral("unable to open output file");
    }

    const qint64 written = file.write(document.toJson(QJsonDocument::Indented));
    if (written < 0) {
        return QStringLiteral("unable to write output file");
    }

    return QString();
}

} // namespace

resource_monitor::resource_monitor(QObject* parent, int max_timeline_entries)
    : QObject(parent)
    , observed_cache_service(nullptr)
    , timeline_limit(std::max(1, max_timeline_entries))
    , collector_sequence(0)
    , cache_timeline_entries()
    , event_timeline_entries()
    , last_export_metadata()
    , active_export_watcher(nullptr)
    , cadence_mode(debug_cadence_mode::realistic) {
    cache_timeline_entries.reserve(timeline_limit);
    event_timeline_entries.reserve(timeline_limit);
}

void resource_monitor::attach_cache_service(raster_cache* cache_service) {
    if (observed_cache_service == cache_service) {
        return;
    }

    if (observed_cache_service != nullptr) {
        QObject::disconnect(
            observed_cache_service, &raster_cache::debug_snapshot_updated, this,
            &resource_monitor::on_cache_snapshot_updated
        );
    }

    observed_cache_service = cache_service;
    cache_timeline_entries.clear();
    event_timeline_entries.clear();

    if (observed_cache_service == nullptr) {
        return;
    }

    QObject::connect(
        observed_cache_service, &raster_cache::debug_snapshot_updated, this,
        &resource_monitor::on_cache_snapshot_updated
    );

    on_cache_snapshot_updated(observed_cache_service->get_debug_snapshot());
}

int resource_monitor::max_timeline_entries() const { return timeline_limit; }

int resource_monitor::timeline_size() const {
    return size_to_int(cache_timeline_entries.size());
}

int resource_monitor::event_timeline_size() const {
    return size_to_int(event_timeline_entries.size());
}

bool resource_monitor::has_cache_snapshot() const {
    return !cache_timeline_entries.isEmpty();
}

resource_monitor::cache_timeline_entry
resource_monitor::latest_cache_snapshot() const {
    if (cache_timeline_entries.isEmpty()) {
        return cache_timeline_entry();
    }

    return cache_timeline_entries.constLast();
}

QVector<resource_monitor::cache_timeline_entry>
resource_monitor::cache_timeline() const {
    return cache_timeline_entries;
}

QVector<resource_monitor::event_timeline_entry>
resource_monitor::event_timeline() const {
    return event_timeline_entries;
}

void resource_monitor::add_manual_marker(const QString& label) {
    push_event_entry(event_timeline_entry::event_kind::manual_marker, label);
}

void resource_monitor::set_debug_cadence_mode(debug_cadence_mode mode) {
    cadence_mode = mode;
}

resource_monitor::debug_cadence_mode
resource_monitor::get_debug_cadence_mode() const {
    return cadence_mode;
}

resource_monitor::export_request_metadata
resource_monitor::latest_export_metadata() const {
    return last_export_metadata;
}

void resource_monitor::export_debug_snapshot_async(const QString& output_path) {
    const export_request_metadata metadata {
        .collector_sequence = collector_sequence,
        .cache_timeline_size = size_to_int(cache_timeline_entries.size()),
        .event_timeline_size = size_to_int(event_timeline_entries.size()),
    };
    last_export_metadata = metadata;

    const QVector<cache_timeline_entry> cache_entries = cache_timeline_entries;
    const QVector<event_timeline_entry> event_entries = event_timeline_entries;

    if (active_export_watcher != nullptr) {
        active_export_watcher->deleteLater();
        active_export_watcher = nullptr;
    }

    active_export_watcher = new QFutureWatcher<QString>(this);
    QObject::connect(
        active_export_watcher, &QFutureWatcher<QString>::finished, this,
        [this, output_path]() {
            if (active_export_watcher == nullptr) {
                emit snapshot_export_finished(
                    output_path, false, QStringLiteral("export watcher lost")
                );
                return;
            }

            const QString error_message
                = active_export_watcher->future().result();
            const bool success = error_message.isEmpty();

            active_export_watcher->deleteLater();
            active_export_watcher = nullptr;
            emit snapshot_export_finished(output_path, success, error_message);
        }
    );

    const debug_cadence_mode export_mode = cadence_mode;
    active_export_watcher->setFuture(
        QtConcurrent::run([output_path, metadata, cache_entries, event_entries,
                           export_mode]() {
            return write_debug_snapshot_json(
                output_path, metadata, cache_entries, event_entries, export_mode
            );
        })
    );
}

bool resource_monitor::export_debug_snapshot_sync(
    const QString& output_path, QString* error_message
) const {
    const export_request_metadata metadata {
        .collector_sequence = collector_sequence,
        .cache_timeline_size = size_to_int(cache_timeline_entries.size()),
        .event_timeline_size = size_to_int(event_timeline_entries.size()),
    };

    const QString error = write_debug_snapshot_json(
        output_path, metadata, cache_timeline_entries, event_timeline_entries,
        cadence_mode
    );
    if (error_message != nullptr) {
        *error_message = error;
    }
    return error.isEmpty();
}

void resource_monitor::on_cache_snapshot_updated(
    const raster_cache::debug_snapshot& snapshot
) {
    const cache_timeline_entry entry {
        .collector_sequence = ++collector_sequence,
        .cache_snapshot = snapshot,
    };

    cache_timeline_entries.push_back(entry);
    while (cache_timeline_entries.size() > timeline_limit) {
        cache_timeline_entries.removeFirst();
    }

    push_event_entry(
        event_timeline_entry::event_kind::cache_snapshot, QString()
    );

    emit cache_snapshot_collected(entry);
}

void resource_monitor::push_event_entry(
    event_timeline_entry::event_kind kind, const QString& label
) {
    const event_timeline_entry entry {
        .collector_sequence = collector_sequence,
        .kind = kind,
        .timestamp_ms = QDateTime::currentMSecsSinceEpoch(),
        .label = label,
    };

    event_timeline_entries.push_back(entry);
    while (event_timeline_entries.size() > timeline_limit) {
        event_timeline_entries.removeFirst();
    }

    emit event_recorded(entry);
}

QString resource_monitor::cadence_mode_to_string(debug_cadence_mode mode) {
    return ::cadence_mode_to_string(mode);
}
