#ifndef KCUCKOOUNTER_TABLE_GAMEPLAY_SESSION_HPP
#define KCUCKOOUNTER_TABLE_GAMEPLAY_SESSION_HPP

#include "table/gameplay_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Target gameplay domain, independent of widgets and legacy persistence.
namespace gameplay {

class session {
public:
    // IDs are zero-based, session-local creation identities, NOT widget IDs.
    // Creation installs a bijection; no live operation changes the roster size.
    [[nodiscard]] static std::optional<session> create(
        const session_configuration& configuration,
        const std::vector<deck_configuration>& decks, slot_limits limits
    );

    [[nodiscard]] const session_configuration& configuration() const;
    [[nodiscard]] session_phase phase() const;
    [[nodiscard]] int remaining_lives() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] slot_limits slot_constraints() const;
    // Setup-only environmental refresh. Never drops decks/changes the override
    // or preparation. A smaller recommendation may require explicit consent
    // before Start; a cap below the existing roster is rejected atomically.
    [[nodiscard]] bool update_slot_limits(slot_limits bounds);
    [[nodiscard]] const session_statistics& statistics() const;
    [[nodiscard]] const session_timing& timing() const;
    // Present exactly once after normal manual/automatic finish. Disk storage
    // and old progress migration remain G8; this is a plain domain snapshot.
    [[nodiscard]] const std::optional<session_result>& result() const;
    [[nodiscard]] const deck_state* deck(deck_id id) const;
    [[nodiscard]] std::optional<physical_slot_id> slot_for(deck_id id) const;
    [[nodiscard]] std::optional<deck_id> deck_at(physical_slot_id slot) const;

    // Accept Rothko's physical-index permutation, never a widget/deck order.
    // Invalid replacement is atomic. Repacking keeps the last selected
    // physical slot as anchor, even if the deck at that position has stopped.
    [[nodiscard]] bool set_traversal(std::span<const std::size_t> slot_indices);
    [[nodiscard]] std::span<const physical_slot_id> traversal() const;
    [[nodiscard]] std::vector<physical_slot_id> live_traversal() const;
    [[nodiscard]] std::optional<physical_slot_id> sequential_anchor() const;
    // Cursor selection only: no card/quiz advancement or score changes. Select
    // min(N, live_count) distinct positions after the anchor and remember the
    // last one. Non-running/non-Sequential sessions return empty unchanged.
    [[nodiscard]] std::vector<physical_slot_id> next_sequential_slots();
    // Advance cards and consume/create quizzes atomically. No challenge
    // scoring, wall clock, UI or persistence. Rejected steps have no reveals
    // and preserve deck payloads, traversal anchor and Random continuation.
    [[nodiscard]] dealing_step deal_step();
    [[nodiscard]] const random_dealing_continuation& random_dealing() const;

    [[nodiscard]] const std::optional<quiz_batch>& current_quiz_batch() const;
    [[nodiscard]] std::size_t next_table_quiz_target() const;
    [[nodiscard]] std::vector<deck_id> unresolved_quizzes() const;
    // Tab/Shift+Tab and post-answer focus use the same physical traversal.
    // With no anchor return first (last for reverse); wrap once, skip resolved.
    [[nodiscard]] std::optional<deck_id> quiz_focus(
        std::optional<deck_id> after = std::nullopt, bool reverse = false
    ) const;
    [[nodiscard]] bool edit_quiz_input(deck_id id, std::int64_t input);
    [[nodiscard]] std::optional<quiz_answer> check_quiz(deck_id id);
    [[nodiscard]] std::optional<quiz_answer> skip_quiz(deck_id id);
    // Caller supplies elapsed active time, not a wall-clock timestamp. Manual
    // Pause and non-frozen batches reject ticks. Timeout snapshots all inputs
    // before resolving any answer, with no extensions after expiration.
    [[nodiscard]] quiz_clock_step elapse_quiz(std::uint64_t elapsed_ms);
    // One owner-driven active-time slice. Accumulates play/per-deck dealing
    // and answer time, including non-global quizzes, and drives frozen timeout.
    // Do not call both this and elapse_quiz for the same elapsed interval.
    [[nodiscard]] quiz_clock_step advance_time(std::uint64_t elapsed_ms);

    static constexpr std::uint64_t quiz_initial_ms = 60'000;
    static constexpr std::uint64_t quiz_correct_ms = 20'000;
    static constexpr std::uint64_t quiz_wrong_ms = 10'000;
    static constexpr std::uint64_t quiz_skip_ms = 5'000;

