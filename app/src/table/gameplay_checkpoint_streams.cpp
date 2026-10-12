#include "table/gameplay_checkpoint_checks.hpp"

#include "table/gameplay_session_checks.hpp"

#include <algorithm>
#include <limits>

namespace gameplay::checkpoint_checks {
namespace {

    bool consume(std::size_t& remaining, std::size_t size) {
        if (size > remaining) {
            return false;
        }
        remaining -= size;
        return true;
    }

    bool valid_position(const deck_state& deck, quiz_source source) {
        const auto& stream = deck.stream;
        if (deck.next_card > stream.cards.size()
            || deck.next_quiz_target > stream.virtual_quiz_targets.size()) {
            return false;
        }
        const auto offset = static_cast<std::uint64_t>(deck.next_card);
        const auto physical = deck.dealt_physical_cards;
        if (stream.infinite_cards) {
            const auto size = static_cast<std::uint64_t>(stream.cards.size());
            // The recipe ordinal is not an authenticated reveal counter:
            // preparation may supply a nonzero initial ordinal. Buffer offset
            // and cumulative reveals must still describe whole prior chunks.
            if (physical < offset || (physical - offset) % size != 0) {
                return false;
            }
        } else if (
            physical != offset
            || (deck.status == deck_status::active
                && deck.next_card == stream.cards.size())
        ) {
            return false;
        }
        // Finite history is present in this buffer. Infinite history is not:
        // preserve its accumulated signed count instead of regenerating it.
        auto running = stream.initial_running_count;
        std::uint64_t normal = 0;
        for (std::size_t i = 0; i < deck.next_card; ++i) {
            if (const auto rank = normal_card_rank(stream.cards[i])) {
                ++normal;
                if (!stream.infinite_cards) {
                    const auto weight = stream.rank_weights[*rank];
                    if ((weight > 0
                         && running > std::numeric_limits<std::int64_t>::max()
                                 - weight)
                        || (weight < 0
                            && running
                                < std::numeric_limits<std::int64_t>::min()
                                    - weight)) {
                        return false;
                    }
                    running += weight;
                }
            }
        }
        if (stream.infinite_cards) {
            if (deck.statistics.dealt_normal_cards < normal
                || deck.statistics.dealt_normal_cards > physical
                || (source == quiz_source::virtual_interrupt
                    && deck.statistics.dealt_normal_cards != physical)) {
                return false;
            }
        } else if (
            deck.statistics.dealt_normal_cards != normal
            || deck.running_count != running
        ) {
            return false;
        }
        // Consumed Single targets cannot exceed this deck's own exposure.
        // No equivalent bound is valid for table exposure after live-set
        // changes.
        return deck.next_quiz_target == 0
            || stream.virtual_quiz_targets[deck.next_quiz_target - 1]
            <= physical;
    }

} // namespace

bool within_budget(
    std::span<const deck_state> decks,
    std::span<const std::uint64_t> table_targets,
    const generation_policy& resources
) {
    if (!resources.valid()) {
        return false;
    }
    auto cards = resources.maximum_cards;
    auto targets = resources.maximum_targets;
    if (!consume(targets, table_targets.size())) {
        return false;
    }
    for (const auto& deck : decks) {
        if (!consume(cards, deck.stream.cards.size())
            || (deck.stream.infinite_cards
                && !consume(
                    cards, deck.stream.infinite_cards->lookahead.size()
                ))
            || !consume(targets, deck.stream.virtual_quiz_targets.size())) {
            return false;
        }
    }
    return true;
}

bool valid_streams(const session_checkpoint& saved) {
    std::size_t horizon = 0;
    bool infinite = false;
    for (const auto& deck : saved.decks) {
        if (!session_setup::valid(deck.configuration)
            || !session_setup::valid(
                deck.stream, saved.configuration, deck.configuration
            )
            || !valid_position(deck, saved.configuration.source)) {
            return false;
        }
        horizon = std::max(horizon, deck.stream.cards.size());
        infinite = infinite || deck.configuration.infinite;
    }
    const auto& schedule = saved.table_schedule;
    if (schedule.next_target > schedule.targets.size()) {
        return false;
    }
    if (saved.configuration.source != quiz_source::virtual_interrupt
        || saved.configuration.scope != quiz_scope::multi) {
        return schedule.targets.empty() && !schedule.continuation;
    }
    return infinite
        ? session_setup::valid_page(schedule.targets, schedule.continuation)
        : (!schedule.continuation
           && session_setup::valid_targets(schedule.targets, horizon));
}

} // namespace gameplay::checkpoint_checks
