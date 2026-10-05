#ifndef KCUCKOOUNTER_TABLE_GAMEPLAY_TYPES_HPP
#define KCUCKOOUNTER_TABLE_GAMEPLAY_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Target gameplay domain, independent of widgets and legacy persistence.
namespace gameplay {

struct deck_id {
    std::size_t value = 0;
    bool operator==(const deck_id&) const = default;
};

struct physical_slot_id {
    std::size_t value = 0;
    bool operator==(const physical_slot_id&) const = default;
};

enum class deck_status { active, completed, failed };
enum class session_phase { setup, running, paused, finished };
enum class failure_policy { lives, block };
enum class quiz_source { physical_joker, virtual_interrupt };
enum class quiz_scope { single, multi };
enum class dealing_mode { sequential, random, simultaneous };

struct session_configuration {
    failure_policy failure = failure_policy::lives;
    int initial_lives = 3;
    quiz_source source = quiz_source::virtual_interrupt;
    quiz_scope scope = quiz_scope::single;
    bool global_quiz_pause = true;
    bool allow_skip = true;
    dealing_mode dealing = dealing_mode::sequential;
    std::size_t sequential_count = 1;
    bool allow_extra_slots = false;

    bool operator==(const session_configuration&) const = default;
};

struct deck_configuration {
    std::string strategy_slug;
    std::size_t deck_count = 4;
    bool infinite = false;
    bool training = false;

    bool operator==(const deck_configuration&) const = default;
};

// Supplied by the screen/cap policy, not a hard-coded gameplay maximum of 16.
// R-006 owns the formula and the eventual production cap.
struct slot_limits {
    std::size_t recommended;
    std::size_t technical_cap;
    bool operator==(const slot_limits&) const = default;
};

// Frozen generation recipe; current chunk is in prepared_deck::cards.
struct infinite_card_buffer {
    std::uint64_t seed = 0;
    std::uint64_t chunk = 0;
    std::size_t decks_per_chunk = 100;
    std::vector<std::uint8_t> lookahead;
    bool operator==(const infinite_card_buffer&) const = default;
};

// One bounded page of absolute quiz positions, independent of card chunks.
// The session consumes pages; G8 chooses their eventual checkpoint encoding.
struct quiz_schedule_continuation {
    std::uint64_t seed = 0;
    std::uint64_t next_page = 0;
    std::uint64_t after_target = 0;
    std::uint32_t minimum_gap = 20;
    std::uint32_t maximum_gap = 40;
    std::size_t page_size = 64;
    bool operator==(const quiz_schedule_continuation&) const = default;
};

// Prepared by G3. Face indices 0..51 are normal cards, 52/53 physical Jokers.
// A virtual interrupt is deliberately not a card. No rendering data lives here.
struct prepared_deck {
    std::vector<std::uint8_t> cards;
    std::array<int, 13> rank_weights {};
    std::int64_t initial_running_count = 0;
    // Virtual Single positions count revealed normal cards, not zero-based
    // card offsets. Physical-Joker and Multi decks keep this vector empty.
    std::vector<std::uint64_t> virtual_quiz_targets {};
    std::optional<infinite_card_buffer> infinite_cards {};
    std::optional<quiz_schedule_continuation> quiz_continuation {};

    bool operator==(const prepared_deck&) const = default;
};

struct deck_statistics {
    std::uint64_t dealt_normal_cards = 0;
    // Challenge credit only; Training always remains zero. Training errors/
    // skips below are informational per-deck data, excluded from session
    // totals.
    std::uint64_t verified_cards = 0;
    std::uint64_t errors = 0;
    std::uint64_t skips = 0;
    std::uint64_t dealing_time_ms = 0;
    std::uint64_t answer_time_ms = 0;

    bool operator==(const deck_statistics&) const = default;
};

// Challenge counters exclude Training. Time is wall duration, not a sum of
// overlapping per-deck timers; quiz time is a subset of active play time.
struct session_statistics {
    std::uint64_t dealt_normal_cards = 0;
    std::uint64_t verified_cards = 0;
    std::uint64_t errors = 0;
    std::uint64_t skips = 0;
    bool operator==(const session_statistics&) const = default;
};

struct session_timing {
    std::uint64_t play_time_ms = 0;
    std::uint64_t quiz_time_ms = 0;
    bool operator==(const session_timing&) const = default;
};

struct pending_quiz {
    std::uint64_t batch_id = 0;
    std::int64_t expected_count = 0;
    std::int64_t input = 0;
    bool operator==(const pending_quiz&) const = default;
};

// Complete deck-owned payload: moving its mapping never moves only a subset
// of this data. G3/G5/G6 implement stream, schedule, quiz and scoring
// operations.
struct deck_state {
    deck_id id;
    deck_configuration configuration;
    deck_status status = deck_status::active;
    prepared_deck stream;
    std::size_t next_card = 0;
    std::uint64_t dealt_physical_cards = 0;
    std::int64_t running_count = 0;
    std::int64_t latest_input = 0;
    bool show_count = false;
    std::uint64_t unverified_normal_cards = 0;
    // Index into the current finite schedule or bounded infinite page.
    std::size_t next_quiz_target = 0;
    std::optional<pending_quiz> quiz;
    deck_statistics statistics;

