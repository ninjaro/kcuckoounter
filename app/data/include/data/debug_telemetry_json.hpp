#ifndef KCUCKOOUNTER_DATA_DEBUG_TELEMETRY_JSON_HPP
#define KCUCKOOUNTER_DATA_DEBUG_TELEMETRY_JSON_HPP

#include "image/raster_cache.hpp"
#include "debug/geometry_observation.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

class debug_probe_core {
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
        QString protocol_app_name;
        qint64 protocol_process_id = -1;
        QString protocol_session_id;
        QString protocol_build_id;
        QStringList protocol_debug_flags;
        QString protocol_instrumentation_mode;
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

    struct auto_process_report_policy {
        qint64 rss_growth_threshold_bytes = 0;
        qint64 cooldown_ms = 0;
        qint64 consecutive_growth_hits_required = 0;
        qint64 window_ms = 0;
        qint64 window_max_exports = 0;
    };

    struct process_memory_report_inputs {
        debug_cadence_mode cadence_mode = debug_cadence_mode::realistic;
        qint64 captured_at_utc_ms = 0;
        qint64 process_memory_sample_interval_ms = 0;
        qint64 auto_process_report_rss_growth_threshold_bytes = 0;
        qint64 auto_process_report_cooldown_ms = 0;
        qint64 auto_process_report_baseline_rss_bytes = -1;
        qint64 auto_process_report_rss_growth_since_baseline_bytes = 0;
        qint64 auto_process_report_last_trigger_utc_ms = 0;
        qint64 auto_process_report_cooldown_remaining_ms = 0;
        qint64 auto_process_report_consecutive_growth_hits_required = 0;
        qint64 auto_process_report_consecutive_growth_hits_current = 0;
        qint64 latest_process_rss_bytes = -1;
        QString latest_process_rss_source;
        QString latest_process_rss_unavailable_reason;
        QString report_trigger_label;
        qint64 status_vm_rss_bytes = -1;
        qint64 status_vm_hwm_bytes = -1;
        qint64 status_vm_size_bytes = -1;
        qint64 status_vm_swap_bytes = -1;
        bool status_bytes_available = false;
        QString status_bytes_unavailable_reason;
        QJsonObject smaps_rollup_bytes;
        bool smaps_rollup_bytes_available = false;
        QString smaps_rollup_bytes_unavailable_reason;
        QString protocol_app_name;
        qint64 protocol_process_id = -1;
        QString protocol_session_id;
        QString protocol_build_id;
        QStringList protocol_debug_flags;
        QString protocol_instrumentation_mode;
    };

    struct protocol_identity {
        QString app_name;
        qint64 process_id = -1;
        QString session_id;
        QString build_id;
        QStringList debug_flags;
        QString instrumentation_mode;
    };

    static QString cadence_mode_to_string(debug_cadence_mode mode);
    static qint64 sample_interval_ms_for_mode(debug_cadence_mode mode);
    static auto_process_report_policy
    auto_report_policy_for_mode(debug_cadence_mode mode);
    static QJsonObject build_message(
        const QString& message_family, const protocol_identity& identity,
        qint64 monotonic_timestamp_ms, const QJsonObject& payload
    );
    static QJsonObject identity_to_json(const protocol_identity& identity);
    static QJsonArray metric_catalog();
    static QJsonArray extensions();
    static QJsonObject
    geometry_snapshot_to_json(const geometry_debug_snapshot& snapshot);
    static QJsonObject
    cache_snapshot_to_live_json(const raster_cache::debug_snapshot& snapshot);
    static QJsonObject resize_transition_to_live_json(
        const resize_transition_debug_event& transition
    );
    static QJsonObject
    resize_history_entry_to_json(const resize_history_entry& entry);
    static QString
    resize_entry_to_jsonl_line(const resize_history_entry& entry);
    static QJsonObject build_snapshot_export_json(
        const export_request_metadata& metadata,
        const QVector<cache_timeline_entry>& cache_entries,
        const QVector<event_timeline_entry>& event_entries,
        const QVector<geometry_debug_snapshot>& geometry_entries,
        const QVector<resize_history_entry>& resize_entries,
        debug_cadence_mode export_mode
    );
    static QJsonObject snapshot_telemetry_semantics();
    static QJsonObject process_memory_report_telemetry_semantics();
    static QJsonObject build_process_memory_report_json(
        const process_memory_report_inputs& inputs
    );

private:
    static QJsonObject metric_catalog_entry(
        const QString& id, const QString& kind, const QString& provenance,
        const QString& unit, const QString& scope,
        const QString& cardinality_semantics, const QString& stability,
        const QString& additive_semantics, const QString& confidence,
        const QString& default_display_role, const QString& domain_namespace
    );
};

#endif // KCUCKOOUNTER_DATA_DEBUG_TELEMETRY_JSON_HPP
