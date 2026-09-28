// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/table_tests.hpp"

#include "card_helpers/card_sheet.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"

#include <QDialog>
#include <QElapsedTimer>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QtTest/QtTest>

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

void table_tests::quiz_hides_skip_when_skipping_disabled() {
    table_slot slot;
    slot.start_quiz(0);
    slot.set_allow_skipping(false);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    QVERIFY(skip_button != nullptr);
    QVERIFY(!skip_button->isVisible());
}

void table_tests::quiz_training_mode_does_not_adjust_score() {
    table_slot slot;
    QSignalSpy score_spy(&slot, &table_slot::score_adjusted);

    auto training_check_box
        = slot.findChild<BaseCheckBox*>(QStringLiteral("training_check_box"));
    QVERIFY(training_check_box != nullptr);
    training_check_box->setChecked(true);

    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(score_spy.count(), 0);

    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    QVERIFY(skip_button != nullptr);
    skip_button->click();
    QCOMPARE(score_spy.count(), 0);
}

void table_tests::quiz_wrong_answer_exhausts_deck_without_training() {
    table_slot slot;
    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto answer_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_answer_button"));
    auto feedback_label
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    QVERIFY(spin_box != nullptr);
    QVERIFY(answer_button != nullptr);
    QVERIFY(feedback_label != nullptr);

    auto card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    const int expected = card->current_total_weight();
    const int provided = expected + 1;
    spin_box->setValue(provided);
    answer_button->click();

    const QString expected_message
        = str_label("You've set %1 while the correct answer is %2.")
              .arg(provided)
              .arg(expected);
    QCOMPARE(feedback_label->text(), expected_message);
    QVERIFY(slot.is_deck_exhausted());
}

void table_tests::quiz_wrong_answer_shows_continue_in_training() {
    table_slot slot;
    auto training_check_box
        = slot.findChild<BaseCheckBox*>(QStringLiteral("training_check_box"));
    QVERIFY(training_check_box != nullptr);
    training_check_box->setChecked(true);

    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto answer_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_answer_button"));
    auto feedback_label
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    auto continue_button = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    QVERIFY(spin_box != nullptr);
    QVERIFY(answer_button != nullptr);
    QVERIFY(feedback_label != nullptr);
    QVERIFY(continue_button != nullptr);

    auto card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    const int expected = card->current_total_weight();
    const int provided = expected + 2;
    spin_box->setValue(provided);
    answer_button->click();

    const QString expected_message
        = str_label("You've set %1 while the correct answer is %2.")
              .arg(provided)
              .arg(expected);
    QCOMPARE(feedback_label->text(), expected_message);
    QVERIFY(continue_button->isVisible());
    QVERIFY(!slot.is_deck_exhausted());
}

void table_tests::quiz_skip_shows_continue_feedback() {
    table_slot slot;
    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    auto feedback_label
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    auto continue_button = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    QVERIFY(spin_box != nullptr);
    QVERIFY(skip_button != nullptr);
    QVERIFY(feedback_label != nullptr);
    QVERIFY(continue_button != nullptr);

    auto card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    const int expected = card->current_total_weight();
    const int provided = expected + 3;
    spin_box->setValue(provided);
    skip_button->click();

    const QString expected_message
        = str_label("You've set %1 while the correct answer is %2.")
              .arg(provided)
              .arg(expected);
    QCOMPARE(feedback_label->text(), expected_message);
    QVERIFY(continue_button->isVisible());
}

