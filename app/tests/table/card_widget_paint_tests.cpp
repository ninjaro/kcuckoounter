// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/card_widget_tests.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "table/card_widget.hpp"
#include "table/gameplay_generation.hpp"

#include <QPainter>
#include <QtTest/QtTest>

#include "table/card_widget_fixture.hpp"

using namespace card_test;

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
#ifdef KC_KDE
        return i18n(
            "Current card: %1",
            card_label_from_index(widget.picker.current_card_index())
        );
#else
        return str_label("Current card: %1")
            .arg(card_label_from_index(widget.picker.current_card_index()));
#endif
    };
    QCOMPARE(widget.accessibleDescription(), str_label("Card back"));
    widget.set_running(true);
    QCOMPARE(widget.accessibleDescription(), visible_description());
    QVERIFY(!widget.accessibleDescription().contains(
        QStringLiteral("I18N_ARGUMENT_MISSING")
    ));
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

void card_widget_tests::rotated_stack_stays_inside_slot() {
    card_widget widget;
    widget.set_shared_card_faces_mode(true);
    widget.start_quiz(0, 1, false);
    QImage face(16, 24, QImage::Format_ARGB32_Premultiplied);
    face.fill(Qt::magenta);
    widget.set_shared_card_faces(QVector<QImage>(55, face), face.size());
    widget.show();
    for (const QSize size :
         { QSize(1920, 1080), QSize(1080, 1920), QSize(60, 42) }) {
        widget.resize(size);
        widget.set_slot_rotated(size.height() > size.width());
        for (const auto style :
             { slot_frame_style::classic, slot_frame_style::thin }) {
            widget.set_frame_style(style);
            for (const qreal angle : { -3.5, 3.5 }) {
                widget.card_rotation_deg = angle;
                widget.card_offset = QPointF(7.2, 7.2);
                QImage rendered(size, QImage::Format_ARGB32_Premultiplied);
                rendered.fill(Qt::transparent);
                widget.render(&rendered);
                const QRect interior = rendered.rect().adjusted(4, 4, -4, -4);
                for (int y = 0; y < rendered.height(); ++y) {
                    for (int x = 0; x < rendered.width(); ++x) {
                        if (!interior.contains(x, y)) {
                            QVERIFY2(
                                rendered.pixelColor(x, y)
                                    != QColor(Qt::magenta),
                                qPrintable(QStringLiteral(
                                               "card reaches slot edge at "
                                               "%1,%2 in %3x%4"
                                )
                                               .arg(x)
                                               .arg(y)
                                               .arg(size.width())
                                               .arg(size.height()))
                            );
                        }
                    }
                }
            }
        }
    }
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

void card_widget_tests::retained_transforms_fit_after_resize() {
    card_widget widget;
    widget.set_shared_card_faces_mode(true);
    widget.show();
    widget.discard_history = { { -3.5, { -7.2, -7.2 } },
                               { 3.5, { 7.2, 7.2 } },
                               { -1.75, { -7.2, 7.2 } },
                               { 1.75, { 7.2, -7.2 } },
                               { 0.0, { 0.0, 0.0 } } };
    const auto history = widget.discard_history;
    QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    for (const auto size : { QSize(1920, 1080), QSize(120, 85), QSize(42, 60),
                             QSize(16, 24), QSize(1080, 1920) }) {
        widget.resize(size);
        for (const bool rotated : { false, true }) {
            widget.set_slot_rotated(rotated);
            const auto geometry = widget.layout_geometry();
            QVERIFY(!geometry.card.isEmpty());
            QCOMPARE(
                widget.card_face_target_size(),
                geometry.card.size().toSize().expandedTo(QSize(1, 1))
            );
            for (const auto& discard : widget.discard_history) {
                painter.resetTransform();
                card_widget::apply_card_transform(
                    painter, geometry.card, geometry.slot_rotation,
                    discard.rotation_deg,
                    geometry.bounded_offset(discard.offset)
                );
                const QRectF bounds
                    = painter.transform().mapRect(geometry.card);
                QVERIFY2(
                    geometry.frame.contains(bounds),
                    "rotated discard leaves its frame after resize"
                );
            }
            // Projection may clamp an old pixel offset, but history is not
            // rewritten every time the window shrinks and grows again.
            QCOMPARE(widget.discard_history.size(), history.size());
            for (size_t i = 0; i < history.size(); ++i) {
                QCOMPARE(
                    widget.discard_history[i].rotation_deg,
                    history[i].rotation_deg
                );
                QCOMPARE(widget.discard_history[i].offset, history[i].offset);
            }
        }
    }
}
