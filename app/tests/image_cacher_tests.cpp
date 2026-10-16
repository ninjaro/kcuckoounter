// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/image_cacher_tests.hpp"
#include "arch/asset_locator.hpp"

#include "image/image_cacher.hpp"

#include <QtTest/QtTest>

void image_cacher_tests::rasterizes_svg_for_target_size() {
    image_cacher cacher;
    cacher.set_source(bundled_asset_path(QStringLiteral("brand/logo.svg")));
    cacher.set_target_size(QSize(64, 64));

    QVERIFY(cacher.has_source());
    QVERIFY(cacher.is_ready());
    QVERIFY(!cacher.pixmap().isNull());
    QCOMPARE(cacher.display_size(), QSize(64, 64));
}

void image_cacher_tests::namespace_switch_keeps_image_ready() {
    image_cacher cacher(bundled_asset_path(QStringLiteral("brand/logo.svg")));
    cacher.set_target_size(QSize(72, 72));
    QVERIFY(cacher.is_ready());

    cacher.set_cache_namespace(raster_cache::cache_namespace::settings);

    QCOMPARE(cacher.cache_namespace(), raster_cache::cache_namespace::settings);
    QVERIFY(cacher.is_ready());
    QCOMPARE(cacher.display_size(), QSize(72, 72));
}

void image_cacher_tests::reuses_raster_inside_size_window_data() {
    QTest::addColumn<QSize>("initial_size");
    QTest::addColumn<QSize>("next_size");
    QTest::newRow("square") << QSize(100, 100) << QSize(110, 110);
    QTest::newRow("portrait") << QSize(100, 200) << QSize(110, 220);
    QTest::newRow("landscape") << QSize(200, 100) << QSize(220, 110);
}

void image_cacher_tests::reuses_raster_inside_size_window() {
    QFETCH(QSize, initial_size);
    QFETCH(QSize, next_size);
    raster_cache service;
    image_cacher cacher(
        bundled_asset_path(QStringLiteral("brand/logo.svg")), &service
    );
    cacher.set_target_size(initial_size);
    QVERIFY(cacher.is_ready());
    QCOMPARE(service.ready_entry_count(), 1);
    const qint64 initial_cache_key = cacher.pixmap().cacheKey();
    const QSize initial_raster_size = cacher.pixmap().size();

    cacher.set_target_size(next_size);

    QCOMPARE(cacher.pixmap().cacheKey(), initial_cache_key);
    QCOMPARE(cacher.pixmap().size(), initial_raster_size);
    QCOMPARE(service.ready_entry_count(), 1);
}

void image_cacher_tests::shared_cache_separates_raster_shapes_data() {
    QTest::addColumn<QSize>("first_size");
    QTest::addColumn<QSize>("second_size");
    QTest::addColumn<bool>("settings_namespace");
    QTest::newRow("main-portrait-landscape")
        << QSize(64, 128) << QSize(128, 64) << false;
    QTest::newRow("main-square-landscape")
        << QSize(64, 64) << QSize(128, 64) << false;
    QTest::newRow("settings-portrait-landscape")
        << QSize(64, 128) << QSize(128, 64) << true;
    QTest::newRow("settings-square-landscape")
        << QSize(64, 64) << QSize(128, 64) << true;
}

void image_cacher_tests::shared_cache_separates_raster_shapes() {
    QFETCH(QSize, first_size);
    QFETCH(QSize, second_size);
    QFETCH(bool, settings_namespace);
    raster_cache shared;
    raster_cache isolated;
    const QString source
        = bundled_asset_path(QStringLiteral("brand/cuckoo.svg"));
    image_cacher first(source, &shared);
    image_cacher second(source, &shared);
    image_cacher reference(source, &isolated);
    if (settings_namespace) {
        first.set_cache_namespace(raster_cache::cache_namespace::settings);
        second.set_cache_namespace(raster_cache::cache_namespace::settings);
        reference.set_cache_namespace(raster_cache::cache_namespace::settings);
    }
    QSignalSpy publications(&shared, &raster_cache::result_updated);
    first.set_target_size(first_size);
    QVERIFY(first.is_ready());
    const QImage first_pixels = first.pixmap().toImage();
    reference.set_target_size(second_size);
    second.set_target_size(second_size);

    QCOMPARE(second.pixmap().size(), reference.pixmap().size());
    QCOMPARE(second.pixmap().toImage(), reference.pixmap().toImage());
    QCOMPARE(first.pixmap().toImage(), first_pixels);
    QCOMPARE(publications.count(), 2);
    QCOMPARE(shared.ready_entry_count(), 2);
    QCOMPARE(shared.in_flight_count(), 0);
}

void image_cacher_tests::retargeting_changes_raster_shape_data() {
    QTest::addColumn<QSize>("first_size");
    QTest::addColumn<QSize>("second_size");
    QTest::newRow("square-landscape") << QSize(64, 64) << QSize(128, 64);
    QTest::newRow("portrait-landscape") << QSize(64, 128) << QSize(128, 64);
    QTest::newRow("rounded-portrait") << QSize(100, 137) << QSize(100, 139);
}

