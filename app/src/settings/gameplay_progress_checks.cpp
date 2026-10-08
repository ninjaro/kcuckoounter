#include "settings/gameplay_progress_codec.hpp"

#include "settings/gameplay_codec.hpp"
#include "table/gameplay_result.hpp"

#include <algorithm>
#include <limits>

namespace progress_storage {
namespace {

    bool add(std::uint64_t& total, std::uint64_t value) {
        if (value > std::numeric_limits<std::uint64_t>::max() - total)
            return false;
        total += value;
        return true;
    }

} // namespace

bool accumulate(
    gameplay::session_statistics& counters, gameplay::session_timing& timing,
    const gameplay::session_statistics& next,
    const gameplay::session_timing& elapsed
) {
    return add(counters.dealt_normal_cards, next.dealt_normal_cards)
        && add(counters.verified_cards, next.verified_cards)
        && add(counters.errors, next.errors) && add(counters.skips, next.skips)
        && add(timing.play_time_ms, elapsed.play_time_ms)
        && add(timing.quiz_time_ms, elapsed.quiz_time_ms);
}

bool valid_record(
    const gameplay_result_record& record, gameplay::slot_limits limits
) {
    return record.sequence > 0 && record.completed_at_utc_ms > 0
        && gameplay::valid_result(
               record.result, limits,
               gameplay_progress_service::maximum_result_targets
        )
        && std::ranges::all_of(record.result.decks, [](const auto& owner) {
               return gameplay_storage::valid_slug(
                   owner.configuration.strategy_slug
               );
           });
}

bool valid(const gameplay_progress& progress, gameplay::slot_limits limits) {
    const auto retained = std::min<std::uint64_t>(
        progress.completed_sessions, gameplay_progress::maximum_recent_results
    );
    if (!gameplay_storage::valid_limits(limits)
        || progress.recent_results.size() != retained
        || progress.challenge.verified_cards
            > progress.challenge.dealt_normal_cards
        || progress.timing.quiz_time_ms > progress.timing.play_time_ms)
        return false;
    gameplay::session_statistics counters {};
    gameplay::session_timing timing {};
    auto sequence = progress.completed_sessions;
    for (const auto& entry : progress.recent_results) {
        if (entry.sequence != sequence || !valid_record(entry, limits)
            || !accumulate(
                counters, timing, entry.result.challenge, entry.result.timing
            ))
            return false;
        --sequence;
    }
    if (progress.completed_sessions
        <= gameplay_progress::maximum_recent_results)
        return counters == progress.challenge && timing == progress.timing;
    // Evicted context is intentionally unavailable; do not invent a replay
    // proof. Retained sums must still fit inside raw lifetime counters.
    return counters.dealt_normal_cards <= progress.challenge.dealt_normal_cards
        && counters.verified_cards <= progress.challenge.verified_cards
        && counters.errors <= progress.challenge.errors
        && counters.skips <= progress.challenge.skips
        && timing.play_time_ms <= progress.timing.play_time_ms
        && timing.quiz_time_ms <= progress.timing.quiz_time_ms;
}

} // namespace progress_storage