void table_tests::quiz_spin_box_remembers_last_input() {
    table_slot slot;
    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    auto continue_button = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    QVERIFY(spin_box != nullptr);
    QVERIFY(skip_button != nullptr);
    QVERIFY(continue_button != nullptr);

    spin_box->setValue(7);
    skip_button->click();
    continue_button->click();

    for (int i = 0; i < 30; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(spin_box->value(), 7);
}

void table_tests::shared_card_faces_presence_tracks_set_and_clear() {
    table_slot slot;
    QVERIFY(!slot.has_shared_card_faces());

    QVector<QImage> shared_faces;
    shared_faces.push_back(QImage(24, 36, QImage::Format_ARGB32_Premultiplied));
    shared_faces[0].fill(Qt::red);

    slot.set_shared_card_faces(shared_faces, QSize(24, 36));
    QVERIFY(slot.has_shared_card_faces());

    slot.clear_shared_card_faces();
    QVERIFY(!slot.has_shared_card_faces());
}

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
        QVERIFY(button->width() >= button->minimumSizeHint().width());
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

void table_tests::quiz_variants_preserve_count_semantics_data() {
    QTest::addColumn<bool>("chips");
    QTest::addColumn<bool>("stamp");
    QTest::addColumn<bool>("compact");
    for (const bool chips : { false, true }) {
        for (const bool stamp : { false, true }) {
            for (const bool compact : { false, true }) {
                const auto name = QStringLiteral("chips=%1/stamp=%2/compact=%3")
                                      .arg(chips)
                                      .arg(stamp)
                                      .arg(compact)
                                      .toLatin1();
                QTest::newRow(name.constData()) << chips << stamp << compact;
            }
        }
    }
}

void table_tests::quiz_variants_preserve_count_semantics() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop quiz variants; Android retains its existing controls");
#endif
    QFETCH(bool, chips);
    QFETCH(bool, stamp);
    QFETCH(bool, compact);
    table_slot slot;
    slot.set_shared_card_faces_mode(true);
    slot.resize(compact ? QSize(90, 65) : QSize(800, 600));
    slot.set_quiz_presentation(
        chips ? quiz_answer_style::chips : quiz_answer_style::numeric,
        stamp ? quiz_feedback_style::stamp : quiz_feedback_style::classic
    );
    slot.show();
    slot.start_quiz(0);
    auto* card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    card->set_strategy_weights(QVector<int>(13, 1));
    for (int i = 0; i < 29; ++i)
        slot.advance_card();
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(
        card->current_total_weight(), 30
    ); // Accumulated, not last-card +1.
    const auto question = slot.capture_session_state();
    const QRect card_bounds = card->geometry();
    const int raster_need = card->card_face_target_short_px();
    auto* input = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto* stepper
        = slot.findChild<QWidget*>(QStringLiteral("quiz_chip_stepper"));
    auto* add_two = slot.findChild<QPushButton*>(QStringLiteral("quiz_chip_2"));
    auto* subtract_two
        = slot.findChild<QPushButton*>(QStringLiteral("quiz_chip_-2"));
    auto* stamp_heading
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_stamp"));
    auto* feedback
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    auto* resume = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    auto* skip
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    QVERIFY(
        input && stepper && add_two && subtract_two && stamp_heading && feedback
        && resume && skip
    );
    const auto open_controls = [&] {
        auto* trigger = slot.findChild<QToolButton*>(
            QStringLiteral("compact_slot_controls")
        );
        if (trigger != nullptr && trigger->isVisible())
            QTest::mouseClick(trigger, Qt::LeftButton);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    };
    const auto restore_question = [&](bool training) {
        auto state = question;
        state.training_mode = training;
        const bool restored = slot.restore_session_state(state);
        card->set_strategy_weights(QVector<int>(13, 1));
        open_controls();
        return restored;
    };
    open_controls();
    QVERIFY(input->isVisible());
    QCOMPARE(stepper->isVisible(), chips);
    QVERIFY(
        !stamp_heading->isVisible()
    ); // No answer/result leakage during input.
    QSignalSpy scores(&slot, &table_slot::score_adjusted);
    if (chips) {
        input->setFocus();
        for (const int step : { -2, -1, 1, 2 }) {
            auto* button = slot.findChild<QPushButton*>(
                QStringLiteral("quiz_chip_%1").arg(step)
            );
            QVERIFY(button != nullptr);
            QTest::keyClick(input->window()->focusWidget(), Qt::Key_Tab);
            QCOMPARE(input->window()->focusWidget(), button);
            QVERIFY(!button->accessibleName().isEmpty());
        }
        input->setValue(9998);
        QTest::mouseClick(add_two, Qt::LeftButton);
        QCOMPARE(input->value(), 9999);
        QVERIFY(!add_two->isEnabled());
        input->setValue(-9998);
        QTest::keyClick(subtract_two, Qt::Key_Return);
        QCOMPARE(input->value(), -9999);
        QVERIFY(!subtract_two->isEnabled());
        input->setValue(28);
        QTest::keyClick(add_two, Qt::Key_Space);
        QCOMPARE(input->value(), 30);
    } else {
        input->setValue(30);
    }
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(scores.count(), 0);
    QKeyEvent repeated_enter(
        QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(), true
    );
    QCoreApplication::sendEvent(input, &repeated_enter);
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(scores.count(), 0);
    input->findChild<QLineEdit*>()->setText(QStringLiteral("30"));
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(!slot.is_quiz_prompt_active());
    QCOMPARE(scores.count(), 1);
    QCOMPARE(scores.at(0).at(0).toInt(), 1);
    QVERIFY(!card->is_deck_exhausted());

    QVERIFY(restore_question(true));
    scores.clear();
    input->setValue(1); // Last card's weight is deliberately the wrong answer.
    QTest::keyClick(input->findChild<QLineEdit*>(), Qt::Key_Enter);
    QVERIFY(slot.is_quiz_prompt_active());
    QVERIFY(feedback->isVisible());
    QCOMPARE(
        feedback->text(),
        str_label("You've set %1 while the correct answer is %2.")
            .arg(1)
            .arg(30)
    );
    for (int i = 0; i < 4; ++i)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QVERIFY2(
        feedback->height() >= feedback->heightForWidth(feedback->width()),
        "Wrapped correction must not be clipped"
    );
    QCOMPARE(stamp_heading->isVisible(), stamp);
    QVERIFY(resume->isVisible());
    QCOMPARE(scores.count(), 0);
    const auto feedback_state = slot.capture_session_state();
    slot.set_quiz_presentation(
        quiz_answer_style::numeric, quiz_feedback_style::classic
    );
    QCOMPARE(slot.capture_session_state(), feedback_state);
    QVERIFY(stamp_heading->isHidden());
    QVERIFY(feedback->styleSheet().isEmpty());
    slot.set_quiz_presentation(
        chips ? quiz_answer_style::chips : quiz_answer_style::numeric,
        stamp ? quiz_feedback_style::stamp : quiz_feedback_style::classic
    );
    QCOMPARE(slot.capture_session_state(), feedback_state);
    QCOMPARE(card->geometry(), card_bounds);
    QCOMPARE(card->card_face_target_short_px(), raster_need);
    QVERIFY(slot.restore_session_state(feedback_state));
    open_controls();
    for (int i = 0; i < 4; ++i)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QVERIFY(feedback->height() >= feedback->heightForWidth(feedback->width()));
    QCOMPARE(feedback->text(), feedback_state.quiz_feedback_text);
    QCOMPARE(stamp_heading->isVisible(), stamp);
    QTest::mouseClick(resume, Qt::LeftButton);
    QVERIFY(!slot.is_quiz_prompt_active());

    QVERIFY(restore_question(false));
    scores.clear();
    input->setValue(1);
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(slot.is_deck_exhausted());
    QVERIFY(!resume->isVisible());
    QCOMPARE(stamp_heading->isVisible(), stamp);
    QVERIFY(scores.isEmpty());
    // A queued/repeated submit cannot reinterpret already displayed feedback.
    QVERIFY(
        QMetaObject::invokeMethod(
            &slot, "on_quiz_answer_button_clicked", Qt::DirectConnection
        )
    );
    QCOMPARE(
        feedback->text(),
        str_label("You've set %1 while the correct answer is %2.")
            .arg(1)
            .arg(30)
    );

    QVERIFY(restore_question(false));
    slot.set_allow_skipping(false);
    QVERIFY(!skip->isVisible());
    QVERIFY(
        QMetaObject::invokeMethod(
            &slot, "on_quiz_skip_button_clicked", Qt::DirectConnection
        )
    );
    QVERIFY(!slot.capture_session_state().quiz_feedback_active);
    slot.set_allow_skipping(true);
    QTest::mouseClick(skip, Qt::LeftButton);
    QVERIFY(resume->isVisible());
    const auto skipped = slot.capture_session_state();
    QVERIFY(
        QMetaObject::invokeMethod(
            &slot, "on_quiz_skip_button_clicked", Qt::DirectConnection
        )
    );
    QCOMPARE(slot.capture_session_state(), skipped);
    QCOMPARE(scores.count(), 1);
}

