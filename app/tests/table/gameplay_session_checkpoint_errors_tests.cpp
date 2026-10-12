// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/gameplay_session_tests.hpp"

#include "table/gameplay_checkpoint.hpp"
#include "table/gameplay_session_fixture.hpp"

#include <QtTest/QtTest>
#include <array>
#include <functional>
#include <limits>

using namespace gameplay;
using namespace gameplay_test;

void gameplay_session_tests::checkpoint_rejects_invalid_identity_and_streams() {
    auto model = ready_session();
    QVERIFY(model.start());
    const std::array<std::size_t, 4> order { 0, 1, 2, 3 };
    const auto saved = capture_checkpoint(model);
    QVERIFY(saved);
    const std::function<void(session_checkpoint&)> changes[] {
        [](auto& state) { ++state.continuation_revision; },
        [](auto& state) { state.decks.clear(); },
        [](auto& state) { state.decks[1].id.value = 0; },
        [](auto& state) { state.deck_to_slot.pop_back(); },
        [](auto& state) { state.deck_to_slot[0].value = 1; },
        [](auto& state) { state.deck_to_slot[0].value = 4; },
        [](auto& state) { state.sequential_anchor = physical_slot_id { 4 }; },
        [](auto& state) {
            state.configuration.failure = static_cast<failure_policy>(99);
        },
        [](auto& state) {
            state.configuration.source = static_cast<quiz_source>(99);
        },
        [](auto& state) {
            state.configuration.scope = static_cast<quiz_scope>(99);
        },
        [](auto& state) {
            state.configuration.dealing = static_cast<dealing_mode>(99);
        },
        [](auto& state) { state.configuration.initial_lives = 0; },
        [](auto& state) { state.configuration.sequential_count = 5; },
        [](auto& state) { state.configuration.sequential_count = 0; },
        [](auto& state) { state.decks[0].configuration.strategy_slug.clear(); },
        [](auto& state) { state.decks[0].configuration.deck_count = 0; },
        [](auto& state) { state.decks[0].stream.cards.clear(); },
        [](auto& state) { state.decks[0].stream.cards[0] = 52; },
        [](auto& state) { state.decks[0].next_card = 5; },
        [](auto& state) { ++state.decks[0].dealt_physical_cards; },
        [](auto& state) { ++state.decks[0].running_count; },
        [](auto& state) {
            state.decks[0].stream.virtual_quiz_targets = { 1, 1 };
        },
        [](auto& state) { state.decks[0].stream.virtual_quiz_targets = { 4 }; },
        [](auto& state) { state.decks[0].next_quiz_target = 1; },
        [](auto& state) { state.decks[3].stream.infinite_cards.reset(); },
        [](auto& state) {
            state.decks[3].stream.infinite_cards->decks_per_chunk = 2;
        },
        [](auto& state) {
            state.decks[3].stream.infinite_cards->lookahead.pop_back();
        },
        [](auto& state) {
            state.decks[3].stream.infinite_cards->lookahead[0] = 255;
        },
        [](auto& state) { state.decks[3].dealt_physical_cards = 1; },
        [](auto& state) {
            state.decks[3].dealt_physical_cards = 52;
        }, // Virtual reveals are all normal, even across older chunks.
        [](auto& state) { state.decks[3].stream.quiz_continuation.reset(); },
        [](auto& state) {
            state.decks[3].stream.quiz_continuation->minimum_gap = 0;
        },
        [](auto& state) {
            state.decks[3].stream.quiz_continuation->maximum_gap = 0;
        },
        [](auto& state) {
            state.decks[3].stream.quiz_continuation->page_size = 3;
        },
        [](auto& state) {
            state.decks[3].stream.quiz_continuation->next_page = 0;
        },
        [](auto& state) {
            ++state.decks[3].stream.quiz_continuation->after_target;
        },
        [](auto& state) {
            state.decks[3].stream.virtual_quiz_targets = { 2, 1 };
        },
        [](auto& state) { state.table_schedule.targets = { 1 }; },
        [](auto& state) { state.table_schedule.next_target = 1; },
        [](auto& state) {
            state.table_schedule.continuation = quiz_schedule_continuation {};
        },
    };
    for (const auto& change : changes) {
        auto corrupted = *saved;
        change(corrupted);
        const auto before = corrupted;
        QVERIFY(!restore_checkpoint(corrupted, { 4, 4 }, order));
        QVERIFY(corrupted == before);
        QVERIFY(capture_checkpoint(model) == saved);
    }
    QVERIFY(!restore_checkpoint(*saved, { 4, 3 }, order));
    QVERIFY(!restore_checkpoint(*saved, { 0, 4 }, order));
    QVERIFY(!restore_checkpoint(*saved, { 5, 4 }, order));
    QVERIFY(!restore_checkpoint(
        *saved, { 4, 4 }, std::array<std::size_t, 3> { 0, 1, 2 }
    ));
    QVERIFY(!restore_checkpoint(
        *saved, { 4, 4 }, std::array<std::size_t, 4> { 0, 1, 2, 2 }
    ));
    QVERIFY(!restore_checkpoint(
        *saved, { 4, 4 }, std::array<std::size_t, 4> { 0, 1, 2, 4 }
    ));
    QVERIFY(restore_checkpoint(*saved, { 4, 4 }, order));
}

