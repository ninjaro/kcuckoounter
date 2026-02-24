#include "include/table_tests.hpp"

#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/table_slot.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"

#include <QFrame>
#include <QLabel>
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
