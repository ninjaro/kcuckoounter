#ifndef KCUCKOOUNTER_TABLE_GAMEPLAY_CHECKPOINT_CHECKS_HPP
#define KCUCKOOUNTER_TABLE_GAMEPLAY_CHECKPOINT_CHECKS_HPP

#include "table/gameplay_checkpoint.hpp"

namespace gameplay::checkpoint_checks {

[[nodiscard]] bool within_budget(
    std::span<const deck_state> decks,
    std::span<const std::uint64_t> table_targets,
    const generation_policy& resources
);
[[nodiscard]] bool valid_streams(const session_checkpoint& saved);
[[nodiscard]] bool valid_state(const session_checkpoint& saved);

} // namespace gameplay::checkpoint_checks

#endif // KCUCKOOUNTER_TABLE_GAMEPLAY_CHECKPOINT_CHECKS_HPP
