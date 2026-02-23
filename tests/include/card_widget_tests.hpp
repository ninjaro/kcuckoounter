#ifndef KCUCKOOUNTER_CARD_WIDGET_TESTS_HPP
#define KCUCKOOUNTER_CARD_WIDGET_TESTS_HPP

#include <QObject>

class card_widget_tests : public QObject {
    Q_OBJECT

private slots:
    void stretches_between_raster_intervals();
    void memory_cache_tracks_resize();
    void shared_faces_disable_local_rasterization();
};

#endif // KCUCKOOUNTER_CARD_WIDGET_TESTS_HPP
