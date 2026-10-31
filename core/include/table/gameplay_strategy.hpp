#ifndef KCUCKOOUNTER_TABLE_GAMEPLAY_STRATEGY_HPP
#define KCUCKOOUNTER_TABLE_GAMEPLAY_STRATEGY_HPP

#include "table/gameplay_generation.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

struct strategy_catalog;

namespace gameplay {

// Setup-only metadata, resolved from the existing catalogue. Recommendations
// are hints; they never change/restrict the caller's chosen deck_count.
struct strategy_parameters {
    std::array<int, 13> rank_weights {};
    std::int64_t initial_running_count = 0;
    std::size_t recommended_decks = 1;
};

[[nodiscard]] std::optional<strategy_parameters> resolve_strategy(
    const strategy_catalog& catalog, const deck_configuration& configuration
);

// Explicit fresh-preset opt-in. Does not mutate a session or a saved drill.
[[nodiscard]] std::optional<deck_configuration> recommended_configuration(
    const strategy_catalog& catalog, const deck_configuration& configuration
);

// Explicit NEW-preset entry point: apply catalogue counts to each template,
// then validate through session::create. Never use for existing/imported/stored
// choices or recovery; those use session::create with their exact counts.
// No cards/schedules or persistence are produced here.
[[nodiscard]] std::optional<session> create_fresh_session(
    const strategy_catalog& catalog, const session_configuration& configuration,
    std::span<const deck_configuration> templates, slot_limits bounds
);

// Setup-only roster replacement, never live mutation. Preserve surviving exact
// choices, Show Count and speed; apply recommendations only to newly added
// templates. New roster invalidates preparation/mapping; shrink clamps N only
// when it exceeds the explicitly chosen new M. Failure leaves input untouched.
[[nodiscard]] std::optional<session> resize_setup_roster(
    const session& current, std::size_t count, const strategy_catalog& catalog,
    const deck_configuration& new_deck_template, slot_limits bounds
);

// Supplied finite physical cards; this adapter resolves the
// session's configured slug and applies its automatic count/weights. Invalid
// input or a locked session leaves all preparation unchanged.
[[nodiscard]] bool prepare_strategy_deck(
    session& game, deck_id id, const strategy_catalog& catalog,
    std::span<const std::uint8_t> cards
);

// G3b finite preparation: resolve all strategies and generate shoes/schedules
// under one aggregate budget. Failure leaves the session unchanged. Infinite
// configurations are deliberately rejected; use the general entry below.
[[nodiscard]] bool prepare_finite_session(
    session& game, const strategy_catalog& catalog, std::uint64_t seed,
    const generation_policy& policy = {}
);

// Finite/mixed/infinite setup under aggregate current+lookahead/page budgets.
// Applies starting counts once from chosen deck_count, never chunk capacity.
[[nodiscard]] bool prepare_generated_session(
    session& game, const strategy_catalog& catalog, std::uint64_t seed,
    const generation_policy& policy = {}
);

} // namespace gameplay

#endif // KCUCKOOUNTER_TABLE_GAMEPLAY_STRATEGY_HPP
