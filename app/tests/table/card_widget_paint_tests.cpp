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
#include <array>

#include "table/card_widget_fixture.hpp"

using namespace card_test;

namespace {
QImage detailed_face() {
    QImage image(240, 360, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::black, 1.0));
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(QRectF(1, 1, 238, 358), 12, 12);
    painter.setPen(QPen(Qt::red, 1.0));
    for (int index = 10; index < 225; index += 7)
        painter.drawLine(QPointF(index, 18), QPointF(225, index + 90));
    painter.setPen(QPen(Qt::blue, 1.0));
    painter.drawRect(QRectF(15, 16, 31, 49));
    return image;
}
}

void card_widget_tests::selected_presentation_preserves_master_pixels() {
    card_widget widget;
    auto master = detailed_face();
    widget.set_presented_card_images(
        { { card_element_ids().front(), master } }
    );
    widget.update_card_faces(QSize(32, 48));
    master.setDevicePixelRatio(widget.devicePixelRatioF());
    QCOMPARE(widget.card_face_pixmap(0).toImage(), master);
    widget.update_card_faces(QSize(100, 150));
    QCOMPARE(widget.card_face_pixmap(0).toImage(), master);
}

void card_widget_tests::rotated_master_uses_smooth_single_transform_data() {
    QTest::addColumn<qreal>("angle");
    QTest::addColumn<QSize>("display");
    for (const auto& display : { QSize(32, 48), QSize(100, 150) })
        for (const qreal angle : { 0.0, 2.0, 3.5, 15.0, 90.0 })
            QTest::newRow(
                qPrintable(QStringLiteral("%1x%2-%3deg")
                               .arg(display.width())
                               .arg(display.height())
                               .arg(angle))
            ) << angle
              << display;
}

void card_widget_tests::rotated_master_uses_smooth_single_transform() {
    QFETCH(qreal, angle);
    QFETCH(QSize, display);
    const card_widget widget;
    const qreal dpr = widget.devicePixelRatioF();
    auto master = QPixmap::fromImage(detailed_face());
    master.setDevicePixelRatio(dpr);
    const auto canvas = [&] {
        QImage result(
            QSize(240, 240) * dpr, QImage::Format_ARGB32_Premultiplied
        );
        result.setDevicePixelRatio(dpr);
        result.fill(Qt::transparent);
        return result;
    };
    auto actual = canvas();
    auto expected = canvas();
    const QRectF destination(
        QPointF(120 - display.width() / 2.0, 120 - display.height() / 2.0),
        display
    );
    const QPointF offset(0.4, -0.2);
    {
        QPainter painter(&actual);
        painter.setRenderHint(QPainter::Antialiasing);
        card_widget::draw_card_pixmap(
            painter, destination, 0, angle, offset, master
        );
        QVERIFY(!painter.testRenderHint(QPainter::SmoothPixmapTransform));
    }
    {
        QPainter painter(&expected);
        painter.setRenderHints(
            QPainter::Antialiasing | QPainter::SmoothPixmapTransform
        );
        painter.translate(destination.center() + offset);
        painter.rotate(angle);
        painter.translate(-destination.center());
        // One final scaling/rotation from the complete 2:3 master, with no
        // display-sized intermediate and no angle-dependent image identity.
        painter.drawPixmap(destination, master, QRectF(master.rect()));
    }
    QCOMPARE(actual, expected);
}

void card_widget_tests::accumulated_count_survives_presentation_changes() {
    gameplay::session_configuration configuration;
    configuration.failure = gameplay::failure_policy::block;
    auto owner = gameplay::session::create(
                     configuration, { { "test", 1, false, true } }, { 1, 64 }
    )
                     .value();
    gameplay::prepared_deck stream;
    stream.cards = { 0, 1, 2, 3 };
    stream.rank_weights[0] = 2;
    stream.rank_weights[1] = -1;
    stream.rank_weights[2] = 3;
    stream.rank_weights[3] = -2;
    QVERIFY(owner.prepare_decks(std::array { stream }));
    QVERIFY(owner.set_traversal(std::array<std::size_t, 1> { 0 }));
    card_widget widget;
    QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
    QVERIFY(owner.start());
    for (int index = 0; index < 3; ++index)
        (void)owner.deal_step();
    widget.refresh_gameplay_deck();
    QCOMPARE(owner.deck({ 0 })->running_count, 4);
    const auto state = *owner.deck({ 0 });
    widget.resize(200, 300);
    widget.set_slot_rotated(true);
    widget.set_hide_cards(true);
    widget.set_show_card_indexing(true);
    widget.set_show_strategy_name(true);
    QCOMPARE(*owner.deck({ 0 }), state);
    widget.set_hide_cards(false);
    (void)owner.deal_step();
    widget.refresh_gameplay_deck();
    QCOMPARE(owner.deck({ 0 })->running_count, 2);
}