void gameplay_session_tests::
    checkpoint_rejects_invalid_accounting_and_quizzes() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.scope = quiz_scope::multi;
    settings.dealing = dealing_mode::simultaneous;
    auto configured = configurations(3);
    configured[2].training = true;
    auto model = quiz_session(
        settings, { quiz_cards({}), quiz_cards({}), quiz_cards({}) },
        std::array<std::uint64_t, 2> { 1, 3 }, configured
    );
    QCOMPARE(model.advance_time(10).elapsed_ms, 10U);
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QCOMPARE(model.advance_time(100).elapsed_ms, 100U);
    QVERIFY(model.edit_quiz_input(deck_id { 0 }, 1));
    QVERIFY(model.check_quiz(deck_id { 0 }));
    const auto saved = capture_checkpoint(model);
    QVERIFY(saved);
    const std::array<std::size_t, 3> order { 0, 1, 2 };
    const std::function<void(session_checkpoint&)> changes[] {
        [](auto& state) {
            state.decks[0].status = static_cast<deck_status>(99);
        },
        [](auto& state) { state.decks[0].show_count = true; },
        [](auto& state) { state.decks[2].status = deck_status::failed; },
        [](auto& state) { state.decks[2].statistics.verified_cards = 1; },
        [](auto& state) { state.decks[0].statistics.verified_cards = 2; },
        [](auto& state) { state.decks[0].unverified_normal_cards = 1; },
        [](auto& state) { state.decks[0].statistics.dealt_normal_cards = 0; },
        [](auto& state) { ++state.challenge.verified_cards; },
        [](auto& state) { ++state.challenge.errors; },
        [](auto& state) { ++state.challenge.skips; },
        [](auto& state) { ++state.challenge.dealt_normal_cards; },
        [](auto& state) {
            state.decks[1].statistics.errors = UINT64_MAX;
            state.decks[0].statistics.errors = 1;
        },
        [](auto& state) {
            state.decks[1].statistics.skips = UINT64_MAX;
            state.decks[0].statistics.skips = 1;
        },
        [](auto& state) { state.timing.quiz_time_ms = 111; },
        [](auto& state) { state.decks[1].statistics.answer_time_ms = 101; },
        [](auto& state) { state.decks[1].statistics.dealing_time_ms = 111; },
        [](auto& state) { state.decks[1].statistics.dealing_time_ms = 11; },
        [](auto& state) { state.pickup_interval_ms = 99; },
        [](auto& state) { state.pickup_interval_ms = 1001; },
        [](auto& state) { state.remaining_lives = 0; },
        [](auto& state) { state.batch.reset(); },
        [](auto& state) { state.batch->id = 0; },
        [](auto& state) { ++state.last_batch_id; },
        [](auto& state) { state.batch->remaining_ms.reset(); },
        [](auto& state) { state.batch->remaining_ms = 0; },
        [](auto& state) { state.batch->remaining_ms = 120001; },
        [](auto& state) { ++state.decks[1].quiz->batch_id; },
        [](auto& state) { ++state.decks[1].quiz->expected_count; },
        [](auto& state) { ++state.decks[1].quiz->input; },
        [](auto& state) { state.decks[1].status = deck_status::completed; },
        [](auto& state) {
            state.decks[1].quiz.reset();
            state.decks[2].quiz.reset();
        },
        [](auto& state) { state.table_schedule.next_target = 3; },
        [](auto& state) { state.table_schedule.targets = { 1, 1 }; },
        [](auto& state) { state.table_schedule.targets = { 8 }; },
        [](auto& state) {
            state.table_schedule.continuation = quiz_schedule_continuation {};
        },
    };
    for (const auto& change : changes) {
        auto corrupted = *saved;
        change(corrupted);
        QVERIFY(!restore_checkpoint(corrupted, { 3, 3 }, order));
        QVERIFY(capture_checkpoint(model) == saved);
    }
    // Non-global batches may accumulate prompts, but cannot have a countdown.
    auto continuous = *saved;
    continuous.configuration.global_quiz_pause = false;
    QVERIFY(!restore_checkpoint(continuous, { 3, 3 }, order));
    continuous.batch->remaining_ms.reset();
    QVERIFY(restore_checkpoint(continuous, { 3, 3 }, order));
    // Failed decks are Block-only even when they have no pending prompt.
    continuous.decks[0].status = deck_status::failed;
    continuous.configuration.failure = failure_policy::lives;
    continuous.configuration.global_quiz_pause = true;
    continuous.batch->remaining_ms = 1000;
    QVERIFY(!restore_checkpoint(continuous, { 3, 3 }, order));
}

