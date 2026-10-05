// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/gameplay_session_tests.hpp"

#include "packing/layout/equal_rectangles.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "table/gameplay_cadence.hpp"
#include "table/gameplay_generation.hpp"
#include "table/gameplay_layout.hpp"
#include "table/gameplay_session.hpp"
#include "table/gameplay_strategy.hpp"

#include <QtTest/QtTest>
#include <array>
#include <limits>

#include "table/gameplay_session_fixture.hpp"

using namespace gameplay;
using namespace gameplay_test;

void gameplay_session_tests::single_quiz_freezes_triggered_decks_or_table() {
    for (const bool global_pause : { false, true }) {
        session_configuration settings;
        settings.failure = failure_policy::block;
        settings.dealing = dealing_mode::simultaneous;
        settings.global_quiz_pause = global_pause;
        auto model = quiz_session(
            settings,
            { quiz_cards({ 1 }), quiz_cards({ 1 }), quiz_cards({ 3 }) }
        );
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
        QVERIFY((
            model.unresolved_quizzes() == std::vector<deck_id> { { 0 }, { 1 } }
        ));
        const auto original = deck_payloads(model);
        const auto batch = model.current_quiz_batch();
        QVERIFY(batch);
        QCOMPARE(batch->remaining_ms.has_value(), global_pause);
        const auto step = model.deal_step();
        if (global_pause) {
            QCOMPARE(step.status, dealing_step_status::quiz_blocked);
            QVERIFY(deck_payloads(model) == original);
            QCOMPARE(*batch->remaining_ms, session::quiz_initial_ms);
        } else {
            QCOMPARE(step.status, dealing_step_status::advanced);
            QCOMPARE(step.cards.size(), 1U);
            QVERIFY(step.cards.front().owner == deck_id { 2 });
            QVERIFY(*model.deck(deck_id { 0 }) == original[0]);
            QVERIFY(*model.deck(deck_id { 1 }) == original[1]);
        }
        QCOMPARE(model.deck(deck_id { 0 })->next_quiz_target, 1U);
        QCOMPARE(model.deck(deck_id { 1 })->next_quiz_target, 1U);
        QVERIFY(model.skip_quiz(deck_id { 0 }));
        QVERIFY(model.skip_quiz(deck_id { 1 }));
        QVERIFY(!model.current_quiz_batch());
    }
}

void gameplay_session_tests::
    quiz_single_accumulates_prompts_without_a_countdown() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.global_quiz_pause = false;
    settings.dealing = dealing_mode::simultaneous;
    auto model = quiz_session(
        settings, { quiz_cards({ 1, 2 }), quiz_cards({ 2 }), quiz_cards({ 3 }) }
    );
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    const auto first = model.current_quiz_batch()->id;
    QCOMPARE(model.unresolved_quizzes().size(), 1U);
    QCOMPARE(model.deal_step().cards.size(), 2U);
    QCOMPARE(model.unresolved_quizzes().size(), 2U);
    QCOMPARE(model.current_quiz_batch()->id, first);
    QVERIFY(!model.current_quiz_batch()->remaining_ms);
    QVERIFY(!model.elapse_quiz(1'000'000).accepted);
    QVERIFY(model.edit_quiz_input(deck_id { 0 }, 9));
    QVERIFY(model.skip_quiz(deck_id { 0 }));
    QCOMPARE(model.deal_step().cards.size(), 2U);
    QVERIFY(
        (model.unresolved_quizzes()
         == std::vector<deck_id> { { 0 }, { 1 }, { 2 } })
    );
    QCOMPARE(model.deck(deck_id { 0 })->quiz->input, 9);
    QCOMPARE(model.deck(deck_id { 0 })->quiz->batch_id, first);
    QCOMPARE(model.deck(deck_id { 1 })->dealt_physical_cards, 2U);
    QCOMPARE(model.deal_step().status, dealing_step_status::no_eligible_decks);
    for (const auto id : model.unresolved_quizzes()) {
        QVERIFY(model.skip_quiz(id));
    }
    QVERIFY(!model.current_quiz_batch());
}

