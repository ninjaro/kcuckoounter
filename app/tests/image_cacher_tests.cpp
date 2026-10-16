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

void image_cacher_tests::publication_observers_can_retarget_producer_data() {
    QTest::addColumn<QString>("change");
    QTest::addColumn<bool>("settings_namespace");
    QTest::addColumn<bool>("warm");
    for (const char* change : { "size", "source", "namespace", "scale",
                                "minimum", "clear-size", "clear-source" }) {
        for (const bool settings_namespace : { false, true }) {
            for (const bool warm : { false, true }) {
                const QByteArray name = QByteArray(change)
                    + (settings_namespace ? "-settings" : "-main")
                    + (warm ? "-warm" : "-cold");
                QTest::newRow(name.constData()) << QString::fromLatin1(change)
                                                << settings_namespace << warm;
            }
        }
    }
}

void image_cacher_tests::publication_observers_can_retarget_producer() {
    QFETCH(QString, change);
    QFETCH(bool, settings_namespace);
    QFETCH(bool, warm);
    raster_cache service;
    raster_cache reference_service;
    const QString original_source
        = bundled_asset_path(QStringLiteral("brand/cuckoo.svg"));
    const QString next_source
        = bundled_asset_path(QStringLiteral("brand/logo.svg"));
    const auto initial_namespace = settings_namespace
        ? raster_cache::cache_namespace::settings
        : raster_cache::cache_namespace::main;
    const auto other_namespace = settings_namespace
        ? raster_cache::cache_namespace::main
        : raster_cache::cache_namespace::settings;
    image_cacher producer(original_source, &service);
    producer.set_cache_namespace(initial_namespace);
    if (warm)
        producer.set_target_size(QSize(200, 100));

    const QSize initial_size(72, 72);
    const QSize next_size(144, 72);
    bool changed = false;
    QVector<raster_cache::entry_key> publications;
    QVector<int> in_flight_at_publication;
    QObject::connect(
        &service, &raster_cache::result_updated, &service,
        [&](const raster_cache::entry_key& key) {
            publications.append(key);
            in_flight_at_publication.append(service.in_flight_count());
            if (changed)
                return;
            changed = true;
            if (change == QStringLiteral("size"))
                producer.set_target_size(next_size);
            else if (change == QStringLiteral("source"))
                producer.set_source(next_source);
            else if (change == QStringLiteral("namespace"))
                producer.set_cache_namespace(other_namespace);
            else if (change == QStringLiteral("scale"))
                producer.set_base_scale(2.0);
            else if (change == QStringLiteral("minimum"))
                producer.set_min_short_px(250);
            else if (change == QStringLiteral("clear-size"))
                producer.set_target_size(QSize());
            else
                producer.set_source(QString());
        },
        Qt::DirectConnection
    );
    producer.set_target_size(initial_size);
    QVERIFY(changed);

    QString expected_source = original_source;
    if (change == QStringLiteral("source"))
        expected_source = next_source;
    else if (change == QStringLiteral("clear-source"))
        expected_source.clear();
    image_cacher reference(expected_source, &reference_service);
    reference.set_cache_namespace(
        change == QStringLiteral("namespace") ? other_namespace
                                              : initial_namespace
    );
    if (change == QStringLiteral("scale"))
        reference.set_base_scale(2.0);
    if (change == QStringLiteral("minimum"))
        reference.set_min_short_px(250);
    QSize expected_size = initial_size;
    if (change == QStringLiteral("size"))
        expected_size = next_size;
    else if (change == QStringLiteral("clear-size"))
        expected_size = QSize();
    reference.set_target_size(expected_size);

    QCOMPARE(producer.display_size(), reference.display_size());
    QCOMPARE(producer.cache_namespace(), reference.cache_namespace());
    QCOMPARE(producer.has_source(), reference.has_source());
    QCOMPARE(producer.is_ready(), reference.is_ready());
    QCOMPARE(producer.pixmap().toImage(), reference.pixmap().toImage());
    QCOMPARE(service.ready_entry_count(), reference.is_ready() ? 1 : 0);
    QCOMPARE(service.in_flight_count(), 0);
    QCOMPARE(publications.size(), reference.is_ready() ? 2 : 1);
    if (reference.is_ready()) {
        const auto ready = service.get_if_ready(publications.constLast());
        QVERIFY(ready.has_value());
        QCOMPARE(ready->single_image, reference.pixmap().toImage());
    }
    for (const int count : in_flight_at_publication)
        QCOMPARE(count, 0);

    // Cleanup must follow the accepted callback result, then allow fresh work.
    producer.set_target_size(QSize());
    QCOMPARE(service.ready_entry_count(), 0);
    producer.set_source(original_source);
    producer.set_cache_namespace(initial_namespace);
    producer.set_base_scale(1.25);
    producer.set_min_short_px(63);
    producer.set_target_size(initial_size);
    QVERIFY(producer.is_ready());
    QCOMPARE(service.ready_entry_count(), 1);
    QCOMPARE(service.in_flight_count(), 0);
    producer.set_target_size(QSize());
    QCOMPARE(service.ready_entry_count(), 0);
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
