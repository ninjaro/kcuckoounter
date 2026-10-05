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

#include <QSignalBlocker>
#include <QtGlobal>

table_slot_session_state table_slot::capture_session_state() const {
    if (gameplay_owner)
        return {};
    table_slot_session_state state;
    if (card_widget_internal != nullptr) {
        state.card = card_widget_internal->capture_session_state();
    }
    state.paused = current_phase == slot_phase::paused;
    state.infinity_enabled = is_infinity_enabled();
    state.deck_count
        = deck_count_spin_box != nullptr ? deck_count_spin_box->value() : 1;
    if (strategy_combo_box != nullptr) {
        state.strategy_slug = strategy_combo_box->currentData().toString();
        state.strategy_id
            = strategy_combo_box->currentData(Qt::UserRole + 1).toInt();
    }
    state.show_card_indexing
        = show_card_indexing != nullptr && show_card_indexing->isChecked();
    state.show_strategy_name
        = show_strategy_name != nullptr && show_strategy_name->isChecked();
    state.training_mode
        = training_check_box != nullptr && training_check_box->isChecked();
    state.quiz_prompt_active = quiz_prompt_active;
    state.quiz_feedback_active = quiz_feedback_active;
    state.quiz_continue_visible = quiz_continue_visible;
    state.quiz_input_value
        = quiz_spin_box != nullptr ? quiz_spin_box->value() : 0;
    if (quiz_feedback_label != nullptr) {
        state.quiz_feedback_text = quiz_feedback_label->text();
    }
    return state;
}

bool table_slot::restore_session_state(const table_slot_session_state& state) {
    if (gameplay_owner || !is_session_state_valid(state)
        || card_widget_internal == nullptr || infinity_check_box == nullptr
        || deck_count_spin_box == nullptr || strategy_combo_box == nullptr
        || show_card_indexing == nullptr || show_strategy_name == nullptr
        || training_check_box == nullptr || quiz_spin_box == nullptr
        || state.deck_count < deck_count_spin_box->minimum()
        || state.deck_count > deck_count_spin_box->maximum()) {
        return false;
    }

    int strategy_index = -1;
    for (int index = 0; index < strategy_combo_box->count(); ++index) {
        if (strategy_combo_box->itemData(index).toString()
            == state.strategy_slug) {
            strategy_index = index;
            break;
        }
    }
    if (strategy_index < 0) {
        for (int index = 0; index < strategy_combo_box->count(); ++index) {
            if (strategy_combo_box->itemData(index, Qt::UserRole + 1).toInt()
                == state.strategy_id) {
                strategy_index = index;
                break;
            }
        }
    }
    if (strategy_index < 0 && strategy_combo_box->isEnabled()) {
        return false;
    }

    finish_settings_editor(false);
    const QSignalBlocker infinity_blocker(infinity_check_box);
    const QSignalBlocker deck_blocker(deck_count_spin_box);
    const QSignalBlocker strategy_blocker(strategy_combo_box);
    const QSignalBlocker indexing_blocker(show_card_indexing);
    const QSignalBlocker name_blocker(show_strategy_name);
    const QSignalBlocker training_blocker(training_check_box);
    infinity_check_box->setChecked(state.infinity_enabled);
    deck_count_spin_box->setValue(state.deck_count);
    if (strategy_index >= 0) {
        strategy_combo_box->setCurrentIndex(strategy_index);
    }
    show_card_indexing->setChecked(state.show_card_indexing);
    show_strategy_name->setChecked(state.show_strategy_name);
    training_check_box->setChecked(state.training_mode);
    update_infinity_state(infinity_check_box, deck_count_spin_box);

    if (!card_widget_internal->restore_session_state(state.card)) {
        return false;
    }
    sync_card_display_settings();

    current_phase = slot_phase::paused;
    quiz_prompt_active = state.quiz_prompt_active;
    quiz_feedback_active = state.quiz_feedback_active;
    quiz_continue_visible = state.quiz_continue_visible;
    last_quiz_input_value = state.quiz_input_value;
    quiz_spin_box->setValue(state.quiz_input_value);
    if (quiz_feedback_label != nullptr) {
        quiz_feedback_label->setText(state.quiz_feedback_text);
    }
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_hide_cards(quiz_prompt_active);
        card_widget_internal->set_table_marking_source(bundled_asset_path(
            quiz_prompt_active ? QStringLiteral("mad.svg")
                               : QStringLiteral("cuckoo.svg")
        ));
    }
    update_overlay_layout();
    update_quiz_controls_visibility();
    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->setVisible(quiz_prompt_active);
    }
    set_paused(true);
    return true;
}

bool table_slot::is_session_state_valid(const table_slot_session_state& state) {
    if (state.infinity_enabled != state.card.infinity_enabled
        || state.deck_count != state.card.decks_count || state.deck_count < 1
        || state.deck_count > 16 || state.quiz_input_value < -9999
        || state.quiz_input_value > 9999
        || state.quiz_feedback_text.size() > 2048
        || state.strategy_slug.size() > 128
        || (state.quiz_feedback_active && !state.quiz_prompt_active)
        || !card_widget::can_restore_session_state(state.card)) {
        return false;
    }

    const strategy_catalog& repository = strategy_repository();
    if (!repository.is_valid()) {
        return state.strategy_slug.isEmpty() && state.strategy_id == 0;
    }
    return std::ranges::any_of(
        repository.strategies, [&state](const strategy_data& strategy) {
            return (!state.strategy_slug.isEmpty()
                    && strategy.slug == state.strategy_slug)
                || (state.strategy_id > 0 && strategy.id == state.strategy_id);
        }
    );
}
