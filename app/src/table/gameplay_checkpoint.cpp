#include "table/gameplay_checkpoint.hpp"

#include "table/gameplay_checkpoint_checks.hpp"
#include "table/gameplay_session_checks.hpp"

#include <utility>

namespace gameplay {

bool valid_checkpoint(
    const session_checkpoint& saved, slot_limits bounds,
    const generation_policy& resources
) {
    const auto count = saved.decks.size();
    return saved.continuation_revision == session_checkpoint::current_revision
        && session_setup::valid_slot_limits(bounds, count)
        && valid_configuration(
               saved.configuration, count, { count, bounds.technical_cap }
        )
        && checkpoint_checks::within_budget(
               saved.decks, saved.table_schedule.targets, resources
        )
        && checkpoint_checks::valid_streams(saved)
        && checkpoint_checks::valid_state(saved);
}

std::optional<session_checkpoint>
capture_checkpoint(const session& source, const generation_policy& resources) {
    if (source.current_phase != session_phase::running
        && source.current_phase != session_phase::paused) {
        return std::nullopt;
    }
    return source.snapshot_checkpoint(resources);
}

std::optional<session_checkpoint> capture_start(
    const session& source, std::uint64_t dealing_seed,
    const generation_policy& resources
) {
    if (!source.can_start())
        return std::nullopt;
    auto saved = source.snapshot_checkpoint(resources);
    if (saved)
        saved->random_dealing = { .seed = dealing_seed, .next_step = 0 };
    return saved;
}

std::optional<session_checkpoint>
session::snapshot_checkpoint(const generation_policy& resources) const {
    if (!checkpoint_checks::within_budget(decks, multi_quiz_targets, resources))
        return std::nullopt;
    session_checkpoint saved {
        .configuration = settings,
        .decks = decks,
        .deck_to_slot = deck_to_slot,
        .sequential_anchor = last_selected_slot,
        .random_dealing = random_dealing_state,
        .table_schedule
        = { multi_quiz_targets, multi_quiz_cursor, multi_quiz_continuation },
        .batch = batch,
        .last_batch_id = last_quiz_batch_id,
        .challenge = challenge,
        .timing = active_timing,
        .remaining_lives = lives,
        .pickup_interval_ms = interval_ms,
    };
    if (!valid_checkpoint(saved, limits, resources)) {
        return std::nullopt;
    }
    return saved;
}

std::optional<session> restore_checkpoint(
    const session_checkpoint& saved, slot_limits bounds,
    std::span<const std::size_t> traversal, const generation_policy& resources
) {
    const auto count = saved.decks.size();
    // A recovered active roster is immutable. A changed recommendation must not
    // truncate it or manufacture new extra-slot consent; Start is not replayed.
    const slot_limits active_bounds { count, bounds.technical_cap };
    if (!valid_checkpoint(saved, bounds, resources)
        || traversal.size() != count) {
        return std::nullopt;
    }
    std::vector<deck_configuration> configured;
    configured.reserve(count);
    for (const auto& deck : saved.decks) {
        configured.push_back(deck.configuration);
    }
    auto restored
        = session::create(saved.configuration, configured, active_bounds);
    if (!restored || !restored->update_slot_limits(bounds)
        || !restored->set_traversal(traversal)) {
        return std::nullopt;
    }
    restored->decks = saved.decks;
    restored->deck_to_slot = saved.deck_to_slot;
    for (std::size_t id = 0; id < count; ++id) {
        restored->slot_to_deck[saved.deck_to_slot[id].value] = deck_id { id };
    }
    restored->last_selected_slot = saved.sequential_anchor;
    restored->random_dealing_state = saved.random_dealing;
    restored->multi_quiz_targets = saved.table_schedule.targets;
    restored->multi_quiz_cursor = saved.table_schedule.next_target;
    restored->multi_quiz_continuation = saved.table_schedule.continuation;
    restored->multi_schedule_prepared
        = saved.configuration.source == quiz_source::virtual_interrupt
        && saved.configuration.scope == quiz_scope::multi;
    restored->batch = saved.batch;
    restored->last_quiz_batch_id = saved.last_batch_id;
    restored->challenge = saved.challenge;
    restored->active_timing = saved.timing;
    restored->lives = saved.remaining_lives;
    restored->interval_ms = saved.pickup_interval_ms;
    restored->current_phase = session_phase::paused;
    return restored;
}

} // namespace gameplay
