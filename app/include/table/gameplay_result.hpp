#ifndef KCUCKOOUNTER_TABLE_GAMEPLAY_RESULT_HPP
#define KCUCKOOUNTER_TABLE_GAMEPLAY_RESULT_HPP

#include "table/gameplay_types.hpp"

namespace gameplay {

/// Check bounded archived accounting/context, not discarded shoe authenticity.
/// Today's display recommendation is not a historical Start-consent constraint.
[[nodiscard]] bool valid_result(
    const session_result& result, slot_limits limits,
    std::size_t maximum_targets = 65'536
);

} // namespace gameplay

#endif // KCUCKOOUNTER_TABLE_GAMEPLAY_RESULT_HPP
