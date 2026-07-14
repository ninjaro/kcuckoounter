#ifndef KCUCKOOUNTER_MONITOR_RESOURCE_MONITOR_HPP
#define KCUCKOOUNTER_MONITOR_RESOURCE_MONITOR_HPP

#include "image/raster_cache.hpp"
#include "monitor/debug_probe_core.hpp"
#include "monitor/geometry_debug_telemetry.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <optional>

class debug_broadcaster;
class resize_history_writer;

class resource_monitor : public QObject {
    Q_OBJECT

public:
    using resize_history_entry = debug_probe_core::resize_history_entry;
    using debug_cadence_mode = debug_probe_core::debug_cadence_mode;
    using cache_timeline_entry = debug_probe_core::cache_timeline_entry;
    using event_timeline_entry = debug_probe_core::event_timeline_entry;
    using export_request_metadata = debug_probe_core::export_request_metadata;
    using auto_process_report_runtime_state
        = debug_probe_core::auto_process_report_runtime_state;

    enum class process_memory_report_trigger {
        manual_on_demand,
        threshold_rss_growth,
    };

    explicit resource_monitor(
        QObject* parent = nullptr, int max_timeline_entries = 256
    );
    ~resource_monitor() override;

    void attach_cache_service(raster_cache* cache_service);
    void attach_table_service(QObject* table_service);
    [[nodiscard]] int max_timeline_entries() const;
    [[nodiscard]] int timeline_size() const;
    [[nodiscard]] int event_timeline_size() const;
    [[nodiscard]] int geometry_timeline_size() const;
    [[nodiscard]] int resize_history_size() const;
    [[nodiscard]] bool has_cache_snapshot() const;
    [[nodiscard]] bool has_geometry_snapshot() const;
    [[nodiscard]] cache_timeline_entry latest_cache_snapshot() const;
    [[nodiscard]] geometry_debug_snapshot latest_geometry_snapshot() const;
    [[nodiscard]] QVector<cache_timeline_entry> cache_timeline() const;
    [[nodiscard]] QVector<event_timeline_entry> event_timeline() const;
    [[nodiscard]] QVector<geometry_debug_snapshot> geometry_timeline() const;
    [[nodiscard]] QVector<resize_history_entry> resize_history() const;
    [[nodiscard]] QString resize_history_log_path() const;
    [[nodiscard]] QJsonObject resize_history_writer_runtime_state() const;
    void add_manual_marker(const QString& label);
    void set_debug_cadence_mode(debug_cadence_mode mode);
    [[nodiscard]] debug_cadence_mode get_debug_cadence_mode() const;
    [[nodiscard]] export_request_metadata latest_export_metadata() const;
    [[nodiscard]] auto_process_report_runtime_state
    current_auto_report_state() const;
    [[nodiscard]] qint64 latest_process_rss_bytes() const;
    [[nodiscard]] qint64 process_memory_sample_interval_ms() const;
    void set_debug_broadcaster_enabled(bool enabled);
    [[nodiscard]] bool is_debug_broadcaster_enabled() const;
    [[nodiscard]] QString debug_broadcaster_endpoint_name() const;
    [[nodiscard]] QJsonObject debug_broadcaster_runtime_state() const;
    void export_debug_snapshot_async(const QString& output_path);
    void export_process_memory_report_async(const QString& output_path);
    bool export_debug_snapshot_sync(
        const QString& output_path, QString* error_message = nullptr
    ) const;
    bool export_process_memory_report_sync(
        const QString& output_path, QString* error_message = nullptr
    ) const;
    void set_test_auto_report_policy(
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
    void debug_broadcaster_state_changed();
    void resize_history_persistence_warning(
        const QString& warning_code, const QString& warning_message
    );

private slots:
    void
    on_cache_snapshot_updated(const raster_cache::debug_snapshot& snapshot);
    void
    on_geometry_debug_snapshot_updated(const geometry_debug_snapshot& snapshot);
    void
    on_resize_transition_recorded(const resize_transition_debug_event& event);
    void on_periodic_collection_tick();
    void on_broadcaster_listener_connection_changed(bool connected);
    void on_broadcaster_warning_raised(
        const QString& warning_code, const QString& warning_message
    );
    void on_snapshot_export_watcher_finished();
    void on_memory_export_finished();

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
    resize_history_writer* resize_history_log_writer;
    export_request_metadata last_export_metadata;
    QFutureWatcher<QString>* active_export_watcher;
    QFutureWatcher<QString>* active_process_report_export_watcher;
    debug_cadence_mode cadence_mode;
    qint64 last_process_sample_ms;
    qint64 current_process_rss_bytes;
    QString current_process_rss_source;
    QString current_process_rss_unavailable_reason;
    qint64 auto_dump_growth_threshold_bytes;
    qint64 auto_process_dump_cooldown_ms;
    bool auto_dump_test_policy_override;
    qint64 auto_dump_required_hits_override;
    qint64 auto_dump_last_trigger_ms;
    qint64 auto_dump_baseline_rss_bytes;
    qint64 auto_dump_growth_hits;
    qint64 auto_dump_window_start_ms;
    qint64 auto_dump_exports_used;
    QString protocol_app_name;
    QString protocol_session_id;
    QString protocol_build_id;
    QStringList protocol_debug_flags;
    debug_broadcaster* telemetry_broadcaster;
    QElapsedTimer broadcaster_monotonic_clock;
    QByteArray last_broadcast_cache_decision_signature;
    QTimer process_sampling_timer;

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
    void ensure_resize_history_path();
    void maybe_trigger_auto_process_report(qint64 ready_bytes_delta_hint = 0);
    void refresh_memory_sample_if_needed();
    void refresh_process_memory_sample_now();
    [[nodiscard]] qint64 sample_interval_ms_for_mode() const;
    [[nodiscard]] qint64 auto_dump_growth_threshold() const;
    [[nodiscard]] qint64 auto_dump_cooldown_ms() const;
    [[nodiscard]] qint64 auto_dump_growth_from_baseline() const;
    [[nodiscard]] qint64 auto_dump_cooldown_remaining(qint64 now_ms) const;
    [[nodiscard]] qint64 auto_dump_required_hits() const;
    [[nodiscard]] qint64 auto_dump_window_ms() const;
    [[nodiscard]] qint64 auto_dump_max_exports() const;
    static QString cadence_mode_to_string(debug_cadence_mode mode);
    [[nodiscard]] qint64 broadcaster_monotonic_timestamp_ms() const;
    [[nodiscard]] bool broadcaster_listener_is_ready() const;
    [[nodiscard]] debug_probe_core::protocol_identity
    broadcaster_protocol_identity() const;
    void publish_broadcaster_session_start();
    void publish_broadcaster_session_end(const QString& reason);
    void publish_broadcaster_sample_batch(const cache_timeline_entry& entry);
    void publish_broadcaster_event(const event_timeline_entry& entry);
    void publish_broadcaster_resize_transition(
        const resize_transition_debug_event& event
    );
    void
    publish_broadcaster_layout_transition(const resize_history_entry& entry);
    void publish_cache_decision_change(const geometry_debug_snapshot& geometry);
    void publish_broadcaster_snapshot_now();
    void publish_broadcaster_warning(
        const QString& warning_code, const QString& warning_message
    );
    void export_process_memory_report_async(
        const QString& output_path, process_memory_report_trigger trigger
    );
};

#endif // KCUCKOOUNTER_MONITOR_RESOURCE_MONITOR_HPP