void table_tests::quiz_presentation_reaches_existing_and_new_slots() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop quiz variants");
#endif
    table view;
    view.resize(800, 600);
    view.set_slot_count(1);
    view.show();
    auto* first = view.findChild<table_slot*>();
    QVERIFY(first != nullptr);
    first->start_quiz(0);
    for (int i = 0; i < 29; ++i)
        first->advance_card();
    const auto state = first->capture_session_state();
    view.set_quiz_presentation(
        quiz_answer_style::chips, quiz_feedback_style::stamp
    );
    QCOMPARE(first->capture_session_state(), state);
    for (const int count : { 4, 16, 64, 1 }) {
        view.set_slot_count(count);
        const auto slot_widgets = view.findChildren<table_slot*>();
        QCOMPARE(slot_widgets.size(), count);
        for (const QSize size : { QSize(800, 600), QSize(360, 640) }) {
            view.resize(size);
            QList<QRect> bounds;
            QList<int> demands;
            for (auto* slot : slot_widgets) {
                auto* chips = slot->findChild<QWidget*>(
                    QStringLiteral("quiz_chip_stepper")
                );
                QVERIFY(chips && !chips->isHidden());
                bounds.append(slot->geometry());
                demands.append(slot->card_face_need_short_px());
            }
            for (const bool alternative : { false, true }) {
                view.set_quiz_presentation(
                    alternative ? quiz_answer_style::chips
                                : quiz_answer_style::numeric,
                    alternative ? quiz_feedback_style::stamp
                                : quiz_feedback_style::classic
                );
                QCoreApplication::sendPostedEvents(
                    nullptr, QEvent::LayoutRequest
                );
                QCOMPARE(first->capture_session_state(), state);
                for (qsizetype i = 0; i < slot_widgets.size(); ++i) {
                    QCOMPARE(
                        slot_widgets[i]
                            ->findChild<QWidget*>(
                                QStringLiteral("quiz_chip_stepper")
                            )
                            ->isHidden(),
                        !alternative
                    );
                    QCOMPARE(slot_widgets[i]->geometry(), bounds[i]);
                    QCOMPARE(
                        slot_widgets[i]->findChild<card_widget*>()->geometry(),
                        slot_widgets[i]->rect()
                    );
                    QCOMPARE(
                        slot_widgets[i]->card_face_need_short_px(), demands[i]
                    );
                }
            }
        }
    }
}

