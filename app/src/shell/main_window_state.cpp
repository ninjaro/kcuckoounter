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
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "shell/gameplay_reservation.hpp"

#include <QDateTime>
#include <QDialog>
#include <QSignalBlocker>

#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
void main_window::persist_desktop_shell_state() const {
    desktop_shell_state state;
    state.geometry = saveGeometry();
    state.main_window_state = saveState();
    save_desktop_shell_state(state);
}

void main_window::restore_desktop_shell_state() {
    const desktop_shell_state state = load_desktop_shell_state();
    if (!state.geometry.isEmpty()) {
        restoreGeometry(state.geometry);
    }
    if (!state.main_window_state.isEmpty()) {
        restoreState(state.main_window_state);
    }
}

#endif

void main_window::pause_mobile_gameplay() {
    const QPointer<main_window> guard(this);
    mobile_checkpoint_pending = true;
    if (const auto* owner = table_widget->active_gameplay_session();
        owner && owner->phase() == gameplay::session_phase::running)
        (void)table_widget->pause_gameplay_runtime();
    if (!guard || gameplay_storage_busy)
        return; // Admission drains this request after its serialized operation.
    mobile_checkpoint_pending = false;
    const auto binding = gameplay_binding;
    if (binding && binding->completion())
        (void)complete_gameplay_from_ui();
    else
        (void)save_gameplay_from_ui();
}

void main_window::apply_mobile_state(Qt::ApplicationState state) {
    if (state != Qt::ApplicationActive && !mobile_background)
        ++mobile_pause_revision;
    mobile_background = state != Qt::ApplicationActive;
    if (!mobile_background) {
        update_status_text();
        return; // Foregrounding never implicitly resumes gameplay.
    }
    if (table_widget
        && (table_widget->active_gameplay_session()
            || (gameplay_binding && gameplay_binding->completion()))) {
        pause_mobile_gameplay();
        return;
    }
}
