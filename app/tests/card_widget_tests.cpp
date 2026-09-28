// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/card_widget_tests.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
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
    QVERIFY(!widget.card_face_pixmap(0).isNull());
    QVERIFY(!widget.card_face_raster_size.isEmpty());

    QCOMPARE(widget.card_face_pixmap(0).size(), target_size);
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

void card_widget_tests::accumulated_count_survives_presentation_changes() {
    card_widget widget;
    card_session_state state;
    state.cards_per_deck = 52;
    state.decks_count = 1;
    for (int index = 0; index < 52; ++index) {
        state.deck.push_back(index);
    }
    state.deck_position = 2;
    QVERIFY(widget.restore_session_state(state));
    QVector<int> weights(13, 0);
    weights[0] = 2;
    weights[1] = -1;
    weights[2] = 3;
    weights[3] = -2;
    widget.set_strategy_weights(weights);
    QCOMPARE(widget.current_total_weight(), 4); // Not last-card weight 3.

    widget.resize(200, 300);
    widget.set_slot_rotated(true);
    widget.set_hide_cards(true);
    widget.set_show_card_indexing(true);
    widget.set_show_strategy_name(true);
    QCOMPARE(widget.current_total_weight(), 4);
    QCOMPARE(widget.capture_session_state().deck, state.deck);
    QCOMPARE(widget.current_position(), 2);
    widget.set_hide_cards(false);
    widget.set_running(true);
    widget.advance_card();
    QCOMPARE(widget.current_total_weight(), 2);
    widget.set_running(false);
    widget.advance_card();
    QCOMPARE(widget.current_position(), 3);
    QCOMPARE(widget.current_total_weight(), 2);
}

void card_widget_tests::recent_stack_retains_five_transforms() {
    card_widget widget;
    widget.start_quiz(0, 1, false);
    widget.set_running(true);
    // Use known transforms, not probabilistic assertions about RNG output.
    for (int index = 0; index < 8; ++index) {
        widget.card_rotation_deg = index * 0.25;
        widget.card_offset = QPointF(index * 0.1, -index * 0.1);
        widget.advance_card();
    }
    QCOMPARE(widget.discard_history.size(), size_t(5));
    for (size_t index = 0; index < widget.discard_history.size(); ++index) {
        const auto expected = static_cast<qreal>(index + 3);
        QCOMPARE(widget.discard_history[index].rotation_deg, expected * 0.25);
        QCOMPARE(
            widget.discard_history[index].offset,
            QPointF(expected * 0.1, -expected * 0.1)
        );
    }
    widget.set_slot_rotated(true);
    widget.resize(300, 200);
    QCOMPARE(widget.discard_history.size(), size_t(5));
    widget.clear_quiz();
    QVERIFY(widget.discard_history.empty());
    widget.start_quiz(0, 1, false);
    QVERIFY(widget.discard_history.empty());
}

void card_widget_tests::accessible_description_tracks_visible_card_state() {
    card_widget widget;
    QCOMPARE(widget.accessibleDescription(), str_label("Empty card slot"));
    widget.start_quiz(0, 1, false);
    const auto visible_description = [&widget] {
        return str_label("Current card: %1")
            .arg(card_label_from_index(widget.picker.current_card_index()));
    };
    QCOMPARE(widget.accessibleDescription(), str_label("Card back"));
    widget.set_running(true);
    QCOMPARE(widget.accessibleDescription(), visible_description());
    const auto state = widget.capture_session_state();
    widget.set_hide_cards(true);
    QCOMPARE(widget.accessibleDescription(), str_label("Card hidden"));
    widget.set_running(true);
    widget.advance_card();
    QCOMPARE(widget.accessibleDescription(), str_label("Card hidden"));
    widget.set_hide_cards(false);
    QCOMPARE(widget.accessibleDescription(), visible_description());
    widget.set_running(false);
    QCOMPARE(widget.accessibleDescription(), str_label("Card back"));
    widget.set_running(true);
    QCOMPARE(widget.accessibleDescription(), visible_description());
    widget.mark_deck_exhausted();
    QCOMPARE(widget.accessibleDescription(), str_label("Card back"));
    QVERIFY(widget.restore_session_state(state));
    QCOMPARE(widget.accessibleDescription(), str_label("Card back"));
    widget.set_running(true);
    QCOMPARE(widget.accessibleDescription(), visible_description());
    widget.set_hide_cards(true);
    widget.clear_quiz();
    QCOMPARE(widget.accessibleDescription(), str_label("Empty card slot"));
    widget.set_hide_cards(false);
    QCOMPARE(widget.accessibleDescription(), str_label("Empty card slot"));
}

void card_widget_tests::frame_choice_only_changes_presentation() {
    card_widget widget;
    widget.resize(240, 360);
    widget.start_quiz(0, 1, false);
    widget.set_running(true);
    widget.advance_card();
    widget.set_hide_cards(true); // Render framing without scheduling face jobs.
    QImage classic(widget.size(), QImage::Format_ARGB32_Premultiplied);
    classic.fill(Qt::transparent);
    widget.render(&classic); // Deliver any initial resize before the snapshot.
    const auto state = widget.capture_session_state();
    const auto target_size = widget.card_face_target_size();
    const auto rotation = widget.card_rotation_deg;
    const auto offset = widget.card_offset;
    const auto history_size = widget.discard_history.size();
    widget.set_frame_style(slot_frame_style::thin);
    QImage thin(widget.size(), QImage::Format_ARGB32_Premultiplied);
    thin.fill(Qt::transparent);
    widget.render(&thin);
    QVERIFY(classic != thin);
    QCOMPARE(widget.capture_session_state(), state);
    QCOMPARE(widget.card_face_target_size(), target_size);
    QCOMPARE(widget.card_rotation_deg, rotation);
    QCOMPARE(widget.card_offset, offset);
    QCOMPARE(widget.discard_history.size(), history_size);
    QVERIFY(!widget.rasterizing);
    widget.set_frame_style(slot_frame_style::classic);
    QImage restored(widget.size(), QImage::Format_ARGB32_Premultiplied);
    restored.fill(Qt::transparent);
    widget.render(&restored);
    QCOMPARE(restored, classic);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
