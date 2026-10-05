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
#include <limits>

#include "table/card_widget_fixture.hpp"

using namespace card_test;

void card_widget_tests::gameplay_layout_rotation_fits_every_frame() {
    auto owner = renderer_session();
    card_widget widget;
    supply_renderer_faces(widget);
    QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
    widget.show();
    QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    for (const auto size : { QSize(1920, 1080), QSize(1080, 1920),
                             QSize(300, 300), QSize(120, 85), QSize(16, 24) }) {
        widget.resize(size);
        for (const bool rotated : { false, true }) {
            widget.set_slot_rotated(rotated);
            const auto endpoint = widget.layout_geometry();
            widget.set_gameplay_layout_rotation(rotated ? 0.0 : 90.0);
            QCOMPARE(widget.layout_geometry().card, endpoint.card);
            QCOMPARE(
                widget.layout_geometry().slot_rotation, endpoint.slot_rotation
            );
            for (int angle = 0; angle <= 90; angle += 5) {
                widget.set_gameplay_layout_rotation(static_cast<qreal>(angle));
                const auto geometry = widget.layout_geometry();
                QCOMPARE(geometry.slot_rotation, static_cast<qreal>(angle));
                QVERIFY(!geometry.card.isEmpty());
                for (const qreal jitter : { -3.5, 0.0, 3.5 }) {
                    for (const QPointF offset :
                         { QPointF(-7.2, -7.2), QPointF(7.2, 7.2) }) {
                        painter.resetTransform();
                        card_widget::apply_card_transform(
                            painter, geometry.card, geometry.slot_rotation,
                            jitter, geometry.bounded_offset(offset)
                        );
                        QVERIFY2(
                            geometry.frame.contains(
                                painter.transform().mapRect(geometry.card)
                            ),
                            "Intermediate card rotation must remain inside its "
                            "frame"
                        );
                    }
                }
            }
            for (const qreal invalid :
                 { -1.0, 91.0, std::numeric_limits<qreal>::infinity(),
                   std::numeric_limits<qreal>::quiet_NaN() }) {
                widget.set_gameplay_layout_rotation(invalid);
                QCOMPARE(widget.layout_geometry().slot_rotation, 90.0);
            }
            widget.set_gameplay_layout_rotation(std::nullopt);
            QCOMPARE(widget.layout_geometry().card, endpoint.card);
        }
    }
    widget.unbind_gameplay_deck();
    widget.set_gameplay_layout_rotation(45.0);
    QVERIFY(
        !widget.gameplay_layout_rotation_deg
    ); // legacy remains endpoint-only
}

void card_widget_tests::layout_frames_preserve_jitter_and_shared_faces() {
    auto owner = renderer_session();
    QVERIFY(owner.start());
    QCOMPARE(owner.deal_step().status, gameplay::dealing_step_status::advanced);
    card_widget widget;
    supply_renderer_faces(widget);
    QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
    widget.show();
    (void)render_card(
        widget
    ); // Initial resize/jitter/marking before the snapshot.
    const auto before = *owner.deck({ 0 });
    const auto jitter = widget.card_rotation_deg;
    const auto offset = widget.card_offset;
    const auto marking_size = widget.table_marking.display_size();
    const auto faces = widget.card_faces_rasterized;
    const auto marking_key = widget.table_marking.pixmap().cacheKey();
    const auto description = widget.accessibleDescription();
    for (int frame = 0; frame <= 12; ++frame) {
        widget.set_gameplay_layout_rotation(static_cast<qreal>(frame) * 7.5);
        widget.resize(260 + frame * 4, 360 - frame * 8);
        (void)render_card(widget);
        QCOMPARE(widget.card_rotation_deg, jitter);
        QCOMPARE(widget.card_offset, offset);
        QCOMPARE(widget.table_marking.display_size(), marking_size);
        QCOMPARE(widget.table_marking.pixmap().cacheKey(), marking_key);
        QCOMPARE(widget.card_faces_rasterized.constData(), faces.constData());
        QVERIFY(!widget.rasterizing);
        QCOMPARE(widget.accessibleDescription(), description);
        QCOMPARE(*owner.deck({ 0 }), before);
    }
    widget.set_gameplay_layout_rotation(std::nullopt);
    QCOMPARE(widget.card_rotation_deg, jitter);
    QCOMPARE(widget.card_offset, offset);
    QVERIFY(widget.table_marking.display_size() != marking_size);
    QCOMPARE(widget.card_faces_rasterized.constData(), faces.constData());
    widget.unbind_gameplay_deck();
    QVERIFY(!widget.gameplay_layout_rotation_deg);
}

