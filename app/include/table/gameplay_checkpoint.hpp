#ifndef KCUCKOOUNTER_TABLE_GAMEPLAY_CHECKPOINT_HPP
#define KCUCKOOUNTER_TABLE_GAMEPLAY_CHECKPOINT_HPP

#include "table/gameplay_generation.hpp"

namespace gameplay {

/// Exact active-domain continuation, not a drill, result or storage container.
/// No geometry, animation history, wall clock or host pickup debt is included.
struct session_checkpoint {
    // Increment when generator, selection, cadence or scoring continuation
    // semantics become incompatible. Independently version the durable codec.
    static constexpr std::uint32_t current_revision = 1;
    std::uint32_t continuation_revision = current_revision;
    session_configuration configuration;
    std::vector<deck_state> decks;
    std::vector<physical_slot_id> deck_to_slot;
    std::optional<physical_slot_id> sequential_anchor;
    random_dealing_continuation random_dealing;
    quiz_schedule_snapshot table_schedule;
    std::optional<quiz_batch> batch;
    std::uint64_t last_batch_id = 0;
    session_statistics challenge;
    session_timing timing;
    int remaining_lives = 0;
    int pickup_interval_ms = 300;

    friend bool operator==(const session_checkpoint&, const session_checkpoint&)
        = default;
};

/// Allocation-free stream preflight except bounded mapping checks. Shared by
/// restore and storage; active roster capacity does not reapply Start consent.
[[nodiscard]] bool valid_checkpoint(
    const session_checkpoint& saved, slot_limits bounds,
    const generation_policy& resources = {}
);

/// Only Running/Paused sessions qualify. Aggregate stream budgets apply before
/// copying; this never changes or pauses the source session.
[[nodiscard]] std::optional<session_checkpoint> capture_checkpoint(
    const session& source, const generation_policy& resources = {}
);

/// Checked initial continuation for a prepared Setup, with the intended Random
/// seed. Never starts or mutates the source; the host admits it before
/// activation.
[[nodiscard]] std::optional<session_checkpoint> capture_start(
    const session& source, std::uint64_t dealing_seed,
    const generation_policy& resources = {}
);

/// Reject inconsistent/unsupported state; rebuild derived indexes and restore
/// Paused without dealing, regeneration or replay. Traversal is a fresh
/// complete physical-slot permutation. Bounds enforce capacity, not new Start
/// consent.
[[nodiscard]] std::optional<session> restore_checkpoint(
    const session_checkpoint& saved, slot_limits bounds,
    std::span<const std::size_t> traversal,
    const generation_policy& resources = {}
);

} // namespace gameplay

#endif // KCUCKOOUNTER_TABLE_GAMEPLAY_CHECKPOINT_HPP
