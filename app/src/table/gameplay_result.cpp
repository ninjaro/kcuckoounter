#include "table/gameplay_result.hpp"

#include "table/gameplay_session.hpp"
#include "table/gameplay_session_checks.hpp"

#include <limits>

namespace gameplay {
namespace {

    bool add(std::uint64_t& total, std::uint64_t value) {
        if (value > std::numeric_limits<std::uint64_t>::max() - total)
            return false;
        total += value;
        return true;
    }

    bool valid_schedule(
        const quiz_schedule_snapshot& schedule, bool owned, bool infinite,
        std::size_t& remaining
    ) {
        if (schedule.targets.size() > remaining
            || schedule.next_target > schedule.targets.size())
            return false;
        remaining -= schedule.targets.size();
        if (!owned)
            return schedule.targets.empty() && schedule.next_target == 0
                && !schedule.continuation;
        return infinite
            ? session_setup::valid_page(schedule.targets, schedule.continuation)
            : (!schedule.continuation
               && session_setup::valid_targets(schedule.targets, std::nullopt));
    }

} // namespace

bool valid_result(
    const session_result& result, slot_limits limits,
    std::size_t maximum_targets
) {
    const auto count = result.decks.size();
    const auto& config = result.configuration;
    if (!session_setup::valid_slot_limits(limits, count)
        || !valid_configuration(config, count, { count, limits.technical_cap })
        || maximum_targets == 0 || result.cadence_revision != 1
        || result.timing.quiz_time_ms > result.timing.play_time_ms)
        return false;
    std::vector<bool> occupied(count, false);
    session_statistics challenge {};
    std::size_t active = 0, completed = 0, failed = 0;
    bool infinite = false;
    const bool virtual_quiz = config.source == quiz_source::virtual_interrupt;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& deck = result.decks[i];
        const auto& stats = deck.statistics;
        if (deck.id.value != i || deck.slot.value >= count
            || occupied[deck.slot.value]
            || !session_setup::valid(deck.configuration)
            || deck.configuration.infinite != deck.infinite_position.has_value()
            || (deck.configuration.infinite
                && deck.status == deck_status::completed)
            || (deck.infinite_position
                && deck.infinite_position->decks_per_chunk == 0)
            || stats.dealt_normal_cards > deck.dealt_physical_cards
            || (virtual_quiz
                && stats.dealt_normal_cards != deck.dealt_physical_cards)
            || stats.verified_cards > stats.dealt_normal_cards
            || deck.unverified_normal_cards
                > stats.dealt_normal_cards - stats.verified_cards
            || (deck.configuration.training && stats.verified_cards != 0)
            || stats.answer_time_ms > result.timing.quiz_time_ms
            || stats.dealing_time_ms > result.timing.play_time_ms
            || stats.answer_time_ms
                > result.timing.play_time_ms - stats.dealing_time_ms
            || !valid_schedule(
                deck.schedule,
                virtual_quiz && config.scope == quiz_scope::single,
                deck.configuration.infinite, maximum_targets
            ))
            return false;
        occupied[deck.slot.value] = true;
        infinite = infinite || deck.configuration.infinite;
        switch (deck.status) {
        case deck_status::active:
            ++active;
            break;
        case deck_status::completed:
            ++completed;
            break;
        case deck_status::failed:
            if (deck.configuration.training
                || config.failure != failure_policy::block)
                return false;
            ++failed;
            break;
        default:
            return false;
        }
        if (!deck.configuration.training
            && (!add(challenge.dealt_normal_cards, stats.dealt_normal_cards)
                || !add(challenge.verified_cards, stats.verified_cards)
                || !add(challenge.errors, stats.errors)
                || !add(challenge.skips, stats.skips)))
            return false;
    }
    const auto initial = config.initial_lives;
    const auto expected_lives = config.failure == failure_policy::block
        ? initial
        : (challenge.errors >= static_cast<std::uint64_t>(initial)
               ? 0
               : initial - static_cast<int>(challenge.errors));
    if (challenge != result.challenge
        || result.remaining_lives != expected_lives
        || active != result.active_decks || completed != result.completed_decks
        || failed != result.failed_decks
        || !valid_schedule(
            result.table_schedule,
            virtual_quiz && config.scope == quiz_scope::multi, infinite,
            maximum_targets
        ))
        return false;
    switch (result.reason) {
    case session_end_reason::manual_finish:
        return true;
    case session_end_reason::lives_exhausted:
        return config.failure == failure_policy::lives && expected_lives == 0;
    case session_end_reason::no_live_decks:
        return active == 0
            && (config.failure != failure_policy::lives || expected_lives != 0);
    default:
        return false;
    }
}

} // namespace gameplay
