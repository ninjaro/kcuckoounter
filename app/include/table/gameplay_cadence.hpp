#ifndef KCUCKOOUNTER_TABLE_GAMEPLAY_CADENCE_HPP
#define KCUCKOOUNTER_TABLE_GAMEPLAY_CADENCE_HPP

#include "table/gameplay_session.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace gameplay {

// Read-only virtual-Multi readiness, not target consumption or batch creation.
// Progress is cumulative physical reveals (normal cards in virtual mode).
// Only active decks participate, including Training. Empty rosters, zero
// targets and invalid modes are never ready. Input is trusted domain state,
// not a checkpoint decoder. R-003 owns calibration of Random's lower guard.
[[nodiscard]] bool multi_quiz_target_reached(
    std::span<const deck_state> decks, dealing_mode mode, std::uint64_t target
);

// Project one atomic dealing/terminal transition without copying card buffers.
struct cadence_exposure {
    deck_status status;
    std::uint64_t dealt_physical_cards;
};

// Largest ready absolute target, or nullopt for an empty/invalid live roster.
[[nodiscard]] std::optional<std::uint64_t> multi_quiz_exposure_limit(
    std::span<const cadence_exposure> decks, dealing_mode mode
);

} // namespace gameplay

#endif // KCUCKOOUNTER_TABLE_GAMEPLAY_CADENCE_HPP
