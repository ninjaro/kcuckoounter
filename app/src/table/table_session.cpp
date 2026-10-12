#include "table/table.hpp"

#include "table/table_slot.hpp"

#include <QPointer>
#include <QScopeGuard>

#include <algorithm>
#include <utility>

bool table::install_gameplay_session(std::unique_ptr<gameplay::session> owner) {
    if (gameplay_start_pending || !owner || owner->size() == 0
        || owner->size() > gameplay::desktop_slot_technical_cap)
        return false;
    const auto packed = pack_gameplay_slots(owner->size());
    return packed && install_gameplay_scene(std::move(owner), *packed);
}

bool table::install_gameplay_scene(
    std::unique_ptr<gameplay::session> owner,
    const packing::equal_packing_result& packed,
    std::optional<std::uint64_t> pickup_elapsed, const scene_binding& bind
) {
    if (gameplay_start_pending
        || (!gameplay_owner
            && (quiz_running
                || std::ranges::any_of(
                    slot_widgets,
                    [](const auto* slot) { return slot->has_cards(); }
                )))
        || (pickup_elapsed
            && (owner->phase() != gameplay::session_phase::paused
                || *pickup_elapsed
                    >= static_cast<std::uint64_t>(owner->pick_interval_ms()))))
        return false;
    auto paths = std::make_unique<gameplay::layout_transition>(*owner);
    if (!paths->repack(packed))
        return false;
    std::vector<rotation_motion> rotations;
    rotations.reserve(owner->size());
    for (std::size_t index = 0; index < owner->size(); ++index) {
        const double angle
            = paths->deck_motion({ index })->to.rotated ? 0.0 : 90.0;
        rotations.push_back({ angle, angle });
    }
    std::vector<std::unique_ptr<table_slot>> staged;
    staged.reserve(owner->size());
    std::vector<table_slot*> next_slots;
    next_slots.reserve(owner->size());
    for (std::size_t index = 0; index < owner->size(); ++index) {
        auto slot = create_slot_widget();
        slot->hide();
        if (!slot->bind_gameplay_deck(*this, *owner, { index }))
            return false;
        // Until commit, unique_ptr alone owns the hidden views. Parent teardown
        // during the about-to-change signal must not delete them a second time.
        slot->setParent(nullptr);
        next_slots.push_back(slot.get());
        staged.push_back(std::move(slot));
    }

    const QPointer<table> guard(this);
    gameplay_start_pending = true; // admission protects Start and scene commit
    const auto pending = qScopeGuard([guard] {
        if (guard)
            guard->gameplay_start_pending = false;
    });
    stop_gameplay_clock();
    gameplay_layout_animation.stop();
    gameplay_runtime_attached = false;
    gameplay_pick_elapsed_ms = 0;
    gameplay_runtime_failure.clear();
    gameplay_presented_batch.reset();
    emit gameplay_session_about_to_change();
    if (!guard)
        return false;
    destroy_slot_widgets();
    gameplay_completion = {};
    gameplay_layout.reset();
    gameplay_owner = std::move(owner);
    ++gameplay_scene_revision;
    gameplay_layout = std::move(paths);
    gameplay_layout_progress = 1.0;
    gameplay_rotation_paths = std::move(rotations);
    slot_widgets
        = std::move(next_slots); // already allocated before replacement
    for (auto& slot : staged) {
        slot->setParent(this);
        (void)slot.release(); // QObject/table now owns each view
    }
    quiz_running = false;
    quiz_paused = false;
    pick_elapsed_ms = 0;
    next_slot_index = 0;
    // A recovered scene is resumable before observers inspect its new owner.
    // Ordinary installation retains its unattached/no-timer contract.
    if (pickup_elapsed) {
        gameplay_runtime_attached = true;
        gameplay_pick_elapsed_ms = *pickup_elapsed;
    }
    const auto revision = gameplay_scene_revision;
    if (bind)
        bind(revision);
    if (!guard || gameplay_scene_revision != revision)
        return false;
    if (pickup_elapsed) {
        refresh_gameplay_session();
    }
    project_gameplay_layout();
    if (displayed_shared_faces_key)
        apply_shared_faces_entry(*displayed_shared_faces_key);
    schedule_card_preload();
    update_shared_card_face_need();
    if (!guard || gameplay_scene_revision != revision)
        return false;
    gameplay_start_pending = false;
    emit gameplay_session_changed();
    if (!guard || gameplay_scene_revision != revision)
        return false;
    emit gameplay_layout_changed();
    return guard && gameplay_scene_revision == revision;
}

std::optional<gameplay_checkpoint> table::capture_gameplay_checkpoint(
    const gameplay::generation_policy& resources
) {
    if (!gameplay_runtime_attached || !gameplay_owner || !resources.valid()
        || (gameplay_owner->phase() != gameplay::session_phase::running
            && gameplay_owner->phase() != gameplay::session_phase::paused))
        return std::nullopt;
    const QPointer<table> guard(this);
    const auto revision = gameplay_scene_revision;
    std::vector<gameplay::quiz_answer> answers;
    const bool accepted = synchronize_gameplay_clock(answers);
    publish_gameplay_runtime(answers);
    if (!guard || gameplay_scene_revision != revision || !accepted
        || !gameplay_runtime_attached || !gameplay_owner)
        return std::nullopt;
    auto state = gameplay::capture_checkpoint(*gameplay_owner, resources);
    return state ? std::make_optional(
                       gameplay_checkpoint { std::move(*state),
                                             gameplay_pick_elapsed_ms }
                   )
                 : std::nullopt;
}

bool table::restore_gameplay_checkpoint(
    const gameplay_checkpoint& checkpoint,
    const gameplay::generation_policy& resources
) {
    return restore_gameplay_checkpoint(checkpoint, resources, {});
}

bool table::restore_gameplay_checkpoint(
    const gameplay_checkpoint& checkpoint,
    const gameplay::generation_policy& resources, const scene_binding& bind
) {
    if (gameplay_start_pending)
        return false;
    const auto count = checkpoint.state.decks.size();
    if (count == 0 || count > gameplay::desktop_slot_technical_cap
        || checkpoint.state.pickup_interval_ms <= 0
        || checkpoint.pickup_elapsed_ms
            >= static_cast<std::uint64_t>(checkpoint.state.pickup_interval_ms))
        return false;
    const auto packed = pack_gameplay_slots(count);
    if (!packed)
        return false;
    auto owner = gameplay::restore_checkpoint(
        checkpoint.state, { 1, gameplay::desktop_slot_technical_cap },
        packed->traversal, resources
    );
    return owner
        && install_gameplay_scene(
               std::make_unique<gameplay::session>(std::move(*owner)), *packed,
               checkpoint.pickup_elapsed_ms, bind
        );
}
