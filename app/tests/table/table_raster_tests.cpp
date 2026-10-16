// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "arch/asset_locator.hpp"
#include "table/table_tests.hpp"

#include "card_helpers/card_sheet.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "settings/training_progress.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/slot_settings.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"

#include <QElapsedTimer>
#include <QtTest/QtTest>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "image_lifetime_fixture.hpp"
#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::shared_faces_track_set_and_clear() {
    table_slot slot;
    QVERIFY(!slot.has_shared_card_faces());

    QVector<QImage> shared_faces;
    shared_faces.push_back(QImage(24, 36, QImage::Format_ARGB32_Premultiplied));
    shared_faces[0].fill(Qt::red);

    slot.set_shared_card_faces(shared_faces, QSize(24, 36));
    QVERIFY(slot.has_shared_card_faces());

    slot.clear_shared_card_faces();
    QVERIFY(!slot.has_shared_card_faces());
}

void table_tests::shared_cache_rasterization_populates_visible_slots() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(str_label("assets/non_existent_cards.svg"));

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(1);
    table_widget.show();
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 128)
    ));
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 4000);

    QCOMPARE(
        table_widget.shared_raster_cache_service()->ready_entry_count(), 1
    );
    const QList<table_slot*> slot_widgets
        = table_widget.findChildren<table_slot*>();
    QVERIFY(!slot_widgets.isEmpty());
    QVERIFY(slot_widgets.front()->has_shared_card_faces());
}

void table_tests::shared_cache_generation_cutover_stays_bounded() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(
        bundled_asset_path(QStringLiteral("sets/cards/cards_0.svg"))
    );

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(1);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 1024)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);

    set_card_sheet_source_path(
        bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg"))
    );
    table_widget.apply_theme();
    set_card_sheet_source_path(
        bundled_asset_path(QStringLiteral("sets/cards/cards_40.svg"))
    );
    table_widget.apply_theme();
    set_card_sheet_source_path(
        bundled_asset_path(QStringLiteral("sets/cards/cards_0.svg"))
    );
    table_widget.apply_theme();

    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 15000);

    QVERIFY(
        table_widget.shared_raster_cache_service()->ready_entry_count() <= 2
    );
    QVERIFY(table_widget.shared_raster_cache_service()->in_flight_count() <= 1);
}

void table_tests::shared_generation_cutover_keeps_single_visible_generation() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(
        bundled_asset_path(QStringLiteral("sets/cards/cards_0.svg"))
    );

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(3);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 256)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 10000);

    set_card_sheet_source_path(
        bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg"))
    );
    table_widget.apply_theme();
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 10000);

    QVERIFY(
        table_widget.shared_raster_cache_service()->ready_entry_count() <= 2
    );
    QVERIFY(table_widget.shared_raster_cache_service()->in_flight_count() <= 1);

    const QList<table_slot*> slot_list
        = table_widget.findChildren<table_slot*>();
    int visible_slot_count = 0;
    for (table_slot* slot : slot_list) {
        if (slot == nullptr || !slot->isVisible()) {
            continue;
        }
        ++visible_slot_count;
        QVERIFY(slot->has_shared_card_faces());
    }
    QVERIFY(visible_slot_count > 0);
}

void table_tests::theme_apply_clears_stale_worker_state() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(
        bundled_asset_path(QStringLiteral("sets/cards/cards_0.svg"))
    );

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(1);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 1024)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);

    set_card_sheet_source_path(
        bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg"))
    );
    table_widget.apply_theme();

    QTest::qWait(50);
    QCoreApplication::processEvents();

    QVERIFY(table_widget.shared_raster_cache_service()->in_flight_count() <= 1);
}

void table_tests::theme_and_resize_transitions_are_non_blocking() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(
        bundled_asset_path(QStringLiteral("sets/cards/cards_0.svg"))
    );

    table table_widget;
    table_widget.resize(920, 720);
    table_widget.set_slot_count(4);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 1024)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);

    set_card_sheet_source_path(
        bundled_asset_path(QStringLiteral("sets/cards/cards_40.svg"))
    );
    QElapsedTimer theme_timer;
    theme_timer.start();
    table_widget.apply_theme();
    const qint64 theme_apply_elapsed_ms = theme_timer.elapsed();

    QElapsedTimer resize_timer;
    resize_timer.start();
    table_widget.resize(1000, 760);
    QCoreApplication::processEvents();
    const qint64 resize_elapsed_ms = resize_timer.elapsed();

    QVERIFY(theme_apply_elapsed_ms < 350);
    QVERIFY(resize_elapsed_ms < 350);

    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 15000);
}

void table_tests::completed_shared_result_releases_future_data() {
    QTest::addColumn<bool>("accepted");
    QTest::newRow("accepted-then-evicted") << true;
    QTest::newRow("obsolete-generation") << false;
}