void gameplay_session_tests::checkpoint_budgets_include_all_buffers() {
    auto model = ready_session();
    QVERIFY(model.start());
    const std::array<std::size_t, 4> order { 0, 1, 2, 3 };
    generation_policy policy;
    policy.maximum_cards
        = 116; // Three finite 4-card + 52 current + 52 lookahead.
    policy.maximum_targets = 2; // One infinite Single page.
    const auto saved = capture_checkpoint(model, policy);
    QVERIFY(saved && restore_checkpoint(*saved, { 4, 4 }, order, policy));
    --policy.maximum_cards;
    QVERIFY(!capture_checkpoint(model, policy));
    QVERIFY(!restore_checkpoint(*saved, { 4, 4 }, order, policy));
    ++policy.maximum_cards;
    --policy.maximum_targets;
    QVERIFY(!capture_checkpoint(model, policy));
    QVERIFY(!restore_checkpoint(*saved, { 4, 4 }, order, policy));
    policy.maximum_targets = 0;
    QVERIFY(!capture_checkpoint(model, policy));
    QVERIFY(!restore_checkpoint(*saved, { 4, 4 }, order, policy));
    session_configuration settings;
    settings.scope = quiz_scope::multi;
    auto multi = quiz_session(
        settings, { quiz_cards({}), quiz_cards({}) },
        std::array<std::uint64_t, 2> { 1, 3 }
    );
    policy = {};
    policy.maximum_cards = 16;
    policy.maximum_targets = 2;
    QVERIFY(capture_checkpoint(multi, policy));
    policy.maximum_targets = 1;
    QVERIFY(!capture_checkpoint(multi, policy));
    QVERIFY(capture_checkpoint(model) == saved);
}

void gameplay_session_tests::checkpoint_preserves_arithmetic_limit_behavior() {
    const std::array<std::size_t, 1> order { 0 };
    auto model = quiz_session({}, { cards(INT64_MAX) });
    const auto saved = capture_checkpoint(model);
    QVERIFY(saved);
    auto restored = restore_checkpoint(*saved, { 1, 1 }, order);
    QVERIFY(restored && restored->resume());
    QCOMPARE(model.deal_step().status, dealing_step_status::arithmetic_limit);
    QCOMPARE(
        restored->deal_step().status, dealing_step_status::arithmetic_limit
    );
    QVERIFY(capture_checkpoint(*restored) == saved);
    auto random = *saved;
    random.configuration.dealing = dealing_mode::random;
    random.random_dealing.next_step = UINT64_MAX;
    restored = restore_checkpoint(random, { 1, 1 }, order);
    QVERIFY(restored && restored->resume());
    QCOMPARE(
        restored->deal_step().status, dealing_step_status::arithmetic_limit
    );
    QVERIFY(capture_checkpoint(*restored) == random);
    auto time_limit = *saved;
    time_limit.timing.play_time_ms = UINT64_MAX;
    restored = restore_checkpoint(time_limit, { 1, 1 }, order);
    QVERIFY(restored && restored->resume());
    QVERIFY(!restored->advance_time(1).accepted);
    QVERIFY(capture_checkpoint(*restored) == time_limit);
    auto batch_limit
        = capture_checkpoint(quiz_session({}, { quiz_cards() })).value();
    batch_limit.last_batch_id = UINT64_MAX;
    restored = restore_checkpoint(batch_limit, { 1, 1 }, order);
    QVERIFY(restored && restored->resume());
    QCOMPARE(
        restored->deal_step().status, dealing_step_status::quiz_schedule_limit
    );
    QVERIFY(capture_checkpoint(*restored) == batch_limit);
    auto infinite_model = ready_session();
    QVERIFY(infinite_model.start());
    auto chunk_limit = capture_checkpoint(infinite_model).value();
    auto& exhausted = chunk_limit.decks[3];
    exhausted.stream.infinite_cards->chunk = UINT64_MAX;
    exhausted.next_card = exhausted.stream.cards.size();
    exhausted.dealt_physical_cards = exhausted.next_card;
    exhausted.statistics.dealt_normal_cards = exhausted.next_card;
    exhausted.unverified_normal_cards = exhausted.next_card;
    exhausted.stream.virtual_quiz_targets = { 100, 101 };
    exhausted.stream.quiz_continuation->after_target = 101;
    chunk_limit.challenge.dealt_normal_cards = exhausted.next_card;
    chunk_limit.sequential_anchor = physical_slot_id { 2 };
    restored = restore_checkpoint(
        chunk_limit, { 4, 4 }, std::array<std::size_t, 4> { 0, 1, 2, 3 }
    );
    QVERIFY(restored && restored->resume());
    QCOMPARE(restored->deal_step().status, dealing_step_status::invalid_stream);
    QVERIFY(capture_checkpoint(*restored) == chunk_limit);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
