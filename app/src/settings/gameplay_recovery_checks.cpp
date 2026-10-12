#include "settings/gameplay_recovery_codec.hpp"

#include <algorithm>
#include <limits>
#include <span>

namespace recovery_storage {
namespace {

    bool advances(
        const gameplay::deck_statistics& before,
        const gameplay::deck_statistics& after
    ) {
        return after.dealt_normal_cards >= before.dealt_normal_cards
            && after.verified_cards >= before.verified_cards
            && after.errors >= before.errors && after.skips >= before.skips
            && after.dealing_time_ms >= before.dealing_time_ms
            && after.answer_time_ms >= before.answer_time_ms;
    }

    bool advances(
        const gameplay::session_timing& before,
        const gameplay::session_timing& after
    ) {
        return after.play_time_ms >= before.play_time_ms
            && after.quiz_time_ms >= before.quiz_time_ms;
    }

    bool same_schedule(
        std::span<const std::uint64_t> before_targets,
        std::size_t before_cursor,
        const std::optional<gameplay::quiz_schedule_continuation>& before,
        std::span<const std::uint64_t> after_targets, std::size_t after_cursor,
        const std::optional<gameplay::quiz_schedule_continuation>& after
    ) {
        if (before.has_value() != after.has_value())
            return false;
        if (!before)
            return std::ranges::equal(before_targets, after_targets)
                && after_cursor >= before_cursor;
        const auto& a = *before;
        const auto& b = *after;
        return a.seed == b.seed && a.minimum_gap == b.minimum_gap
            && a.maximum_gap == b.maximum_gap && a.page_size == b.page_size
            && b.next_page >= a.next_page && b.after_target >= a.after_target
            && (b.next_page != a.next_page
                || (std::ranges::equal(before_targets, after_targets)
                    && before == after && after_cursor >= before_cursor));
    }

    bool same_schedule(
        const gameplay::quiz_schedule_snapshot& before,
        const gameplay::quiz_schedule_snapshot& after
    ) {
        return same_schedule(
            before.targets, before.next_target, before.continuation,
            after.targets, after.next_target, after.continuation
        );
    }

    bool same_deck(
        const gameplay::deck_state& before,
        const gameplay::deck_configuration& config, gameplay::deck_id id,
        const std::array<int, 13>& weights, std::int64_t initial,
        gameplay::deck_status status, std::uint64_t physical,
        const gameplay::deck_statistics& stats
    ) {
        return before.configuration == config && before.id == id
            && before.stream.rank_weights == weights
            && before.stream.initial_running_count == initial
            && (before.status == gameplay::deck_status::active
                || before.status == status)
            && physical >= before.dealt_physical_cards
            && advances(before.statistics, stats);
    }

    bool same_stream(
        const std::optional<gameplay::infinite_card_buffer>& before,
        const std::optional<gameplay::infinite_stream_position>& after
    ) {
        return before.has_value() == after.has_value()
            && (!before
                || (before->seed == after->seed
                    && before->decks_per_chunk == after->decks_per_chunk
                    && after->chunk >= before->chunk));
    }

} // namespace

bool fail(QString* error, const QString& message) {
    if (error)
        *error = message;
    return false;
}

bool contains(
    const gameplay_progress& progress, const gameplay_result_record& record
) {
    return std::ranges::find(progress.recent_results, record)
        != progress.recent_results.end();
}

bool admissible(
    const gameplay_recovery& recovery, const gameplay_progress& progress
) {
    const auto sequence = std::visit(
        [](const auto& value) { return value.sequence; }, recovery
    );
    if (sequence > progress.completed_sessions)
        return progress.completed_sessions
            != std::numeric_limits<std::uint64_t>::max()
            && sequence == progress.completed_sessions + 1;
    const auto* pending = std::get_if<gameplay_result_record>(&recovery);
    return pending && contains(progress, *pending);
}

bool continues(
    const gameplay_checkpoint& previous, const gameplay_checkpoint& next
) {
    const auto& a = previous.state;
    const auto& b = next.state;
    if (a.configuration != b.configuration || a.decks.size() != b.decks.size()
        || a.random_dealing.seed != b.random_dealing.seed
        || b.random_dealing.next_step < a.random_dealing.next_step
        || !advances(a.timing, b.timing)
        || !same_schedule(a.table_schedule, b.table_schedule))
        return false;
    for (std::size_t i = 0; i < a.decks.size(); ++i) {
        const auto& before = a.decks[i];
        const auto& after = b.decks[i];
        if (!same_deck(
                before, after.configuration, after.id,
                after.stream.rank_weights, after.stream.initial_running_count,
                after.status, after.dealt_physical_cards, after.statistics
            )
            || !same_schedule(
                before.stream.virtual_quiz_targets, before.next_quiz_target,
                before.stream.quiz_continuation,
                after.stream.virtual_quiz_targets, after.next_quiz_target,
                after.stream.quiz_continuation
            ))
            return false;
        if (!before.stream.infinite_cards) {
            if (after.stream.infinite_cards
                || before.stream.cards != after.stream.cards)
                return false;
        } else {
            if (!after.stream.infinite_cards)
                return false;
            const auto& stream = *after.stream.infinite_cards;
            if (!same_stream(
                    before.stream.infinite_cards,
                    gameplay::infinite_stream_position {
                        stream.seed, stream.chunk, stream.decks_per_chunk }
                )
                || (stream.chunk == before.stream.infinite_cards->chunk
                    && (before.stream.cards != after.stream.cards
                        || before.stream.infinite_cards->lookahead
                            != stream.lookahead)))
                return false;
        }
    }
    return true;
}

bool continues(
    const gameplay_checkpoint& previous, const gameplay::session_result& next
) {
    const auto& a = previous.state;
    if (a.configuration != next.configuration
        || a.decks.size() != next.decks.size()
        || a.random_dealing.seed != next.random_dealing.seed
        || next.random_dealing.next_step < a.random_dealing.next_step
        || !advances(a.timing, next.timing)
        || !same_schedule(a.table_schedule, next.table_schedule))
        return false;
    for (std::size_t i = 0; i < a.decks.size(); ++i) {
        const auto& before = a.decks[i];
        const auto& after = next.decks[i];
        if (!same_deck(
                before, after.configuration, after.id, after.rank_weights,
                after.initial_running_count, after.status,
                after.dealt_physical_cards, after.statistics
            )
            || !same_stream(
                before.stream.infinite_cards, after.infinite_position
            )
            || !same_schedule(
                before.stream.virtual_quiz_targets, before.next_quiz_target,
                before.stream.quiz_continuation, after.schedule.targets,
                after.schedule.next_target, after.schedule.continuation
            ))
            return false;
    }
    return true;
}

} // namespace recovery_storage