void table_tests::completed_shared_result_releases_future() {
    QFETCH(bool, accepted);
    const auto totals = std::make_shared<image_lifetime_test::accounting>();
    table table_widget;
    const auto key = table_widget.entry_key_for_generation(
        card_sheet_source_path(), 32, 7
    );
    table_widget.active_shared_generation_id = accepted ? 7 : 8;
    table_widget.active_card_sheet_source_id = key.source_id;
    table_widget.active_shared_bucket_px = 32;
    table_widget.active_shared_faces_key = key;
    table_widget.raster_cache_service.mark_in_flight(
        {
            .name_space = key.name_space,
            .kind = key.kind,
            .source_id = key.source_id,
            .render_scope = key.render_scope,
        },
        key
    );
    QVector<QImage> images;
    for (int index = 0; index < 55; ++index)
        images.append(image_lifetime_test::tracked_image(totals, 32));
    table_widget.shared_faces_watcher.setFuture(
        image_lifetime_test::completed_future(std::move(images))
    );
    table_widget.on_shared_rasterization_finished();
    QCOMPARE(table_widget.raster_cache_service.in_flight_count(), 0);
    if (accepted) {
        QCOMPARE(totals->live_images.load(), 55);
        const auto ready = table_widget.raster_cache_service.get_if_ready(key);
        QVERIFY(ready.has_value());
        QCOMPARE(ready->face_images.size(), 55);
        QCOMPARE(ready->face_images.front().pixelColor(0, 0), QColor(Qt::red));
    }
    table_widget.raster_cache_service.erase_result(key);
    QCOMPARE(table_widget.raster_cache_service.ready_entry_count(), 0);
    QCOMPARE(totals->live_bytes.load(), qsizetype { 0 });
    QCOMPARE(totals->live_images.load(), 0);
    table_widget.on_shared_rasterization_finished();
    QCoreApplication::processEvents();
    QVERIFY(!table_widget.active_shared_faces_key.has_value());
}

void table_tests::retiring_warming_generation_cancels_shared_work_data() {
    QTest::addColumn<bool>("completed");
    QTest::newRow("worker-still-running") << false;
    QTest::newRow("finished-before-gui-delivery") << true;
}

void table_tests::retiring_warming_generation_cancels_shared_work() {
    QFETCH(bool, completed);
    table table_widget;
    const QString source = card_sheet_source_path();
    table_widget.begin_warming_generation(source, 32);
    const auto key = *table_widget.warming_shared_faces_key;
    const raster_cache::family_key family {
        .name_space = key.name_space,
        .kind = key.kind,
        .source_id = key.source_id,
        .render_scope = key.render_scope,
    };
    table_widget.active_shared_faces_key = key;
    table_widget.raster_cache_service.mark_in_flight(family, key);

    // Hold completion independently of GUI delivery; no scheduler sleeps.
    QPromise<QVector<QImage>> producer;
    producer.start();
    const auto obsolete = producer.future();
    if (completed) {
        QVector<QImage> images(55, QImage(32, 32, QImage::Format_ARGB32));
        QVERIFY(producer.addResult(std::move(images)));
        producer.finish();
    }
    table_widget.shared_faces_watcher.setFuture(obsolete);
    QSignalSpy publications(
        &table_widget.raster_cache_service, &raster_cache::result_updated
    );

    table_widget.begin_warming_generation(source, 32);
    QVERIFY(!obsolete.isCanceled());

    // Resizing retires this warming generation; the old job has no consumers.
    table_widget.begin_warming_generation(source, 64);
    QVERIFY(obsolete.isCanceled());
    QVERIFY(!table_widget.raster_cache_service.is_in_flight(family));
    if (!completed) {
        // New demand waits until the canceled producer actually finishes.
        table_widget.on_shared_rasterization_requested(64);
        QVERIFY(table_widget.active_shared_faces_key.has_value());
        QVERIFY(*table_widget.active_shared_faces_key == key);
        producer.finish();
    }
    table_widget.on_shared_rasterization_finished();
    QVERIFY(!table_widget.active_shared_faces_key.has_value());
    QVERIFY(!table_widget.raster_cache_service.get_if_ready(key).has_value());
    QCOMPARE(publications.size(), 0);
    QCOMPARE(table_widget.raster_cache_service.in_flight_count(), 0);

    // A real subsequent worker still publishes one complete current deck.
    table_widget.on_shared_rasterization_requested(64);
    const auto current = *table_widget.warming_shared_faces_key;
    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 4000);
    const auto ready = table_widget.raster_cache_service.get_if_ready(current);
    QVERIFY(ready.has_value());
    QCOMPARE(ready->face_images.size(), required_card_ids_with_back().size());
    QCOMPARE(publications.size(), 1);
    QCOMPARE(table_widget.raster_cache_service.in_flight_count(), 0);
    QVERIFY(!table_widget.active_shared_faces_key.has_value());
}