void image_cacher_tests::retargeting_changes_raster_shape() {
    QFETCH(QSize, first_size);
    QFETCH(QSize, second_size);
    raster_cache shared;
    raster_cache isolated;
    const QString source
        = bundled_asset_path(QStringLiteral("brand/cuckoo.svg"));
    image_cacher cacher(source, &shared);
    image_cacher reference(source, &isolated);
    QSignalSpy publications(&shared, &raster_cache::result_updated);
    cacher.set_target_size(first_size);
    QVERIFY(cacher.is_ready());
    const QImage first_pixels = cacher.pixmap().toImage();
    reference.set_target_size(second_size);
    cacher.set_target_size(second_size);

    QCOMPARE(cacher.pixmap().size(), reference.pixmap().size());
    QCOMPARE(cacher.pixmap().toImage(), reference.pixmap().toImage());
    QCOMPARE(cacher.display_size(), second_size);
    QCOMPARE(publications.count(), 2);
    QCOMPARE(shared.ready_entry_count(), 1);
    QCOMPARE(shared.in_flight_count(), 0);
    cacher.set_target_size(first_size);
    QCOMPARE(cacher.pixmap().toImage(), first_pixels);
    QCOMPARE(publications.count(), 3);
    QCOMPARE(shared.ready_entry_count(), 1);
}

void image_cacher_tests::shared_cache_reuses_identical_rasters_data() {
    QTest::addColumn<QSize>("target_size");
    QTest::addColumn<bool>("settings_namespace");
    QTest::newRow("main-square") << QSize(64, 64) << false;
    QTest::newRow("main-portrait") << QSize(64, 128) << false;
    QTest::newRow("settings-square") << QSize(64, 64) << true;
    QTest::newRow("settings-portrait") << QSize(64, 128) << true;
}

void image_cacher_tests::shared_cache_reuses_identical_rasters() {
    QFETCH(QSize, target_size);
    QFETCH(bool, settings_namespace);
    raster_cache shared;
    const QString source
        = bundled_asset_path(QStringLiteral("brand/cuckoo.svg"));
    image_cacher first(source, &shared);
    image_cacher second(source, &shared);
    if (settings_namespace) {
        first.set_cache_namespace(raster_cache::cache_namespace::settings);
        second.set_cache_namespace(raster_cache::cache_namespace::settings);
    }
    QSignalSpy publications(&shared, &raster_cache::result_updated);
    first.set_target_size(target_size);
    QVERIFY(first.is_ready());
    second.set_target_size(target_size);
    QCOMPARE(second.pixmap().toImage(), first.pixmap().toImage());
    QCOMPARE(publications.count(), 1);
    QCOMPARE(shared.ready_entry_count(), 1);
    QCOMPARE(shared.in_flight_count(), 0);
}

void image_cacher_tests::published_pixels_outlive_producer_changes_data() {
    QTest::addColumn<bool>("settings_namespace");
    QTest::newRow("main") << false;
    QTest::newRow("settings") << true;
}

void image_cacher_tests::published_pixels_outlive_producer_changes() {
    QFETCH(bool, settings_namespace);
    raster_cache service;
    const QString source
        = bundled_asset_path(QStringLiteral("brand/cuckoo.svg"));
    image_cacher producer(source, &service);
    image_cacher observer(source, &service);
    if (settings_namespace) {
        producer.set_cache_namespace(raster_cache::cache_namespace::settings);
        observer.set_cache_namespace(raster_cache::cache_namespace::settings);
    }
    const QSize original_size(72, 72);
    std::optional<raster_cache::result> published;
    QImage displayed_at_publication;
    QObject::connect(
        &service, &raster_cache::result_updated, &service,
        [&](const raster_cache::entry_key& key) {
            if (published.has_value()) {
                return;
            }
            published = service.get_if_ready(key);
            if (!published.has_value()) {
                return;
            }
            displayed_at_publication = producer.pixmap().toImage();
            observer.set_target_size(original_size);
        }
    );
    producer.set_target_size(original_size);
    QVERIFY(published.has_value());
    QVERIFY(!displayed_at_publication.isNull());
    QCOMPARE(published->single_image, displayed_at_publication);
    QCOMPARE(observer.pixmap().toImage(), displayed_at_publication);
    QCOMPARE(service.ready_entry_count(), 1);
    QCOMPARE(service.in_flight_count(), 0);

    QPixmap modified = producer.pixmap();
    modified.fill(Qt::magenta);
    QCOMPARE(modified.toImage().pixelColor(0, 0), QColor(Qt::magenta));
    QCOMPARE(producer.pixmap().toImage(), displayed_at_publication);
    QCOMPARE(published->single_image, displayed_at_publication);
    QCOMPARE(observer.pixmap().toImage(), displayed_at_publication);

    producer.set_target_size(QSize(144, 72));
    QVERIFY(producer.pixmap().size() != displayed_at_publication.size());
    QCOMPARE(observer.pixmap().toImage(), displayed_at_publication);
    QCOMPARE(published->single_image, displayed_at_publication);
    QCOMPARE(service.ready_entry_count(), 1);
    QCOMPARE(service.in_flight_count(), 0);

    producer.set_target_size(QSize());
    QCOMPARE(service.ready_entry_count(), 0);
    QVERIFY(!producer.is_ready());
    QCOMPARE(observer.pixmap().toImage(), displayed_at_publication);
    QCOMPARE(published->single_image, displayed_at_publication);
}

void image_cacher_tests::replaces_oversized_and_undersized_rasters() {
    raster_cache service;
    image_cacher cacher(
        bundled_asset_path(QStringLiteral("brand/logo.svg")), &service
    );
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
    image_cacher cacher(
        bundled_asset_path(QStringLiteral("brand/logo.svg")), &service
    );
    cacher.set_target_size(QSize(100, 100));
    QVERIFY(cacher.is_ready());
    QCOMPARE(service.ready_entry_count(), 1);

    cacher.set_target_size(QSize());

    QVERIFY(!cacher.is_ready());
    QCOMPARE(service.ready_entry_count(), 0);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