    // Configuration is immutable after start, including while manually paused.
    // Rejected edits leave configuration AND prepared data untouched.
    [[nodiscard]] bool configure(const session_configuration& configuration);
    [[nodiscard]] bool
    configure_deck(deck_id id, const deck_configuration& configuration);
    [[nodiscard]] bool prepare_deck(deck_id id, const prepared_deck& prepared);
    // Install an entire preparation atomically without relocating deck records.
    // Nonempty table targets are only allowed for virtual Multi, not per deck.
    [[nodiscard]] bool prepare_decks(
        std::span<const prepared_deck> prepared,
        std::span<const std::uint64_t> table_targets = {},
        std::optional<quiz_schedule_continuation> continuation = std::nullopt
    );
    [[nodiscard]] std::span<const std::uint64_t> table_quiz_targets() const;
    [[nodiscard]] const std::optional<quiz_schedule_continuation>&
    table_quiz_continuation() const;
    // Readiness only, independent of the session's consumption cursor. Invalid
    // indices, inactive phases and non-virtual-Multi sessions return false.
    [[nodiscard]] bool
    multi_quiz_target_reached(std::size_t target_index) const;

    // Manual pause is not global quiz pause.
    [[nodiscard]] bool start(std::uint64_t dealing_seed = 0);
    [[nodiscard]] bool pause();
    [[nodiscard]] bool resume();
    [[nodiscard]] bool finish();

    // Swap exchanges mappings only; stopped decks retain their physical slots
    // and may also be swapped while manually paused (US-026..US-029).
    [[nodiscard]] bool swap_decks(deck_id first, deck_id second);
    [[nodiscard]] bool set_show_count(deck_id id, bool enabled);

    // Trusted engine transitions, not UI answer handlers. G4 proves natural
    // exhaustion; Check/timeout privately apply G6's answer penalties.
    [[nodiscard]] bool complete_deck(deck_id id);
    [[nodiscard]] bool fail_deck(deck_id id);

    static constexpr int minimum_pick_interval_ms = 100;
    static constexpr int maximum_pick_interval_ms = 1000;
    [[nodiscard]] int pick_interval_ms() const;
    [[nodiscard]] bool set_pick_interval_ms(int interval);

private:
    struct quiz_transition;
    struct progress_transition;
    // Consume the one checked numeric projection, never duplicate penalty or
    // terminal-status logic in scheduling and never copy shoes here.
    [[nodiscard]] std::optional<quiz_transition> plan_quizzes(
        std::span<const card_reveal> reveals,
        const progress_transition& progress
    ) const;
    void apply_quizzes(quiz_transition&& transition);
    [[nodiscard]] quiz_answer
    answer_snapshot(deck_id id, bool skip, bool timeout) const;
    [[nodiscard]] std::optional<progress_transition> plan_progress(
        std::span<const card_reveal> reveals = {},
        std::span<const quiz_answer> answers = {}, std::uint64_t elapsed_ms = 0,
        std::optional<deck_id> stopped = std::nullopt,
        deck_status stop_status = deck_status::completed,
        bool manual_finish = false
    ) const;
    void apply_progress(progress_transition&& transition);
    [[nodiscard]] std::optional<quiz_answer> answer_quiz(deck_id id, bool skip);
    session(
        const session_configuration& configuration,
        const std::vector<deck_configuration>& decks, slot_limits limits
    );
    [[nodiscard]] std::vector<physical_slot_id>
    select_sequential_slots(bool exclude_pending_quizzes) const;
    session_configuration settings;
    slot_limits limits;
    session_phase current_phase = session_phase::setup;
    int lives;
    int interval_ms = 300;
    std::vector<deck_state> decks;
    std::vector<physical_slot_id> deck_to_slot;
    std::vector<deck_id> slot_to_deck;
    std::vector<physical_slot_id> physical_order;
    std::vector<std::size_t> traversal_positions;
    std::optional<physical_slot_id> last_selected_slot;
    random_dealing_continuation random_dealing_state;
    std::vector<std::uint64_t> multi_quiz_targets;
    std::optional<quiz_schedule_continuation> multi_quiz_continuation;
    bool multi_schedule_prepared = false;
    std::size_t multi_quiz_cursor = 0;
    std::optional<quiz_batch> batch;
    std::uint64_t last_quiz_batch_id = 0;
    session_statistics challenge;
    session_timing active_timing;
    std::optional<session_result> completed_result;
};

} // namespace gameplay

#endif // KCUCKOOUNTER_TABLE_GAMEPLAY_SESSION_HPP
