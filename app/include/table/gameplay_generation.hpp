#ifndef KCUCKOOUNTER_TABLE_GAMEPLAY_GENERATION_HPP
#define KCUCKOOUNTER_TABLE_GAMEPLAY_GENERATION_HPP

#include "table/gameplay_session.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gameplay {

// Internal provisional R-001 policy, not a user-facing frequency setting.
// The session adapter applies budgets to the entire roster, not each deck.
struct generation_policy {
    std::uint32_t minimum_quiz_gap = 20;
    std::uint32_t maximum_quiz_gap = 40;
    std::size_t maximum_cards = 1'048'576;
    std::size_t maximum_targets = 65'536;
    // Provisional R-005 and bounded schedule paging, not user-facing controls.
    std::size_t infinite_chunk_decks = 100;
    std::size_t quiz_page_targets = 64;

    [[nodiscard]] bool valid() const;
};

// Normal face rank in the existing A/2..10/J/Q/K order. Jokers (52/53)
// and invalid faces have no rank: they are not zero-weight normal cards.
[[nodiscard]] std::optional<std::size_t> normal_card_rank(std::uint8_t face);

// Complete balanced shoe: each face occurs deck_count times. Owner is a
// stable deck identity, never its physical slot. This does not model infinity.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> generate_shoe(
    quiz_source source, std::size_t deck_count, std::uint64_t seed,
    deck_id owner, const generation_policy& policy = {}
);

// Strictly increasing, positive targets below the exclusive finite horizon.
// Owner present = Single's independent deck schedule; absent = one Multi
// table-exposure schedule. Cards and schedules use separate random streams.
[[nodiscard]] std::optional<std::vector<std::uint64_t>> generate_quiz_targets(
    std::uint64_t exclusive_horizon, std::uint64_t seed,
    std::optional<deck_id> owner, const generation_policy& policy = {}
);

struct generated_infinite_shoe {
    std::vector<std::uint8_t> cards;
    infinite_card_buffer continuation;
};

struct generated_quiz_page {
    std::vector<std::uint64_t> targets;
    quiz_schedule_continuation continuation;
};

[[nodiscard]] std::optional<generated_infinite_shoe> generate_infinite_shoe(
    quiz_source source, std::uint64_t seed, deck_id owner,
    const generation_policy& policy = {}
);

// Engine seam: only at exact exhaustion of an active infinite deck. Generate
// the following lookahead before mutation; retain every cumulative payload.
[[nodiscard]] bool rollover_infinite_shoe(
    deck_state& deck, quiz_source source, const generation_policy& policy = {}
);

// Frozen continuation controls gaps/quota; policy only imposes resource bounds.
// Owner present = Single, absent = Multi. Returns a replacement, not history.
[[nodiscard]] std::optional<generated_quiz_page> generate_quiz_page(
    const quiz_schedule_continuation& continuation,
    std::optional<deck_id> owner, const generation_policy& policy = {}
);

} // namespace gameplay

#endif // KCUCKOOUNTER_TABLE_GAMEPLAY_GENERATION_HPP
