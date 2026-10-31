// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"
#include <shared_ui/qt/adaptive_surface.hpp>
#include <shared_ui/qt/presentation.hpp>

#include "card_helpers/card_sheet.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"

#include <QDialog>
#include <QFocusEvent>
#include <QFrame>
#include <QLabel>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QtTest/QtTest>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::overlay_palette_applies_to_bars() {
    const QColor base_color(0x1B, 0x3C, 0xF0);

    table_slot slot;
    auto presentation = shared_ui::qt::presentation_for(slot);
    presentation.context.felt_surface = base_color;
    shared_ui::qt::apply_presentation(slot, presentation);
    slot.apply_theme();

    const theme_palette_option& palette_option = theme_palette_registry::option(
        theme_palette_registry::id_from_color(base_color)
    );
    const QColor expected_panel = palette_option.panel_color();

    auto swap_frame = slot.findChild<QFrame*>(QStringLiteral("swap_bar_frame"));
    QVERIFY2(
        swap_frame != nullptr,
        "swap bar frame should be present for palette updates"
    );
    QCOMPARE(swap_frame->palette().color(QPalette::Window), expected_panel);
}

void table_tests::overlay_palette_uses_gold_text_on_frames() {
    // Native gold must not become a surface-adjusted Felt foreground.
    const QColor base_color(Qt::white);

    table_slot slot;
    auto presentation = shared_ui::qt::presentation_for(slot);
    presentation.context.felt_surface = base_color;
    shared_ui::qt::apply_presentation(slot, presentation);
    slot.apply_theme();

    const QColor expected_text(QStringLiteral("#d4af37"));
    auto swap_frame = slot.findChild<QFrame*>(QStringLiteral("swap_bar_frame"));
    QVERIFY(swap_frame != nullptr);
    QCOMPARE(swap_frame->palette().color(QPalette::WindowText), expected_text);
}

void table_tests::overlay_frames_enable_auto_fill() {
    table_slot slot;
    slot.apply_theme();

    auto swap_frame = slot.findChild<QFrame*>(QStringLiteral("swap_bar_frame"));
    QVERIFY(swap_frame != nullptr);
    QVERIFY(swap_frame->autoFillBackground());
}

void table_tests::compact_slot_controls_remain_reachable_data() {
    QTest::addColumn<QSize>("slot_size");
    QTest::newRow("landscape") << QSize(120, 85);
    QTest::newRow("portrait") << QSize(85, 120);
    QTest::newRow("extreme-landscape") << QSize(54, 38);
    QTest::newRow("extreme-portrait") << QSize(38, 54);
}

void table_tests::compact_slot_controls_remain_reachable() {
    QFETCH(QSize, slot_size);
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    QVERIFY(view.start_gameplay_runtime(21));
    view.stop_gameplay_clock();
    QVERIFY(view.pause_gameplay_runtime());
    auto& slot = *view.slot_widgets.front();
    slot.set_presented_image_mode(true);
    slot.set_rotated(slot_size.height() > slot_size.width());
    slot.resize(slot_size);
    view.show();
    slot.resize(slot_size);
    QCoreApplication::processEvents();
    const auto before = view.capture_gameplay_checkpoint();
    auto* trigger
        = slot.findChild<shared_ui::qt::adaptive_surface*>()->trigger();
    QVERIFY(trigger && trigger->isVisible());
    qInfo() << "REACH" << view.window()->geometry() << view.geometry()
            << slot.geometry() << "trigger" << trigger->geometry()
            << trigger->minimumSize() << trigger->sizeHint() << "parent"
            << trigger->parentWidget() << QApplication::activeWindow()
            << QApplication::focusWidget();
    const auto profile = shared_ui::qt::presentation_for(slot).profile;
    if (profile == shared_ui::qt::platform_profile::android) {
        QVERIFY(trigger->width() >= 48);
        QVERIFY(trigger->height() >= 48);
    }
    if (slot.width() >= trigger->width()
        && slot.height() >= trigger->height()) {
        QCOMPARE(trigger->parentWidget(), &slot);
        QVERIFY(slot.rect().contains(trigger->geometry()));
    } else {
        // A 48x48 target cannot fit inside a 54x38 or 38x54 slot.
        // Its containing surface must expose the complete target instead.
        QVERIFY(trigger->parentWidget() != &slot);
        QVERIFY(trigger->parentWidget()->isAncestorOf(&slot));
        QVERIFY(trigger->parentWidget()->rect().contains(trigger->geometry()));
    }
    QVERIFY(trigger->visibleRegion().contains(trigger->rect()));
    QCOMPARE(
        QApplication::widgetAt(trigger->mapToGlobal(trigger->rect().center())),
        trigger
    );
    QVERIFY(!trigger->accessibleName().isEmpty());
    trigger->click();
    QPointer<QDialog> dialog
        = slot.findChild<QDialog*>(QStringLiteral("slot_controls_dialog"));
    QVERIFY(dialog && dialog->isVisible());
    auto* details = slot.findChild<QAbstractButton*>(
        QStringLiteral("slot_details_button")
    );
    QVERIFY(details && details->isVisible());
    QVERIFY(!details->accessibleName().isEmpty());
    QTest::keyClick(dialog, Qt::Key_Escape);
    QVERIFY(!dialog->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(dialog.isNull());
    QCOMPARE(view.capture_gameplay_checkpoint(), before);
}

void table_tests::compact_controls_follow_slot_lifetime() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop compact controls; Android retains its existing surface");
#endif
    table table_widget;
    table_widget.resize(320, 240);
    QVERIFY(table_widget.install_gameplay_session(
        setup_table_fixture(static_cast<std::size_t>(4))
    ));
    table_widget.show();
    auto* slot = table_widget.findChild<table_slot*>();
    QVERIFY(slot != nullptr);
    slot->resize(60, 40);
    QCoreApplication::processEvents();
    auto* trigger = slot->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    QVERIFY(trigger != nullptr && trigger->isVisible());
    QTest::mouseClick(trigger, Qt::LeftButton);
    QPointer<QDialog> dialog
        = slot->findChild<QDialog*>(QStringLiteral("slot_controls_dialog"));
    QVERIFY(dialog && dialog->isVisible());
    table_widget.clear_gameplay_session();
    QVERIFY(dialog.isNull());
    QVERIFY(table_widget.findChildren<table_slot*>().isEmpty());
}

