// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "arch/asset_locator.hpp"
#include "table/card_widget_tests.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "table/card_widget.hpp"
#include "table/gameplay_generation.hpp"

#include <QFile>
#include <QPointer>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "image_lifetime_fixture.hpp"
#include "table/card_widget_fixture.hpp"

using namespace card_test;

void card_widget_tests::stretches_between_raster_intervals() {
    card_widget widget;
    widget.start_quiz(0, 1, false);

    const QSize initial_size(120, 180);
    const QSize initial_raster_size
        = card_widget::raster_cache_size(initial_size);
    widget.update_card_faces(initial_size);
    QVERIFY2(
        widget.rasterizing,
        "card-sheet assets must be available to start the job"
    );
    widget.rasterize_watcher.waitForFinished();
    QVERIFY2(
        !widget.card_face_pixmap(0).isNull(), "card faces were not rasterized"
    );
    QCOMPARE(widget.card_face_raster_size, initial_raster_size);
    QCOMPARE(widget.card_face_size, initial_size);

    widget.picks_since_rasterize = 1;
    const QSize stretched_size(140, 200);
    widget.update_card_faces(stretched_size);
    QCOMPARE(widget.card_face_raster_size, initial_raster_size);
    QCOMPARE(widget.card_face_size, stretched_size);
    QCOMPARE(widget.card_face_pixmap(0).size(), stretched_size);

    widget.picks_since_rasterize = 3;
    const QSize reraster_size(160, 220);
    const QSize reraster_raster_size
        = card_widget::raster_cache_size(reraster_size);
    widget.update_card_faces(reraster_size);
    widget.rasterize_watcher.waitForFinished();
    QCOMPARE(widget.card_face_raster_size, reraster_raster_size);
    QCOMPARE(widget.card_face_size, reraster_size);
    QCOMPARE(widget.picks_since_rasterize, 0);
}

void card_widget_tests::memory_cache_tracks_resize() {
    card_widget widget;
    widget.start_quiz(0, 1, false);

    widget.resize(360, 520);
    const QSize large_card = widget.card_face_target_size();
    const QSize large_cache = card_widget::raster_cache_size(large_card);
    QVERIFY2(!large_card.isEmpty(), "large card size should not be empty");
    QVERIFY2(!large_cache.isEmpty(), "large cache size should not be empty");

    widget.resize(220, 340);
    const QSize small_card = widget.card_face_target_size();
    const QSize small_cache = card_widget::raster_cache_size(small_card);
    QVERIFY2(!small_card.isEmpty(), "small card size should not be empty");
    QVERIFY2(!small_cache.isEmpty(), "small cache size should not be empty");
    QVERIFY2(
        pixel_area(small_card) < pixel_area(large_card),
        "resizing down should reduce card size"
    );
    QVERIFY2(
        pixel_area(small_cache) < pixel_area(large_cache),
        "resizing down should reduce raster cache size"
    );

    widget.resize(480, 640);
    const QSize bigger_card = widget.card_face_target_size();
    const QSize bigger_cache = card_widget::raster_cache_size(bigger_card);
    QVERIFY2(
        pixel_area(bigger_card) > pixel_area(small_card),
        "resizing up should increase card size"
    );
    QVERIFY2(
        pixel_area(bigger_cache) > pixel_area(small_cache),
        "resizing up should increase raster cache size"
    );
}

void card_widget_tests::local_resize_coalesces_latest_demand_data() {
    QTest::addColumn<QVector<QSize>>("targets");
    QTest::addColumn<bool>("change_source");
    QTest::addColumn<bool>("completed");
    QTest::addColumn<bool>("retained_cache");
    const QSize active(40, 60);
    const QSize larger(60, 90);
    const QSize smaller(20, 30);
    for (bool completed : { false, true }) {
        const QByteArray suffix = completed ? "-awaiting-delivery" : "-running";
        QTest::newRow(("up-and-back" + suffix).constData())
            << QVector<QSize> { larger, active } << false << completed << false;
        QTest::newRow(("down-and-back" + suffix).constData())
            << QVector<QSize> { smaller, active } << false << completed
            << false;
        QTest::newRow(("several-and-back" + suffix).constData())
            << QVector<QSize> { larger, smaller, active } << false << completed
            << false;
        QTest::newRow(("newest-size" + suffix).constData())
            << QVector<QSize> { larger, smaller } << false << completed
            << false;
        QTest::newRow(("same-size-new-source" + suffix).constData())
            << QVector<QSize> { larger, active } << true << completed << false;
        QTest::newRow(("return-to-ready-cache" + suffix).constData())
            << QVector<QSize> { larger, smaller } << false << completed << true;
    }
}