void table_tests::shared_worker_publishes_only_complete_decks_data() {
    QTest::addColumn<QString>("source");
    QTest::addColumn<bool>("canceled");
    QTest::addColumn<int>("bucket");
    const QString bundled
        = bundled_asset_path(QStringLiteral("sets/cards/cards_0.svg"));
    QTest::newRow("bundled") << bundled << false << 32;
    QTest::newRow("alternate")
        << bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg")) << false
        << 32;
    QTest::newRow("canceled-before-parse") << bundled << true << 32;
    QTest::newRow("invalid-size") << bundled << false << 0;
}

void table_tests::shared_worker_publishes_only_complete_decks() {
    QFETCH(QString, source);
    QFETCH(bool, canceled);
    QFETCH(int, bucket);
    QPromise<QVector<QImage>> producer;
    producer.start();
    auto future = producer.future();
    if (canceled)
        future.cancel();
    table::render_shared_faces(producer, source, bucket);
    producer.finish();

    if (canceled || bucket <= 0) {
        QCOMPARE(future.resultCount(), 0);
        return;
    }
    QCOMPARE(future.resultCount(), 1);
    const auto images = future.result();
    QCOMPARE(images.size(), required_card_ids_with_back().size());
    const auto expected
        = rasterize_card_faces_with_fallback(source, QSize(bucket, bucket));
    QCOMPARE(images, expected);
}

void table_tests::destruction_retires_raster_notifications() {
    launcher_preferences_guard preferences;
    auto window = std::make_unique<main_window>();
    auto* view = window->findChild<table*>();
    QVERIFY(view);
    QVERIFY(view->install_gameplay_session(owned_table_fixture(1)));
    QVERIFY(!view->shared_faces_watcher.isRunning());
    QSignalSpy notifications(view, &table::rasterization_busy_changed);
    QSignalSpy destruction(view, &QObject::destroyed);

    // Ordinary busy notifications still reach the living application shell.
    view->update_rasterization_state(view->slot_widgets.front(), true);
    QCOMPARE(notifications.size(), 1);
    notifications.clear();

    // QWidget destroys the table after the main_window destructor has run.
    // Clearing its slots must not call back into that retired derived shell.
    window.reset();
    QCOMPARE(notifications.size(), 0);
    QCOMPARE(destruction.size(), 1);
}

void table_tests::destruction_cancels_shared_work_data() {
    QTest::addColumn<bool>("completed");
    QTest::newRow("worker-still-running") << false;
    QTest::newRow("finished-before-gui-delivery") << true;
}

void table_tests::destruction_cancels_shared_work() {
    QFETCH(bool, completed);
    const QString source = card_sheet_source_path();
    const auto assign_work
        = [&source](table& view, const QFuture<QVector<QImage>>& future) {
              view.begin_warming_generation(source, 32);
              const auto key = *view.warming_shared_faces_key;
              view.active_shared_faces_key = key;
              view.raster_cache_service.submit_request({
                  .name_space = key.name_space,
                  .kind = key.kind,
                  .source_id = key.source_id,
                  .render_scope = key.render_scope,
                  .target_bucket_px = key.target_bucket_px,
              });
              view.shared_faces_watcher.setFuture(future);
              return key;
          };

    QPromise<QVector<QImage>> departing_producer;
    QPromise<QVector<QImage>> surviving_producer;
    departing_producer.start();
    surviving_producer.start();
    const auto departing = departing_producer.future();
    const auto surviving = surviving_producer.future();
    if (completed) {
        QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        QVERIFY(departing_producer.addResult(QVector<QImage>(55, image)));
        departing_producer.finish();
    }
    auto owner = std::make_unique<table>();
    table survivor;
    assign_work(*owner, departing);
    const auto surviving_key = assign_work(survivor, surviving);
    QSignalSpy destruction(owner.get(), &QObject::destroyed);
    QSignalSpy publications(
        &survivor.raster_cache_service, &raster_cache::result_updated
    );
    owner.reset();

    QCOMPARE(destruction.size(), 1);
    QVERIFY(departing.isCanceled());
    QVERIFY(!surviving.isCanceled());
    if (!completed) {
        QVERIFY(!departing_producer.addResult(QVector<QImage>()));
        departing_producer.finish();
    } else {
        // Cancellation does not consume an independent observer's snapshot.
        QCOMPARE(departing.result().size(), 55);
        QCOMPARE(departing.result().front().pixelColor(0, 0), QColor(Qt::red));
    }
    QCoreApplication::processEvents();
    QCOMPARE(publications.size(), 0);

    // Another table requesting the same source retains its own job and cache.
    table::render_shared_faces(surviving_producer, source, 32);
    surviving_producer.finish();
    survivor.on_shared_rasterization_finished();
    QCoreApplication::processEvents();
    const auto ready
        = survivor.raster_cache_service.get_if_ready(surviving_key);
    QVERIFY(ready.has_value());
    QCOMPARE(ready->face_images.size(), required_card_ids_with_back().size());
    QCOMPARE(publications.size(), 1);
    QCOMPARE(survivor.raster_cache_service.in_flight_count(), 0);
    QVERIFY(!survivor.active_shared_faces_key.has_value());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
