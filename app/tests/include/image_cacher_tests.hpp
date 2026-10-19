#ifndef KCUCKOOUNTER_TESTS_IMAGE_CACHER_TESTS_HPP
#define KCUCKOOUNTER_TESTS_IMAGE_CACHER_TESTS_HPP

#include <QObject>

class image_cacher_tests : public QObject {
    Q_OBJECT
private slots:
    void source_acquisition_is_asynchronous_and_shares_content();
    void source_queue_replaces_obsolete_intent_data();
    void source_queue_replaces_obsolete_intent();
    void source_started_observer_can_retire_data();
    void source_started_observer_can_retire();
    void source_finished_observer_can_retarget_data();
    void source_finished_observer_can_retarget();
    void source_acquisition_errors_recover();
    void source_acquisition_rejects_foreign_thread_access();
    void rasterizes_svg_for_target_size();
    void shared_consumers_coalesce_geometry_and_dpr();
    void reuses_raster_inside_size_window();
    void device_pixel_ratio_updates_reused_presentation();
    void withdrawing_consumer_preserves_other_demand();
    void published_pixels_outlive_producer_changes();
    void publication_observers_can_retarget_producer_data();
    void publication_observers_can_retarget_producer();
    void replaces_oversized_and_undersized_rasters();
    void empty_display_size_withdraws_demand();
    void source_replacement_has_new_content_identity();
    void source_identity_does_not_need_path_hash_labels();
    void decodes_svgz_and_rejects_truncation();
    void invalid_svg_reports_failure();
    void renderer_matches_reference_without_distortion();
};

#endif
