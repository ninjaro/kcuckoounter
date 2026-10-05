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
    const QColor original_base = theme_settings::base_color();
    const QColor base_color(0x1B, 0x3C, 0xF0);
    theme_settings::set_base_color(base_color);

    table_slot slot;
    slot.apply_theme();

    const theme_palette_option& palette_option = theme_palette_registry::option(
        theme_palette_registry::id_from_color(base_color)
    );
    const QColor expected_panel = palette_option.panel_color();

    auto settings_frame
        = slot.findChild<QFrame*>(QStringLiteral("settings_bar_frame"));
    auto swap_frame = slot.findChild<QFrame*>(QStringLiteral("swap_bar_frame"));
    QVERIFY2(
        settings_frame != nullptr,
        "settings bar frame should be present for palette updates"
    );
    QVERIFY2(
        swap_frame != nullptr,
        "swap bar frame should be present for palette updates"
    );
    QCOMPARE(settings_frame->palette().color(QPalette::Window), expected_panel);
    QCOMPARE(swap_frame->palette().color(QPalette::Window), expected_panel);

    theme_settings::set_base_color(original_base);
}

void table_tests::overlay_palette_uses_gold_text_on_frames() {
    const QColor original_base = theme_settings::base_color();
    const QColor base_color(0x1B, 0x3C, 0xF0);
    theme_settings::set_base_color(base_color);

    table_slot slot;
    slot.apply_theme();

    const QColor expected_text = theme_settings::slot_border_color();
    auto settings_frame
        = slot.findChild<QFrame*>(QStringLiteral("settings_bar_frame"));
    auto swap_frame = slot.findChild<QFrame*>(QStringLiteral("swap_bar_frame"));
    QVERIFY(settings_frame != nullptr);
    QVERIFY(swap_frame != nullptr);
    QCOMPARE(
        settings_frame->palette().color(QPalette::WindowText), expected_text
    );
    QCOMPARE(swap_frame->palette().color(QPalette::WindowText), expected_text);

    theme_settings::set_base_color(original_base);
}

