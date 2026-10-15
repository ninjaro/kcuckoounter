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

void card_widget_tests::standalone_source_validation_rejects_invalid_data() {
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