    bool operator==(const deck_state&) const = default;
};

// Plain continuation, not a checkpoint format. Only successful Random steps
// consume an ordinal; card and quiz generation have independent streams.
struct random_dealing_continuation {
    std::uint64_t seed = 0;
    std::uint64_t next_step = 0;
    bool operator==(const random_dealing_continuation&) const = default;
};

enum class dealing_step_status {
    advanced,
    not_running,
    quiz_blocked,
    no_eligible_decks,
    invalid_stream,
    quiz_schedule_limit,
    arithmetic_limit
};

struct card_reveal {
    deck_id owner;
    physical_slot_id slot;
    std::uint8_t face;
    // One-based cumulative physical reveals, NOT the current chunk offset.
    std::uint64_t dealt_physical_cards;
    std::int64_t running_count;
    bool completed;
    bool operator==(const card_reveal&) const = default;
};

struct dealing_step {
    dealing_step_status status = dealing_step_status::not_running;
    std::vector<card_reveal> cards {};
};

struct quiz_batch {
    std::uint64_t id = 0;
    // Absent when non-queried decks may continue dealing.
    std::optional<std::uint64_t> remaining_ms;
    bool operator==(const quiz_batch&) const = default;
};

enum class quiz_outcome { correct, wrong, skipped };

// The session already applies credit/penalties once before returning outcomes.
// Correction is the whole frozen count, never the last visible card's weight.
struct quiz_answer {
    deck_id owner;
    std::uint64_t batch_id;
    std::int64_t submitted_count;
    std::int64_t expected_count;
    std::uint64_t normal_cards;
    quiz_outcome outcome;
    bool timed_out;
    bool operator==(const quiz_answer&) const = default;
};

struct quiz_clock_step {
    bool accepted = false;
    // Applied duration, clipped at frozen timeout.
    std::uint64_t elapsed_ms = 0;
    std::vector<quiz_answer> answers;
};

enum class session_end_reason { manual_finish, lives_exhausted, no_live_decks };

struct quiz_schedule_snapshot {
    std::vector<std::uint64_t> targets;
    std::size_t next_target = 0;
    std::optional<quiz_schedule_continuation> continuation;
    bool operator==(const quiz_schedule_snapshot&) const = default;
};

// Result context only: no current/lookahead card buffers or full checkpoint.
struct infinite_stream_position {
    std::uint64_t seed = 0;
    std::uint64_t chunk = 0;
    std::size_t decks_per_chunk = 0;
    bool operator==(const infinite_stream_position&) const = default;
};

struct deck_result {
    deck_id id;
    physical_slot_id slot;
    deck_configuration configuration;
    // active means active-at-finish, not failed/aborted.
    deck_status status = deck_status::active;
    std::array<int, 13> rank_weights {};
    std::int64_t initial_running_count = 0;
    std::int64_t final_running_count = 0;
    std::uint64_t dealt_physical_cards = 0;
    std::uint64_t unverified_normal_cards = 0;
    deck_statistics statistics;
    quiz_schedule_snapshot schedule;
    std::optional<infinite_stream_position> infinite_position;
    bool operator==(const deck_result&) const = default;
};

struct session_result {
    session_end_reason reason = session_end_reason::manual_finish;
    session_configuration configuration;
    session_statistics challenge;
    session_timing timing;
    int remaining_lives = 0;
    std::size_t active_decks = 0;
    std::size_t completed_decks = 0;
    std::size_t failed_decks = 0;
    // Identifies the documented absolute-target/Random half-guard policy.
    // Not a file/schema version or a user-selectable frequency control.
    std::uint32_t cadence_revision = 1;
    quiz_schedule_snapshot table_schedule;
    random_dealing_continuation random_dealing;
    std::vector<deck_result> decks;
    bool operator==(const session_result&) const = default;
};

} // namespace gameplay

#endif // KCUCKOOUNTER_TABLE_GAMEPLAY_TYPES_HPP
