// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/image_cacher_tests.hpp"

#include "image/image_cacher.hpp"

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

    cacher.set_cache_namespace(raster_cache::cache_namespace::settings);

    QCOMPARE(cacher.cache_namespace(), raster_cache::cache_namespace::settings);
    QVERIFY(cacher.is_ready());
    QCOMPARE(cacher.display_size(), QSize(72, 72));
}

void image_cacher_tests::reuses_raster_inside_size_window() {
    raster_cache service;
    image_cacher cacher(QStringLiteral("assets/logo.svg"), &service);
    cacher.set_target_size(QSize(100, 100));
    QVERIFY(cacher.is_ready());
    QCOMPARE(service.ready_entry_count(), 1);
    const qint64 initial_cache_key = cacher.pixmap().cacheKey();
    const QSize initial_raster_size = cacher.pixmap().size();

    cacher.set_target_size(QSize(110, 110));

    QCOMPARE(cacher.pixmap().cacheKey(), initial_cache_key);
    QCOMPARE(cacher.pixmap().size(), initial_raster_size);
    QCOMPARE(service.ready_entry_count(), 1);
}

void image_cacher_tests::replaces_oversized_and_undersized_rasters() {
    raster_cache service;
    image_cacher cacher(QStringLiteral("assets/logo.svg"), &service);
    cacher.set_target_size(QSize(100, 100));
    QVERIFY(cacher.is_ready());
    const QSize initial_raster_size = cacher.pixmap().size();

    cacher.set_target_size(QSize(80, 80));
    const QSize smaller_raster_size = cacher.pixmap().size();
    QVERIFY(smaller_raster_size.width() < initial_raster_size.width());
    QCOMPARE(service.ready_entry_count(), 1);

    cacher.set_target_size(QSize(130, 130));
    const QSize larger_raster_size = cacher.pixmap().size();
    QVERIFY(larger_raster_size.width() > smaller_raster_size.width());
    QCOMPARE(service.ready_entry_count(), 1);

}

void image_cacher_tests::empty_display_size_retires_cached_raster() {
    raster_cache service;
    image_cacher cacher(QStringLiteral("assets/logo.svg"), &service);
    cacher.set_target_size(QSize(100, 100));
    QVERIFY(cacher.is_ready());
    QCOMPARE(service.ready_entry_count(), 1);

    cacher.set_target_size(QSize());

    QVERIFY(!cacher.is_ready());
    QCOMPARE(service.ready_entry_count(), 0);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
