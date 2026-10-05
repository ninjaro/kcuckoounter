#include "table/gameplay_session.hpp"

#include "table/gameplay_cadence.hpp"
#include "table/gameplay_generation.hpp"

#include <algorithm>
#include <limits>
#include <random>
#include <type_traits>
#include <utility>

namespace gameplay {
namespace {

    bool add_checked(std::uint64_t& value, std::uint64_t delta) {
        if (delta > std::numeric_limits<std::uint64_t>::max() - value) {
            return false;
        }
        value += delta;
        return true;
    }

    // Provisional R-007: uniform over eligible stable deck identities, with
    // immediate repeats allowed (also the current legacy policy). This is the
    // sole repeat-policy boundary, independent of slot geometry/traversal.
    deck_id select_random_deck(
        std::span<const deck_id> eligible,
        const random_dealing_continuation& continuation
    ) {
        std::seed_seq seed {
            static_cast<std::uint32_t>(continuation.seed),
            static_cast<std::uint32_t>(continuation.seed >> 32),
            7U, // Independent of G3 card/quiz domains 1..6.
            static_cast<std::uint32_t>(continuation.next_step),
            static_cast<std::uint32_t>(continuation.next_step >> 32),
        };
        std::mt19937_64 engine(seed);
        const auto bound = static_cast<std::uint64_t>(eligible.size());
        const auto threshold = (std::uint64_t { 0 } - bound) % bound;
        auto value = engine();
        while (value < threshold) {
            value = engine();
        }
        return eligible[static_cast<std::size_t>(value % bound)];
    }

    struct schedule_progress {
        std::size_t cursor;
        std::optional<generated_quiz_page> replacement;
        bool reached = false;
    };

    // Consume all ready targets as one event. Catch-up retains only the newest
    // page, not history. At exact page end refill lazily on the next ready
    // tick.
    std::optional<schedule_progress> consume_targets(
        std::span<const std::uint64_t> targets, std::size_t cursor,
        const std::optional<quiz_schedule_continuation>& continuation,
        std::uint64_t exposure, std::optional<deck_id> owner
    ) {
        if (cursor > targets.size()) {
            return std::nullopt;
        }
        schedule_progress result { .cursor = cursor,
                                   .replacement = std::nullopt };
        auto recipe = continuation;
        for (;;) {
            const auto next = std::upper_bound(
                targets.begin() + static_cast<std::ptrdiff_t>(result.cursor),
                targets.end(), exposure
            );
            const auto index = static_cast<std::size_t>(next - targets.begin());
            result.reached = result.reached || index != result.cursor;
            result.cursor = index;
            if (index < targets.size() || !recipe
                || exposure < recipe->after_target
                || exposure - recipe->after_target < recipe->minimum_gap) {
                return result;
            }
            generation_policy capacity;
            capacity.maximum_targets = recipe->page_size;
            auto page = generate_quiz_page(*recipe, owner, capacity);
            if (!page) {
                return std::nullopt;
            }
            result.replacement = std::move(page);
            targets = result.replacement->targets;
            recipe = result.replacement->continuation;
            result.cursor = 0;
        }
    }

} // namespace

struct session::quiz_transition {
    struct deck_schedule {
        deck_id owner;
        schedule_progress progress;
    };

    struct question {
        deck_id owner;
        std::int64_t expected;
    };

    std::vector<deck_schedule> schedules;
    std::optional<schedule_progress> table_schedule;
    std::vector<question> questions;
    std::optional<quiz_batch> next_batch;
    std::uint64_t next_batch_id;
};

struct session::progress_transition {
    struct deck_progress {
        deck_status status;
        std::uint64_t physical_cards;
        std::int64_t running_count;
        std::uint64_t unverified_cards;
        std::optional<pending_quiz> quiz;
        deck_statistics statistics;
    };