void gameplay_session_tests::
    multi_joker_snapshots_live_decks_including_training() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.source = quiz_source::physical_joker;
    settings.scope = quiz_scope::multi;
    settings.dealing = dealing_mode::simultaneous;
    auto normal = cards(10);
    normal.cards = { 0, 0, 0 };
    auto joker = cards(-5);
    joker.cards = { 52, 0, 53 };
    auto final = cards(20);
    final.cards = { 53 };
    auto configured = configurations(4);
    configured[1].training = true;
    auto model = quiz_session(
        settings, { normal, joker, final, normal }, {}, configured
    );
    QVERIFY(model.fail_deck(deck_id { 3 }));
    const auto step = model.deal_step();
    QCOMPARE(step.cards.size(), 3U);
    QCOMPARE(model.deck(deck_id { 2 })->status, deck_status::completed);
    QVERIFY(
        (model.unresolved_quizzes() == std::vector<deck_id> { { 0 }, { 1 } })
    );
    QCOMPARE(model.deck(deck_id { 0 })->quiz->expected_count, 11);
    QCOMPARE(model.deck(deck_id { 1 })->quiz->expected_count, -5);
    QCOMPARE(model.deck(deck_id { 1 })->unverified_normal_cards, 0U);
    const auto batch = model.current_quiz_batch()->id;
    QVERIFY(model.edit_quiz_input(deck_id { 0 }, 11));
    auto correct = model.check_quiz(deck_id { 0 });
    QVERIFY(correct);
    QCOMPARE(correct->outcome, quiz_outcome::correct);
    QCOMPARE(correct->normal_cards, 1U);
    const auto wrong = model.check_quiz(deck_id { 1 });
    QVERIFY(wrong);
    QCOMPARE(wrong->outcome, quiz_outcome::wrong);
    QCOMPARE(wrong->expected_count, -5);
    QCOMPARE(wrong->normal_cards, 0U);
    QCOMPARE(wrong->batch_id, batch);
    QVERIFY(!model.current_quiz_batch());
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    // Both final cards complete their owners. No final Joker batch is invented.
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(!model.current_quiz_batch());

    // A final-card Joker alone does not start a new Multi quiz for survivors.
    auto final_only = quiz_session(settings, { final, normal });
    QCOMPARE(final_only.deal_step().status, dealing_step_status::advanced);
    QCOMPARE(final_only.deck(deck_id { 0 })->status, deck_status::completed);
    QCOMPARE(final_only.deck(deck_id { 1 })->status, deck_status::active);
    QVERIFY(!final_only.current_quiz_batch());
}

void gameplay_session_tests::
    non_global_multi_quiz_coalesces_overlapping_targets() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.scope = quiz_scope::multi;
    settings.global_quiz_pause = false;
    settings.dealing = dealing_mode::random;
    const auto prepared = quiz_cards({});
    auto model = quiz_session(
        settings, { prepared, prepared },
        std::array<std::uint64_t, 3> { 1, 2, 3 }
    );
    for (int attempts = 0; attempts < 32 && !model.current_quiz_batch();
         ++attempts) {
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    }
    QVERIFY(model.current_quiz_batch());
    const auto batch = model.current_quiz_batch()->id;
    QVERIFY(!model.current_quiz_batch()->remaining_ms);
    const auto slow = model.deck(deck_id { 0 })->dealt_physical_cards
            <= model.deck(deck_id { 1 })->dealt_physical_cards
        ? deck_id { 0 }
        : deck_id { 1 };
    const auto fast = deck_id { 1 - slow.value };
    const auto frozen = *model.deck(slow);
    QVERIFY(model.skip_quiz(fast));
    while (model.deck(fast)->dealt_physical_cards < 7) {
        const auto step = model.deal_step();
        QCOMPARE(step.status, dealing_step_status::advanced);
        QVERIFY(step.cards.front().owner == fast);
        QVERIFY(*model.deck(slow) == frozen);
        QVERIFY(!model.deck(fast)->quiz);
        QCOMPARE(model.current_quiz_batch()->id, batch);
    }
    QVERIFY(model.next_table_quiz_target() >= 2);
    QVERIFY(model.skip_quiz(slow));
    QVERIFY(!model.current_quiz_batch());
    // The already-reached target cannot immediately re-open the old batch.
    const auto cursor = model.next_table_quiz_target();
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    if (model.current_quiz_batch()) {
        QVERIFY(model.current_quiz_batch()->id > batch);
        QVERIFY(model.next_table_quiz_target() > cursor);
    }
}