void card_widget_tests::local_resize_coalesces_latest_demand() {
    QFETCH(QVector<QSize>, targets);
    QFETCH(bool, change_source);
    QFETCH(bool, completed);
    QFETCH(bool, retained_cache);
    const QString original_source = card_sheet_source_path();
    const auto restore_source = qScopeGuard([&original_source] {
        set_card_sheet_source_path(original_source);
    });
    const QString source
        = bundled_asset_path(QStringLiteral("sets/cards/cards_0.svg"));
    set_card_sheet_source_path(source);
    const QSize active(40, 60);
    const QSize active_raster = card_widget::raster_cache_size(active);
    const auto images
        = rasterize_card_faces_with_fallback(source, active_raster);
    QCOMPARE(images.size(), 55);
    QPromise<QVector<QImage>> producer;
    producer.start();
    const auto current = producer.future();
    QVERIFY(producer.addResult(images));
    if (completed) {
        producer.finish();
    }

    card_widget widget;
    if (retained_cache) {
        const QSize cached = card_widget::raster_cache_size(targets.back());
        widget.apply_rasterized_images(
            rasterize_card_faces_with_fallback(source, cached), cached
        );
        widget.picks_since_rasterize = 3;
    }
    QSignalSpy busy(&widget, &card_widget::rasterization_busy_changed);
    widget.raster_task_size = active_raster;
    widget.raster_task_source = source;
    widget.set_rasterizing(true);
    widget.rasterize_watcher.setFuture(current);
    widget.update_card_faces(active);
    for (const auto& size : targets) {
        widget.update_card_faces(size);
    }
    if (change_source) {
        set_card_sheet_source_path(
            bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg"))
        );
        widget.sync_card_sheet_source();
    }
    const QSize wanted = card_widget::raster_cache_size(targets.back());
    const bool follow_up
        = change_source || (!retained_cache && wanted != active_raster);
    QCOMPARE(widget.pending_raster_size, follow_up ? wanted : QSize());
    QVERIFY(!current.isCanceled());
    QCOMPARE(busy.size(), 1);
    if (!completed) {
        producer.finish();
    }
    widget.rasterize_watcher.waitForFinished();
    QCOMPARE(widget.rasterizing, follow_up);
    if (follow_up) {
        widget.rasterize_watcher.waitForFinished();
    }
    QVERIFY(!widget.rasterizing);
    QCOMPARE(widget.pending_raster_size, QSize());
    const QSize expected_raster = follow_up ? wanted : active_raster;
    QCOMPARE(widget.card_face_raster_size, expected_raster);
    QCOMPARE(widget.card_face_size, targets.back());
    QCOMPARE(widget.raster_task_source, widget.card_sheet_source);
    QCOMPARE(
        widget.card_faces_rasterized,
        rasterize_card_faces_with_fallback(
            widget.card_sheet_source, expected_raster
        )
    );
    QCOMPARE(busy.size(), 2);
    QCOMPARE(busy.at(0).front().toBool(), true);
    QCOMPARE(busy.at(1).front().toBool(), false);
    QCoreApplication::processEvents();
    QVERIFY(!widget.rasterizing);
}

void card_widget_tests::shared_faces_disable_local_rasterization() {
    card_widget widget;
    widget.start_quiz(0, 1, false);

    QVector<QImage> shared_faces;
    shared_faces.push_back(QImage(128, 196, QImage::Format_ARGB32_Premultiplied)
    );
    shared_faces[0].fill(Qt::red);
    shared_faces.push_back(QImage(128, 196, QImage::Format_ARGB32_Premultiplied)
    );
    shared_faces[1].fill(Qt::blue);

    widget.set_shared_card_faces(shared_faces, QSize(128, 196));
    QVERIFY(widget.has_shared_card_faces());

    const QSize target_size(120, 180);
    widget.update_card_faces(target_size);
    QVERIFY(!widget.rasterizing);
    QVERIFY(!widget.card_face_pixmap(0).isNull());
    QVERIFY(!widget.card_face_raster_size.isEmpty());

    QCOMPARE(widget.card_face_pixmap(0).size(), target_size);
}

