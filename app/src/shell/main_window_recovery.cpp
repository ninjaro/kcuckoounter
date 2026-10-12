#include "shell/main_window.hpp"

#include "arch/str_label.hpp"
#include "shell/gameplay_reservation.hpp"
#include "table/table.hpp"

#include <QDialog>
#include <QLabel>
#include <QScopeGuard>
#include <QSettings>

void main_window::report_gameplay_storage(const QString& error) {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    const auto hint = str_label("Use Retry gameplay storage to try again.");
#else
    const auto hint
        = str_label("Use Game → Retry gameplay storage to try again.");
#endif
    gameplay_storage_error
        = error.isEmpty() ? QString() : error + QLatin1Char(' ') + hint;
    update_status_text();
    if (!gameplay_storage_error.isEmpty())
        report_gameplay_error(gameplay_storage_error);
    else if (gameplay_setup_error)
        gameplay_setup_error->clear();
}

bool main_window::recover_gameplay_from_ui() {
    if (gameplay_storage_busy)
        return false;
    const QPointer<main_window> guard(this);
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    const bool first_check = !gameplay_recovery_checked;
#endif
    gameplay_recovery_checked = true;
    gameplay_recovery_ready = false;
    const auto binding = std::make_shared<gameplay_reservation>();
    gameplay_binding = binding;
    gameplay_completion_attempted = false;
    QString error;
    {
        gameplay_storage_busy = true;
        const auto operation = qScopeGuard([guard] {
            if (guard)
                guard->gameplay_storage_busy = false;
        });
        QSettings storage(
            QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
        );
        const auto cap = gameplay::desktop_slot_technical_cap;
        gameplay_recovery_service recovery(storage, { cap, cap });
        const auto outcome = binding->recover(*table_widget, recovery, &error);
        if (!guard)
            return false;
        gameplay_recovery_ready
            = outcome != gameplay_reservation::recovery_outcome::failed;
        if (outcome == gameplay_reservation::recovery_outcome::paused
            && setup_dialog)
            setup_dialog->hide();
    }
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    // First native actions and queued startup share this ordered check. Never
    // let early legacy Start overwrite a checkpoint not yet offered to Resume.
    if (first_check && gameplay_recovery_ready
        && !table_widget->active_gameplay_session() && !quiz_started)
        (void)restore_mobile_session_checkpoint();
    if (!guard)
        return false;
#endif
    report_gameplay_storage(error);
    if (!guard)
        return false;
    if (binding->completion() && !gameplay_completion_attempted)
        (void)complete_gameplay_from_ui();
    if (guard && mobile_checkpoint_pending)
        pause_mobile_gameplay();
    if (guard)
        refresh_gameplay_actions();
    return guard && guard->gameplay_recovery_ready;
}

bool main_window::gameplay_storage_ready() {
    if (gameplay_storage_busy)
        return false;
    if (!gameplay_recovery_checked)
        return recover_gameplay_from_ui();
    if (!gameplay_recovery_ready) {
        report_gameplay_error(gameplay_storage_error);
        return false;
    }
    return true;
}

bool main_window::gameplay_retirement_allowed() {
    if (gameplay_storage_busy)
        return false;
    const auto binding = gameplay_binding;
    if (!binding || !binding->sequence() || binding->acknowledged())
        return true;
    if (binding->completion())
        return complete_gameplay_from_ui();
    report_gameplay_storage(str_label(
        "The identified session is still unfinished. Finish it first, or "
        "retry recovery if its activation was interrupted. Stored work is "
        "unchanged."
    ));
    return false;
}

bool main_window::reserve_gameplay_from_ui(std::uint64_t seed) {
    if (gameplay_storage_busy || mobile_background)
        return false;
    const QPointer<main_window> guard(this);
    const auto binding = std::make_shared<gameplay_reservation>();
    gameplay_binding = binding;
    gameplay_completion_attempted = false;
    bool started = false;
    QString error;
    {
        gameplay_storage_busy = true;
        const auto operation = qScopeGuard([guard] {
            if (guard)
                guard->gameplay_storage_busy = false;
        });
        QSettings storage(
            QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
        );
        const auto cap = gameplay::desktop_slot_technical_cap;
        gameplay_recovery_service recovery(storage, { cap, cap });
        started = binding->start(*table_widget, recovery, seed, &error);
        if (!guard)
            return false;
    }
    if (!started)
        report_gameplay_storage(error);
    else
        report_gameplay_storage({});
    if (!guard)
        return false;
    // A nested Finish during Start publication captured while admission was
    // busy. Retry only after the binding's serialized operation has returned.
    if (binding->completion() && !gameplay_completion_attempted)
        (void)complete_gameplay_from_ui();
    if (guard && mobile_checkpoint_pending)
        pause_mobile_gameplay();
    if (guard)
        refresh_gameplay_actions();
    return guard && started;
}

void main_window::retry_gameplay_from_ui() {
    if (gameplay_storage_busy)
        return;
    const auto binding = gameplay_binding;
    if (binding && binding->completion()) {
        (void)complete_gameplay_from_ui();
    } else if (binding && binding->owns(*table_widget)) {
        // A failed checkpoint write never authorizes replacing live work.
        if (table_widget->active_gameplay_session()->phase()
            == gameplay::session_phase::running)
            pause_for_dialog();
        else
            (void)save_gameplay_from_ui();
    } else {
        // Explicit retry can restore a durable reservation whose Start or
        // scene publication was interrupted, using a fresh binding/settings.
        (void)recover_gameplay_from_ui();
    }
}
