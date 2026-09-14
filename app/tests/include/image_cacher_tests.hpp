#ifndef KCUCKOOUNTER_TESTS_IMAGE_CACHER_TESTS_HPP
#define KCUCKOOUNTER_TESTS_IMAGE_CACHER_TESTS_HPP

#include <QObject>

class image_cacher_tests : public QObject {
    Q_OBJECT

private slots:
    void rasterizes_svg_for_target_size();
    void namespace_switch_keeps_image_ready();
    void reuses_raster_inside_size_window();
    void replaces_oversized_and_undersized_rasters();
    void empty_display_size_retires_cached_raster();
};

#endif // KCUCKOOUNTER_TESTS_IMAGE_CACHER_TESTS_HPP