void table_tests::overlay_frames_enable_auto_fill() {
    table_slot slot;
    slot.apply_theme();

    auto settings_frame
        = slot.findChild<QFrame*>(QStringLiteral("settings_bar_frame"));
    auto swap_frame = slot.findChild<QFrame*>(QStringLiteral("swap_bar_frame"));
    QVERIFY(settings_frame != nullptr);
    QVERIFY(swap_frame != nullptr);
    QVERIFY(settings_frame->autoFillBackground());
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
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop compact controls; Android retains its existing surface");
#endif
    QFETCH(QSize, slot_size);
    table_slot slot;
    slot.set_shared_card_faces_mode(true);
    slot.set_rotated(slot_size.height() > slot_size.width());
    slot.resize(slot_size);
    slot.show();
    slot.set_paused(true);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QPixmap preview(slot.size());
    slot.render(&preview);
    auto* bar = slot.findChild<QFrame*>(QStringLiteral("swap_bar_frame"));
    QVERIFY(bar != nullptr);
    auto* trigger
        = slot.findChild<QToolButton*>(QStringLiteral("compact_slot_controls"));
    QVERIFY(trigger != nullptr);
    QVERIFY(trigger->isVisible());
    QVERIFY(slot.rect().contains(trigger->geometry()));
    QVERIFY(!bar->isVisible());
    const QSize original_slot_size = slot.size();
    auto* card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    const int raster_need = card->card_face_target_short_px();
    QTest::mouseClick(trigger, Qt::LeftButton);
    auto* dialog
        = slot.findChild<QDialog*>(QStringLiteral("slot_controls_dialog"));
    QVERIFY(dialog != nullptr);
    QVERIFY(dialog->isVisible());
    QVERIFY(bar->isVisible());
    for (auto* button : bar->findChildren<BasePushButton*>()) {
        const QRect button_bounds(
            button->mapTo(dialog, QPoint()), button->size()
        );
        QVERIFY(dialog->rect().contains(button_bounds));
        QVERIFY2(
            button->width() >= button->minimumSizeHint().width(),
            qPrintable(QStringLiteral("%1 actual=%2 minimum=%3")
                           .arg(button->objectName())
                           .arg(button->width())
                           .arg(button->minimumSizeHint().width()))
        );
    }
    QCOMPARE(slot.size(), original_slot_size);
    QCOMPARE(card->card_face_target_short_px(), raster_need);
    QTest::keyClick(dialog, Qt::Key_Escape);
    QVERIFY(!dialog->isVisible());
    QVERIFY(trigger->isVisible());
    QCOMPARE(slot.focusWidget(), trigger);

    // A quiz uses the original input and handlers, not a second answer model.
    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());
    QVERIFY(trigger->isVisible());
    auto* input = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    QVERIFY(input != nullptr);
    input->setValue(-17);
    const auto state = slot.capture_session_state();
    QTest::keyClick(trigger, Qt::Key_Space);
    // The closed window is scheduled for deletion; locate the visible host.
    for (auto* candidate :
         slot.findChildren<QDialog*>(QStringLiteral("slot_controls_dialog"))) {
        if (candidate->isVisible())
            dialog = candidate;
    }
    QVERIFY(dialog->isVisible());
    QVERIFY(input->isVisible());
    QCOMPARE(dialog->focusWidget(), input);
    QCOMPARE(slot.capture_session_state(), state);
    QVERIFY(dialog->rect().contains(
        QRect(input->mapTo(dialog, QPoint()), input->size())
    ));
    QTest::keyClick(dialog, Qt::Key_Escape);
    QCOMPARE(slot.capture_session_state(), state);
    // Growing restores inline controls without losing the pending answer.
    slot.resize(800, 600);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QVERIFY(!trigger->isVisible());
    QVERIFY(input->isVisible());
    QCOMPARE(input->value(), -17);
    QCOMPARE(slot.capture_session_state(), state);

    slot.resize(slot_size);
    QTest::mouseClick(trigger, Qt::LeftButton);
    auto* skip
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    auto* feedback
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    auto* resume = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    QVERIFY(skip != nullptr && feedback != nullptr && resume != nullptr);
    QTest::mouseClick(skip, Qt::LeftButton);
    QVERIFY(feedback->isVisible());
    QVERIFY(
        feedback->text().contains(QString::number(card->current_total_weight()))
    );
    QVERIFY(resume->isVisible());
    QCOMPARE(slot.size(), original_slot_size);
    QCOMPARE(card->card_face_target_short_px(), raster_need);
    QTest::mouseClick(resume, Qt::LeftButton);
    QVERIFY(!slot.is_quiz_prompt_active());
    QVERIFY(!trigger->isVisible());

    // The checkpoint has no host/window state. Restoring still pauses the
    // same pending question and supplies a compact entry point when needed.
    QVERIFY(slot.restore_session_state(state));
    auto expected_restored = state;
    expected_restored.paused = true;
    QCOMPARE(slot.capture_session_state(), expected_restored);
    auto* quiz_bar = slot.findChild<QFrame*>(QStringLiteral("quiz_bar_frame"));
    QVERIFY2(
        trigger->isVisible(),
        qPrintable(
            QStringLiteral(
                "slot=%1x%2 visible=%3 triggerHidden=%4 quizHidden=%5 "
                "hint=%6x%7 overlayHint=%8x%9"
            )
                .arg(slot.width())
                .arg(slot.height())
                .arg(slot.isVisible())
                .arg(trigger->isHidden())
                .arg(quiz_bar->isHidden())
                .arg(quiz_bar->minimumSizeHint().width())
                .arg(quiz_bar->minimumSizeHint().height())
                .arg(quiz_bar->parentWidget()->minimumSizeHint().width())
                .arg(quiz_bar->parentWidget()->minimumSizeHint().height())
        )
    );
    QTest::mouseClick(trigger, Qt::LeftButton);
    QVERIFY(input->isVisible());
    QCOMPARE(input->value(), -17);
    slot.set_allow_skipping(false);
    QVERIFY(!skip->isVisible());
    input->setValue(9999);
    auto* check
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_answer_button"));
    QVERIFY(check != nullptr);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QVERIFY(check->isVisible() && check->isEnabled());
    QVERIFY(check->width() >= check->minimumSizeHint().width());
    QTest::mouseClick(check, Qt::LeftButton);
    QVERIFY(slot.is_deck_exhausted());
    QVERIFY(feedback->isVisible());
    QVERIFY(!resume->isVisible());
}