void card_widget_tests::shared_faces_outlive_source_file() {
    const QString original_source = card_sheet_source_path();
    const auto restore_source = qScopeGuard([&original_source] {
        set_card_sheet_source_path(original_source);
    });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("cards.svg"));
    QVERIFY(QFile::copy(
        bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg")), source
    ));
    set_card_sheet_source_path(source);
    QCOMPARE(card_sheet_source_path(), source);

    const QSize size(32, 48);
    const QImage face = rasterize_card_face_with_fallback(
        source, card_element_ids().front(), size
    );
    QVERIFY(!face.isNull());
    QVERIFY(QFile::remove(source));

    card_widget widget;
    widget.set_shared_card_faces_mode(true);
    widget.set_shared_card_faces({ face }, size);
    widget.update_card_faces(size);
    QVERIFY(widget.has_shared_card_faces());
    QVERIFY(!widget.rasterizing);
    QCOMPARE(
        widget.card_face_pixmap(0).toImage(), QPixmap::fromImage(face).toImage()
    );

    const QSize resized(20, 30);
    widget.update_card_faces(resized);
    QCOMPARE(widget.card_face_raster_size, size);
    QCOMPARE(widget.card_face_pixmap(0).size(), resized);
    QVERIFY(!widget.rasterizing);
}

void card_widget_tests::source_validation_follows_raster_ownership() {
    card_widget widget;
    QVERIFY(!widget.card_sheet_renderer);
    widget.set_shared_card_faces_mode(true);
    widget.update_card_faces(QSize(20, 30));
    QVERIFY(!widget.card_sheet_renderer);
    QVERIFY(!widget.rasterizing);

    widget.set_shared_card_faces_mode(false);
    QVERIFY(widget.card_sheet_renderer);
    QVERIFY(widget.card_sheet_renderer->isValid());
    widget.rasterize_watcher.waitForFinished();
    const auto faces = widget.card_faces_rasterized;
    const auto raster_size = widget.card_face_raster_size;
    QVERIFY(!widget.card_face_pixmap(0).isNull());
    QPointer<QSvgRenderer> local_source = widget.card_sheet_renderer.get();

    widget.set_shared_card_faces_mode(true);
    QVERIFY(local_source.isNull());
    widget.set_shared_card_faces(faces, raster_size);
    QVERIFY(!widget.card_face_pixmap(0).isNull());
    widget.update_card_faces(QSize(24, 36));
    QVERIFY(!widget.card_sheet_renderer);
    QVERIFY(!widget.rasterizing);

    widget.clear_shared_card_faces();
    widget.set_shared_card_faces_mode(false);
    QVERIFY(widget.card_sheet_renderer);
    QVERIFY(widget.card_sheet_renderer->isValid());
    local_source = widget.card_sheet_renderer.get();
    widget.set_shared_card_faces(faces, raster_size);
    QVERIFY(local_source.isNull());
    QVERIFY(!widget.rasterizing);
}

void card_widget_tests::standalone_source_validation_rejects_invalid_svg() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile source(directory.filePath(QStringLiteral("invalid.svg")));
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("not an SVG"), qint64 { 10 });
    source.close();

    card_widget widget;
    widget.card_sheet_source = source.fileName();
    widget.update_card_faces(QSize(20, 30));
    QVERIFY(!widget.rasterizing);
    QVERIFY(widget.card_faces_rasterized.isEmpty());
    QVERIFY(widget.card_face_pixmap(0).isNull());

    widget.sync_card_sheet_source();
    widget.update_card_faces(QSize(20, 30));
    QVERIFY(widget.rasterizing);
    widget.rasterize_watcher.waitForFinished();
    QVERIFY(!widget.card_face_pixmap(0).isNull());
}

void card_widget_tests::shared_faces_reject_late_local_completion_data() {
    QTest::addColumn<bool>("shared_ready");
    QTest::newRow("awaiting-shared-faces") << false;
    QTest::newRow("shared-faces-ready") << true;
}

