#ifndef KCUCKOOUNTER_MONITOR_RESOURCE_MONITOR_HPP
#define KCUCKOOUNTER_MONITOR_RESOURCE_MONITOR_HPP

#include "image/raster_cache.hpp"
#include "monitor/geometry_debug_telemetry.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVector>

#include <optional>

class resource_monitor : public QObject {
    Q_OBJECT

public:
    struct resize_history_entry {
        qint64 collector_sequence = 0;
        qint64 transition_start_timestamp_ms = 0;
        qint64 transition_end_timestamp_ms = 0;
        qint64 prewarm_completion_ms = -1;
        QSize old_window_size;
        QSize new_window_size;
        int old_active_bucket_px = 0;
        int new_active_bucket_px = 0;
        int old_warming_bucket_px = 0;
        int new_warming_bucket_px = 0;
        geometry_debug_snapshot geometry_after_resize;
        qint64 before_process_rss_bytes = -1;
        qint64 after_process_rss_bytes = -1;
        qint64 before_cache_accounted_ready_bytes = 0;
        qint64 after_cache_accounted_ready_bytes = 0;
        qint64 before_widget_local_display_bytes_estimated = 0;
        qint64 after_widget_local_display_bytes_estimated = 0;
        qint64 before_measured_accounted_gap_bytes = 0;
        qint64 after_measured_accounted_gap_bytes = 0;
    };

    enum class debug_cadence_mode {
        realistic,
        instrumented,
    };

    struct cache_timeline_entry {
        qint64 collector_sequence = 0;
        raster_cache::debug_snapshot cache_snapshot;
        int cache_entries_added_interval = 0;
        int cache_entries_removed_interval = 0;
        qint64 cache_bytes_added_interval = 0;
        qint64 cache_bytes_removed_interval = 0;
        int cache_images_added_interval = 0;
        int cache_images_removed_interval = 0;
        qint64 cache_accounted_ready_bytes_delta = 0;
        qint64 widget_local_display_bytes_estimated_delta = 0;
        qint64 widget_local_display_bytes_materialized_interval = 0;
        qint64 widget_local_display_bytes_released_interval = 0;
        qint64 process_rss_bytes = -1;
        qint64 process_rss_bytes_delta = 0;
        qint64 process_rss_bytes_growth_interval = 0;
        qint64 process_rss_bytes_drop_interval = 0;
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
        int geometry_timeline_size = 0;
        int resize_history_size = 0;
        QString resize_history_log_path;
        qint64 latest_process_rss_bytes = -1;
        QString process_memory_source;
        QString process_memory_unavailable_reason;
        qint64 process_memory_sample_interval_ms = 0;
        qint64 auto_process_report_rss_growth_threshold_bytes = 0;
        qint64 auto_process_report_cooldown_ms = 0;
        qint64 auto_process_report_baseline_rss_bytes = -1;
        qint64 auto_process_report_rss_growth_since_baseline_bytes = 0;
        qint64 auto_process_report_last_trigger_utc_ms = 0;
        qint64 auto_process_report_cooldown_remaining_ms = 0;
        qint64 auto_process_report_consecutive_growth_hits_required = 0;
        qint64 auto_process_report_consecutive_growth_hits_current = 0;
        qint64 auto_process_report_window_ms = 0;
        qint64 auto_process_report_window_max_exports = 0;
        qint64 auto_process_report_window_exports_used = 0;
    };

    struct auto_process_report_runtime_state {
        qint64 rss_growth_threshold_bytes = 0;
        qint64 cooldown_ms = 0;
        qint64 baseline_rss_bytes = -1;
        qint64 rss_growth_since_baseline_bytes = 0;
        qint64 last_trigger_utc_ms = 0;
        qint64 cooldown_remaining_ms = 0;
        qint64 consecutive_growth_hits_required = 0;
        qint64 consecutive_growth_hits_current = 0;
        qint64 window_ms = 0;
        qint64 window_max_exports = 0;
        qint64 window_exports_used = 0;
    };

    enum class process_memory_report_trigger {
        manual_on_demand,
        threshold_rss_growth,
    };

    explicit resource_monitor(
        QObject* parent = nullptr, int max_timeline_entries = 256
    );

