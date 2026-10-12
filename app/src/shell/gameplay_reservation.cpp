#include "shell/gameplay_reservation.hpp"

#include "arch/str_label.hpp"
#include "table/table.hpp"

#include <QScopedValueRollback>

gameplay_reservation::~gameplay_reservation() { detach_result(); }

void gameplay_reservation::attach_result() {
    if (owner && owner->gameplay_scene() == scene)
        owner->gameplay_completion
            = [this](const auto& result) { capture_result(result); };
}

void gameplay_reservation::detach_result() {
    // A failed Start can still have an unattached Setup owner. Do not require
    // owns(), and never unregister a replacement scene's hook.
    if (owner && owner->gameplay_scene() == scene)
        owner->gameplay_completion = {};
}

std::optional<std::uint64_t> gameplay_reservation::sequence() const {
    return identity;
}

bool gameplay_reservation::owns(const table& view) const {
    return identity && owner == &view && scene == view.gameplay_scene()
        && view.has_gameplay_runtime();
}

bool gameplay_reservation::start(
    table& view, gameplay_recovery_service& recovery,
    std::uint64_t dealing_seed, QString* error,
    const gameplay::generation_policy& resources
) {
    if (error)
        error->clear();
    if (busy || identity) {
        if (error)
            *error = str_label(
                "This binding is busy or already retains a gameplay "
                "reservation."
            );
        return false;
    }
    const QScopedValueRollback<bool> operation(busy, true);
    const QPointer<table> guard(&view);
    const auto revision = view.gameplay_scene();
    const bool started = view.start_gameplay_runtime(
        dealing_seed,
        [this, &recovery, error, guard, revision](const auto& checkpoint) {
            identity = recovery.begin(checkpoint, error);
            if (!identity)
                return false;
            // Retain before the table can publish Running. If activation is
            // aborted, storage still owns an exact unplayed recovery record.
            owner = guard;
            scene = revision;
            attach_result();
            return true;
        },
        resources
    );
    if (started && guard && owns(*guard))
        return true;
    detach_result();
    owner.clear(); // A later raw Start cannot claim an aborted reservation.
    if (error && error->isEmpty())
        *error = identity ? str_label(
                                "Gameplay activation was interrupted. Its "
                                "reservation is preserved for recovery."
                            )
                          : str_label(
                                "The prepared gameplay scene could not be "
                                "reserved or started."
                            );
    return false;
}