void card_widget_tests::shared_faces_reject_late_local_completion() {
    QFETCH(bool, shared_ready);
    const QSize shared_size(32, 48);
    const QImage face = rasterize_card_face_with_fallback(
        bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg")),
        card_element_ids().front(), shared_size
    );
    QVERIFY(!face.isNull());
    const QVector<QImage> shared_faces { face };

    card_widget widget;
    widget.update_card_faces(QSize(20, 30));
    QVERIFY(widget.rasterizing);
    // Join the real worker without delivering the queued GUI completion.
    widget.rasterize_watcher.future().waitForFinished();
    if (shared_ready) {
        widget.set_shared_card_faces(shared_faces, shared_size);
    } else {
        widget.set_shared_card_faces_mode(true);
    }

    widget.rasterize_watcher.waitForFinished();
    QVERIFY(!widget.rasterizing);
    if (!shared_ready) {
        QVERIFY(widget.card_faces_rasterized.isEmpty());
        widget.set_shared_card_faces(shared_faces, shared_size);
    }
    QCOMPARE(
        widget.card_faces_rasterized.constData(), shared_faces.constData()
    );
    QCOMPARE(widget.card_face_raster_size, shared_size);
    QVERIFY(widget.has_shared_card_faces());
    QCOMPARE(
        widget.card_face_pixmap(0).toImage(),
        QPixmap::fromImage(face)
            .scaled(
                QSize(20, 30), Qt::IgnoreAspectRatio, Qt::FastTransformation
            )
            .toImage()
    );

    widget.clear_shared_card_faces();
    widget.set_shared_card_faces_mode(false);
    widget.picks_since_rasterize = 3;
    widget.update_card_faces(QSize(24, 36));
    QVERIFY(widget.rasterizing);
    widget.rasterize_watcher.waitForFinished();
    QCOMPARE(
        widget.card_faces_rasterized.size(),
        required_card_ids_with_back().size()
    );
    QVERIFY(!widget.card_face_pixmap(0).isNull());
}

void card_widget_tests::local_work_retires_with_ownership_data() {
    QTest::addColumn<int>("ownership");
    QTest::addColumn<bool>("completed");
    QTest::newRow("destroy-running") << 0 << false;
    QTest::newRow("destroy-completed") << 0 << true;
    QTest::newRow("shared-waiting-running") << 1 << false;
    QTest::newRow("shared-waiting-completed") << 1 << true;
    QTest::newRow("shared-ready-running") << 2 << false;
    QTest::newRow("shared-ready-completed") << 2 << true;
}

void card_widget_tests::local_work_retires_with_ownership() {
    QFETCH(int, ownership);
    QFETCH(bool, completed);
    const QSize size(32, 48);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::red);
    const QVector<QImage> shared_faces(55, image);
    QPromise<QVector<QImage>> departing_producer;
    QPromise<QVector<QImage>> surviving_producer;
    departing_producer.start();
    surviving_producer.start();
    const auto departing = departing_producer.future();
    const auto surviving = surviving_producer.future();
    if (completed) {
        QVERIFY(departing_producer.addResult(shared_faces));
        departing_producer.finish();
    }

    auto owner = std::make_unique<card_widget>();
    card_widget survivor;
    for (card_widget* view : { owner.get(), &survivor }) {
        view->raster_task_size = size;
        view->raster_task_source = view->card_sheet_source;
        view->set_rasterizing(true);
    }
    owner->rasterize_watcher.setFuture(departing);
    survivor.rasterize_watcher.setFuture(surviving);
    QSignalSpy destruction(owner.get(), &QObject::destroyed);
    if (ownership == 0) {
        owner.reset();
        QCOMPARE(destruction.size(), 1);
    } else if (ownership == 1) {
        owner->set_shared_card_faces_mode(true);
    } else {
        owner->set_shared_card_faces(shared_faces, size);
    }
    QVERIFY(departing.isCanceled());
    QVERIFY(!surviving.isCanceled());
    if (owner) {
        QVERIFY(owner->rasterize_watcher.isFinished());
        QCOMPARE(owner->rasterize_watcher.future().resultCount(), 0);
    }
    if (!completed) {
        QVERIFY(!departing_producer.addResult(shared_faces));
        departing_producer.finish();
    } else {
        QCOMPARE(departing.result().constData(), shared_faces.constData());
    }
    QVERIFY(surviving_producer.addResult(shared_faces));
    surviving_producer.finish();
    survivor.rasterize_watcher.waitForFinished();
    QCOMPARE(
        survivor.card_faces_rasterized.constData(), shared_faces.constData()
    );
    QVERIFY(!survivor.rasterizing);

    if (owner) {
        if (ownership == 2) {
            // Return before delivery: a canceled local future may have no
            // result, and must not erase the last usable snapshot or block the
            // GUI.
            owner->clear_shared_card_faces();
        }
        owner->rasterize_watcher.waitForFinished();
        QVERIFY(!owner->rasterizing);
        if (ownership == 1) {
            QVERIFY(owner->card_faces_rasterized.isEmpty());
            owner->set_shared_card_faces(shared_faces, size);
        }
        QCOMPARE(
            owner->card_faces_rasterized.constData(), shared_faces.constData()
        );
        owner->clear_shared_card_faces();
        owner->set_shared_card_faces_mode(false);
        owner->picks_since_rasterize = 3;
        owner->update_card_faces(QSize(20, 30));
        QVERIFY(owner->rasterizing);
        owner->rasterize_watcher.waitForFinished();
        QVERIFY(!owner->rasterizing);
        QCOMPARE(owner->card_faces_rasterized.size(), 55);
        QVERIFY(!owner->card_face_pixmap(0).isNull());
    }
    QCoreApplication::processEvents();
}