void gameplay_session_tests::
    quiz_multi_reevaluates_after_terminal_transitions() {
    for (const bool fail : { false, true }) {
        session_configuration settings;
        settings.failure = failure_policy::block;
        settings.scope = quiz_scope::multi;
        const auto prepared = quiz_cards({});
        auto model = quiz_session(
            settings, { prepared, prepared },
            std::array<std::uint64_t, 2> { 1, 2 }
        );
        QCOMPARE(model.deal_step().cards.front().owner.value, 0U);
        QVERIFY(!model.current_quiz_batch());
        QVERIFY(
            fail ? model.fail_deck(deck_id { 1 })
                 : model.complete_deck(deck_id { 1 })
        );
        QVERIFY((model.unresolved_quizzes() == std::vector<deck_id> { { 0 } }));
        QCOMPARE(model.next_table_quiz_target(), 1U);
        const auto batch = model.current_quiz_batch()->id;
        QVERIFY(model.edit_quiz_input(
            deck_id { 0 }, model.deck(deck_id { 0 })->quiz->expected_count
        ));
        QVERIFY(model.check_quiz(deck_id { 0 }));
        const auto next = model.deal_step();
        QCOMPARE(next.status, dealing_step_status::advanced);
        QCOMPARE(next.cards.front().owner.value, 0U);
        QCOMPARE(model.current_quiz_batch()->id, batch + 1);
        QCOMPARE(model.next_table_quiz_target(), 2U);
        QVERIFY(model.skip_quiz(deck_id { 0 }));
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
        QVERIFY(!model.current_quiz_batch());
        QCOMPARE(model.next_table_quiz_target(), 2U);
    }
}

void gameplay_session_tests::
    infinite_single_pages_cross_chunks_without_reset() {
    session_configuration settings;
    auto configured = configurations(1);
    configured[0].infinite = true;
    auto model = session::create(settings, configured, { 1, 64 }).value();
    QVERIFY(model.prepare_deck(
        deck_id { 0 }, prepared_cards_for(model, deck_id { 0 }, 7)
    ));
    QVERIFY(model.set_traversal(std::array<std::size_t, 1> { 0 }));
    QVERIFY(model.start());
    for (std::uint64_t exposure = 1; exposure <= 120; ++exposure) {
        const auto step = model.deal_step();
        QCOMPARE(step.status, dealing_step_status::advanced);
        const auto& record = *model.deck(deck_id { 0 });
        QCOMPARE(record.dealt_physical_cards, exposure);
        QCOMPARE(record.unverified_normal_cards, 1U);
        QCOMPARE(record.next_quiz_target, (exposure - 1) % 2 + 1);
        QCOMPARE(record.stream.virtual_quiz_targets.size(), 2U);
        QCOMPARE(
            record.stream.quiz_continuation->next_page, (exposure - 1) / 2 + 1
        );
        QCOMPARE(
            record.stream.virtual_quiz_targets.back(),
            ((exposure - 1) / 2 + 1) * 2
        );
        QCOMPARE(record.stream.infinite_cards->chunk, (exposure - 1) / 52);
        QCOMPARE(record.quiz->expected_count, record.running_count);
        QCOMPARE(record.quiz->batch_id, exposure);
        const auto before = deck_payloads(model);
        QCOMPARE(model.deal_step().status, dealing_step_status::quiz_blocked);
        QVERIFY(deck_payloads(model) == before);
        QVERIFY(model.edit_quiz_input(deck_id { 0 }, record.running_count));
        const auto answer = model.check_quiz(deck_id { 0 });
        QVERIFY(answer);
        QCOMPARE(answer->outcome, quiz_outcome::correct);
        QCOMPARE(answer->normal_cards, 1U);
        QCOMPARE(model.deck(deck_id { 0 })->unverified_normal_cards, 0U);
    }
    auto replay = model;
    for (int i = 0; i < 8; ++i) {
        QVERIFY(model.deal_step().cards == replay.deal_step().cards);
        QVERIFY(
            model.skip_quiz(deck_id { 0 }) == replay.skip_quiz(deck_id { 0 })
        );
        QVERIFY(deck_payloads(model) == deck_payloads(replay));
        QVERIFY(model.current_quiz_batch() == replay.current_quiz_batch());
    }
}