void table_tests::action_variants_reuse_controls_data() {
    QTest::addColumn<int>("style_id");
    QTest::addColumn<bool>("rotated");
    QTest::addColumn<bool>("compact");
    for (const auto style :
         { slot_action_style::classic, slot_action_style::rail,
           slot_action_style::pills }) {
        for (const bool rotated : { false, true }) {
            for (const bool compact : { false, true }) {
                const auto name
                    = QStringLiteral("style=%1/rotated=%2/compact=%3")
                          .arg(static_cast<int>(style))
                          .arg(rotated)
                          .arg(compact)
                          .toLatin1();
                QTest::newRow(name.constData())
                    << static_cast<int>(style) << rotated << compact;
            }
        }
    }
}

void table_tests::action_variants_reuse_controls() {
    QFETCH(int, style_id);
    QFETCH(bool, rotated);
    QFETCH(bool, compact);
    const auto style = static_cast<slot_action_style>(style_id);
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    QVERIFY(view.start_gameplay_runtime(21));
    view.stop_gameplay_clock();
    QVERIFY(view.pause_gameplay_runtime());
    auto& slot = *view.slot_widgets.front();
    slot.set_presented_image_mode(true);
    slot.set_rotated(rotated);
    slot.set_action_style(style);
    view.show();
    slot.resize(compact ? QSize(60, 40) : QSize(800, 600));
    QCoreApplication::processEvents();
    const auto before = view.capture_gameplay_checkpoint();
    auto* trigger
        = slot.findChild<shared_ui::qt::adaptive_surface*>()->trigger();
    QVERIFY(trigger);
    QCOMPARE(trigger->isVisible(), compact);
    if (compact)
        trigger->click();
    auto* details = slot.findChild<QAbstractButton*>(
        QStringLiteral("slot_details_button")
    );
    auto* swap
        = slot.findChild<QAbstractButton*>(QStringLiteral("slot_swap_button"));
    QVERIFY(details && swap && details->isVisible() && swap->isVisible());
    for (auto* button : { details, swap }) {
        QVERIFY(!button->accessibleName().isEmpty());
        QVERIFY(!button->toolTip().isEmpty());
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
        // Android retains labelled touch actions for an unavailable rail
        // choice.
        QVERIFY(shows_label(button));
#else
        QCOMPARE(!shows_label(button), style == slot_action_style::rail);
#endif
    }
    details->setFocus(Qt::TabFocusReason);
    QTest::keyClick(details, Qt::Key_Tab);
    QCOMPARE(details->window()->focusWidget(), swap);
    QSignalSpy swaps(&slot, &table_slot::swap_clicked);
    QTest::keyClick(swap, Qt::Key_Space);
    QCOMPARE(swaps.size(), 1);
    QCOMPARE(view.capture_gameplay_checkpoint(), before);
    slot.set_action_style(slot_action_style::classic);
    QCOMPARE(
        slot.findChild<QAbstractButton*>(QStringLiteral("slot_details_button")),
        details
    );
    QCOMPARE(
        slot.findChild<QAbstractButton*>(QStringLiteral("slot_swap_button")),
        swap
    );
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