void card_widget_tests::local_work_retires_without_valid_demand_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<bool>("completed");
    QTest::addColumn<bool>("recover_before_finish");
    for (const bool completed : { false, true }) {
        for (const bool recover_before_finish : { false, true }) {
            const QByteArray suffix
                = QByteArray(completed ? "-completed" : "-running")
                + (recover_before_finish ? "-early-recovery" : "-late-recovery"
                );
            QTest::newRow(("empty" + suffix).constData())
                << 0 << completed << recover_before_finish;
            QTest::newRow(("zero-width" + suffix).constData())
                << 1 << completed << recover_before_finish;
            QTest::newRow(("invalid-source" + suffix).constData())
                << 2 << completed << recover_before_finish;
        }
    }
}

void card_widget_tests::local_work_retires_without_valid_demand() {
    QFETCH(int, mode);
    QFETCH(bool, completed);
    QFETCH(bool, recover_before_finish);
    const QString original_source = card_sheet_source_path();
    const auto restore_source = qScopeGuard([&original_source] {
        set_card_sheet_source_path(original_source);
    });
    const QString source
        = bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg"));
    set_card_sheet_source_path(source);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString invalid_source
        = directory.filePath(QStringLiteral("invalid.svg"));
    QFile file(invalid_source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not an SVG"), qint64 { 10 });
    file.close();

    const QSize target(20, 30);
    const QSize raster = card_widget::raster_cache_size(target);
    const auto images = rasterize_card_faces_with_fallback(source, raster);
    QCOMPARE(images.size(), 55);
    QPromise<QVector<QImage>> departing_producer;
    QPromise<QVector<QImage>> surviving_producer;
    departing_producer.start();
    surviving_producer.start();
    const auto departing = departing_producer.future();
    const auto surviving = surviving_producer.future();
    if (completed) {
        QVERIFY(departing_producer.addResult(images));
        departing_producer.finish();
    }

    card_widget widget;
    card_widget survivor;
    widget.apply_rasterized_images(images, raster);
    QSignalSpy busy(&widget, &card_widget::rasterization_busy_changed);
    for (card_widget* view : { &widget, &survivor }) {
        view->card_face_size = target;
        view->raster_task_size = raster;
        view->raster_task_source = source;
        view->set_rasterizing(true);
    }
    QVERIFY(!widget.card_face_pixmap(0).isNull());
    widget.pending_raster_size = QSize(140, 210);
    widget.rasterize_watcher.setFuture(departing);
    survivor.rasterize_watcher.setFuture(surviving);
    if (mode == 2) {
        set_card_sheet_source_path(invalid_source);
        QCOMPARE(card_sheet_source_path(), invalid_source);
        widget.sync_card_sheet_source();
    } else {
        widget.update_card_faces(mode == 0 ? QSize() : QSize(0, 30));
    }
    QVERIFY(departing.isCanceled());
    QVERIFY(!surviving.isCanceled());
    QVERIFY(!widget.rasterizing);
    QVERIFY(widget.rasterize_watcher.isFinished());
    QCOMPARE(widget.rasterize_watcher.future().resultCount(), 0);
    QVERIFY(widget.pending_raster_size.isEmpty());
    QVERIFY(widget.card_face_size.isEmpty());
    QVERIFY(widget.card_faces_rasterized.isEmpty());
    QVERIFY(widget.card_face_raster_size.isEmpty());
    QVERIFY(widget.selected_card_face.isNull());
    QCOMPARE(busy.size(), 2);
    QCOMPARE(busy.at(0).front().toBool(), true);
    QCOMPARE(busy.at(1).front().toBool(), false);

    const auto recover = [&] {
        set_card_sheet_source_path(source);
        widget.sync_card_sheet_source();
        widget.update_card_faces(target);
    };
    if (recover_before_finish) {
        recover();
        QVERIFY(widget.rasterizing);
        QVERIFY(!widget.rasterize_watcher.isCanceled());
    }
    if (!completed) {
        QVERIFY(!departing_producer.addResult(images));
        departing_producer.finish();
    } else {
        QCOMPARE(departing.result().constData(), images.constData());
    }
    QVERIFY(surviving_producer.addResult(images));
    surviving_producer.finish();
    survivor.rasterize_watcher.waitForFinished();
    QCOMPARE(survivor.card_faces_rasterized.constData(), images.constData());
    QVERIFY(!survivor.rasterizing);
    if (!recover_before_finish) {
        widget.rasterize_watcher.waitForFinished();
        QVERIFY(widget.card_faces_rasterized.isEmpty());
        QVERIFY(!widget.rasterizing);
        recover();
    }
    widget.rasterize_watcher.waitForFinished();
    QVERIFY(!widget.rasterizing);
    QCOMPARE(widget.card_face_size, target);
    QCOMPARE(widget.card_face_raster_size, raster);
    QCOMPARE(widget.card_faces_rasterized, images);
    QVERIFY(!widget.card_face_pixmap(0).isNull());
    QCOMPARE(busy.size(), 4);
    QCoreApplication::processEvents();
    QCOMPARE(widget.card_faces_rasterized, images);
    QCOMPARE(busy.size(), 4);
}

void card_widget_tests::local_worker_publishes_only_complete_decks_data() {
    QTest::addColumn<QString>("source");
    QTest::addColumn<bool>("canceled");
    QTest::addColumn<QSize>("size");
    const QString bundled
        = bundled_asset_path(QStringLiteral("sets/cards/cards_0.svg"));
    QTest::newRow("bundled") << bundled << false << QSize(32, 48);
    QTest::newRow("alternate")
        << bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg")) << false
        << QSize(32, 48);
    QTest::newRow("fallback")
        << QStringLiteral(":/missing-source.svg") << false << QSize(32, 48);
    QTest::newRow("canceled-before-parse") << bundled << true << QSize(32, 48);
    QTest::newRow("invalid-size") << bundled << false << QSize();
}

void card_widget_tests::local_worker_publishes_only_complete_decks() {
    QFETCH(QString, source);
    QFETCH(bool, canceled);
    QFETCH(QSize, size);
    QPromise<QVector<QImage>> producer;
    producer.start();
    auto future = producer.future();
    if (canceled) {
        future.cancel();
    }
    card_widget::rasterize_card_faces(producer, source, size);
    producer.finish();
    if (canceled || size.isEmpty()) {
        QCOMPARE(future.resultCount(), 0);
        return;
    }
    QCOMPARE(future.resultCount(), 1);
    const auto images = future.result();
    QCOMPARE(images.size(), required_card_ids_with_back().size());
    QCOMPARE(images, rasterize_card_faces_with_fallback(source, size));
}

void card_widget_tests::selected_presentation_reuses_and_invalidates() {
    QVector<QImage> images;
    for (int index = 0; index < 55; ++index) {
        QImage image(32, 48, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor::fromHsv(index * 6, 180, 220));
        image.setPixelColor(10, 12, Qt::transparent);
        images.push_back(image);
    }
    images[3] = QImage(); // Missing face keeps the existing text fallback.
    card_widget first;
    card_widget second;
    for (card_widget* widget : { &first, &second }) {
        widget->set_shared_card_faces(images, QSize(32, 48));
        widget->update_card_faces(QSize(20, 30));
        QVERIFY(widget->selected_card_face.isNull());
        QCOMPARE(widget->card_faces_rasterized.constData(), images.constData());
        QCOMPARE(
            widget->card_faces_rasterized.at(0).constBits(),
            images.at(0).constBits()
        );
        QVERIFY(!widget->rasterizing);
    }
    const auto expected = [&images](int index, const QSize& size) {
        return QPixmap::fromImage(images.at(index))
            .scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation)
            .toImage();
    };
    for (int index :
         { 0, 1, 54, 0 }) { // Includes face advance and back display.
        QCOMPARE(
            first.card_face_pixmap(index).toImage(),
            expected(index, QSize(20, 30))
        );
        const auto key = first.selected_card_face.cacheKey();
        QCOMPARE(first.card_face_pixmap(index).cacheKey(), key);
        first.update_card_faces(QSize(20, 30));
        QCOMPARE(first.card_face_pixmap(index).cacheKey(), key);
    }
    QVERIFY(second.selected_card_face.isNull());
    QVERIFY(first.card_face_pixmap(3).isNull());
    QVERIFY(first.card_face_pixmap(-1).isNull());
    QVERIFY(first.card_face_pixmap(55).isNull());

    for (const QSize& size : { QSize(12, 18), QSize(28, 42) }) {
        first.update_card_faces(size);
        QVERIFY(first.selected_card_face.isNull());
        QCOMPARE(first.card_face_pixmap(0).toImage(), expected(0, size));
        QCOMPARE(first.card_face_raster_size, QSize(32, 48));
        QVERIFY(!first.rasterizing);
    }
    auto replacement = images;
    replacement[0].fill(Qt::green);
    first.set_shared_card_faces(replacement, QSize(32, 48));
    QVERIFY(first.selected_card_face.isNull());
    QCOMPARE(
        first.card_face_pixmap(0).toImage().pixelColor(0, 0), QColor(Qt::green)
    );
    QCOMPARE(second.card_face_pixmap(0).toImage(), expected(0, QSize(20, 30)));
    first.update_card_faces(QSize());
    QVERIFY(first.card_face_pixmap(0).isNull());
    QVERIFY(first.card_faces_rasterized.isEmpty());
}

