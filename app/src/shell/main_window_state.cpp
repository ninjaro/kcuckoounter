#include "shell/main_window.hpp"

#include "card_helpers/card_sheet.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"

#include "arch/android_ui.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/session_checkpoint.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "settings/training_progress.hpp"

#include <QDateTime>
#include <QSignalBlocker>

void main_window::persist_setup_preferences() const {
    if (table_widget && table_widget->active_gameplay_session())
        return; // G8 owns target preference mapping; never write target to v1
    if (table_slots_count == nullptr || quiz_type == nullptr
        || wait_for_answers == nullptr || allow_skipping == nullptr
        || dealing_mode == nullptr || speed_slider == nullptr) {
        return;
    }

    trainer_preferences preferences = load_trainer_preferences();
    preferences.slot_count = table_slots_count->value();
    preferences.quiz_type = quiz_type->currentIndex();
    preferences.wait_for_answers = wait_for_answers->isChecked();
    preferences.allow_skipping = allow_skipping->isChecked();
    preferences.dealing_mode = dealing_mode->currentIndex();
    preferences.pickup_interval_ms = speed_slider->value();
    save_trainer_preferences(preferences);
}

void main_window::persist_desktop_shell_state() const {
#if defined(Q_OS_ANDROID)
    return;
#else
    desktop_shell_state state;
    state.geometry = saveGeometry();
    state.main_window_state
        = saveState(desktop_shell_state::qt_main_window_state_version);
    save_desktop_shell_state(state);
#endif
}

void main_window::restore_desktop_shell_state() {
#if defined(Q_OS_ANDROID)
    return;
#else
    const desktop_shell_state state = load_desktop_shell_state();
    if (!state.geometry.isEmpty()) {
        restoreGeometry(state.geometry);
    }
    if (!state.main_window_state.isEmpty()) {
        restoreState(
            state.main_window_state,
            desktop_shell_state::qt_main_window_state_version
        );
    }
#endif
}

void main_window::persist_mobile_session_checkpoint(bool was_running) const {
#if defined(Q_OS_ANDROID)
    if (!quiz_started || table_widget == nullptr || clock_timer == nullptr) {
        return;
    }
    trainer_session_checkpoint checkpoint;
    checkpoint.table = table_widget->capture_session_state();
    checkpoint.score_correct = score_correct;
    checkpoint.score_total = score_total;
    checkpoint.elapsed_ms = clock_timer->elapsed_time_ms();
    checkpoint.was_running = was_running;
    checkpoint.saved_at_utc_ms = QDateTime::currentMSecsSinceEpoch();
    save_trainer_session_checkpoint(checkpoint);
#else
    Q_UNUSED(was_running);
#endif
}

bool main_window::restore_mobile_session_checkpoint() {
#if !defined(Q_OS_ANDROID)
    return false;
#else
    const std::optional<trainer_session_checkpoint> checkpoint
        = load_trainer_session_checkpoint();
    if (!checkpoint.has_value() || table_widget == nullptr
        || clock_timer == nullptr
        || !table_widget->restore_session_state(checkpoint->table)) {
        if (checkpoint.has_value()) {
            clear_trainer_session_checkpoint();
        }
        return false;
    }

    if (table_slots_count != nullptr) {
        const QSignalBlocker blocker(table_slots_count);
        table_slots_count->setValue(
            static_cast<int>(checkpoint->table.slot_states.size())
        );
    }
    score_correct = checkpoint->score_correct;
    score_total = checkpoint->score_total;
    quiz_started = true;
    quiz_paused = true;
    quiz_finished = false;
    mobile_checkpoint_restored = true;
    mobile_lifecycle_paused = checkpoint->was_running;
    clock_timer->set_elapsed_time_ms(checkpoint->elapsed_ms);
    last_mobile_checkpoint_elapsed_ms = checkpoint->elapsed_ms;
    if (start_pause_action != nullptr) {
        update_start_pause_action(true);
        start_pause_action->setEnabled(true);
    }
    if (finish_action != nullptr) {
        finish_action->setEnabled(true);
    }
    if (setup_dialog != nullptr) {
        setup_dialog->hide();
    }
    refresh_clock_label();
    update_status_text();
    return true;
#endif
}

void main_window::clear_mobile_session_checkpoint() {
#if defined(Q_OS_ANDROID)
    clear_trainer_session_checkpoint();
#endif
}

void main_window::on_application_state_changed(Qt::ApplicationState state) {
#if defined(Q_OS_ANDROID)
    if (state == Qt::ApplicationActive || !quiz_started) {
        update_status_text();
        return;
    }

    const bool was_running = !quiz_paused || mobile_lifecycle_paused;
    if (was_running && table_widget != nullptr) {
        table_widget->set_paused(true);
        quiz_paused = true;
        mobile_lifecycle_paused = true;
        if (start_pause_action != nullptr) {
            update_start_pause_action(true);
        }
        if (clock_timer != nullptr) {
            clock_timer->pause();
        }
    }
    persist_setup_preferences();
    persist_mobile_session_checkpoint(was_running);
    update_status_text();
#else
    Q_UNUSED(state);
#endif
}