    std::vector<deck_progress> decks;
    session_statistics challenge;
    session_timing timing;
    int lives;
    std::optional<session_result> result;
};

std::optional<session::progress_transition> session::plan_progress(
    std::span<const card_reveal> reveals, std::span<const quiz_answer> answers,
    std::uint64_t elapsed_ms, std::optional<deck_id> stopped,
    deck_status stop_status, bool manual_finish
) const {
    progress_transition next;
    next.lives = lives;
    next.timing = active_timing;
    next.decks.reserve(decks.size());
    const bool any_question = batch.has_value();
    if (!add_checked(next.timing.play_time_ms, elapsed_ms)
        || (any_question
            && !add_checked(next.timing.quiz_time_ms, elapsed_ms))) {
        return std::nullopt;
    }
    for (const auto& record : decks) {
        auto stats = record.statistics;
        if (record.status == deck_status::active) {
            if (record.quiz) {
                if (!add_checked(stats.answer_time_ms, elapsed_ms)) {
                    return std::nullopt;
                }
            } else if (!settings.global_quiz_pause || !any_question) {
                if (!add_checked(stats.dealing_time_ms, elapsed_ms)) {
                    return std::nullopt;
                }
            }
        }
        next.decks.push_back(
            { record.status, record.dealt_physical_cards, record.running_count,
              record.unverified_normal_cards, record.quiz, stats }
        );
    }
    for (const auto& reveal : reveals) {
        auto& progress = next.decks[reveal.owner.value];
        progress.physical_cards = reveal.dealt_physical_cards;
        progress.running_count = reveal.running_count;
        if (normal_card_rank(reveal.face)) {
            if (!add_checked(progress.statistics.dealt_normal_cards, 1)
                || !add_checked(progress.unverified_cards, 1)) {
                return std::nullopt;
            }
        }
        if (reveal.completed) {
            progress.status = deck_status::completed;
        }
    }
    for (const auto& answer : answers) {
        auto& progress = next.decks[answer.owner.value];
        const bool training = decks[answer.owner.value].configuration.training;
        auto& stats = progress.statistics;
        if (answer.outcome == quiz_outcome::correct && !training) {
            if (!add_checked(stats.verified_cards, answer.normal_cards)) {
                return std::nullopt;
            }
        } else if (answer.outcome == quiz_outcome::wrong) {
            if (!add_checked(stats.errors, 1)) {
                return std::nullopt;
            }
            if (!training) {
                if (settings.failure == failure_policy::block) {
                    progress.status = deck_status::failed;
                } else if (next.lives > 0) {
                    --next.lives;
                }
            }
        } else if (answer.outcome == quiz_outcome::skipped) {
            if (!add_checked(stats.skips, 1)) {
                return std::nullopt;
            }
        }
        progress.unverified_cards = 0;
        progress.quiz.reset();
    }
    if (stopped) {
        next.decks[stopped->value].status = stop_status;
        next.decks[stopped->value].quiz.reset();
    }
    bool live = false;
    bool pending = false;
    for (std::size_t i = 0; i < decks.size(); ++i) {
        const auto& progress = next.decks[i];
        live = live || progress.status == deck_status::active;
        pending = pending || progress.quiz.has_value();
        if (!decks[i].configuration.training) {
            const auto& stats = progress.statistics;
            if (!add_checked(
                    next.challenge.dealt_normal_cards, stats.dealt_normal_cards
                )
                || !add_checked(
                    next.challenge.verified_cards, stats.verified_cards
                )
                || !add_checked(next.challenge.errors, stats.errors)
                || !add_checked(next.challenge.skips, stats.skips)) {
                return std::nullopt;
            }
        }
    }
    const bool out_of_lives = settings.failure == failure_policy::lives
        && next.lives == 0 && !pending;
    if (!manual_finish && live && !out_of_lives) {
        return next;
    }

    // Allocate only the bounded result context, before committing anything.
    // No shoe/lookahead or pending-input checkpoint is copied into history.
    session_result result {};
    result.reason = manual_finish
        ? session_end_reason::manual_finish
        : (out_of_lives ? session_end_reason::lives_exhausted
                        : session_end_reason::no_live_decks);
    result.configuration = settings;
    result.challenge = next.challenge;
    result.timing = next.timing;
    result.remaining_lives = next.lives;
    result.table_schedule
        = { multi_quiz_targets, multi_quiz_cursor, multi_quiz_continuation };
    result.random_dealing = random_dealing_state;
    if (settings.dealing == dealing_mode::random && !reveals.empty()) {
        ++result.random_dealing.next_step; // G4 preflight already checked room.
    }
    result.decks.reserve(decks.size());
    for (std::size_t i = 0; i < decks.size(); ++i) {
        const auto& record = decks[i];
        const auto& progress = next.decks[i];
        std::optional<infinite_stream_position> position;
        if (record.stream.infinite_cards) {
            const auto& recipe = *record.stream.infinite_cards;
            position = { recipe.seed, recipe.chunk, recipe.decks_per_chunk };
        }
        result.decks.push_back(
            {
                .id = record.id,
                .slot = deck_to_slot[i],
                .configuration = record.configuration,
                .status = progress.status,
                .rank_weights = record.stream.rank_weights,
                .initial_running_count = record.stream.initial_running_count,
                .final_running_count = progress.running_count,
                .dealt_physical_cards = progress.physical_cards,
                .unverified_normal_cards = progress.unverified_cards,
                .statistics = progress.statistics,
                .schedule
                = { record.stream.virtual_quiz_targets, record.next_quiz_target,
                    record.stream.quiz_continuation },
                .infinite_position = position,
            }
        );
        switch (progress.status) {
        case deck_status::active:
            ++result.active_decks;
            break;
        case deck_status::completed:
            ++result.completed_decks;
            break;
        case deck_status::failed:
            ++result.failed_decks;
            break;
        }
    }
    next.result = std::move(result);
    return next;
}

void session::apply_progress(progress_transition&& transition) {
    for (std::size_t i = 0; i < decks.size(); ++i) {
        auto& record = decks[i];
        const auto& progress = transition.decks[i];
        record.status = progress.status;
        record.dealt_physical_cards = progress.physical_cards;
        record.running_count = progress.running_count;
        record.unverified_normal_cards = progress.unverified_cards;
        record.quiz = progress.quiz;
        record.statistics = progress.statistics;
    }
    challenge = transition.challenge;
    active_timing = transition.timing;
    lives = transition.lives;
    if (transition.result) {
        static_assert(std::is_nothrow_move_assignable_v<session_result>);
        completed_result = std::move(transition.result);
        current_phase = session_phase::finished;
    }
}

std::optional<session::quiz_transition> session::plan_quizzes(
    std::span<const card_reveal> reveals, const progress_transition& progress
) const {
    quiz_transition result;
    result.next_batch_id = last_quiz_batch_id;
    const bool pending = std::any_of(
        progress.decks.begin(), progress.decks.end(),
        [](const auto& record) { return record.quiz.has_value(); }
    );
    result.next_batch = pending ? batch : std::nullopt;
    if (settings.failure == failure_policy::lives && progress.lives == 0
        && !pending) {
        return result; // Finish the resolved batch, never open a new one.
    }
    const auto live = [&](const deck_state& record) {
        return progress.decks[record.id.value].status == deck_status::active;
    };
    const auto ask = [&](const deck_state& record) {
        result.questions.push_back(
            { record.id, progress.decks[record.id.value].running_count }
        );
    };

    if (settings.scope == quiz_scope::single) {
        for (const auto& reveal : reveals) {
            const auto& record = decks[reveal.owner.value];
            // Natural finite completion precedes a last-card Joker event.
            if (!live(record) || progress.decks[record.id.value].quiz) {
                continue;
            }
            bool triggered = !normal_card_rank(reveal.face);
            if (settings.source == quiz_source::virtual_interrupt) {
                auto schedule = consume_targets(
                    record.stream.virtual_quiz_targets, record.next_quiz_target,
                    record.stream.quiz_continuation,
                    progress.decks[record.id.value]
                        .statistics.dealt_normal_cards,
                    record.id
                );
                if (!schedule) {
                    return std::nullopt;
                }
                triggered = schedule->reached;
                result.schedules.push_back({ record.id, std::move(*schedule) });
            }
            if (triggered) {
                ask(record);
            }
        }
    } else {
        bool triggered = std::any_of(
            reveals.begin(), reveals.end(), [&](const auto& reveal) {
                return live(decks[reveal.owner.value])
                    && !normal_card_rank(reveal.face);
            }
        );
        if (settings.source == quiz_source::virtual_interrupt) {
            std::vector<cadence_exposure> exposure;
            exposure.reserve(decks.size());
            for (const auto& record : progress.decks) {
                exposure.push_back({ record.status, record.physical_cards });
            }
            const auto limit
                = multi_quiz_exposure_limit(exposure, settings.dealing);
            if (limit) {
                auto schedule = consume_targets(
                    multi_quiz_targets, multi_quiz_cursor,
                    multi_quiz_continuation, *limit, std::nullopt
                );
                if (!schedule) {
                    return std::nullopt;
                }
                triggered = schedule->reached;
                result.table_schedule = std::move(schedule);
            }
        }
        // Targets reached while an earlier Multi batch is open are coalesced,
        // never queued for duplicate questions at the same exposure.
        if (triggered && !pending) {
            for (const auto& record : decks) {
                if (live(record)) {
                    ask(record);
                }
            }
        }
    }
    if (!result.questions.empty() && !result.next_batch) {
        if (last_quiz_batch_id == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        result.next_batch_id = last_quiz_batch_id + 1;
        result.next_batch = quiz_batch {
            .id = result.next_batch_id,
            .remaining_ms = settings.global_quiz_pause
                ? std::optional { quiz_initial_ms }
                : std::nullopt,
        };
    }
    return result;
}

void session::apply_quizzes(quiz_transition&& transition) {
    for (auto& schedule : transition.schedules) {
        auto& record = decks[schedule.owner.value];
        record.next_quiz_target = schedule.progress.cursor;
        if (schedule.progress.replacement) {
            record.stream.virtual_quiz_targets
                = std::move(schedule.progress.replacement->targets);
            record.stream.quiz_continuation
                = schedule.progress.replacement->continuation;
        }
    }
    if (transition.table_schedule) {
        multi_quiz_cursor = transition.table_schedule->cursor;
        if (transition.table_schedule->replacement) {
            multi_quiz_targets
                = std::move(transition.table_schedule->replacement->targets);
            multi_quiz_continuation
                = transition.table_schedule->replacement->continuation;
        }
    }
    for (const auto& question : transition.questions) {
        auto& record = decks[question.owner.value];
        record.quiz = pending_quiz { transition.next_batch->id,
                                     question.expected, record.latest_input };
    }
    batch = transition.next_batch;
    last_quiz_batch_id = transition.next_batch_id;
}

session_phase session::phase() const { return current_phase; }

int session::remaining_lives() const { return lives; }

std::size_t session::size() const { return decks.size(); }

const session_statistics& session::statistics() const { return challenge; }

const session_timing& session::timing() const { return active_timing; }

const std::optional<session_result>& session::result() const {
    return completed_result;
}

const deck_state* session::deck(deck_id id) const {
    return id.value < decks.size() ? &decks[id.value] : nullptr;
}

std::optional<physical_slot_id> session::slot_for(deck_id id) const {
    if (id.value >= deck_to_slot.size()) {
        return std::nullopt;
    }
    return deck_to_slot[id.value];
}

std::optional<deck_id> session::deck_at(physical_slot_id slot) const {
    if (slot.value >= slot_to_deck.size()) {
        return std::nullopt;
    }
    return slot_to_deck[slot.value];
}

bool session::set_traversal(std::span<const std::size_t> slot_indices) {
    if (current_phase == session_phase::finished
        || slot_indices.size() != decks.size()) {
        return false;
    }
    std::vector<physical_slot_id> order;
    std::vector<std::size_t> positions(decks.size(), decks.size());
    order.reserve(decks.size());
    for (const auto index : slot_indices) {
        if (index >= decks.size() || positions[index] != decks.size()) {
            return false;
        }
        positions[index] = order.size();
        order.push_back(physical_slot_id { index });
    }
    physical_order = std::move(order);
    traversal_positions = std::move(positions);
    return true;
}

std::span<const physical_slot_id> session::traversal() const {
    return physical_order;
}

std::vector<physical_slot_id> session::live_traversal() const {
    std::vector<physical_slot_id> live;
    live.reserve(physical_order.size());
    for (const auto slot : physical_order) {
        if (decks[slot_to_deck[slot.value].value].status
            == deck_status::active) {
            live.push_back(slot);
        }
    }
    return live;
}

std::optional<physical_slot_id> session::sequential_anchor() const {
    return last_selected_slot;
}

std::vector<physical_slot_id> session::next_sequential_slots() {
    if (current_phase != session_phase::running
        || settings.dealing != dealing_mode::sequential
        || physical_order.empty()) {
        return {};
    }
    auto selected = select_sequential_slots(false);
    if (!selected.empty()) {
        last_selected_slot = selected.back();
    }
    return selected;
}

std::vector<physical_slot_id>
session::select_sequential_slots(bool exclude_pending_quizzes) const {
    std::vector<physical_slot_id> selected;
    selected.reserve(settings.sequential_count);
    auto position = last_selected_slot
        ? (traversal_positions[last_selected_slot->value] + 1) % decks.size()
        : 0;
    // At most one full traversal: a step never deals twice to one live deck,
    // including when fewer than N positions still participate.
    for (std::size_t visited = 0; visited < decks.size(); ++visited) {
        const auto slot = physical_order[position];
        const auto& deck = decks[slot_to_deck[slot.value].value];
        if (deck.status == deck_status::active
            && (!exclude_pending_quizzes || !deck.quiz)) {
            selected.push_back(slot);
            if (selected.size() == settings.sequential_count) {
                break;
            }
        }
        position = (position + 1) % decks.size();
    }
    return selected;
}

const random_dealing_continuation& session::random_dealing() const {
    return random_dealing_state;
}

dealing_step session::deal_step() {
    if (current_phase != session_phase::running) {
        return { .status = dealing_step_status::not_running };
    }
    if (settings.global_quiz_pause
        && std::any_of(decks.begin(), decks.end(), [](const auto& deck) {
               return deck.quiz.has_value();
           })) {
        return { .status = dealing_step_status::quiz_blocked };
    }
    std::vector<physical_slot_id> selected;
    if (settings.dealing == dealing_mode::sequential) {
        selected = select_sequential_slots(true);
    } else if (settings.dealing == dealing_mode::simultaneous) {
        selected.reserve(decks.size());
        for (const auto slot : physical_order) {
            const auto& deck = decks[slot_to_deck[slot.value].value];
            if (deck.status == deck_status::active && !deck.quiz) {
                selected.push_back(slot);
            }
        }
    } else {
        std::vector<deck_id> eligible;
        eligible.reserve(decks.size());
        for (const auto& deck : decks) {
            if (deck.status == deck_status::active && !deck.quiz) {
                eligible.push_back(deck.id);
            }
        }
        if (!eligible.empty()) {
            if (random_dealing_state.next_step
                == std::numeric_limits<std::uint64_t>::max()) {
                return { .status = dealing_step_status::arithmetic_limit };
            }
            selected.push_back(
                deck_to_slot[select_random_deck(eligible, random_dealing_state)
                                 .value]
            );
        }
    }
    if (selected.empty()) {
        return { .status = dealing_step_status::no_eligible_decks };
    }

    struct planned_reveal {
        card_reveal reveal;
        std::optional<deck_state> rollover;
    };

    std::vector<planned_reveal> plan;
    plan.reserve(selected.size());
    dealing_step result { .status = dealing_step_status::advanced };
    result.cards.reserve(selected.size());
    for (const auto slot : selected) {
        const auto id = slot_to_deck[slot.value];
        const auto& original = decks[id.value];
        const auto* candidate = &original;
        std::optional<deck_state> rollover;
        if (original.stream.cards.empty()
            || original.next_card > original.stream.cards.size()) {
            return { .status = dealing_step_status::invalid_stream };
        }
        if (original.next_card == original.stream.cards.size()) {
            if (!original.configuration.infinite
                || original.stream.cards.size()
                    > std::numeric_limits<std::size_t>::max() / 2) {
                return { .status = dealing_step_status::invalid_stream };
            }
            // Inherit accepted preparation capacity rather than imposing a
            // new default ceiling on a custom-sized, already bounded buffer.
            generation_policy capacity;
            capacity.maximum_cards = 2 * original.stream.cards.size();
            rollover = original; // Only at exhaustion, not on ordinary ticks.
            if (!rollover_infinite_shoe(*rollover, settings.source, capacity)) {
                return { .status = dealing_step_status::invalid_stream };
            }
            candidate = &*rollover;
        }
        const auto face = candidate->stream.cards[candidate->next_card];
        if (face
            >= (settings.source == quiz_source::physical_joker ? 54 : 52)) {
            return { .status = dealing_step_status::invalid_stream };
        }
        const auto rank = normal_card_rank(face);
        auto count = original.running_count;
        if (rank) {
            const auto weight = static_cast<std::int64_t>(
                original.stream.rank_weights[*rank]
            );
            if ((weight > 0
                 && count > std::numeric_limits<std::int64_t>::max() - weight)
                || (weight < 0
                    && count
                        < std::numeric_limits<std::int64_t>::min() - weight)) {
                return { .status = dealing_step_status::arithmetic_limit };
            }
            count += weight;
        }
        const auto limit = std::numeric_limits<std::uint64_t>::max();
        if (original.dealt_physical_cards == limit
            || (rank
                && (original.unverified_normal_cards == limit
                    || original.statistics.dealt_normal_cards == limit))) {
            return { .status = dealing_step_status::arithmetic_limit };
        }
        plan.push_back({
            .reveal = {
                .owner = id,
                .slot = slot,
                .face = face,
                .dealt_physical_cards = original.dealt_physical_cards + 1,
                .running_count = count,
                .completed = !original.configuration.infinite
                    && candidate->next_card + 1 == candidate->stream.cards.size(),
            },
            .rollover = std::move(rollover),
        });
        result.cards.push_back(plan.back().reveal);
    }
    auto progress = plan_progress(result.cards);
    if (!progress) {
        return { .status = dealing_step_status::arithmetic_limit };
    }
    auto quiz_update = plan_quizzes(result.cards, *progress);
    if (!quiz_update) {
        return { .status = dealing_step_status::quiz_schedule_limit };
    }
    // All arithmetic and allocating preparation succeeded. No partial step,
    // cursor advancement or RNG ordinal consumption on rejection/exception.
    static_assert(std::is_nothrow_move_assignable_v<deck_state>);
    for (auto& update : plan) {
        auto& deck = decks[update.reveal.owner.value];
        if (update.rollover) {
            deck = std::move(*update.rollover);
        }
        ++deck.next_card;
    }
    apply_progress(std::move(*progress));
    apply_quizzes(std::move(*quiz_update));
    if (settings.dealing == dealing_mode::sequential) {
        last_selected_slot = selected.back();
    } else if (settings.dealing == dealing_mode::random) {
        ++random_dealing_state.next_step;
    }
    return result;
}

const std::optional<quiz_batch>& session::current_quiz_batch() const {
    return batch;
}

std::size_t session::next_table_quiz_target() const {
    return multi_quiz_cursor;
}

std::vector<deck_id> session::unresolved_quizzes() const {
    std::vector<deck_id> result;
    result.reserve(decks.size());
    for (const auto slot : physical_order) {
        const auto& record = decks[slot_to_deck[slot.value].value];
        if (record.status == deck_status::active && record.quiz) {
            result.push_back(record.id);
        }
    }
    return result;
}

std::optional<deck_id>
session::quiz_focus(std::optional<deck_id> after, bool reverse) const {
    if (physical_order.empty() || (after && !deck(*after))) {
        return std::nullopt;
    }
    const auto count = physical_order.size();
    auto position = after
        ? traversal_positions[deck_to_slot[after->value].value]
        : (reverse ? 0 : count - 1);
    for (std::size_t visited = 0; visited < count; ++visited) {
        position = reverse ? (position == 0 ? count - 1 : position - 1)
                           : (position + 1) % count;
        const auto id = slot_to_deck[physical_order[position].value];
        if (decks[id.value].status == deck_status::active
            && decks[id.value].quiz) {
            return id;
        }
    }
    return std::nullopt;
}

bool session::edit_quiz_input(deck_id id, std::int64_t input) {
    if (current_phase != session_phase::running || !deck(id)
        || !decks[id.value].quiz) {
        return false;
    }
    auto& record = decks[id.value];
    record.latest_input = input;
    record.quiz->input = input;
    return true;
}

quiz_answer
session::answer_snapshot(deck_id id, bool skip, bool timeout) const {
    const auto& record = decks[id.value];
    const auto& question = *record.quiz;
    return {
        .owner = id,
        .batch_id = question.batch_id,
        .submitted_count = question.input,
        .expected_count = question.expected_count,
        .normal_cards = record.unverified_normal_cards,
        .outcome = skip
            ? quiz_outcome::skipped
            : (question.input == question.expected_count ? quiz_outcome::correct
                                                         : quiz_outcome::wrong),
        .timed_out = timeout,
    };
}

std::optional<quiz_answer> session::answer_quiz(deck_id id, bool skip) {
    if (current_phase != session_phase::running || !deck(id)
        || !decks[id.value].quiz || (skip && !settings.allow_skip)) {
        return std::nullopt;
    }
    const auto answer = answer_snapshot(id, skip, false);
    const auto extension = skip
        ? quiz_skip_ms
        : (answer.outcome == quiz_outcome::correct ? quiz_correct_ms
                                                   : quiz_wrong_ms);
    const std::array answers { answer };
    auto progress = plan_progress({}, answers);
    if (!progress) {
        return std::nullopt;
    }
    auto quiz_update = plan_quizzes({}, *progress);
    if (!quiz_update) {
        return std::nullopt;
    }
    auto& next_batch = quiz_update->next_batch;
    if (next_batch && next_batch->id == answer.batch_id
        && next_batch->remaining_ms
        && !add_checked(*next_batch->remaining_ms, extension)) {
        return std::nullopt;
    }
    apply_progress(std::move(*progress));
    apply_quizzes(std::move(*quiz_update));
    return answer;
}

std::optional<quiz_answer> session::check_quiz(deck_id id) {
    return answer_quiz(id, false);
}

std::optional<quiz_answer> session::skip_quiz(deck_id id) {
    return answer_quiz(id, true);
}

quiz_clock_step session::elapse_quiz(std::uint64_t elapsed_ms) {
    if (current_phase != session_phase::running || !batch
        || !batch->remaining_ms) {
        return {};
    }
    return advance_time(elapsed_ms);
}

quiz_clock_step session::advance_time(std::uint64_t elapsed_ms) {
    if (current_phase != session_phase::running) {
        return {};
    }
    const bool frozen = batch && batch->remaining_ms;
    const auto duration
        = frozen ? std::min(elapsed_ms, *batch->remaining_ms) : elapsed_ms;
    const bool timeout = frozen && elapsed_ms >= *batch->remaining_ms;
    quiz_clock_step result { .accepted = true,
                             .elapsed_ms = duration,
                             .answers = {} };
    // Allocate and freeze every unresolved input before changing any state.
    // Neither timeout processing order nor correction can alter another input.
    if (timeout) {
        result.answers.reserve(decks.size());
        for (const auto slot : physical_order) {
            const auto id = slot_to_deck[slot.value];
            if (decks[id.value].quiz) {
                result.answers.push_back(answer_snapshot(id, false, true));
            }
        }
    }
    auto progress = plan_progress({}, result.answers, duration);
    if (!progress) {
        return {};
    }
    if (timeout) {
        auto quiz_update = plan_quizzes({}, *progress);
        if (!quiz_update) {
            return {};
        }
        apply_progress(std::move(*progress));
        apply_quizzes(std::move(*quiz_update));
    } else {
        apply_progress(std::move(*progress));
        if (frozen) {
            *batch->remaining_ms -= duration;
        }
    }
    return result;
}

bool session::pause() {
    if (current_phase != session_phase::running) {
        return false;
    }
    current_phase = session_phase::paused;
    return true;
}

bool session::resume() {
    if (current_phase != session_phase::paused) {
        return false;
    }
    current_phase = session_phase::running;
    return true;
}

bool session::finish() {
    if (current_phase != session_phase::running
        && current_phase != session_phase::paused) {
        return false;
    }
    auto progress
        = plan_progress({}, {}, 0, std::nullopt, deck_status::completed, true);
    if (!progress) {
        return false;
    }
    apply_progress(std::move(*progress));
    return true;
}

bool session::swap_decks(deck_id first, deck_id second) {
    if (current_phase != session_phase::paused || !deck(first)
        || !deck(second)) {
        return false;
    }
    const auto first_slot = deck_to_slot[first.value];
    const auto second_slot = deck_to_slot[second.value];
    std::swap(deck_to_slot[first.value], deck_to_slot[second.value]);
    slot_to_deck[first_slot.value] = second;
    slot_to_deck[second_slot.value] = first;
    return true;
}

bool session::set_show_count(deck_id id, bool enabled) {
    if ((current_phase != session_phase::setup
         && current_phase != session_phase::paused)
        || !deck(id) || !decks[id.value].configuration.training) {
        return false;
    }
    decks[id.value].show_count = enabled;
    return true;
}

bool session::complete_deck(deck_id id) {
    if (current_phase != session_phase::running || !deck(id)) {
        return false;
    }
    const auto& state = decks[id.value];
    if (state.status != deck_status::active || state.configuration.infinite) {
        return false;
    }
    auto progress = plan_progress({}, {}, 0, id, deck_status::completed);
    if (!progress) {
        return false;
    }
    auto quiz_update = plan_quizzes({}, *progress);
    if (!quiz_update) {
        return false;
    }
    apply_progress(std::move(*progress));
    apply_quizzes(std::move(*quiz_update));
    return true;
}

bool session::fail_deck(deck_id id) {
    if (current_phase != session_phase::running || !deck(id)
        || settings.failure != failure_policy::block) {
        return false;
    }
    const auto& state = decks[id.value];
    if (state.status != deck_status::active || state.configuration.training) {
        return false;
    }
    auto progress = plan_progress({}, {}, 0, id, deck_status::failed);
    if (!progress) {
        return false;
    }
    auto quiz_update = plan_quizzes({}, *progress);
    if (!quiz_update) {
        return false;
    }
    apply_progress(std::move(*progress));
    apply_quizzes(std::move(*quiz_update));
    return true;
}

int session::pick_interval_ms() const { return interval_ms; }

bool session::set_pick_interval_ms(int interval) {
    // Existing manual speed control is live, unlike gameplay-defining setup.
    if (current_phase == session_phase::finished
        || interval < minimum_pick_interval_ms
        || interval > maximum_pick_interval_ms) {
        return false;
    }
    interval_ms = interval;
    return true;
}

} // namespace gameplay