void table_tests::quiz_presentation_editor_applies_and_resets() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop presentation editor");
#endif
    struct restore_preferences {
        desktop_ui_preferences ui = load_desktop_ui_preferences();
        trainer_preferences trainer = load_trainer_preferences();
        QColor color = theme_settings::base_color();
        QString source = card_sheet_source_path();

        ~restore_preferences() {
            static_cast<void>(save_desktop_ui_preferences(ui));
            save_trainer_preferences(trainer);
            theme_settings::set_base_color(color);
            set_card_sheet_source_path(source);
        }
    } guard;

    QVERIFY(save_desktop_ui_preferences(desktop_ui_preferences {}));
    table view;
    view.resize(800, 600);
    view.set_slot_count(1);
    auto* slot = view.findChild<table_slot*>();
    QVERIFY(slot != nullptr);
    slot->start_quiz(0);
    for (int i = 0; i < 29; ++i)
        slot->advance_card();
    const auto state = slot->capture_session_state();
    auto* chips
        = slot->findChild<QWidget*>(QStringLiteral("quiz_chip_stepper"));
    QVERIFY(chips != nullptr);
    settings_shared_state shared;
    settings_template_widget editor(
        settings_tab_kind::appearance, nullptr, QString(), &view, &shared
    );
    auto* answer = editor.findChild<QComboBox*>(
        QStringLiteral("desktop_ui_answer_entry")
    );
    auto* feedback
        = editor.findChild<QComboBox*>(QStringLiteral("desktop_ui_feedback"));
    auto* reset
        = editor.findChild<QPushButton*>(QStringLiteral("desktop_ui_reset"));
    QVERIFY(answer && feedback && reset);
    answer->setCurrentIndex(2);
    feedback->setCurrentIndex(2);
    QVERIFY(chips->isHidden()); // Pending editor selection is not applied.
    editor.reset_theme_selection();
    QCOMPARE(answer->currentIndex(), 0);
    QCOMPARE(feedback->currentIndex(), 0);
    answer->setCurrentIndex(2);
    feedback->setCurrentIndex(2);
    QVERIFY(editor.apply_theme_settings());
    QCOMPARE(load_desktop_ui_preferences().answer(), quiz_answer_style::chips);
    QCOMPARE(
        load_desktop_ui_preferences().feedback(), quiz_feedback_style::stamp
    );
    QVERIFY(!chips->isHidden());
    QCOMPARE(slot->capture_session_state(), state);
    answer->setCurrentIndex(1);
    feedback->setCurrentIndex(1);
    editor.reset_theme_selection();
    QCOMPARE(answer->currentIndex(), 2);
    QCOMPARE(feedback->currentIndex(), 2);
    reset->click();
    QCOMPARE(answer->currentIndex(), 0);
    QCOMPARE(feedback->currentIndex(), 0);
    QVERIFY(!chips->isHidden()); // Reset remains pending until Save.
    QVERIFY(editor.apply_theme_settings());
    QVERIFY(chips->isHidden());
    QVERIFY(!load_desktop_ui_preferences().answer_override);
    QVERIFY(!load_desktop_ui_preferences().feedback_override);
    QCOMPARE(slot->capture_session_state(), state);
}

