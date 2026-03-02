#ifndef KCUCKOOUNTER_TESTS_RASTER_CACHE_TESTS_HPP
#define KCUCKOOUNTER_TESTS_RASTER_CACHE_TESTS_HPP

#include <QObject>

class raster_cache_tests : public QObject {
    Q_OBJECT

private slots:
    void stores_and_reads_ready_result();
    void pending_entry_is_latest_per_family();
    void in_flight_state_tracks_family_lifecycle();
    void submit_request_starts_async_for_cache_miss();
    void submit_request_hits_cache_when_ready();
    void submit_request_coalesces_latest_pending_target();
    void finish_active_request_starts_latest_pending_entry();
    void finish_active_request_rejects_stale_completion();
    void namespaces_keep_separate_ready_entries();
    void settings_namespace_evicts_oldest_entries();
    void settings_namespace_limit_can_be_tuned_at_runtime();
    void settings_lookup_can_fallback_to_main_namespace();
    void settings_lookup_prefers_settings_when_both_ready();
    void subset_render_scope_is_normalized_for_dedup_and_cache_hits();
    void debug_snapshot_tracks_stock_and_delta_counters();
    void debug_snapshot_signal_is_emitted_on_cache_updates();
    void interval_deltas_can_be_taken_and_reset();
    void debug_snapshot_tracks_raster_and_coalesced_wait_timings();
    void debug_snapshot_tracks_deadline_readiness_counters();
    void debug_snapshot_includes_size_buckets_and_largest_entries();
    void debug_snapshot_includes_displayed_split_and_task_toplists();
};

#endif // KCUCKOOUNTER_TESTS_RASTER_CACHE_TESTS_HPP
