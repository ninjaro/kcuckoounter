#include "shell/main_window.hpp"

#include "arch/str_label.hpp"
#include "shell/gameplay_reservation.hpp"
#include "table/table.hpp"

#include <QCloseEvent>
#include <QScopeGuard>
#include <QSettings>

bool main_window::complete_gameplay_from_ui() {
    const auto binding = gameplay_binding;
    if (!binding || !binding->completion() || binding->acknowledged())
        return true;
    if (gameplay_storage_busy)
        return false;
    const QPointer<main_window> guard(this);
    gameplay_storage_busy = true;
    gameplay_completion_attempted = true;
    const auto operation = qScopeGuard([guard] {
        if (guard) {
            guard->gameplay_storage_busy = false;
            guard->refresh_gameplay_actions();
        }
    });
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    const auto cap = gameplay::desktop_slot_technical_cap;
    gameplay_recovery_service recovery(storage, { cap, cap });
    QString error;
    const bool saved = binding->complete(recovery, &error);
    if (!guard)
        return false;
    if (saved)
        gameplay_recovery_ready = true;
    report_gameplay_storage(error);
    return guard && saved;
}

bool main_window::save_gameplay_from_ui() {
    const auto binding = gameplay_binding;
    if (!binding || !binding->owns(*table_widget))
        return true; // Explicit raw/legacy hosts do not acquire an identity.
    if (binding->completion())
        return complete_gameplay_from_ui();
    if (gameplay_storage_busy)
        return false;
    if (table_widget->active_gameplay_session()->phase()
        != gameplay::session_phase::paused) {
        report_gameplay_storage(str_label(
            "Pause the identified session before saving its continuation."
        ));
        return false;
    }
    const QPointer<main_window> guard(this);
    gameplay_storage_busy = true;
    const auto operation = qScopeGuard([guard] {
        if (guard) {
            guard->gameplay_storage_busy = false;
            guard->refresh_gameplay_actions();
        }
    });
    const auto checkpoint = table_widget->capture_gameplay_checkpoint();
    if (!guard)
        return false;
    QString error;
    bool saved = false;
    if (checkpoint && binding->owns(*table_widget)) {
        QSettings storage(
            QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
        );
        const auto cap = gameplay::desktop_slot_technical_cap;
        gameplay_recovery_service recovery(storage, { cap, cap });
        saved = recovery.save(*binding->sequence(), *checkpoint, &error);
    } else {
        error = str_label(
            "The identified continuation could not be captured. Its stored "
            "checkpoint is unchanged."
        );
    }
    if (!guard)
        return false;
    if (saved)
        gameplay_recovery_ready = true;
    report_gameplay_storage(error);
    return guard && saved;
}

void main_window::closeEvent(QCloseEvent* event) {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    if (gameplay_storage_busy) {
        event->ignore();
        return;
    }
    const QPointer<main_window> guard(this);
    const auto binding = gameplay_binding;
    if (binding && binding->owns(*table_widget) && !binding->completion()) {
        const auto* owner = table_widget->active_gameplay_session();
        if (owner->phase() == gameplay::session_phase::running)
            (void)table_widget->pause_gameplay_runtime();
        if (!guard)
            return;
        if (!binding->completion() && !save_gameplay_from_ui()) {
            event->ignore();
            return;
        }
    }
    if (!guard)
        return;
    // No new roster is being requested: an interrupted but durable active
    // reservation is safe to reopen. Only unstored terminal metadata blocks.
    if (binding && binding->completion() && !complete_gameplay_from_ui()) {
        event->ignore();
        return;
    }
    if (!guard)
        return;
#endif
    BaseMainWindow::closeEvent(event);
}