void table_tests::shared_cache_rasterization_populates_visible_slots() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(str_label("assets/non_existent_cards.svg"));

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(1);
    table_widget.show();
    QCoreApplication::processEvents();

    QVERIFY(
        QMetaObject::invokeMethod(
            &table_widget, "on_shared_rasterization_requested",
            Qt::DirectConnection, Q_ARG(int, 128)
        )
    );
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 4000);

    QCOMPARE(
        table_widget.shared_raster_cache_service()->ready_entry_count(), 1
    );
    const QList<table_slot*> slot_widgets
        = table_widget.findChildren<table_slot*>();
    QVERIFY(!slot_widgets.isEmpty());
    QVERIFY(slot_widgets.front()->has_shared_card_faces());
}

void table_tests::shared_cache_generation_cutover_stays_bounded() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(str_label("assets/cards_0.svg"));

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(1);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 1024)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);

    set_card_sheet_source_path(str_label("assets/cards_1.svg"));
    table_widget.apply_theme();
    set_card_sheet_source_path(str_label("assets/cards_2.svg"));
    table_widget.apply_theme();
    set_card_sheet_source_path(str_label("assets/cards_0.svg"));
    table_widget.apply_theme();

    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 15000);

    QVERIFY(
        table_widget.shared_raster_cache_service()->ready_entry_count() <= 2
    );
    QVERIFY(table_widget.shared_raster_cache_service()->in_flight_count() <= 1);
}

