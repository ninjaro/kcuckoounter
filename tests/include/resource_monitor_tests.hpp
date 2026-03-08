#ifndef KCUCKOOUNTER_TESTS_RESOURCE_MONITOR_TESTS_HPP
#define KCUCKOOUNTER_TESTS_RESOURCE_MONITOR_TESTS_HPP

#include <QObject>

class resource_monitor_tests : public QObject {
    Q_OBJECT

private slots:
    void collects_initial_snapshot_when_attached();
    void keeps_bounded_timeline_and_emits_updates();
    void records_manual_markers_in_bounded_event_timeline();
    void exports_snapshot_asynchronously();
    void export_contains_size_bucket_and_largest_entry_diagnostics();
    void exports_active_debug_cadence_mode();
    void exports_process_sampling_interval_metadata();
    void exposes_auto_process_report_runtime_state();
    void samples_process_memory_and_exports_it();
    void exports_process_memory_detail_report_on_demand();
    void auto_exports_process_memory_detail_when_rss_growth_threshold_is_hit();
    void auto_export_requires_configured_consecutive_growth_hits();
    void auto_export_is_rate_limited_by_mode_window();
    void periodically_collects_when_cache_is_stable();
    void broadcasts_protocol_messages_over_local_ipc_when_enabled();
    void tracks_memory_class_deltas_between_snapshots();
    void collects_geometry_snapshots_and_resize_history_from_table();
    void exports_geometry_and_resize_history_sections();
};

#endif // KCUCKOOUNTER_TESTS_RESOURCE_MONITOR_TESTS_HPP