void table_tests::compact_controls_follow_slot_lifetime() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop compact controls; Android retains its existing surface");
#endif
    table table_widget;
    table_widget.resize(320, 240);
    table_widget.set_slot_count(4);
    table_widget.show();
    auto* slot = table_widget.findChild<table_slot*>();
    QVERIFY(slot != nullptr);
    slot->set_paused(true);
    auto* trigger = slot->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    QVERIFY(trigger != nullptr && trigger->isVisible());
    QTest::mouseClick(trigger, Qt::LeftButton);
    QPointer<QDialog> dialog
        = slot->findChild<QDialog*>(QStringLiteral("slot_controls_dialog"));
    QVERIFY(dialog && dialog->isVisible());
    table_widget.set_slot_count(0);
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
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop action variants; Android retains its current surface");
#endif
    QFETCH(int, style_id);
    QFETCH(bool, rotated);
    QFETCH(bool, compact);
    const auto style = static_cast<slot_action_style>(style_id);
    table_slot slot;
    slot.set_shared_card_faces_mode(true);
    slot.set_rotated(rotated);
    slot.resize(compact ? QSize(60, 40) : QSize(800, 600));
    slot.set_action_style(style);
    slot.show();
    slot.start_quiz(0);
    for (int i = 0; i < 8; ++i)
        slot.advance_card();
    slot.set_paused(true);
    auto* card = slot.findChild<card_widget*>();
    auto* bar = slot.findChild<QFrame*>(QStringLiteral("swap_bar_frame"));
    auto* settings
        = slot.findChild<QFrame*>(QStringLiteral("settings_bar_frame"));
    auto* details = slot.findChild<BasePushButton*>(
        QStringLiteral("slot_details_button")
    );
    auto* swap
        = slot.findChild<BasePushButton*>(QStringLiteral("slot_swap_button"));
    auto* copy
        = slot.findChild<BasePushButton*>(QStringLiteral("slot_copy_button"));
    auto* copy_all = slot.findChild<BasePushButton*>(
        QStringLiteral("slot_copy_all_button")
    );
    auto* trigger
        = slot.findChild<QToolButton*>(QStringLiteral("compact_slot_controls"));
    QVERIFY(
        card && bar && settings && details && swap && copy && copy_all
        && trigger
    );
    const auto state = slot.capture_session_state();
    const auto card_bounds = card->geometry();
    const auto raster_need = card->card_face_target_short_px();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QCOMPARE(trigger->isVisible(), compact);
    QPointer<QDialog> host;
    if (compact) {
        QTest::keyClick(trigger, Qt::Key_Space);
        host = slot.findChild<QDialog*>(QStringLiteral("slot_controls_dialog"));
        QVERIFY(host && host->isVisible());
    }
    QVERIFY(bar->isVisible());
    for (int i = 0; i < 8; ++i)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    for (auto* button : { details, swap, copy, copy_all }) {
        QVERIFY(button->isVisible());
        QVERIFY(!button->accessibleName().isEmpty());
        QVERIFY(!button->toolTip().isEmpty());
        QCOMPARE(
            button->text().isEmpty(),
            style == slot_action_style::rail && button != copy_all
        );
        QVERIFY(!button->icon().isNull());
        QVERIFY2(
            button->width() >= button->minimumSizeHint().width(),
            qPrintable(QStringLiteral("%1 actual=%2 minimum=%3")
                           .arg(button->objectName())
                           .arg(button->width())
                           .arg(button->minimumSizeHint().width()))
        );
        auto* container = compact ? static_cast<QWidget*>(host.data())
                                  : static_cast<QWidget*>(&slot);
        QVERIFY(container->rect().contains(
            QRect(button->mapTo(container, QPoint()), button->size())
        ));
    }
    details->setFocus(Qt::TabFocusReason);
    for (auto* next : { swap, copy, copy_all }) {
        QTest::keyClick(details->window()->focusWidget(), Qt::Key_Tab);
        QCOMPARE(details->window()->focusWidget(), next);
    }
    if (style == slot_action_style::rail) {
        QVERIFY(
            details->y() < swap->y() && swap->y() < copy->y()
            && copy->y() < copy_all->y()
        );
        const auto bar_size = bar->size();
        QFocusEvent focused(QEvent::FocusIn, Qt::TabFocusReason);
        QCoreApplication::sendEvent(copy_all, &focused);
        QCOMPARE(QToolTip::text(), copy_all->toolTip());
        QCOMPARE(bar->size(), bar_size); // Keyboard labels never change fit.
        QFocusEvent unfocused(QEvent::FocusOut, Qt::TabFocusReason);
        QCoreApplication::sendEvent(copy_all, &unfocused);
    }
    if (style == slot_action_style::pills) {
        QVERIFY(
            details->x() < swap->x() && swap->x() < copy->x()
            && copy->x() < copy_all->x()
        );
    }
    QSignalSpy swaps(&slot, &table_slot::swap_clicked);
    QSignalSpy copies(&slot, &table_slot::copy_clicked);
    QSignalSpy all_copies(&slot, &table_slot::copy_all_clicked);
    QTest::keyClick(swap, Qt::Key_Space);
    QTest::mouseClick(copy, Qt::LeftButton);
    QTest::keyClick(copy_all, Qt::Key_Space);
    QCOMPARE(swaps.count(), 1);
    QCOMPARE(copies.count(), 1);
    QCOMPARE(all_copies.count(), 1);
    QCOMPARE(all_copies.at(0).at(0).value<table_slot*>(), &slot);
    slot.set_swap_selected(true);
    QVERIFY(swap->isChecked());
    QCOMPARE(slot.capture_session_state(), state);

    if (compact) {
        bool inspected = false;
        QSignalSpy opened(&slot, &table_slot::dialog_opened);
        QTimer::singleShot(0, &slot, [&] {
            auto* dialog
                = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog != nullptr) {
                auto* checkbox = dialog->findChild<BaseCheckBox*>();
                inspected = checkbox != nullptr;
                if (checkbox)
                    checkbox->toggle();
                dialog->reject();
            }
        });
        QTest::mouseClick(details, Qt::LeftButton);
        QVERIFY(inspected);
        QCOMPARE(opened.count(), 1);
    } else {
        const bool visible = settings->isVisible();
        QTest::keyClick(details, Qt::Key_Space);
        QCOMPARE(settings->isVisible(), !visible);
        QTest::keyClick(details, Qt::Key_Space);
        QCOMPARE(settings->isVisible(), visible);
    }
    QCOMPARE(slot.capture_session_state(), state);
    slot.set_action_style(slot_action_style::classic);
    QVERIFY(!details->text().isEmpty());
    QVERIFY(swap->isChecked());
    QCOMPARE(
        slot.findChild<BasePushButton*>(QStringLiteral("slot_copy_all_button")),
        copy_all
    );
    QCOMPARE(slot.capture_session_state(), state);
    QCOMPARE(card->geometry(), card_bounds);
    QCOMPARE(card->card_face_target_short_px(), raster_need);
    if (host) {
        QTest::keyClick(host, Qt::Key_Escape);
        QVERIFY(!host->isVisible());
        QCOMPARE(slot.focusWidget(), trigger);
    }
    slot.set_action_style(style);
    slot.set_paused(false);
    QVERIFY(!bar->isVisible());
    for (int i = 0; i < 21; ++i)
        slot.advance_card();
    QVERIFY(slot.is_quiz_prompt_active());
    QVERIFY(!bar->isVisible()); // Never replaces or obstructs the answer path.
    const auto question = slot.capture_session_state();
    slot.set_action_style(slot_action_style::classic);
    QCOMPARE(slot.capture_session_state(), question);
    QVERIFY(slot.restore_session_state(question));
    QVERIFY(!bar->isVisible());
    QCOMPARE(card->geometry(), card_bounds);
}

