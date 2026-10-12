#include "table/gameplay_checkpoint_checks.hpp"

#include <limits>

namespace gameplay::checkpoint_checks {
namespace {

    bool add(std::uint64_t& total, std::uint64_t value) {
        if (value > std::numeric_limits<std::uint64_t>::max() - total) {
            return false;
        }
        total += value;
        return true;
    }

    bool valid_deck(const deck_state& deck, const session_checkpoint& saved) {
        const auto& stats = deck.statistics;
        if ((deck.status != deck_status::active
             && deck.status != deck_status::completed
             && deck.status != deck_status::failed)
            || (deck.status == deck_status::failed
                && (deck.configuration.training
                    || saved.configuration.failure != failure_policy::block))
            || (deck.show_count && !deck.configuration.training)
            || (deck.configuration.training && stats.verified_cards != 0)
            || stats.verified_cards > stats.dealt_normal_cards
            || deck.unverified_normal_cards
                > stats.dealt_normal_cards - stats.verified_cards
            || stats.answer_time_ms > saved.timing.quiz_time_ms
            || stats.dealing_time_ms > saved.timing.play_time_ms
            || stats.answer_time_ms
                > saved.timing.play_time_ms - stats.dealing_time_ms) {
            return false;
        }
        return !deck.quiz
            || (deck.status == deck_status::active && saved.batch
                && deck.quiz->batch_id == saved.batch->id
                && deck.quiz->expected_count == deck.running_count
                && deck.quiz->input == deck.latest_input);
    }

} // namespace

bool valid_state(const session_checkpoint& saved) {
    const auto count = saved.decks.size();
    if (count == 0 || saved.deck_to_slot.size() != count
        || (saved.sequential_anchor && saved.sequential_anchor->value >= count)
        || saved.pickup_interval_ms < session::minimum_pick_interval_ms
        || saved.pickup_interval_ms > session::maximum_pick_interval_ms
        || saved.timing.quiz_time_ms > saved.timing.play_time_ms) {
        return false;
    }
    std::vector<bool> occupied(count, false);
    session_statistics challenge {};
    bool pending = false;
    bool live = false;
    for (std::size_t i = 0; i < count; ++i) {
        const auto slot = saved.deck_to_slot[i].value;
        const auto& deck = saved.decks[i];
        if (slot >= count || occupied[slot] || deck.id.value != i
            || !valid_deck(deck, saved)) {
            return false;
        }
        occupied[slot] = true;
        live = live || deck.status == deck_status::active;
        pending = pending || deck.quiz.has_value();
        if (!deck.configuration.training) {
            const auto& stats = deck.statistics;
            if (!add(challenge.dealt_normal_cards, stats.dealt_normal_cards)
                || !add(challenge.verified_cards, stats.verified_cards)
                || !add(challenge.errors, stats.errors)
                || !add(challenge.skips, stats.skips)) {
                return false;
            }
        }
    }
    if (!live || challenge != saved.challenge
        || pending != saved.batch.has_value()) {
        return false;
    }
    const auto errors = saved.challenge.errors;
    const auto initial = saved.configuration.initial_lives;
    const auto expected_lives
        = saved.configuration.failure == failure_policy::block
        ? initial
        : (errors >= static_cast<std::uint64_t>(initial)
               ? 0
               : initial - static_cast<int>(errors));
    if (saved.remaining_lives != expected_lives
        || (expected_lives == 0 && !pending)) {
        return false;
    }
    if (!saved.batch) {
        return true;
    }
    const auto& batch = *saved.batch;
    if (batch.id == 0 || batch.id != saved.last_batch_id
        || batch.remaining_ms.has_value()
            != saved.configuration.global_quiz_pause) {
        return false;
    }
    if (batch.remaining_ms) {
        // A frozen batch opens at most one question per deck. Non-global
        // batches can accumulate prompts but have no countdown at all.
        const auto extensions = static_cast<std::uint64_t>(count);
        if (extensions > (std::numeric_limits<std::uint64_t>::max()
                          - session::quiz_initial_ms)
                    / session::quiz_correct_ms
            || *batch.remaining_ms == 0
            || *batch.remaining_ms > session::quiz_initial_ms
                    + extensions * session::quiz_correct_ms) {
            return false;
        }
    }
    return true;
}

} // namespace gameplay::checkpoint_checks
