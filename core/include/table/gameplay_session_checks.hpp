#ifndef KCUCKOOUNTER_TABLE_GAMEPLAY_SESSION_CHECKS_HPP
#define KCUCKOOUNTER_TABLE_GAMEPLAY_SESSION_CHECKS_HPP

#include "table/gameplay_types.hpp"

#include <optional>
#include <span>

// Internal shape checks shared by preparation and checkpoint restore.
// Catalogue resolution and aggregate resource policy belong to their callers.
namespace gameplay::session_setup {

[[nodiscard]] bool valid_slot_limits(slot_limits limits, std::size_t count);
[[nodiscard]] bool valid(const deck_configuration& settings);
[[nodiscard]] bool valid_targets(
    std::span<const std::uint64_t> targets,
    std::optional<std::uint64_t> exclusive_horizon
);
[[nodiscard]] bool valid_page(
    std::span<const std::uint64_t> targets,
    const std::optional<quiz_schedule_continuation>& continuation
);
[[nodiscard]] bool valid(
    const prepared_deck& prepared, const session_configuration& settings,
    const deck_configuration& configuration
);

} // namespace gameplay::session_setup

#endif // KCUCKOOUNTER_TABLE_GAMEPLAY_SESSION_CHECKS_HPP