void table_tests::shared_generation_cutover_keeps_single_visible_generation() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(str_label("assets/cards_0.svg"));

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(3);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 256)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 10000);

    set_card_sheet_source_path(str_label("assets/cards_1.svg"));
    table_widget.apply_theme();
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 10000);

    QVERIFY(
        table_widget.shared_raster_cache_service()->ready_entry_count() <= 2
    );
    QVERIFY(table_widget.shared_raster_cache_service()->in_flight_count() <= 1);

    const QList<table_slot*> slot_list
        = table_widget.findChildren<table_slot*>();
    int visible_slot_count = 0;
    for (table_slot* slot : slot_list) {
        if (slot == nullptr || !slot->isVisible()) {
            continue;
        }
        ++visible_slot_count;
        QVERIFY(slot->has_shared_card_faces());
    }
    QVERIFY(visible_slot_count > 0);
}

void table_tests::theme_apply_clears_stale_worker_state() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(str_label("assets/cards_0.svg"));

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(1);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 1024)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);

    set_card_sheet_source_path(str_label("assets/cards_1.svg"));
    table_widget.apply_theme();

    QTest::qWait(50);
    QCoreApplication::processEvents();

    QVERIFY(table_widget.shared_raster_cache_service()->in_flight_count() <= 1);
}

void table_tests::theme_and_resize_transitions_are_non_blocking() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(str_label("assets/cards_0.svg"));

    table table_widget;
    table_widget.resize(920, 720);
    table_widget.set_slot_count(4);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 1024)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);

    set_card_sheet_source_path(str_label("assets/cards_2.svg"));
    QElapsedTimer theme_timer;
    theme_timer.start();
    table_widget.apply_theme();
    const qint64 theme_apply_elapsed_ms = theme_timer.elapsed();

    QElapsedTimer resize_timer;
    resize_timer.start();
    table_widget.resize(1000, 760);
    QCoreApplication::processEvents();
    const qint64 resize_elapsed_ms = resize_timer.elapsed();

    QVERIFY(theme_apply_elapsed_ms < 350);
    QVERIFY(resize_elapsed_ms < 350);

    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 15000);
}

void table_tests::session_restore_preflight_is_transactional() {
    table table_widget;
    table_widget.set_slot_count(1);
    table_widget.start_quiz(0, false);
    const table_session_state before = table_widget.capture_session_state();
    QVERIFY(before.quiz_running);
    QCOMPARE(before.slot_states.size(), 1);

    table_session_state invalid = before;
    table_slot_session_state invalid_later_slot = before.slot_states.first();
    invalid_later_slot.strategy_slug = QStringLiteral("missing_strategy");
    invalid_later_slot.strategy_id = 999999;
    invalid.slot_states.append(invalid_later_slot);

    QVERIFY(!table_widget.restore_session_state(invalid));
    QCOMPARE(table_widget.capture_session_state(), before);
}

void table_tests::session_capture_restore_recovers_paused_quiz_state() {
    table source;
    source.set_slot_count(1);
    source.set_pick_interval(300);
    source.start_quiz(0, false);
    source.on_clock_tick(9000, 9000);

    const table_session_state captured = source.capture_session_state();
    QVERIFY(captured.quiz_running);
    QVERIFY(!captured.quiz_paused);
    QCOMPARE(captured.slot_states.size(), 1);
    QVERIFY(captured.slot_states.first().quiz_prompt_active);
    QVERIFY(captured.slot_states.first().card.deck_position > 0);

    table restored;
    restored.set_slot_count(2);
    QVERIFY(restored.restore_session_state(captured));

    table_session_state expected = captured;
    expected.quiz_paused = true;
    for (table_slot_session_state& slot_state : expected.slot_states) {
        slot_state.paused = true;
    }
    QCOMPARE(restored.capture_session_state(), expected);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
