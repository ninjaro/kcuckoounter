#ifndef KCUCKOOUNTER_MONITOR_RESOURCE_MONITOR_HPP
#define KCUCKOOUNTER_MONITOR_RESOURCE_MONITOR_HPP

#include "image/raster_cache.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVector>

class resource_monitor : public QObject {
    Q_OBJECT

public:
    enum class debug_cadence_mode {
        realistic,
        instrumented,
    };

    struct cache_timeline_entry {
        qint64 collector_sequence = 0;
        raster_cache::debug_snapshot cache_snapshot;
    };

    struct event_timeline_entry {
        enum class event_kind {
            cache_snapshot,
            manual_marker,
        };

        qint64 collector_sequence = 0;
        event_kind kind = event_kind::cache_snapshot;
        qint64 timestamp_ms = 0;
        QString label;
    };

    struct export_request_metadata {
        qint64 collector_sequence = 0;
        int cache_timeline_size = 0;
        int event_timeline_size = 0;
    };

    explicit resource_monitor(
        QObject* parent = nullptr, int max_timeline_entries = 256
    );

    void attach_cache_service(raster_cache* cache_service);
    int max_timeline_entries() const;
    int timeline_size() const;
    int event_timeline_size() const;
    bool has_cache_snapshot() const;
    cache_timeline_entry latest_cache_snapshot() const;
    QVector<cache_timeline_entry> cache_timeline() const;
    QVector<event_timeline_entry> event_timeline() const;
    void add_manual_marker(const QString& label);
    void set_debug_cadence_mode(debug_cadence_mode mode);
    debug_cadence_mode get_debug_cadence_mode() const;
    export_request_metadata latest_export_metadata() const;
    void export_debug_snapshot_async(const QString& output_path);
    bool export_debug_snapshot_sync(
        const QString& output_path, QString* error_message = nullptr
    ) const;

signals:
    void cache_snapshot_collected(
        const resource_monitor::cache_timeline_entry& entry
    );
    void event_recorded(const resource_monitor::event_timeline_entry& entry);
    void snapshot_export_finished(
        const QString& output_path, bool success, const QString& error_message
    );

private slots:
    void
    on_cache_snapshot_updated(const raster_cache::debug_snapshot& snapshot);

private:
    raster_cache* observed_cache_service;
    int timeline_limit;
    qint64 collector_sequence;
    QVector<cache_timeline_entry> cache_timeline_entries;
    QVector<event_timeline_entry> event_timeline_entries;
    export_request_metadata last_export_metadata;
    QFutureWatcher<QString>* active_export_watcher;
    debug_cadence_mode cadence_mode;

    void push_event_entry(
        event_timeline_entry::event_kind kind, const QString& label
    );
    static QString cadence_mode_to_string(debug_cadence_mode mode);
};

#endif // KCUCKOOUNTER_MONITOR_RESOURCE_MONITOR_HPP
