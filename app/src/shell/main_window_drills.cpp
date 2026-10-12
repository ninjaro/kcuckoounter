#include "shell/main_window.hpp"

#include "arch/str_label.hpp"
#include "settings/strategy_data.hpp"
#include "shell/drill_picker.hpp"
#include "table/gameplay_layout.hpp"
#include "table/table.hpp"

#include <QPointer>
#include <QSettings>
#include <QSignalBlocker>

training_drill main_window::capture_training_drill() const {
    training_drill drill;
    drill.quiz_type = quiz_type->currentIndex();
    drill.wait_for_answers = wait_for_answers->isChecked();
    drill.allow_skipping = allow_skipping->isChecked();
    drill.dealing_mode = dealing_mode->currentIndex();
    drill.pickup_interval_ms = speed_slider->value();
    drill.slot_settings = table_widget->capture_drill_settings();
    return drill;
}

bool main_window::launch_training_drill(const training_drill& drill) {
    const QPointer<main_window> guard(this);
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    if (!gameplay_recovery_checked)
        (void)recover_gameplay_from_ui();
    if (!guard)
        return false;
#endif
    if (!gameplay_retirement_allowed() || !guard)
        return false;
    const auto* owner = table_widget->active_gameplay_session();
    if ((owner && owner->phase() != gameplay::session_phase::setup
         && owner->phase() != gameplay::session_phase::finished)
        || !is_drill_configuration_supported(drill, strategy_repository()))
        return false;
    if (owner)
        table_widget->clear_gameplay_session();
    if (!guard)
        return false;
    if (!table_widget->configure_drill(drill))
        return false;
    {
        const QSignalBlocker count_blocker(table_slots_count);
        const QSignalBlocker quiz_blocker(quiz_type);
        const QSignalBlocker wait_blocker(wait_for_answers);
        const QSignalBlocker skip_blocker(allow_skipping);
        const QSignalBlocker dealing_blocker(dealing_mode);
        table_slots_count->setValue(
            static_cast<int>(drill.slot_settings.size())
        );
        quiz_type->setCurrentIndex(drill.quiz_type);
        wait_for_answers->setChecked(drill.wait_for_answers);
        wait_for_answers->setEnabled(drill.quiz_type == 0);
        allow_skipping->setChecked(drill.allow_skipping);
        dealing_mode->setCurrentIndex(drill.dealing_mode);
    }
    speed_slider->setValue(drill.pickup_interval_ms);
    on_continue_button_clicked();
    persist_setup_preferences();
    on_start_pause_triggered();
    return true;
}

void main_window::on_saved_drills_triggered() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    return;
#else
    if (gameplay_storage_busy)
        return;
    const QPointer<main_window> guard(this);
    const auto* owner = table_widget->active_gameplay_session();
    const bool owned = owner != nullptr;
    const bool live_owned = owner
        && (owner->phase() == gameplay::session_phase::running
            || owner->phase() == gameplay::session_phase::paused);
    const bool resume = owned
        ? owner->phase() == gameplay::session_phase::running
        : quiz_started && !quiz_paused;
    const auto pause_revision = mobile_pause_revision;
    const auto cap = gameplay::desktop_slot_technical_cap;
    const drill_picker_policy policy {
        { cap, cap },
        !owned || !table_widget->has_open_gameplay_settings(),
        !live_owned,
        !live_owned && (owned || !quiz_started),
        owned ? !live_owned : quiz_started
    };
    bool replaced = false;
    QPointer<drill_picker> picker;
    const auto replacement = connect(
        table_widget, &table::gameplay_session_about_to_change, this, [&] {
            replaced = true;
            if (picker)
                picker->invalidate();
        }
    );
    pause_for_dialog();
    if (!guard || replaced) {
        disconnect(replacement);
        return;
    }
    saved_drill current = capture_training_drill();
    if (owned) {
        const auto snapshot = capture_gameplay_drill(
            *table_widget->active_gameplay_session(),
            str_label("Current setup"), policy.limits
        );
        if (!snapshot) {
            disconnect(replacement);
            report_gameplay_error(
                str_label("Could not capture current gameplay configuration.")
            );
            if (resume && !mobile_background
                && pause_revision == mobile_pause_revision)
                (void)table_widget->resume_gameplay_runtime();
            return;
        }
        current = *snapshot;
    }
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    picker = new drill_picker(storage, std::move(current), policy, this);
    const auto result = picker->exec();
    std::optional<saved_drill> chosen;
    if (guard && picker && !replaced && result == QDialog::Accepted)
        chosen = picker->chosen();
    delete picker.data();
    disconnect(replacement);
    if (!guard || replaced)
        return;
    bool loaded = false;
    if (chosen) {
        if (const auto* target = std::get_if<gameplay_drill>(&*chosen))
            loaded = load_gameplay_drill(*target);
        else
            loaded = launch_training_drill(std::get<training_drill>(*chosen));
        if (!guard)
            return;
        if (!loaded)
            report_gameplay_error(str_label(
                "This drill can no longer be loaded. The current session is "
                "unchanged."
            ));
    }
    if (!loaded && resume && !mobile_background
        && pause_revision == mobile_pause_revision) {
        if (owned) {
            auto* active = table_widget->active_gameplay_session();
            if (active && active->phase() == gameplay::session_phase::paused)
                (void)table_widget->resume_gameplay_runtime();
        } else if (quiz_started && quiz_paused) {
            on_start_pause_triggered();
        }
    }
#endif
}