void gameplay_session_tests::infinite_multi_pages_remain_table_owned_bounded() {
    for (const auto mode :
         { dealing_mode::sequential, dealing_mode::simultaneous }) {
        session_configuration settings;
        settings.scope = quiz_scope::multi;
        settings.dealing = mode;
        settings.sequential_count = 2;
        auto configured = configurations(2);
        configured[0].infinite = true;
        configured[1].infinite = true;
        configured[1].training = true;
        auto model = session::create(settings, configured, { 2, 64 }).value();
        const auto first = prepared_cards_for(model, deck_id { 0 });
        const auto second = prepared_cards_for(model, deck_id { 1 });
        const auto page = generate_quiz_page(
                              { .seed = 42,
                                .minimum_gap = 1,
                                .maximum_gap = 1,
                                .page_size = 2 },
                              std::nullopt
        )
                              .value();
        QVERIFY(model.prepare_decks(
            std::array { first, second }, page.targets, page.continuation
        ));
        QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 1, 0 }));
        QVERIFY(model.start());
        for (std::uint64_t exposure = 1; exposure <= 108; ++exposure) {
            QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
            QCOMPARE(model.unresolved_quizzes().size(), 2U);
            QCOMPARE(model.next_table_quiz_target(), (exposure - 1) % 2 + 1);
            QCOMPARE(model.table_quiz_targets().size(), 2U);
            QCOMPARE(
                model.table_quiz_continuation()->next_page,
                (exposure - 1) / 2 + 1
            );
            for (const auto id : model.unresolved_quizzes()) {
                const auto& record = *model.deck(id);
                QVERIFY(record.stream.virtual_quiz_targets.empty());
                QVERIFY(!record.stream.quiz_continuation);
                QCOMPARE(record.quiz->batch_id, exposure);
                QCOMPARE(
                    record.stream.infinite_cards->chunk, (exposure - 1) / 52
                );
                QVERIFY(model.skip_quiz(id));
            }
        }
    }
}

void gameplay_session_tests::quiz_multi_live_set_catchup_coalesces_pages() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.scope = quiz_scope::multi;
    settings.global_quiz_pause = false;
    settings.dealing = dealing_mode::random;
    auto configured = configurations(2);
    configured[0].infinite = true;
    configured[1].infinite = true;
    auto model = session::create(settings, configured, { 2, 64 }).value();
    const auto page
        = generate_quiz_page(
              { .minimum_gap = 1, .maximum_gap = 1, .page_size = 2 },
              std::nullopt
        )
              .value();
    QVERIFY(model.prepare_decks(
        std::array { prepared_cards_for(model, deck_id { 0 }),
                     prepared_cards_for(model, deck_id { 1 }) },
        page.targets, page.continuation
    ));
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    QVERIFY(model.start(1234));
    for (int attempt = 0; attempt < 32 && !model.current_quiz_batch();
         ++attempt) {
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    }
    QVERIFY(model.current_quiz_batch());
    const auto old_batch = model.current_quiz_batch()->id;
    const auto closed_segment
        = model.deck(deck_id { 0 })->unverified_normal_cards;
    QVERIFY(model.skip_quiz(deck_id { 0 }));
    const auto frozen = *model.deck(deck_id { 1 });
    while (model.deck(deck_id { 0 })->dealt_physical_cards < 130) {
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
        QVERIFY(*model.deck(deck_id { 1 }) == frozen);
        QVERIFY(!model.deck(deck_id { 0 })->quiz);
    }
    // Removing the laggard makes many absolute targets ready at once. Keep
    // only the current page and ask once, not one question per missed target.
    QVERIFY(model.fail_deck(deck_id { 1 }));
    QVERIFY((model.unresolved_quizzes() == std::vector<deck_id> { { 0 } }));
    QCOMPARE(model.current_quiz_batch()->id, old_batch + 1);
    QCOMPARE(model.table_quiz_targets().size(), 2U);
    QCOMPARE(model.table_quiz_targets().back(), 130U);
    QCOMPARE(model.table_quiz_continuation()->next_page, 65U);
    QCOMPARE(model.next_table_quiz_target(), 2U);
    QCOMPARE(model.deck(deck_id { 0 })->stream.infinite_cards->chunk, 2U);
    const auto answer = model.skip_quiz(deck_id { 0 });
    QVERIFY(answer);
    QCOMPARE(answer->normal_cards, 130U - closed_segment);
    QVERIFY(!model.current_quiz_batch());
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QCOMPARE(model.current_quiz_batch()->id, old_batch + 2);
    QCOMPARE(model.deck(deck_id { 0 })->dealt_physical_cards, 131U);
    QCOMPARE(model.table_quiz_targets().size(), 2U);
}

