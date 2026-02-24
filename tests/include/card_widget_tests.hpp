#ifndef KCUCKOOUNTER_TESTS_INCLUDE_CARD_WIDGET_TESTS_HPP
#define KCUCKOOUNTER_TESTS_INCLUDE_CARD_WIDGET_TESTS_HPP

#include <QObject>

class card_widget_tests : public QObject {
    Q_OBJECT

private slots:
    void stretches_between_raster_intervals();
    void memory_cache_tracks_resize();
    void shared_faces_disable_local_rasterization();
    void theme_source_change_invalidates_stale_raster_result();
};

#endif // KCUCKOOUNTER_TESTS_INCLUDE_CARD_WIDGET_TESTS_HPP
