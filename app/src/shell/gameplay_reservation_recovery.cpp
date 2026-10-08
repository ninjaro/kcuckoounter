#include "shell/gameplay_reservation.hpp"

#include "arch/str_label.hpp"
#include "table/table.hpp"

#include <QScopedValueRollback>

gameplay_reservation::recovery_outcome gameplay_reservation::recover(
    table& view, gameplay_recovery_service& recovery, QString* error,
    const gameplay::generation_policy& resources
) {
    if (error)
        error->clear();
    if (busy || identity) {
        if (error)
            *error
                = str_label("Recovery requires an idle, unidentified binding.");
        return recovery_outcome::failed;
    }
    const QScopedValueRollback<bool> operation(busy, true);
    const QPointer<table> guard(&view);
    QString diagnostic;
    const auto record = recovery.load(&diagnostic);
    if (error)
        *error = diagnostic;
    if (!diagnostic.isEmpty())
        return recovery_outcome::failed;
    if (!record)
        return recovery_outcome::absent;
    if (const auto* pending = std::get_if<gameplay_result_record>(&*record))
        return recovery.complete(*pending, error) ? recovery_outcome::completed
                                                  : recovery_outcome::failed;
    const auto& active = std::get<gameplay_active_record>(*record);
    if (!guard || guard->has_gameplay_runtime()
        || (guard->active_gameplay_session()
            && guard->active_gameplay_session()->phase()
                != gameplay::session_phase::setup)) {
        if (error)
            *error = str_label(
                "Active recovery cannot replace a live gameplay host. Stored "
                "data is unchanged."
            );
        return recovery_outcome::failed;
    }
    const bool restored = guard->restore_gameplay_checkpoint(
        active.checkpoint, resources,
        [this, guard, sequence = active.sequence](std::uint64_t revision) {
            identity = sequence;
            owner = guard;
            scene = revision;
            attach_result();
        }
    );
    if (restored && guard && owns(*guard)
        && guard->active_gameplay_session()->phase()
            == gameplay::session_phase::paused)
        return recovery_outcome::paused;
    if (error)
        *error = identity
            ? str_label(
                  "Recovery publication was interrupted. The identified "
                  "checkpoint is preserved."
              )
            : str_label(
                  "The identified checkpoint could not be installed on this "
                  "table. Stored data is unchanged."
              );
    return recovery_outcome::failed;
}
