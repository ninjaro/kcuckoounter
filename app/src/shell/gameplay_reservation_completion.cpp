#include "shell/gameplay_reservation.hpp"

#include "arch/str_label.hpp"
#include "table/table.hpp"

#include <QDateTime>
#include <QScopedValueRollback>

void gameplay_reservation::capture_result(
    const gameplay::session_result& result
) {
    if (!terminal && owner && owns(*owner))
        terminal = gameplay_result_record { *identity,
                                            QDateTime::currentMSecsSinceEpoch(),
                                            result };
}

const std::optional<gameplay_result_record>&
gameplay_reservation::completion() const {
    return terminal;
}

bool gameplay_reservation::acknowledged() const { return admitted; }

bool gameplay_reservation::complete(
    gameplay_recovery_service& recovery, QString* error
) {
    if (error)
        error->clear();
    if (busy || !terminal) {
        if (error)
            *error = str_label(
                "Completion requires an idle binding with a retained terminal "
                "result."
            );
        return false;
    }
    if (admitted)
        return true;
    const QScopedValueRollback<bool> operation(busy, true);
    admitted = recovery.complete(*terminal, error);
    return admitted;
}
