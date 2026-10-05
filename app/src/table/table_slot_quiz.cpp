#include "table/table_slot.hpp"

#include "arch/android_ui.hpp"
#include "arch/asset_locator.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/card_widget.hpp"
#include "table/gameplay_setup.hpp"
#include "table/infinity_spinbox.hpp"
#include "table/settings_template.hpp"
#include "table/slot_settings.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QStackedLayout>
#include <QToolButton>

#include <QString>
#include <QtGlobal>

#include <limits>

void table_slot::on_quiz_answer_button_clicked() {
    if (gameplay_owner) {
        const auto value = gameplay_input_value();
        if (!gameplay_prompt || !gameplay_input->isEnabled() || !value)
            return;
        const auto prompt = *gameplay_prompt;
        const QPointer<table_slot> guard(this);
        if (!gameplay_host->edit_gameplay_quiz(prompt, *value) || !guard)
            return;
        (void)gameplay_host->check_gameplay_quiz(prompt);
        return;
    }
    if (!quiz_prompt_active || quiz_feedback_active || quiz_spin_box == nullptr
        || card_widget_internal == nullptr) {
        return;
    }

    quiz_spin_box->interpretText();
    const int expected = card_widget_internal->current_total_weight();
    const int provided = quiz_spin_box->value();
    last_quiz_input_value = provided;
    const bool training_enabled = is_training_enabled();
    if (provided == expected) {
        if (!training_enabled) {
            emit score_adjusted(1, 0);
        }
        clear_quiz_prompt();
        return;
    }

    const QString message
        = str_label("You've set %1 while the correct answer is %2.")
              .arg(provided)
              .arg(expected);
    if (training_enabled) {
        show_quiz_feedback(message, true);
        return;
    }

    card_widget_internal->mark_deck_exhausted();
    show_quiz_feedback(message, false);
}

void table_slot::on_quiz_skip_button_clicked() {
    if (gameplay_owner) {
        if (!gameplay_prompt || !quiz_skip_button->isEnabled())
            return;
        // Latest valid text was already edited into the owner. Skip does not
        // coerce an incomplete draft or replace memory with the correction.
        const auto prompt = *gameplay_prompt;
        (void)gameplay_host->skip_gameplay_quiz(prompt);
        return;
    }
    if (!quiz_prompt_active || quiz_feedback_active || !allow_skipping_flag
        || quiz_spin_box == nullptr || card_widget_internal == nullptr) {
        return;
    }

    const int expected = card_widget_internal->current_total_weight();
    const int provided = quiz_spin_box->value();
    last_quiz_input_value = provided;
    if (!is_training_enabled()) {
        emit score_adjusted(0, -1);
    }

    show_quiz_feedback(
        str_label("You've set %1 while the correct answer is %2.")
            .arg(provided)
            .arg(expected),
        true
    );
}

void table_slot::on_quiz_continue_button_clicked() {
    if (gameplay_owner) {
        const bool had_correction = gameplay_feedback.has_value();
        gameplay_feedback.reset();
        const QPointer<table_slot> guard(this);
        refresh_gameplay_quiz(); // presentation dismissal only, no score/deal
        if (guard && had_correction)
            emit gameplay_correction_dismissed();
        return;
    }
    if (!quiz_prompt_active) {
        return;
    }

    clear_quiz_prompt();
}

void table_slot::show_quiz_prompt() {
    if (quiz_prompt_active) {
        return;
    }
    quiz_prompt_active = true;
    quiz_feedback_active = false;
    quiz_continue_visible = false;
    update_overlay_layout();
    if (quiz_spin_box != nullptr) {
        quiz_spin_box->setValue(last_quiz_input_value);
    }
    update_quiz_controls_visibility();
    if (!isVisible()) {
        show();
    }
    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->show();
    }
    if (quiz_spin_box != nullptr) {
        quiz_spin_box->setFocus(Qt::OtherFocusReason);
    }
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_hide_cards(true);
        card_widget_internal->set_table_marking_source(
            bundled_asset_path(QStringLiteral("mad.svg"))
        );
    }
    if (!is_training_enabled()) {
        emit score_adjusted(0, 1);
    }
    set_paused(current_phase == slot_phase::paused);
    if (compact_controls_button != nullptr
        && compact_controls_button->isVisible()) {
        compact_controls_button->setFocus(Qt::OtherFocusReason);
    }
}