    void attach_cache_service(raster_cache* cache_service);
    void attach_table_service(QObject* table_service);
    int max_timeline_entries() const;
    int timeline_size() const;
    int event_timeline_size() const;
    int geometry_timeline_size() const;
    int resize_history_size() const;
    bool has_cache_snapshot() const;
    bool has_geometry_snapshot() const;
    cache_timeline_entry latest_cache_snapshot() const;
    geometry_debug_snapshot latest_geometry_snapshot() const;
    QVector<cache_timeline_entry> cache_timeline() const;
    QVector<event_timeline_entry> event_timeline() const;
    QVector<geometry_debug_snapshot> geometry_timeline() const;
    QVector<resize_history_entry> resize_history() const;
    QString resize_history_log_path() const;
    void add_manual_marker(const QString& label);
    void set_debug_cadence_mode(debug_cadence_mode mode);
    debug_cadence_mode get_debug_cadence_mode() const;
    export_request_metadata latest_export_metadata() const;
    auto_process_report_runtime_state
    current_auto_process_report_runtime_state() const;
    qint64 latest_process_rss_bytes() const;
    qint64 process_memory_sample_interval_ms() const;
    void export_debug_snapshot_async(const QString& output_path);
    void export_process_memory_report_async(const QString& output_path);
    bool export_debug_snapshot_sync(
        const QString& output_path, QString* error_message = nullptr
    ) const;
    bool export_process_memory_report_sync(
        const QString& output_path, QString* error_message = nullptr
    ) const;
    void set_auto_process_report_policy_for_tests(
        qint64 rss_growth_threshold_bytes, qint64 cooldown_ms,
        qint64 consecutive_growth_hits_required = 1
    );

signals:
    void cache_snapshot_collected(
        const resource_monitor::cache_timeline_entry& entry
    );
    void event_recorded(const resource_monitor::event_timeline_entry& entry);
    void geometry_snapshot_collected(const geometry_debug_snapshot& snapshot);
    void resize_history_recorded(
        const resource_monitor::resize_history_entry& entry
    );
    void snapshot_export_finished(
        const QString& output_path, bool success, const QString& error_message
    );
    void process_memory_report_export_finished(
        const QString& output_path, bool success, const QString& error_message
    );

private slots:
    void
    on_cache_snapshot_updated(const raster_cache::debug_snapshot& snapshot);
    void
    on_geometry_debug_snapshot_updated(const geometry_debug_snapshot& snapshot);
    void
    on_resize_transition_recorded(const resize_transition_debug_event& event);

private:
    struct pending_resize_transition {
        resize_transition_debug_event transition;
        qint64 before_process_rss_bytes = -1;
        qint64 before_cache_accounted_ready_bytes = 0;
        qint64 before_widget_local_display_bytes_estimated = 0;
        qint64 before_measured_accounted_gap_bytes = 0;
    };

    raster_cache* observed_cache_service;
    QObject* observed_table_service;
    int timeline_limit;
    qint64 collector_sequence;
    QVector<cache_timeline_entry> cache_timeline_entries;
    QVector<event_timeline_entry> event_timeline_entries;
    QVector<geometry_debug_snapshot> geometry_timeline_entries;
    QVector<resize_history_entry> resize_history_entries;
    std::optional<pending_resize_transition> pending_resize_transition_state;
    QString resize_history_log_stream_path;
    export_request_metadata last_export_metadata;
    QFutureWatcher<QString>* active_export_watcher;
    QFutureWatcher<QString>* active_process_report_export_watcher;
    debug_cadence_mode cadence_mode;
    qint64 last_process_sample_ms;
    qint64 current_process_rss_bytes;
    QString current_process_rss_source;
    QString current_process_rss_unavailable_reason;
    qint64 auto_process_dump_rss_growth_threshold_bytes;
    qint64 auto_process_dump_cooldown_ms;
    bool auto_process_dump_policy_override_for_tests;
    qint64 auto_process_dump_consecutive_growth_hits_required_override;
    qint64 auto_process_dump_last_trigger_ms;
    qint64 auto_process_dump_baseline_rss_bytes;
    qint64 auto_process_dump_consecutive_growth_hits;
    qint64 auto_process_dump_window_start_ms;
    qint64 auto_process_dump_window_exports_used;

    void push_event_entry(
        event_timeline_entry::event_kind kind, const QString& label
    );
    void maybe_finalize_pending_resize_transition(
        const geometry_debug_snapshot& snapshot
    );
    void finalize_pending_resize_transition(
        qint64 transition_end_timestamp_ms, qint64 prewarm_completion_ms
    );
    void append_resize_history_entry_async(const resize_history_entry& entry);
    void initialize_resize_history_log_stream_path_if_needed();
    void maybe_trigger_auto_process_report(
        qint64 cache_accounted_ready_bytes_delta_hint = 0
    );
    void refresh_process_memory_sample_if_needed();
    qint64 process_sample_interval_ms_for_mode() const;
    qint64 auto_process_dump_rss_growth_threshold_bytes_effective() const;
    qint64 auto_process_dump_cooldown_ms_effective() const;
    qint64 auto_process_dump_rss_growth_since_baseline_bytes() const;
    qint64 auto_process_dump_cooldown_remaining_ms(qint64 now_ms) const;
    qint64 auto_process_dump_consecutive_growth_hits_required() const;
    qint64 auto_process_dump_window_ms_effective() const;
    qint64 auto_process_dump_window_max_exports_effective() const;
    static QString cadence_mode_to_string(debug_cadence_mode mode);
    void export_process_memory_report_async(
        const QString& output_path, process_memory_report_trigger trigger
    );
};

#endif // KCUCKOOUNTER_MONITOR_RESOURCE_MONITOR_HPP