void gameplay_session_tests::quiz_page_failure_rejects_whole_dealing_step() {
    session_configuration settings;
    settings.dealing = dealing_mode::simultaneous;
    auto configured = configurations(2);
    configured[0].infinite = true;
    configured[1].infinite = true;
    auto model = session::create(settings, configured, { 2, 64 }).value();
    for (std::size_t i = 0; i < 2; ++i) {
        auto prepared = prepared_cards_for(model, deck_id { i });
        if (i == 1) {
            prepared.quiz_continuation->next_page
                = std::numeric_limits<std::uint64_t>::max();
        }
        QVERIFY(model.prepare_deck(deck_id { i }, prepared));
    }
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    QVERIFY(model.start());
    for (int tick = 0; tick < 2; ++tick) {
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
        for (const auto id : model.unresolved_quizzes()) {
            QVERIFY(model.skip_quiz(id));
        }
    }
    const auto before = deck_payloads(model);
    const auto random = model.random_dealing();
    const auto batch = model.current_quiz_batch();
    for (int retry = 0; retry < 2; ++retry) {
        const auto step = model.deal_step();
        QCOMPARE(step.status, dealing_step_status::quiz_schedule_limit);
        QVERIFY(step.cards.empty());
        QVERIFY(deck_payloads(model) == before);
        QVERIFY(model.random_dealing() == random);
        QVERIFY(model.current_quiz_batch() == batch);
        QVERIFY(!model.sequential_anchor());
    }
}