void table_slot::clear_quiz_prompt() {
    if (!quiz_prompt_active) {
        return;
    }
    quiz_prompt_active = false;
    quiz_feedback_active = false;
    quiz_continue_visible = false;
    update_overlay_layout();
    update_quiz_controls_visibility();
    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->hide();
    }
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_hide_cards(false);
        card_widget_internal->set_table_marking_source(
            bundled_asset_path(QStringLiteral("cuckoo.svg"))
        );
    }
    set_paused(current_phase == slot_phase::paused);
}

void table_slot::show_quiz_feedback(
    const QString& message, bool show_continue
) {
    if (quiz_feedback_label != nullptr) {
        quiz_feedback_label->setText(message);
    }
    quiz_feedback_active = true;
    quiz_continue_visible = show_continue;
    update_quiz_controls_visibility();
    if (!isVisible()) {
        show();
    }
    if (quiz_continue_button != nullptr) {
        quiz_continue_button->setVisible(show_continue);
        quiz_continue_button->setEnabled(show_continue);
    }
    set_paused(current_phase == slot_phase::paused);
}

void table_slot::update_quiz_controls_visibility() {
    if (gameplay_owner && gameplay_input) {
        const bool pending = gameplay_prompt.has_value();
        const bool editable = pending
            && gameplay_owner->phase() == gameplay::session_phase::running;
        const auto value = gameplay_input_value();
        quiz_bar_widget->setEnabled(true);
        quiz_layout->setCurrentWidget(
            quiz_feedback_active ? quiz_feedback_widget : quiz_prompt_widget
        );
        quiz_prompt_widget->setVisible(!quiz_feedback_active);
        quiz_feedback_widget->setVisible(quiz_feedback_active);
        quiz_spin_box->hide();
        quiz_spin_box->setEnabled(false);
        gameplay_input->setVisible(!quiz_feedback_active);
        gameplay_input->setEnabled(editable);
        gameplay_input_error->setVisible(pending && !value);
        gameplay_correction_label->setVisible(
            pending && gameplay_feedback.has_value()
        );
        quiz_answer_button->setVisible(pending);
        quiz_answer_button->setEnabled(editable && value.has_value());
        const bool skip = pending && gameplay_owner->configuration().allow_skip;
        quiz_skip_button->setVisible(skip);
        quiz_skip_button->setEnabled(editable && skip);
        quiz_feedback_label->setVisible(quiz_feedback_active);
        quiz_continue_button->setVisible(quiz_feedback_active);
        quiz_continue_button->setEnabled(quiz_feedback_active);
        if (quiz_chip_widget) {
            quiz_chip_widget->setVisible(
                pending && answer_style == quiz_answer_style::chips
            );
            for (auto* button :
                 quiz_chip_widget->findChildren<QPushButton*>()) {
                const auto step = button->objectName()
                                      .mid(QStringLiteral("quiz_chip_").size())
                                      .toInt();
                button->setEnabled(
                    editable && value
                    && (step > 0
                            ? *value <= std::numeric_limits<std::int64_t>::max()
                                    - step
                            : *value >= std::numeric_limits<std::int64_t>::min()
                                    - step)
                );
            }
        }
        update_compact_controls();
        return;
    }
    if (quiz_layout != nullptr && quiz_prompt_widget != nullptr
        && quiz_feedback_widget != nullptr) {
        quiz_layout->setCurrentWidget(
            quiz_feedback_active ? quiz_feedback_widget : quiz_prompt_widget
        );
    }
    if (quiz_prompt_widget != nullptr) {
        quiz_prompt_widget->setVisible(!quiz_feedback_active);
    }
    if (quiz_feedback_widget != nullptr) {
        quiz_feedback_widget->setVisible(quiz_feedback_active);
    }
    if (quiz_spin_box != nullptr) {
        quiz_spin_box->setVisible(!quiz_feedback_active);
    }
    if (quiz_weight_label != nullptr) {
        quiz_weight_label->setVisible(!quiz_feedback_active);
    }
    if (quiz_answer_button != nullptr) {
        quiz_answer_button->setVisible(!quiz_feedback_active);
    }
    if (quiz_skip_button != nullptr) {
        quiz_skip_button->setVisible(
            !quiz_feedback_active && allow_skipping_flag
        );
        quiz_skip_button->setEnabled(
            !quiz_feedback_active && allow_skipping_flag
        );
    }
    if (quiz_feedback_label != nullptr) {
        quiz_feedback_label->setVisible(quiz_feedback_active);
    }
    if (quiz_continue_button != nullptr) {
        const bool show_continue
            = quiz_feedback_active && quiz_continue_visible;
        quiz_continue_button->setVisible(show_continue);
        quiz_continue_button->setEnabled(show_continue);
    }
    update_compact_controls();
}