void card_widget_tests::theme_source_change_invalidates_stale_raster_result() {
    card_widget widget;
    widget.start_quiz(0, 1, false);

    widget.card_sheet_source
        = bundled_asset_path(QStringLiteral("sets/cards/cards_0.svg"));
    widget.start_rasterization(QSize(120, 180));

    widget.card_sheet_source
        = bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg"));
    widget.pending_raster_size = QSize(120, 180);

    widget.rasterize_watcher.waitForFinished();
    if (widget.rasterizing) {
        widget.rasterize_watcher.waitForFinished();
    }

    QCOMPARE(widget.raster_task_source, widget.card_sheet_source);
    QVERIFY2(
        !widget.card_faces_rasterized.isEmpty(),
        "widget should start a follow-up rasterization for the new source"
    );
}

void card_widget_tests::completed_local_result_releases_future_data() {
    QTest::addColumn<int>("mode");
    QTest::newRow("accepted") << 0;
    QTest::newRow("shared-waiting") << 1;
    QTest::newRow("obsolete-source") << 2;
}

void card_widget_tests::completed_local_result_releases_future() {
    QFETCH(int, mode);
    const auto totals = std::make_shared<image_lifetime_test::accounting>();
    card_widget widget;
    widget.raster_task_size = QSize(32, 32);
    widget.raster_task_source
        = mode == 2 ? QStringLiteral("obsolete") : widget.card_sheet_source;
    widget.set_rasterizing(true);
    widget.rasterize_watcher.setFuture(image_lifetime_test::completed_future(
        QVector<QImage> { image_lifetime_test::tracked_image(totals, 32) }
    ));
    if (mode == 1)
        widget.set_shared_card_faces_mode(true);
    widget.on_rasterization_finished();
    if (mode == 0) {
        QCOMPARE(totals->live_images.load(), 1);
        widget.card_face_size = QSize(32, 32);
        QCOMPARE(
            widget.card_face_pixmap(0).toImage().pixelColor(0, 0),
            QColor(Qt::red)
        );
        widget.invalidate_selected_card_face();
        widget.card_faces_rasterized.clear();
    } else {
        QVERIFY(widget.card_faces_rasterized.isEmpty());
    }
    QCOMPARE(totals->live_bytes.load(), qsizetype { 0 });
    QCOMPARE(totals->live_images.load(), 0);
    // A repeated/queued completion after consumption must be harmless.
    widget.on_rasterization_finished();
    QCoreApplication::processEvents();
    widget.set_shared_card_faces_mode(false);
    widget.update_card_faces(QSize(32, 32));
    widget.rasterize_watcher.waitForFinished();
    QVERIFY(!widget.rasterizing);
    QCOMPARE(widget.card_faces_rasterized.size(), 55);
    QVERIFY(!widget.card_face_pixmap(0).isNull());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
