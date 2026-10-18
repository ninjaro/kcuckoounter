#ifndef KCUCKOOUNTER_TESTS_IMAGE_CACHER_TESTS_HPP
#define KCUCKOOUNTER_TESTS_IMAGE_CACHER_TESTS_HPP

#include <QObject>

class image_cacher_tests : public QObject {
    Q_OBJECT
private slots:
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
    void decodes_svgz_and_rejects_truncation();
    void invalid_svg_reports_failure();
    void renderer_matches_reference_without_distortion();
};

#endif
