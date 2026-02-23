#ifndef KCUCKOOUNTER_IMAGE_CACHER_TESTS_HPP
#define KCUCKOOUNTER_IMAGE_CACHER_TESTS_HPP

#include <QObject>

class image_cacher_tests : public QObject {
    Q_OBJECT

private slots:
    void rasterizes_svg_for_target_size();
    void namespace_switch_keeps_image_ready();
};

#endif // KCUCKOOUNTER_IMAGE_CACHER_TESTS_HPP