void gameplay_session_tests::
    countdown_extensions_reset_batches_including_training() {
    for (const bool training : { false, true }) {
        session_configuration settings;
        settings.dealing = dealing_mode::simultaneous;
        auto configured = configurations(4);
        for (auto& deck : configured) {
            deck.training = training;
        }
        const auto prepared = quiz_cards();
        auto model = quiz_session(
            settings, { prepared, prepared, prepared, prepared }, {}, configured
        );
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
        QCOMPARE(*model.current_quiz_batch()->remaining_ms, 60'000U);
        QVERIFY(model.elapse_quiz(30'000).accepted);
        QVERIFY(model.edit_quiz_input(deck_id { 0 }, 1));
        QCOMPARE(
            model.check_quiz(deck_id { 0 })->outcome, quiz_outcome::correct
        );
        QCOMPARE(*model.current_quiz_batch()->remaining_ms, 50'000U);
        QVERIFY(model.edit_quiz_input(deck_id { 1 }, 9));
        QCOMPARE(model.check_quiz(deck_id { 1 })->outcome, quiz_outcome::wrong);
        QCOMPARE(*model.current_quiz_batch()->remaining_ms, 60'000U);
        QVERIFY(model.edit_quiz_input(deck_id { 2 }, 7));
        QCOMPARE(
            model.skip_quiz(deck_id { 2 })->outcome, quiz_outcome::skipped
        );
        QCOMPARE(*model.current_quiz_batch()->remaining_ms, 65'000U);
        QVERIFY(model.skip_quiz(deck_id { 3 }));
        QVERIFY(!model.current_quiz_batch());
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
        QVERIFY(!model.current_quiz_batch());
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
        QCOMPARE(*model.current_quiz_batch()->remaining_ms, 60'000U);
        QCOMPARE(model.current_quiz_batch()->id, 2U);
        QCOMPARE(model.deck(deck_id { 0 })->quiz->input, 1);
        QCOMPARE(model.deck(deck_id { 1 })->quiz->input, 9);
        QCOMPARE(model.deck(deck_id { 2 })->quiz->input, 7);
        QCOMPARE(model.deck(deck_id { 3 })->quiz->input, 0);
        QCOMPARE(model.deck(deck_id { 3 })->unverified_normal_cards, 2U);
    }
}

void gameplay_session_tests::
    quiz_manual_pause_preserves_inputs_and_countdown() {
    session_configuration settings;
    settings.scope = quiz_scope::multi;
    settings.dealing = dealing_mode::simultaneous;
    const auto prepared = quiz_cards({});
    auto model = quiz_session(
        settings, { prepared, prepared }, std::array<std::uint64_t, 1> { 1 }
    );
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(model.edit_quiz_input(deck_id { 0 }, -8));
    QVERIFY(model.edit_quiz_input(deck_id { 1 }, 1));
    QVERIFY(model.elapse_quiz(47'655).accepted);
    const auto before = deck_payloads(model);
    const auto batch = model.current_quiz_batch();
    QVERIFY(model.pause());
    for (const auto id : { deck_id { 0 }, deck_id { 1 } }) {
        QVERIFY(!model.edit_quiz_input(id, 77));
        QVERIFY(!model.check_quiz(id));
        QVERIFY(!model.skip_quiz(id));
    }
    QVERIFY(
        !model.elapse_quiz(std::numeric_limits<std::uint64_t>::max()).accepted
    );
    QCOMPARE(model.deal_step().status, dealing_step_status::not_running);
    QVERIFY(deck_payloads(model) == before);
    QVERIFY(model.current_quiz_batch() == batch);
    QVERIFY(model.resume());
    QCOMPARE(*model.current_quiz_batch()->remaining_ms, 12'345U);
    QVERIFY(model.elapse_quiz(12'344).answers.empty());
    QCOMPARE(*model.current_quiz_batch()->remaining_ms, 1U);
    const auto timeout = model.elapse_quiz(1);
    QVERIFY(timeout.accepted);
    QCOMPARE(timeout.answers.size(), 2U);
    QCOMPARE(timeout.answers[0].submitted_count, -8);
    QCOMPARE(timeout.answers[1].submitted_count, 1);
}

void gameplay_session_tests::
    quiz_timeout_snapshots_normal_answers_without_resurrection() {
    session_configuration settings;
    settings.dealing = dealing_mode::simultaneous;
    auto configured = configurations(4);
    configured[3].training = true;
    const auto prepared = quiz_cards({ 1, 2 });
    auto model = quiz_session(
        settings, { prepared, prepared, prepared, prepared }, {}, configured
    );
    QVERIFY(model.set_traversal(std::array<std::size_t, 4> { 2, 0, 3, 1 }));
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(model.edit_quiz_input(deck_id { 0 }, 1));
    QVERIFY(model.edit_quiz_input(deck_id { 1 }, 77));
    QVERIFY(model.edit_quiz_input(deck_id { 2 }, 1));
    const auto result
        = model.elapse_quiz(std::numeric_limits<std::uint64_t>::max());
    QVERIFY(result.accepted);
    QCOMPARE(result.answers.size(), 4U);
    const std::array<std::size_t, 4> order { 2, 0, 3, 1 };
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto& answer = result.answers[i];
        QCOMPARE(answer.owner.value, order[i]);
        QCOMPARE(answer.batch_id, 1U);
        QVERIFY(answer.timed_out);
        QCOMPARE(answer.expected_count, 1);
        QCOMPARE(answer.normal_cards, 1U);
        QCOMPARE(
            answer.outcome,
            order[i] < 3 && order[i] != 1 ? quiz_outcome::correct
                                          : quiz_outcome::wrong
        );
        QCOMPARE(
            model.deck(answer.owner)->latest_input, answer.submitted_count
        );
        QVERIFY(!model.edit_quiz_input(answer.owner, 90));
        QVERIFY(!model.check_quiz(answer.owner));
        QVERIFY(!model.skip_quiz(answer.owner));
        QCOMPARE(model.deck(answer.owner)->unverified_normal_cards, 0U);
    }
    QVERIFY(!model.current_quiz_batch());
    QVERIFY(!model.elapse_quiz(1).accepted);
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QCOMPARE(model.current_quiz_batch()->id, 2U);
    QCOMPARE(*model.current_quiz_batch()->remaining_ms, 60'000U);
}

void gameplay_session_tests::
    quiz_memory_and_correction_preserve_latest_edits() {
    session_configuration settings;
    auto prepared = quiz_cards({ 1, 2, 3, 4 });
    prepared.initial_running_count = 100;
    auto model = quiz_session(settings, { prepared });
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(model.edit_quiz_input(deck_id { 0 }, 7));
    QVERIFY(model.edit_quiz_input(deck_id { 0 }, 9));
    const auto skipped = model.skip_quiz(deck_id { 0 });
    QVERIFY(skipped);
    QCOMPARE(skipped->expected_count, 101); // Full accumulated count, not +1.
    QCOMPARE(skipped->submitted_count, 9);
    QCOMPARE(model.deck(deck_id { 0 })->latest_input, 9);
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QCOMPARE(model.deck(deck_id { 0 })->quiz->input, 9);
    const auto wrong = model.check_quiz(deck_id { 0 });
    QVERIFY(wrong);
    QCOMPARE(wrong->outcome, quiz_outcome::wrong);
    QCOMPARE(wrong->expected_count, 102);
    QCOMPARE(model.deck(deck_id { 0 })->latest_input, 9);
    QVERIFY(!model.check_quiz(deck_id { 0 }));
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QCOMPARE(model.deck(deck_id { 0 })->quiz->input, 9);
    QVERIFY(model.edit_quiz_input(deck_id { 0 }, 103));
    QCOMPARE(model.check_quiz(deck_id { 0 })->outcome, quiz_outcome::correct);
    QCOMPARE(model.deck(deck_id { 0 })->latest_input, 103);
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QCOMPARE(model.deck(deck_id { 0 })->quiz->input, 103);
}

void gameplay_session_tests::quiz_focus_and_swap_follow_physical_traversal() {
    session_configuration settings;
    settings.dealing = dealing_mode::simultaneous;
    auto a = quiz_cards(), b = quiz_cards(), c = quiz_cards();
    a.initial_running_count = 10;
    b.initial_running_count = 20;
    c.initial_running_count = 30;
    auto model = quiz_session(settings, { a, b, c });
    QVERIFY(model.set_traversal(std::array<std::size_t, 3> { 2, 0, 1 }));
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(model.quiz_focus() == deck_id { 2 });
    QVERIFY(model.quiz_focus(std::nullopt, true) == deck_id { 1 });
    QVERIFY(model.quiz_focus(deck_id { 2 }) == deck_id { 0 });
    QVERIFY(model.quiz_focus(deck_id { 2 }, true) == deck_id { 1 });
    QVERIFY(!model.quiz_focus(deck_id { 99 }));
    for (std::size_t i = 0; i < 3; ++i) {
        QVERIFY(model.edit_quiz_input(
            deck_id { i }, static_cast<std::int64_t>(100 + i)
        ));
    }
    const auto before = deck_payloads(model);
    const auto batch = model.current_quiz_batch();
    QVERIFY(model.pause());
    QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 2 }));
    QVERIFY(deck_payloads(model) == before);
    QVERIFY(model.current_quiz_batch() == batch);
    QVERIFY(
        (model.unresolved_quizzes()
         == std::vector<deck_id> { { 0 }, { 2 }, { 1 } })
    );
    QVERIFY(model.set_traversal(std::array<std::size_t, 3> { 1, 0, 2 }));
    QVERIFY(
        (model.unresolved_quizzes()
         == std::vector<deck_id> { { 1 }, { 2 }, { 0 } })
    );
    QVERIFY(model.resume());
    QVERIFY(model.quiz_focus() == deck_id { 1 });
    QVERIFY(model.check_quiz(deck_id { 1 }));
    QVERIFY(model.quiz_focus(deck_id { 1 }) == deck_id { 2 });
    QVERIFY(model.skip_quiz(deck_id { 2 }));
    QVERIFY(model.quiz_focus(deck_id { 2 }) == deck_id { 0 });
    QVERIFY(model.quiz_focus(deck_id { 2 }, true) == deck_id { 0 });
    QVERIFY(model.skip_quiz(deck_id { 0 }));
    QVERIFY(!model.quiz_focus());
    QVERIFY(model.unresolved_quizzes().empty());

    // Exercise populated questions through the actual application-local
    // geometry seam as well, not only a replacement traversal permutation.
    auto geometry_game = quiz_session(settings, { a, b, c, a });
    layout_transition geometry(geometry_game);
    QVERIFY(geometry.repack(layout(800.0, 600.0)));
    QCOMPARE(geometry_game.deal_step().status, dealing_step_status::advanced);
    QVERIFY(geometry_game.edit_quiz_input(deck_id { 0 }, 88));
    QVERIFY(geometry_game.elapse_quiz(1234).accepted);
    QVERIFY(geometry_game.pause());
    const auto payloads = deck_payloads(geometry_game);
    const auto pending_batch = geometry_game.current_quiz_batch();
    QVERIFY(geometry.swap_decks(deck_id { 0 }, deck_id { 2 }, 0.5));
    QVERIFY(geometry.repack(layout(600.0, 900.0), 0.25));
    QVERIFY(deck_payloads(geometry_game) == payloads);
    QVERIFY(geometry_game.current_quiz_batch() == pending_batch);
    std::vector<deck_id> physical_questions;
    for (const auto slot : geometry_game.traversal()) {
        physical_questions.push_back(*geometry_game.deck_at(slot));
    }
    QVERIFY(geometry_game.unresolved_quizzes() == physical_questions);
    QVERIFY(geometry_game.quiz_focus() == physical_questions.front());
    QVERIFY(geometry_game.resume());
    const auto first = physical_questions.front();
    QVERIFY(geometry_game.skip_quiz(first));
    QVERIFY(geometry_game.quiz_focus(first) == physical_questions[1]);
}

void gameplay_session_tests::quiz_action_guards_and_terminal_cleanup() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.allow_skip = false;
    settings.dealing = dealing_mode::simultaneous;
    auto model = quiz_session(settings, { quiz_cards(), quiz_cards() });
    QVERIFY(!model.edit_quiz_input(deck_id { 0 }, 7));
    QVERIFY(!model.check_quiz(deck_id { 0 }));
    QVERIFY(!model.elapse_quiz(1).accepted);
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    const auto before = deck_payloads(model);
    const auto batch = model.current_quiz_batch();
    QVERIFY(!model.skip_quiz(deck_id { 0 }));
    QVERIFY(!model.edit_quiz_input(deck_id { 99 }, 7));
    QVERIFY(!model.check_quiz(deck_id { 99 }));
    QVERIFY(!model.skip_quiz(deck_id { 99 }));
    QVERIFY(deck_payloads(model) == before);
    QVERIFY(model.current_quiz_batch() == batch);
    QVERIFY(model.fail_deck(deck_id { 0 }));
    QVERIFY(!model.deck(deck_id { 0 })->quiz);
    QVERIFY(model.current_quiz_batch());
    QVERIFY(model.complete_deck(deck_id { 1 }));
    QVERIFY(!model.current_quiz_batch());
    QVERIFY(model.unresolved_quizzes().empty());
    QCOMPARE(model.phase(), session_phase::finished);
    QCOMPARE(model.deal_step().status, dealing_step_status::not_running);
    auto finished = quiz_session(settings, { quiz_cards() });
    QCOMPARE(finished.deal_step().status, dealing_step_status::advanced);
    const auto retained = deck_payloads(finished);
    const auto frozen = finished.current_quiz_batch();
    QVERIFY(finished.finish());
    QVERIFY(!finished.edit_quiz_input(deck_id { 0 }, 7));
    QVERIFY(!finished.check_quiz(deck_id { 0 }));
    QVERIFY(!finished.skip_quiz(deck_id { 0 }));
    QVERIFY(!finished.elapse_quiz(60'000).accepted);
    QVERIFY(deck_payloads(finished) == retained);
    QVERIFY(finished.current_quiz_batch() == frozen);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
