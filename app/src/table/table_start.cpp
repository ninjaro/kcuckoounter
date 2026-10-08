#include "table/table.hpp"

#include <QPointer>
#include <QScopeGuard>

#include <utility>

std::uint64_t table::gameplay_scene() const { return gameplay_scene_revision; }

bool table::has_gameplay_runtime() const { return gameplay_runtime_attached; }

bool table::start_gameplay_runtime(
    std::uint64_t dealing_seed, const start_gate& admit,
    const gameplay::generation_policy& resources
) {
    if (!admit || gameplay_start_pending || !gameplay_owner
        || gameplay_runtime_attached || !gameplay_layout
        || slot_widgets.size() != gameplay_owner->size()
        || !has_usable_gameplay_layout())
        return false;
    auto initial
        = gameplay::capture_start(*gameplay_owner, dealing_seed, resources);
    if (!initial)
        return false;
    const gameplay_checkpoint checkpoint { std::move(*initial), 0 };
    const QPointer<table> guard(this);
    const auto revision = gameplay_scene_revision;
    gameplay_start_pending = true;
    const auto pending = qScopeGuard([guard] {
        if (guard)
            guard->gameplay_start_pending = false;
    });
    if (!admit(checkpoint) || !guard || gameplay_scene_revision != revision
        || gameplay_runtime_attached || !gameplay_owner
        || !has_usable_gameplay_layout())
        return false;
    // The callback owns I/O, not setup mutation. Recheck the borrowed owner:
    // even direct domain edits must not start a different state under this key.
    const auto current
        = gameplay::capture_start(*gameplay_owner, dealing_seed, resources);
    if (!current || *current != checkpoint.state)
        return false;
    gameplay_start_pending = false;
    return start_gameplay_runtime(dealing_seed);
}
