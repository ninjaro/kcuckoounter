#include "shell/main_window.hpp"

#include "arch/str_label.hpp"
#include "settings/strategy_data.hpp"
#include "shell/drill_picker.hpp"
#include "table/gameplay_layout.hpp"
#include "table/table.hpp"

#include <QPointer>
#include <QSettings>

void main_window::on_saved_drills_triggered() {
    if (gameplay_storage_busy)
        return;
    const QPointer<main_window> guard(this);
    const auto* owner = table_widget->active_gameplay_session();
    const bool owned = owner != nullptr;
    const bool live_owned = owner
        && (owner->phase() == gameplay::session_phase::running
            || owner->phase() == gameplay::session_phase::paused);
    const bool resume
        = owned ? owner->phase() == gameplay::session_phase::running : false;
    const auto pause_revision = mobile_pause_revision;
    const auto cap = gameplay::slot_technical_cap;
    const drill_picker_policy policy {
        { cap, cap },
        owned && !table_widget->has_open_gameplay_settings(),
        !live_owned,
        !live_owned
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
    std::optional<gameplay_drill> current;
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
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    picker->setWindowState(Qt::WindowMaximized);
#endif
    const auto result = picker->exec();
    std::optional<gameplay_drill> chosen;
    if (guard && picker && !replaced && result == QDialog::Accepted)
        chosen = picker->chosen();
    delete picker.data();
    disconnect(replacement);
    if (!guard || replaced)
        return;
    bool loaded = false;
    if (chosen) {
        loaded = load_gameplay_drill(*chosen);
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
        }
    }
}
