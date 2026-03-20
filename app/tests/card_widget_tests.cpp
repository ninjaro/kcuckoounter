#include "include/card_widget_tests.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"

#include <QtTest/QtTest>

static qint64 pixel_area(const QSize& size) {
    return static_cast<qint64>(size.width())
        * static_cast<qint64>(size.height());
}

void card_widget_tests::stretches_between_raster_intervals() {
    card_widget widget;
    widget.start_quiz(0, 1, false);

    const QSize initial_size(120, 180);
    const QSize initial_raster_size = widget.raster_cache_size(initial_size);
    widget.update_card_faces(initial_size);
    widget.rasterize_watcher.waitForFinished();
    QVERIFY2(!widget.card_faces.isEmpty(), "card faces were not rasterized");
    QCOMPARE(widget.card_face_raster_size, initial_raster_size);
    QCOMPARE(widget.card_face_size, initial_size);

    widget.picks_since_rasterize = 1;
    const QSize stretched_size(140, 200);
    widget.update_card_faces(stretched_size);
    QCOMPARE(widget.card_face_raster_size, initial_raster_size);
    QCOMPARE(widget.card_face_size, stretched_size);
    bool saw_scaled = false;
    for (const QPixmap& pixmap : widget.card_faces) {
        if (!pixmap.isNull()) {
            QCOMPARE(pixmap.size(), stretched_size);
            saw_scaled = true;
            break;
        }
    }
    QVERIFY2(saw_scaled, "no scaled card face was produced");

    widget.picks_since_rasterize = 3;
    const QSize reraster_size(160, 220);
    const QSize reraster_raster_size = widget.raster_cache_size(reraster_size);
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
    const QSize large_cache = widget.raster_cache_size(large_card);
    QVERIFY2(!large_card.isEmpty(), "large card size should not be empty");
    QVERIFY2(!large_cache.isEmpty(), "large cache size should not be empty");

    widget.resize(220, 340);
    const QSize small_card = widget.card_face_target_size();
    const QSize small_cache = widget.raster_cache_size(small_card);
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
    const QSize bigger_cache = widget.raster_cache_size(bigger_card);
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
    shared_faces.push_back(
        QImage(128, 196, QImage::Format_ARGB32_Premultiplied)
    );
    shared_faces[0].fill(Qt::red);
    shared_faces.push_back(
        QImage(128, 196, QImage::Format_ARGB32_Premultiplied)
    );
    shared_faces[1].fill(Qt::blue);

    widget.set_shared_card_faces(shared_faces, QSize(128, 196));
    QVERIFY(widget.has_shared_card_faces());

    const QSize target_size(120, 180);
    widget.update_card_faces(target_size);
    QVERIFY(!widget.rasterizing);
    QVERIFY(!widget.card_faces.isEmpty());
    QVERIFY(!widget.card_face_raster_size.isEmpty());

    bool saw_scaled = false;
    for (const QPixmap& pixmap : widget.card_faces) {
        if (pixmap.isNull()) {
            continue;
        }
        QCOMPARE(pixmap.size(), target_size);
        saw_scaled = true;
        break;
    }
    QVERIFY2(saw_scaled, "shared card faces should be scaled for display size");
}

void card_widget_tests::theme_source_change_invalidates_stale_raster_result() {
    card_widget widget;
    widget.start_quiz(0, 1, false);

    widget.card_sheet_source = str_label("assets/cards_0.svg");
    widget.card_sheet_renderer.load(widget.card_sheet_source);
    widget.start_rasterization(QSize(120, 180));

    widget.card_sheet_source = str_label("assets/cards_1.svg");
    widget.card_sheet_renderer.load(widget.card_sheet_source);
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