void card_widget_tests::gameplay_external_paint_matches_native_data() {
    QTest::addColumn<bool>("thin");
    QTest::addColumn<bool>("rotated");
    QTest::newRow("classic-vertical") << false << true;
    QTest::newRow("classic-horizontal") << false << false;
    QTest::newRow("thin-vertical") << true << true;
    QTest::newRow("thin-horizontal") << true << false;
}

void card_widget_tests::gameplay_external_paint_matches_native() {
    QFETCH(bool, thin);
    QFETCH(bool, rotated);
    auto owner = renderer_session();
    QVERIFY(owner.start());
    QCOMPARE(owner.deal_step().status, gameplay::dealing_step_status::advanced);
    card_widget widget;
    supply_renderer_faces(widget);
    QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
    widget.set_frame_style(
        thin ? slot_frame_style::thin : slot_frame_style::classic
    );
    widget.set_slot_rotated(rotated);
    widget.show();
    (void)render_card(widget);
    QImage native(widget.size(), QImage::Format_ARGB32_Premultiplied);
    native.fill(Qt::transparent);
    widget.render(&native, QPoint(), QRegion(), QWidget::RenderFlags {});
    QImage split(widget.size(), QImage::Format_ARGB32_Premultiplied);
    split.fill(Qt::transparent);
    {
        QPainter painter(&split);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setFont(widget.font());
        const auto geometry = widget.layout_geometry(QSizeF(widget.size()));
        widget.paint_slot_frame(painter, geometry);
        widget.paint_deck_content(painter, geometry);
    }
    QCOMPARE(split, native); // No endpoint renderer fork.
    widget.gameplay_paint_external = true;
    QImage suppressed(widget.size(), QImage::Format_ARGB32_Premultiplied);
    suppressed.fill(Qt::transparent);
    widget.render(&suppressed, QPoint(), QRegion(), QWidget::RenderFlags {});
    QImage empty = suppressed;
    empty.fill(Qt::transparent);
    QCOMPARE(suppressed, empty);
    widget.unbind_gameplay_deck();
    QVERIFY(!widget.gameplay_paint_external);
}

void card_widget_tests::gameplay_external_pending_paint_keeps_faces_hidden() {
    auto owner = renderer_session(false);
    auto settings = owner.configuration();
    settings.source = gameplay::quiz_source::physical_joker;
    QVERIFY(owner.configure(settings));
    gameplay::prepared_deck stream;
    stream.cards = { 0, 52, 1 };
    stream.rank_weights.fill(1);
    QVERIFY(owner.prepare_decks(std::array { stream, stream }));
    QVERIFY(owner.start());
    QCOMPARE(owner.deal_step().status, gameplay::dealing_step_status::advanced);
    QCOMPARE(owner.deal_step().status, gameplay::dealing_step_status::advanced);
    QVERIFY(owner.deck({ 0 })->quiz);
    QVERIFY(owner.pause());
    card_widget widget;
    supply_renderer_faces(widget);
    QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
    widget.gameplay_paint_external = true;
    const auto record = *owner.deck({ 0 });
    widget.set_show_card_indexing(true);
    widget.set_strategy_name(QStringLiteral("Allowed hint"));
    widget.set_show_strategy_name(true);
    widget.card_offset = QPointF(7.2, -7.2);
    const auto faces = widget.card_faces_rasterized;
    const auto jitter = widget.card_rotation_deg;
    const auto offset = widget.card_offset;
    const auto marking = widget.table_marking.display_size();
    for (const qreal angle : { 0.0, 22.5, 45.0, 67.5, 90.0 }) {
        widget.set_gameplay_layout_rotation(angle);
        const auto paint_content = [&widget] {
            QImage image(600, 500, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            widget.paint_deck_content(
                painter, widget.layout_geometry(QSizeF(600, 500))
            );
            return image;
        };
        const auto moving = paint_content();
        widget.resize(
            40, 80
        ); // Destination controls can have a different size.
        QCOMPARE(
            paint_content(), moving
        ); // Index/hint offsets use moving geometry, not widget size.
        widget.resize(260, 360);
        QCOMPARE(
            widget.selected_card_face_index, -1
        ); // Neither face nor back selected for pending Joker.
        QCOMPARE(widget.card_faces_rasterized.constData(), faces.constData());
        QVERIFY(!widget.rasterizing);
        QCOMPARE(widget.card_rotation_deg, jitter);
        QCOMPARE(widget.card_offset, offset);
        QCOMPARE(widget.table_marking.display_size(), marking);
        QCOMPARE(widget.accessibleDescription(), str_label("Card hidden"));
        QCOMPARE(*owner.deck({ 0 }), record);
    }
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