void table_tests::action_variants_preserve_copy_and_swap_workflows() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop action variants");
#endif
    for (const auto style :
         { slot_action_style::classic, slot_action_style::rail,
           slot_action_style::pills }) {
        table view;
        view.resize(1200, 900);
        view.set_action_style(style);
        view.set_slot_count(3);
        view.show();
        const auto slot_widgets = view.findChildren<table_slot*>();
        QCOMPARE(slot_widgets.size(), 3);
        for (auto* slot : slot_widgets) {
            slot->start_quiz(0);
            slot->set_paused(true);
        }
        auto* source = slot_widgets[0];
        auto* target = slot_widgets[1];
        auto* other = slot_widgets[2];
        const auto button = [](table_slot* slot, const QString& name) {
            return slot->findChild<BasePushButton*>(name);
        };
        auto* source_copy = button(source, QStringLiteral("slot_copy_button"));
        auto* target_copy = button(target, QStringLiteral("slot_copy_button"));
        auto* source_swap = button(source, QStringLiteral("slot_swap_button"));
        auto* target_swap = button(target, QStringLiteral("slot_swap_button"));
        auto* copy_all = button(source, QStringLiteral("slot_copy_all_button"));
        QVERIFY(
            source_copy && target_copy && source_swap && target_swap && copy_all
        );
        QCOMPARE(
            source_copy->text().isEmpty(), style == slot_action_style::rail
        ); // Newly created slot inherits style.
        auto original = source->capture_session_state();
        auto different = original;
        different.show_card_indexing = !different.show_card_indexing;
        QVERIFY(source->restore_session_state(different));
        source_copy->click();
        QVERIFY(source_copy->isChecked());
        QVERIFY(!source_swap->isChecked());
        QCOMPARE(source_copy->accessibleName(), str_label("Cancel"));
        QCOMPARE(target_copy->accessibleName(), str_label("Set"));
        QCOMPARE(
            source_copy->text().isEmpty(), style == slot_action_style::rail
        );
        view.set_action_style(slot_action_style::rail);
        QVERIFY(source_copy->isChecked());
        QCOMPARE(source_copy->accessibleName(), str_label("Cancel"));
        QVERIFY(target_copy->text().isEmpty());
        target_copy->click();
        QCOMPARE(
            target->capture_session_state().show_card_indexing,
            different.show_card_indexing
        );
        QCOMPARE(
            other->capture_session_state().show_card_indexing,
            original.show_card_indexing
        );
        QCOMPARE(source_copy->accessibleName(), str_label("Copy"));
        QVERIFY(!source_copy->isChecked());
        view.set_action_style(style);
        source_copy->click();
        source_copy->click(); // Cancel selection, not the session.
        QVERIFY(!source_copy->isChecked());
        QCOMPARE(source->capture_session_state(), different);
        copy_all->click();
        QCOMPARE(
            other->capture_session_state().show_card_indexing,
            different.show_card_indexing
        );
        const QRect source_bounds = source->geometry();
        const QRect target_bounds = target->geometry();
        source_swap->click();
        QVERIFY(source_swap->isChecked());
        view.set_action_style(slot_action_style::pills);
        QVERIFY(source_swap->isChecked());
        target_swap->click();
        QCOMPARE(source->geometry(), target_bounds);
        QCOMPARE(target->geometry(), source_bounds);
        QCOMPARE(source->capture_session_state(), different);
        QVERIFY(!source_swap->isChecked());
    }
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