void card_widget_tests::recent_stack_retains_five_transforms() {
    auto widget_owner = renderer_session();
    card_widget widget;
    QVERIFY(widget.bind_gameplay_deck(widget_owner, { 0 }));
    QVERIFY(widget_owner.start());
    (void)widget_owner.deal_step();
    widget.refresh_gameplay_deck();
    // Use known transforms, not probabilistic assertions about RNG output.
    for (int index = 0; index < 8; ++index) {
        widget.card_rotation_deg = index * 0.25;
        widget.card_offset = QPointF(index * 0.1, -index * 0.1);
        (void)widget_owner.deal_step();
        widget.refresh_gameplay_deck();
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
    widget.unbind_gameplay_deck();
    QVERIFY(widget.discard_history.empty());
    QVERIFY(widget.bind_gameplay_deck(widget_owner, { 0 }));
    QVERIFY(widget.discard_history.empty());
}

void card_widget_tests::accessible_description_tracks_visible_card_state() {
    auto owner = renderer_session();
    card_widget widget;
    QCOMPARE(widget.accessibleDescription(), str_label("Empty card slot"));
    QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
    QCOMPARE(widget.accessibleDescription(), str_label("Card back"));
    QVERIFY(owner.start());
    (void)owner.deal_step();
    widget.refresh_gameplay_deck();
    const auto visible = widget.accessibleDescription();
    QVERIFY(
        visible.contains(card_label_from_index(widget.display_card_index()))
    );
    widget.set_hide_cards(true);
    QCOMPARE(widget.accessibleDescription(), str_label("Card hidden"));
    widget.set_hide_cards(false);
    QCOMPARE(widget.accessibleDescription(), visible);
    QVERIFY(owner.pause());
    widget.refresh_gameplay_deck();
    QCOMPARE(widget.accessibleDescription(), str_label("Card back"));
    QVERIFY(owner.resume());
    widget.refresh_gameplay_deck();
    QCOMPARE(widget.accessibleDescription(), visible);
    widget.unbind_gameplay_deck();
    QCOMPARE(widget.accessibleDescription(), str_label("Empty card slot"));
}

void card_widget_tests::frame_choice_only_changes_presentation() {
    auto widget_owner = renderer_session();
    card_widget widget;
    widget.resize(240, 360);
    QVERIFY(widget.bind_gameplay_deck(widget_owner, { 0 }));
    QVERIFY(widget_owner.start());
    (void)widget_owner.deal_step();
    widget.refresh_gameplay_deck();
    (void)widget_owner.deal_step();
    widget.refresh_gameplay_deck();
    widget.set_hide_cards(true); // Render framing without scheduling face jobs.
    QImage classic(widget.size(), QImage::Format_ARGB32_Premultiplied);
    classic.fill(Qt::transparent);
    widget.render(&classic); // Deliver any initial resize before the snapshot.
    const auto state = *widget_owner.deck({ 0 });
    const auto target_size = widget.card_face_target_size();
    const auto rotation = widget.card_rotation_deg;
    const auto offset = widget.card_offset;
    const auto history_size = widget.discard_history.size();
    widget.set_frame_style(slot_frame_style::thin);
    QImage thin(widget.size(), QImage::Format_ARGB32_Premultiplied);
    thin.fill(Qt::transparent);
    widget.render(&thin);
    QVERIFY(classic != thin);
    QCOMPARE(*widget_owner.deck({ 0 }), state);
    QCOMPARE(widget.card_face_target_size(), target_size);
    QCOMPARE(widget.card_rotation_deg, rotation);
    QCOMPARE(widget.card_offset, offset);
    QCOMPARE(widget.discard_history.size(), history_size);
    QVERIFY(!widget.rasterizing);
    // Gold piping paints its notch labels on the frame line only.
    widget.set_frame_style(slot_frame_style::piping);
    widget.set_slot_number(3);
    QImage piping(widget.size(), QImage::Format_ARGB32_Premultiplied);
    piping.fill(Qt::transparent);
    widget.render(&piping);
    QVERIFY(piping != classic && piping != thin);
    widget.set_slot_number(4);
    QImage renumbered(widget.size(), QImage::Format_ARGB32_Premultiplied);
    renumbered.fill(Qt::transparent);
    widget.render(&renumbered);
    QVERIFY(renumbered != piping);
    QCOMPARE(*widget_owner.deck({ 0 }), state);
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
    auto widget_owner = renderer_session();
    card_widget widget;
    widget.set_presented_image_mode(true);
    QVERIFY(widget.bind_gameplay_deck(widget_owner, { 0 }));
    QImage face(16, 24, QImage::Format_ARGB32_Premultiplied);
    face.fill(Qt::magenta);
    QHash<QString, QImage> faces;
    for (const auto& id : required_card_ids_with_back())
        faces.insert(id, face);
    widget.set_presented_card_images(faces);
    widget.show();
    for (const QSize size :
         { QSize(1920, 1080), QSize(1080, 1920), QSize(60, 42) }) {
        widget.resize(size);
        widget.set_slot_rotated(size.height() > size.width());
        for (const auto style :
             { slot_frame_style::classic, slot_frame_style::thin,
               slot_frame_style::piping }) {
            widget.set_frame_style(style);
            widget.set_slot_number(12);
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
    widget.set_presented_image_mode(true);
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
