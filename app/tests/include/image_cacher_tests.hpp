#ifndef KCUCKOOUNTER_TESTS_IMAGE_CACHER_TESTS_HPP
#define KCUCKOOUNTER_TESTS_IMAGE_CACHER_TESTS_HPP

#include <QObject>

class image_cacher_tests : public QObject {
    Q_OBJECT

private slots:
    void rasterizes_svg_for_target_size();
    void namespace_switch_keeps_image_ready();
    void reuses_raster_inside_size_window_data();
    void reuses_raster_inside_size_window();
    void shared_cache_separates_raster_shapes_data();
    void shared_cache_separates_raster_shapes();
    void retargeting_changes_raster_shape_data();
    void retargeting_changes_raster_shape();
    void shared_cache_reuses_identical_rasters_data();
    void shared_cache_reuses_identical_rasters();
    void published_pixels_outlive_producer_changes_data();
    void published_pixels_outlive_producer_changes();
    void publication_observers_can_retarget_producer_data();
    void publication_observers_can_retarget_producer();
    void replaces_oversized_and_undersized_rasters();
    void empty_display_size_retires_cached_raster();
};

#endif // KCUCKOOUNTER_TESTS_IMAGE_CACHER_TESTS_HPP
