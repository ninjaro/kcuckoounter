#include "include/image_cacher_tests.hpp"

#include "helpers/image_cacher.hpp"

#include <QtTest/QtTest>

void image_cacher_tests::rasterizes_svg_for_target_size() {
    image_cacher cacher;
    cacher.set_source(QStringLiteral("assets/logo.svg"));
    cacher.set_target_size(QSize(64, 64));

    QVERIFY(cacher.has_source());
    QVERIFY(cacher.is_ready());
    QVERIFY(!cacher.pixmap().isNull());
    QCOMPARE(cacher.display_size(), QSize(64, 64));
}

void image_cacher_tests::namespace_switch_keeps_image_ready() {
    image_cacher cacher(QStringLiteral("assets/logo.svg"));
    cacher.set_target_size(QSize(72, 72));
    QVERIFY(cacher.is_ready());

    cacher.set_cache_namespace(
        svg_raster_cache_service::cache_namespace::settings
    );

    QCOMPARE(
        cacher.cache_namespace(),
        svg_raster_cache_service::cache_namespace::settings
    );
    QVERIFY(cacher.is_ready());
    QCOMPARE(cacher.display_size(), QSize(72, 72));
}
