#include "table/gameplay_cadence.hpp"

#include <algorithm>
#include <limits>

namespace gameplay {
namespace {

    template <class Deck>
    std::optional<std::uint64_t>
    exposure_limit(std::span<const Deck> decks, dealing_mode mode) {
        if (mode != dealing_mode::sequential
            && mode != dealing_mode::simultaneous
            && mode != dealing_mode::random) {
            return std::nullopt;
        }
        std::uint64_t live_count = 0;
        auto minimum = std::numeric_limits<std::uint64_t>::max();
        for (const auto& deck : decks) {
            if (deck.status == deck_status::active) {
                ++live_count;
                minimum = std::min(minimum, deck.dealt_physical_cards);
            }
        }
        if (live_count == 0) {
            return std::nullopt;
        }
        if (mode != dealing_mode::random) {
            return minimum;
        }
        // Provisional R-003 rule, not a user-facing setting. Keep this absolute
        // exposure threshold consistent with the cumulative prepared targets.
        // Exact floor(sum / live_count), without overflowing a raw sum or
        // relying on floating precision. Partial quotients/carries never exceed
        // UINT64_MAX because there are at most live_count contributions of that
        // size.
        std::uint64_t mean = 0;
        std::uint64_t remainder = 0;
        for (const auto& deck : decks) {
            if (deck.status != deck_status::active) {
                continue;
            }
            mean += deck.dealt_physical_cards / live_count;
            const auto part = deck.dealt_physical_cards % live_count;
            if (part >= live_count - remainder) {
                ++mean;
                remainder = part - (live_count - remainder);
            } else {
                remainder += part;
            }
        }
        const auto doubled_minimum
            = minimum > std::numeric_limits<std::uint64_t>::max() / 2
            ? std::numeric_limits<std::uint64_t>::max()
            : 2 * minimum;
        return std::min(mean, doubled_minimum);
    }

} // namespace

bool multi_quiz_target_reached(
    std::span<const deck_state> decks, dealing_mode mode, std::uint64_t target
) {
    const auto limit = exposure_limit(decks, mode);
    return target > 0 && limit && target <= *limit;
}

std::optional<std::uint64_t> multi_quiz_exposure_limit(
    std::span<const cadence_exposure> decks, dealing_mode mode
) {
    return exposure_limit(decks, mode);
}

} // namespace gameplay
