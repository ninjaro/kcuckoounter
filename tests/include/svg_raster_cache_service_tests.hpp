#ifndef KCUCKOOUNTER_SVG_RASTER_CACHE_SERVICE_TESTS_HPP
#define KCUCKOOUNTER_SVG_RASTER_CACHE_SERVICE_TESTS_HPP

#include <QObject>

class svg_raster_cache_service_tests : public QObject {
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
    void settings_lookup_can_fallback_to_main_namespace();
    void settings_lookup_prefers_settings_when_both_ready();
    void subset_render_scope_is_normalized_for_dedup_and_cache_hits();
};

#endif // KCUCKOOUNTER_SVG_RASTER_CACHE_SERVICE_TESTS_HPP
