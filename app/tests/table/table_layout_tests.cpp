// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

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

#include <QFrame>
#include <QtTest/QtTest>

#include <algorithm>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::card_orientation_constrains_packed_slot_geometry() {
    table table_widget;
    table_widget.resize(900, 600);
    table_widget.set_slot_count(6);

    table_widget.set_card_orientation(card_orientation_mode::horizontal);
    const QList<table_slot*> horizontal_slots
        = table_widget.findChildren<table_slot*>();
    QCOMPARE(horizontal_slots.size(), 6);
    for (const table_slot* slot : horizontal_slots) {
        QVERIFY(slot->width() > slot->height());
    }

    table_widget.set_card_orientation(card_orientation_mode::vertical);
    const QList<table_slot*> vertical_slots
        = table_widget.findChildren<table_slot*>();
    QCOMPARE(vertical_slots.size(), 6);
    for (const table_slot* slot : vertical_slots) {
        QVERIFY(slot->height() > slot->width());
    }
}

void table_tests::presentation_preserves_packed_slots_data() {
    QTest::addColumn<QSize>("viewport");
    QTest::addColumn<int>("count");
    QTest::addColumn<int>("orientation");
    for (const QSize size :
         { QSize(1280, 720), QSize(720, 1280), QSize(480, 320) }) {
        for (const int count : { 1, 4, 16, 64 }) {
            for (const auto orientation : { card_orientation_mode::automatic,
                                            card_orientation_mode::horizontal,
                                            card_orientation_mode::vertical }) {
                const QByteArray name = QStringLiteral("%1x%2/%3/%4")
                                            .arg(size.width())
                                            .arg(size.height())
                                            .arg(count)
                                            .arg(static_cast<int>(orientation))
                                            .toLatin1();
                QTest::newRow(name.constData())
                    << size << count << static_cast<int>(orientation);
            }
        }
    }
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

void table_tests::presentation_preserves_packed_slots() {
    QFETCH(QSize, viewport);
    QFETCH(int, count);
    QFETCH(int, orientation);
    table table_widget;
    table_widget.resize(viewport.transposed());
    table_widget.set_card_orientation(
        static_cast<card_orientation_mode>(orientation)
    );
    table_widget.set_slot_count(1);
    table_widget.show();
    table_widget.resize(viewport);
    table_widget.set_slot_count(count + 1);
    table_widget.set_slot_count(count);
    // Deliver widget layout, without waiting on unrelated SVG warmup timers.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    const auto slot_widgets = table_widget.findChildren<table_slot*>();
    QCOMPARE(slot_widgets.size(), count);
    QList<QRect> bounds;
    QList<int> raster_demands;
    for (auto* slot : slot_widgets) {
        QVERIFY(slot->isVisible());
        const QRect geometry = slot->geometry();
        QVERIFY2(
            table_widget.rect().contains(geometry),
            qPrintable(QStringLiteral("slot %1,%2 %3x%4 outside %5x%6")
                           .arg(geometry.x())
                           .arg(geometry.y())
                           .arg(geometry.width())
                           .arg(geometry.height())
                           .arg(viewport.width())
                           .arg(viewport.height()))
        );
        QVERIFY(geometry.width() > 0 && geometry.height() > 0);
        if (!bounds.isEmpty()) {
            QCOMPARE(
                std::min(geometry.width(), geometry.height()),
                std::min(bounds[0].width(), bounds[0].height())
            );
            QCOMPARE(
                std::max(geometry.width(), geometry.height()),
                std::max(bounds[0].width(), bounds[0].height())
            );
        }
        for (const QRect& previous : bounds) {
            QVERIFY(!geometry.intersects(previous));
        }
        bounds.append(geometry);
        auto* card = slot->findChild<card_widget*>();
        QVERIFY(card != nullptr);
        QCOMPARE(card->geometry(), slot->rect());
        QVERIFY(card->card_face_target_short_px() > 0);
        raster_demands.append(card->card_face_target_short_px());
    }
    for (const auto style :
         { slot_frame_style::thin, slot_frame_style::classic }) {
        table_widget.set_frame_style(style);
        for (qsizetype i = 0; i < slot_widgets.size(); ++i) {
            auto* slot = slot_widgets[i];
            slot->set_swap_selected(true);
            slot->set_paused(true);
            QCOMPARE(slot->geometry(), bounds[i]);
            QCOMPARE(slot->card_face_need_short_px(), raster_demands[i]);
            slot->set_paused(false);
            QCOMPARE(slot->geometry(), bounds[i]);
        }
    }
    for (const auto style : { slot_action_style::rail, slot_action_style::pills,
                              slot_action_style::classic }) {
        table_widget.set_action_style(style);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        for (qsizetype i = 0; i < slot_widgets.size(); ++i) {
            auto* slot = slot_widgets[i];
            QCOMPARE(slot->geometry(), bounds[i]);
            QCOMPARE(slot->findChild<card_widget*>()->geometry(), slot->rect());
            QCOMPARE(slot->card_face_need_short_px(), raster_demands[i]);
        }
    }
    for (const auto style :
         { slot_settings_style::card, slot_settings_style::drawer,
           slot_settings_style::sill, slot_settings_style::classic }) {
        table_widget.set_settings_style(style);
        for (qsizetype i = 0; i < slot_widgets.size(); ++i) {
            QCOMPARE(slot_widgets[i]->geometry(), bounds[i]);
            QCOMPARE(
                slot_widgets[i]->card_face_need_short_px(), raster_demands[i]
            );
        }
        if (style != slot_settings_style::classic) {
            auto* slot = slot_widgets[0];
            const auto state = slot->capture_session_state();
            QVERIFY(
                QMetaObject::invokeMethod(
                    slot, "on_settings_button_clicked", Qt::DirectConnection
                )
            );
            auto* panel = slot->findChild<QFrame*>(
                QStringLiteral("slot_settings_editor")
            );
            QVERIFY(panel && panel->isVisible());
            QCOMPARE(slot->geometry(), bounds[0]);
            QCOMPARE(slot->findChild<card_widget*>()->geometry(), slot->rect());
            QCOMPARE(slot->card_face_need_short_px(), raster_demands[0]);
            QCOMPARE(slot->capture_session_state(), state);
            QVERIFY(
                QMetaObject::invokeMethod(
                    slot, "on_settings_button_clicked", Qt::DirectConnection
                )
            );
            QVERIFY(!panel->isVisible());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
}
