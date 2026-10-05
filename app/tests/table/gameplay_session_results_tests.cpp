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
#include <algorithm>
#include <array>
#include <limits>

#include "table/gameplay_session_fixture.hpp"

using namespace gameplay;
using namespace gameplay_test;

void gameplay_session_tests::scoring_avoids_double_counting_and_tail_credit() {
    session_configuration settings;
    settings.initial_lives = 12;
    auto model = quiz_session(settings, { quiz_cards({ 1, 3, 5, 7 }) });
    for (std::uint64_t exposure = 1; exposure <= 8; ++exposure) {
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
        if (exposure == 1 || exposure == 7) {
            QVERIFY(model.edit_quiz_input(
                deck_id { 0 }, static_cast<std::int64_t>(exposure)
            ));
            const auto answer = model.check_quiz(deck_id { 0 });
            QVERIFY(answer);
            QCOMPARE(answer->outcome, quiz_outcome::correct);
            QCOMPARE(answer->normal_cards, exposure == 1 ? 1U : 2U);
        } else if (exposure == 3) {
            QCOMPARE(
                model.check_quiz(deck_id { 0 })->outcome, quiz_outcome::wrong
            );
            QCOMPARE(model.statistics().verified_cards, 1U);
        } else if (exposure == 5) {
            QCOMPARE(
                model.skip_quiz(deck_id { 0 })->outcome, quiz_outcome::skipped
            );
            QCOMPARE(model.statistics().verified_cards, 1U);
        }
        const auto stats = model.statistics();
        QVERIFY(!model.check_quiz(deck_id { 0 }));
        QVERIFY(!model.skip_quiz(deck_id { 0 }));
        QVERIFY(model.statistics() == stats);
    }
    QCOMPARE(model.phase(), session_phase::finished);
    QVERIFY(model.result());
    QCOMPARE(model.result()->reason, session_end_reason::no_live_decks);
    QCOMPARE(model.statistics().dealt_normal_cards, 8U);
    QCOMPARE(model.statistics().verified_cards, 3U);
    QCOMPARE(model.statistics().errors, 1U);
    QCOMPARE(model.statistics().skips, 1U);
    QCOMPARE(model.remaining_lives(), 11);
    QCOMPARE(model.deck(deck_id { 0 })->unverified_normal_cards, 1U);
    QCOMPARE(model.result()->decks[0].unverified_normal_cards, 1U);
    QCOMPARE(model.result()->decks[0].final_running_count, 8);
    QVERIFY(model.result()->challenge == model.statistics());
}

void gameplay_session_tests::physical_jokers_and_zero_weight_cards_differ() {
    session_configuration settings;
    settings.source = quiz_source::physical_joker;
    auto prepared = cards(10);
    prepared.cards = { 52, 0, 5, 53, 13, 52, 51, 53 };
    auto model = quiz_session(settings, { prepared });
    std::uint64_t questions = 0;
    for (std::size_t i = 0; i < prepared.cards.size(); ++i) {
        const auto step = model.deal_step();
        QCOMPARE(step.status, dealing_step_status::advanced);
        QCOMPARE(step.cards.front().face, prepared.cards[i]);
        if (model.deck(deck_id { 0 })->quiz) {
            QVERIFY(model.edit_quiz_input(
                deck_id { 0 }, model.deck(deck_id { 0 })->running_count
            ));
            const auto answer = model.check_quiz(deck_id { 0 });
            QVERIFY(answer);
            QCOMPARE(answer->outcome, quiz_outcome::correct);
            ++questions;
            if (questions == 1) {
                QCOMPARE(answer->normal_cards, 0U);
            }
            if (questions == 2) {
                QCOMPARE(answer->normal_cards, 2U);
            }
            if (questions == 3) {
                QCOMPARE(answer->normal_cards, 1U);
            }
        }
    }
    QCOMPARE(questions, 3U); // Final Joker is informational completion only.
    QCOMPARE(model.statistics().dealt_normal_cards, 4U);
    QCOMPARE(model.statistics().verified_cards, 3U);
    QCOMPARE(model.statistics().errors, 0U);
    QCOMPARE(model.result()->decks[0].dealt_physical_cards, 8U);
    QCOMPARE(model.result()->decks[0].final_running_count, 11);
    QCOMPARE(model.result()->decks[0].unverified_normal_cards, 1U);
}

void gameplay_session_tests::lives_scoring_resolves_whole_batch_after_zero() {
    session_configuration settings;
    settings.scope = quiz_scope::multi;
    settings.dealing = dealing_mode::simultaneous;
    auto configured = configurations(6);
    configured[5].training = true;
    const auto prepared = quiz_cards({});
    auto model = quiz_session(
        settings, std::vector<prepared_deck>(6, prepared),
        std::array<std::uint64_t, 2> { 1, 2 }, configured
    );
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    for (std::size_t i = 0; i < 4; ++i) {
        const auto answer = model.check_quiz(deck_id { i });
        QVERIFY(answer);
        QCOMPARE(answer->outcome, quiz_outcome::wrong);
        QCOMPARE(
            model.remaining_lives(), std::max(0, 3 - static_cast<int>(i + 1))
        );
        QCOMPARE(model.statistics().errors, i + 1);
        QCOMPARE(model.phase(), session_phase::running);
        QVERIFY(!model.result());
        QCOMPARE(model.deck(deck_id { i })->status, deck_status::active);
        QCOMPARE(model.deal_step().status, dealing_step_status::quiz_blocked);
    }
    QVERIFY(model.edit_quiz_input(deck_id { 4 }, 1));
    QVERIFY(model.check_quiz(deck_id { 4 }));
    QCOMPARE(model.statistics().verified_cards, 1U);
    QCOMPARE(
        model.phase(), session_phase::running
    ); // Training still unresolved.
    const auto training = model.check_quiz(deck_id { 5 });
    QVERIFY(training);
    QCOMPARE(training->outcome, quiz_outcome::wrong);
    QCOMPARE(model.phase(), session_phase::finished);
    QCOMPARE(model.remaining_lives(), 0);
    QVERIFY(model.result());
    QCOMPARE(model.result()->reason, session_end_reason::lives_exhausted);
    QCOMPARE(
        model.result()->challenge.errors, 4U
    ); // Not clamped to initial lives.
    QCOMPARE(model.result()->challenge.verified_cards, 1U);
    QCOMPARE(model.result()->challenge.dealt_normal_cards, 5U);
    QCOMPARE(
        model.result()->decks[5].statistics.errors, 1U
    ); // Training-only data.
    QCOMPARE(model.result()->active_decks, 6U);
    QCOMPARE(model.result()->failed_decks, 0U);
    QCOMPARE(model.next_table_quiz_target(), 1U);
    QVERIFY(!model.current_quiz_batch());
    QVERIFY(!model.advance_time(1).accepted);
}

void gameplay_session_tests::timeout_records_every_error_and_correct_segment() {
    session_configuration settings;
    settings.initial_lives = 1;
    settings.scope = quiz_scope::multi;
    settings.dealing = dealing_mode::simultaneous;
    auto configured = configurations(4);
    configured[3].training = true;
    const auto prepared = quiz_cards({});
    auto model = quiz_session(
        settings, std::vector<prepared_deck>(4, prepared),
        std::array<std::uint64_t, 1> { 1 }, configured
    );
    QVERIFY(model.set_traversal(std::array<std::size_t, 4> { 2, 1, 3, 0 }));
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(model.edit_quiz_input(deck_id { 1 }, 1));
    const auto tick
        = model.advance_time(std::numeric_limits<std::uint64_t>::max());
    QVERIFY(tick.accepted);
    QCOMPARE(tick.elapsed_ms, 60'000U);
    QCOMPARE(tick.answers.size(), 4U);
    for (const auto& answer : tick.answers) {
        QVERIFY(answer.timed_out);
        QVERIFY(answer.outcome != quiz_outcome::skipped);
        QCOMPARE(answer.normal_cards, 1U);
    }
    QCOMPARE(model.statistics().errors, 2U);
    QCOMPARE(model.statistics().verified_cards, 1U);
    QCOMPARE(model.statistics().skips, 0U);
    QCOMPARE(model.remaining_lives(), 0);
    QCOMPARE(model.result()->reason, session_end_reason::lives_exhausted);
    QCOMPARE(model.result()->timing.play_time_ms, 60'000U);
    QCOMPARE(model.result()->timing.quiz_time_ms, 60'000U);
    const auto result = model.result();
    QVERIFY(!model.elapse_quiz(1).accepted);
    for (const auto id :
         { deck_id { 0 }, deck_id { 1 }, deck_id { 2 }, deck_id { 3 } }) {
        QVERIFY(!model.check_quiz(id));
        QCOMPARE(model.deck(id)->statistics.answer_time_ms, 60'000U);
    }
    QVERIFY(model.result() == result);
}

void gameplay_session_tests::block_filters_failed_decks_keeps_training_live() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.scope = quiz_scope::multi;
    settings.dealing = dealing_mode::simultaneous;
    auto configured = configurations(3);
    configured[2].training = true;
    const auto prepared = quiz_cards({});
    auto model = quiz_session(
        settings, { prepared, prepared, prepared },
        std::array<std::uint64_t, 4> { 1, 2, 3, 4 }, configured
    );
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    const auto slot = model.slot_for(deck_id { 0 });
    QCOMPARE(model.check_quiz(deck_id { 0 })->outcome, quiz_outcome::wrong);
    QCOMPARE(model.deck(deck_id { 0 })->status, deck_status::failed);
    QVERIFY(model.slot_for(deck_id { 0 }) == slot);
    const auto failed = *model.deck(deck_id { 0 });
    QVERIFY(model.skip_quiz(deck_id { 1 }));
    QVERIFY(model.check_quiz(deck_id { 2 }));
    QCOMPARE(model.statistics().errors, 1U);
    QCOMPARE(model.statistics().skips, 1U);
    QCOMPARE(model.remaining_lives(), 3);
    QCOMPARE(model.deck(deck_id { 2 })->status, deck_status::active);
    QCOMPARE(model.deck(deck_id { 2 })->statistics.errors, 1U);
    const auto next = model.deal_step();
    QCOMPARE(next.status, dealing_step_status::advanced);
    QCOMPARE(next.cards.size(), 2U);
    QVERIFY(*model.deck(deck_id { 0 }) == failed);
    for (const auto id : { deck_id { 1 }, deck_id { 2 } }) {
        QVERIFY(model.edit_quiz_input(id, 2));
        QVERIFY(model.check_quiz(id));
    }
    QCOMPARE(
        model.statistics().verified_cards, 1U
    ); // Training credit stays zero.
    QCOMPARE(model.deck(deck_id { 2 })->statistics.verified_cards, 0U);
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(model.check_quiz(deck_id { 1 })); // Remembered 2 is wrong for 3.
    QVERIFY(model.skip_quiz(deck_id { 2 }));
    QCOMPARE(model.deck(deck_id { 1 })->status, deck_status::failed);
    QCOMPARE(model.deck(deck_id { 2 })->status, deck_status::active);
    QVERIFY(!model.fail_deck(deck_id { 2 }));
    QCOMPARE(model.phase(), session_phase::running);
    QVERIFY(model.pause());
    QVERIFY(model.set_show_count(deck_id { 2 }, true));
    QVERIFY(model.set_show_count(deck_id { 2 }, false));
    QVERIFY(model.resume());
    const auto training_only = model.deal_step();
    QCOMPARE(training_only.status, dealing_step_status::advanced);
    QCOMPARE(training_only.cards.size(), 1U);
    QVERIFY(training_only.cards.front().owner == deck_id { 2 });
    QVERIFY(model.skip_quiz(deck_id { 2 }));
    QVERIFY(model.finish());
    QCOMPARE(model.result()->challenge.errors, 2U);
    QCOMPARE(model.result()->challenge.skips, 1U);
    QCOMPARE(model.result()->challenge.verified_cards, 1U);
    QCOMPARE(model.result()->failed_decks, 2U);
    QCOMPARE(model.result()->active_decks, 1U);
    QCOMPARE(model.result()->decks[2].statistics.skips, 2U);
}

void gameplay_session_tests::
    block_reevaluates_multi_cadence_after_wrong_answer() {
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
        std::array { prepared_cards_for(model, deck_id { 0 }, 100),
                     prepared_cards_for(model, deck_id { 1 }, 100) },
        page.targets, page.continuation
    ));
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    QVERIFY(model.start(1234));
    for (int attempt = 0; attempt < 32 && !model.current_quiz_batch();
         ++attempt) {
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    }
    QVERIFY(model.current_quiz_batch());
    const auto slow = model.deck(deck_id { 0 })->dealt_physical_cards
            <= model.deck(deck_id { 1 })->dealt_physical_cards
        ? deck_id { 0 }
        : deck_id { 1 };
    const auto fast = deck_id { 1 - slow.value };
    const auto batch = model.current_quiz_batch()->id;
    QVERIFY(model.skip_quiz(fast));
    while (model.deck(fast)->dealt_physical_cards < 130) {
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    }
    const auto wrong = model.check_quiz(slow);
    QVERIFY(wrong);
    QCOMPARE(wrong->outcome, quiz_outcome::wrong);
    QCOMPARE(model.statistics().errors, 1U);
    QCOMPARE(model.deck(slow)->status, deck_status::failed);
    QCOMPARE(model.current_quiz_batch()->id, batch + 1);
    QVERIFY((model.unresolved_quizzes() == std::vector<deck_id> { fast }));
    QCOMPARE(model.table_quiz_targets().back(), 130U);
    QCOMPARE(model.table_quiz_continuation()->next_page, 65U);
    QCOMPARE(
        model.deck(fast)->quiz->expected_count, model.deck(fast)->running_count
    );
}

void gameplay_session_tests::
    schedule_failure_keeps_answer_and_statistics_atomic() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.scope = quiz_scope::multi;
    settings.global_quiz_pause = false;
    settings.dealing = dealing_mode::random;
    auto configured = configurations(2);
    configured[0].infinite = true;
    configured[1].infinite = true;
    auto model = session::create(settings, configured, { 2, 64 }).value();
    auto page = generate_quiz_page(
                    { .minimum_gap = 1, .maximum_gap = 1, .page_size = 2 },
                    std::nullopt
    )
                    .value();
    page.continuation.next_page = std::numeric_limits<std::uint64_t>::max();
    QVERIFY(model.prepare_decks(
        std::array { prepared_cards_for(model, deck_id { 0 }, 100),
                     prepared_cards_for(model, deck_id { 1 }, 100) },
        page.targets, page.continuation
    ));
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    QVERIFY(model.start(1234));
    for (int attempt = 0; attempt < 32 && !model.current_quiz_batch();
         ++attempt) {
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    }
    QVERIFY(model.current_quiz_batch());
    const auto slow = model.deck(deck_id { 0 })->dealt_physical_cards
            <= model.deck(deck_id { 1 })->dealt_physical_cards
        ? deck_id { 0 }
        : deck_id { 1 };
    const auto fast = deck_id { 1 - slow.value };
    QVERIFY(model.skip_quiz(fast));
    while (model.deck(fast)->dealt_physical_cards < 10) {
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    }
    const auto before = deck_payloads(model);
    const auto stats = model.statistics();
    const auto batch = model.current_quiz_batch();
    const auto cursor = model.next_table_quiz_target();
    for (int retry = 0; retry < 2; ++retry) {
        QVERIFY(!model.check_quiz(slow));
        QVERIFY(deck_payloads(model) == before);
        QVERIFY(model.statistics() == stats);
        QVERIFY(model.current_quiz_batch() == batch);
        QCOMPARE(model.next_table_quiz_target(), cursor);
        QCOMPARE(model.phase(), session_phase::running);
        QVERIFY(!model.result());
    }
}

void gameplay_session_tests::
    manual_finish_preserves_statuses_and_infinite_context() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.dealing = dealing_mode::simultaneous;
    auto configured = configurations(3);
    configured[2].infinite = true;
    auto model = session::create(settings, configured, { 3, 64 }).value();
    auto short_shoe = cards(7);
    short_shoe.cards = { 0 };
    auto failed = quiz_cards({ 1 });
    auto infinite = prepared_cards_for(model, deck_id { 2 }, 100);
    QVERIFY(model.prepare_decks(std::array { short_shoe, failed, infinite }));
    QVERIFY(model.set_traversal(std::array<std::size_t, 3> { 2, 0, 1 }));
    QVERIFY(model.start(42));
    QVERIFY(model.advance_time(500).accepted);
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(model.check_quiz(deck_id { 1 }));
    QCOMPARE(model.deck(deck_id { 1 })->status, deck_status::failed);
    QVERIFY(model.advance_time(1000).accepted);
    QVERIFY(model.pause());
    QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 2 }));
    const auto payloads = deck_payloads(model);
    QVERIFY(
        model.finish()
    ); // Paused, pending infinite question is not auto-Skip.
    QVERIFY(deck_payloads(model) == payloads);
    const auto result = model.result();
    QVERIFY(result);
    QCOMPARE(result->reason, session_end_reason::manual_finish);
    QVERIFY(result->configuration == settings);
    QCOMPARE(result->remaining_lives, 3);
    QCOMPARE(result->completed_decks, 1U);
    QCOMPARE(result->failed_decks, 1U);
    QCOMPARE(result->active_decks, 1U);
    QCOMPARE(result->challenge.errors, 1U);
    QCOMPARE(result->challenge.skips, 0U);
    QCOMPARE(result->challenge.verified_cards, 0U);
    QCOMPARE(result->timing.play_time_ms, 1500U);
    QCOMPARE(result->timing.quiz_time_ms, 1000U);
    QCOMPARE(result->decks[0].final_running_count, 8);
    QVERIFY(result->decks[0].slot == physical_slot_id { 2 });
    QVERIFY(result->decks[2].slot == physical_slot_id { 0 });
    QCOMPARE(result->decks[2].initial_running_count, 100);
    QVERIFY(result->decks[2].rank_weights == infinite.rank_weights);
    QVERIFY(result->decks[2].schedule.targets == infinite.virtual_quiz_targets);
    QCOMPARE(result->decks[2].schedule.next_target, 1U);
    QVERIFY(
        result->decks[2].schedule.continuation == infinite.quiz_continuation
    );
    QVERIFY(result->decks[2].infinite_position);
    QCOMPARE(result->decks[2].infinite_position->decks_per_chunk, 1U);
    QCOMPARE(result->cadence_revision, 1U);
    QVERIFY(!model.finish());
    QVERIFY(!model.advance_time(1).accepted);
    QVERIFY(model.result() == result);
}

void gameplay_session_tests::
    results_points_and_errors_are_independent_dimensions() {
    std::array<session_result, 2> results {};
    for (std::size_t variant = 0; variant < 2; ++variant) {
        session_configuration settings;
        settings.initial_lives = 12;
        auto prepared = quiz_cards({ 1, 2, 3, 103 });
        prepared.cards.assign(104, 0);
        auto model = quiz_session(settings, { prepared });
        for (int exposure = 1; exposure <= 104; ++exposure) {
            QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
            if (exposure <= 3) {
                QVERIFY(
                    variant == 0 ? model.skip_quiz(deck_id { 0 })
                                 : model.check_quiz(deck_id { 0 })
                );
            } else if (exposure == 103) {
                QVERIFY(model.edit_quiz_input(deck_id { 0 }, 103));
                QVERIFY(model.check_quiz(deck_id { 0 }));
            }
        }
        QVERIFY(model.result());
        results[variant] = *model.result();
        QCOMPARE(results[variant].challenge.verified_cards, 100U);
        QCOMPARE(results[variant].decks[0].unverified_normal_cards, 1U);
    }
    QCOMPARE(results[0].challenge.errors, 0U);
    QCOMPARE(results[1].challenge.errors, 3U);
    QVERIFY(results[0].configuration == results[1].configuration);
    QVERIFY(results[0] != results[1]);
}

void gameplay_session_tests::
    natural_finish_preserves_reveal_and_random_ordinal() {
    for (const auto mode :
         { dealing_mode::sequential, dealing_mode::simultaneous,
           dealing_mode::random }) {
        session_configuration settings;
        settings.failure = failure_policy::block;
        settings.dealing = mode;
        auto prepared = quiz_cards({});
        prepared.cards = { 0 };
        auto model = quiz_session(settings, { prepared, prepared });
        for (int steps = 0;
             steps < 2 && model.phase() == session_phase::running; ++steps) {
            const auto reveal = model.deal_step();
            QCOMPARE(reveal.status, dealing_step_status::advanced);
            for (const auto& card : reveal.cards) {
                QVERIFY(card.completed);
            }
        }
        QCOMPARE(model.phase(), session_phase::finished);
        QVERIFY(model.result());
        QCOMPARE(model.result()->reason, session_end_reason::no_live_decks);
        QCOMPARE(model.result()->completed_decks, 2U);
        QCOMPARE(model.result()->challenge.dealt_normal_cards, 2U);
        QCOMPARE(model.result()->challenge.verified_cards, 0U);
        QVERIFY(model.result()->random_dealing == model.random_dealing());
        QCOMPARE(
            model.random_dealing().next_step,
            mode == dealing_mode::random ? 2U : 0U
        );
        QVERIFY(!model.current_quiz_batch());
        QVERIFY(!model.finish());
    }
}

void gameplay_session_tests::running_frozen_and_paused_times_are_distinct() {
    session_configuration settings;
    settings.dealing = dealing_mode::simultaneous;
    auto model
        = quiz_session(settings, { quiz_cards({ 1 }), quiz_cards({ 3 }) });
    QVERIFY(model.advance_time(1000).accepted);
    QCOMPARE(model.timing().play_time_ms, 1000U);
    QCOMPARE(model.timing().quiz_time_ms, 0U);
    QCOMPARE(model.deck(deck_id { 0 })->statistics.dealing_time_ms, 1000U);
    QCOMPARE(model.deck(deck_id { 1 })->statistics.dealing_time_ms, 1000U);
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(model.advance_time(500).accepted);
    QCOMPARE(model.timing().play_time_ms, 1500U);
    QCOMPARE(model.timing().quiz_time_ms, 500U);
    QCOMPARE(model.deck(deck_id { 0 })->statistics.answer_time_ms, 500U);
    QCOMPARE(model.deck(deck_id { 1 })->statistics.answer_time_ms, 0U);
    QCOMPARE(
        model.deck(deck_id { 1 })->statistics.dealing_time_ms, 1000U
    ); // Globally held, not answering.
    const auto before = deck_payloads(model);
    const auto timing = model.timing();
    const auto budget = model.current_quiz_batch();
    QVERIFY(model.pause());
    QVERIFY(!model.advance_time(100'000).accepted);
    QVERIFY(!model.elapse_quiz(100'000).accepted);
    QVERIFY(model.timing() == timing);
    QVERIFY(deck_payloads(model) == before);
    QVERIFY(model.current_quiz_batch() == budget);
    QVERIFY(model.resume());
    QVERIFY(model.edit_quiz_input(deck_id { 0 }, 1));
    QVERIFY(model.check_quiz(deck_id { 0 }));
    QVERIFY(model.advance_time(250).accepted);
    QCOMPARE(model.timing().play_time_ms, 1750U);
    QCOMPARE(model.timing().quiz_time_ms, 500U);
    QCOMPARE(model.deck(deck_id { 0 })->statistics.dealing_time_ms, 1250U);
    QCOMPARE(model.deck(deck_id { 1 })->statistics.dealing_time_ms, 1250U);
    QVERIFY(model.finish());
    QVERIFY(model.result()->timing == model.timing());
}

void gameplay_session_tests::
    non_global_quizzes_overlap_without_double_counting() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.global_quiz_pause = false;
    settings.dealing = dealing_mode::simultaneous;
    auto model
        = quiz_session(settings, { quiz_cards({ 1 }), quiz_cards({ 2 }) });
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(!model.elapse_quiz(1000).accepted);
    QVERIFY(model.advance_time(1000).accepted);
    QCOMPARE(model.deck(deck_id { 0 })->statistics.answer_time_ms, 1000U);
    QCOMPARE(model.deck(deck_id { 1 })->statistics.dealing_time_ms, 1000U);
    QCOMPARE(model.timing().play_time_ms, 1000U);
    QCOMPARE(model.timing().quiz_time_ms, 1000U); // Union, not summed per deck.
    QCOMPARE(model.deal_step().cards.size(), 1U);
    QVERIFY(model.advance_time(500).accepted);
    QCOMPARE(model.deck(deck_id { 0 })->statistics.answer_time_ms, 1500U);
    QCOMPARE(model.deck(deck_id { 1 })->statistics.answer_time_ms, 500U);
    QCOMPARE(model.timing().quiz_time_ms, 1500U);
    QVERIFY(model.skip_quiz(deck_id { 0 }));
    QVERIFY(model.advance_time(250).accepted);
    QCOMPARE(model.deck(deck_id { 0 })->statistics.dealing_time_ms, 250U);
    QCOMPARE(model.deck(deck_id { 1 })->statistics.answer_time_ms, 750U);
    QCOMPARE(model.timing().play_time_ms, 1750U);
    QCOMPARE(model.timing().quiz_time_ms, 1750U);
    QVERIFY(model.finish());
    QCOMPARE(model.result()->timing.quiz_time_ms, 1750U);
}

void gameplay_session_tests::
    timeout_clips_elapsed_and_excludes_stopped_decks() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.dealing = dealing_mode::simultaneous;
    auto completed = quiz_cards({});
    completed.cards = { 0 };
    auto model = quiz_session(
        settings, { completed, quiz_cards({ 1 }), quiz_cards({ 2 }) }
    );
    QVERIFY(model.advance_time(100).accepted);
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QCOMPARE(model.deck(deck_id { 0 })->status, deck_status::completed);
    const auto tick = model.advance_time(90'000);
    QVERIFY(tick.accepted);
    QCOMPARE(tick.elapsed_ms, 60'000U);
    QCOMPARE(tick.answers.size(), 1U);
    QCOMPARE(model.deck(deck_id { 1 })->status, deck_status::failed);
    QCOMPARE(model.timing().play_time_ms, 60'100U);
    QCOMPARE(model.timing().quiz_time_ms, 60'000U);
    QCOMPARE(model.deck(deck_id { 0 })->statistics.dealing_time_ms, 100U);
    QCOMPARE(model.deck(deck_id { 0 })->statistics.answer_time_ms, 0U);
    QCOMPARE(model.deck(deck_id { 1 })->statistics.answer_time_ms, 60'000U);
    QCOMPARE(model.deck(deck_id { 2 })->statistics.dealing_time_ms, 100U);
    QVERIFY(model.advance_time(200).accepted);
    QCOMPARE(model.deck(deck_id { 1 })->statistics.answer_time_ms, 60'000U);
    QCOMPARE(model.deck(deck_id { 2 })->statistics.dealing_time_ms, 300U);
}

void gameplay_session_tests::
    timing_overflow_rejects_the_whole_slice_atomically() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.global_quiz_pause = false;
    settings.dealing = dealing_mode::simultaneous;
    auto model = quiz_session(settings, { quiz_cards({ 1 }), quiz_cards({}) });
    QVERIFY(model.advance_time(std::numeric_limits<std::uint64_t>::max() - 10)
                .accepted);
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    const auto before = deck_payloads(model);
    const auto timing = model.timing();
    const auto batch = model.current_quiz_batch();
    for (int retry = 0; retry < 2; ++retry) {
        const auto rejected = model.advance_time(11);
        QVERIFY(!rejected.accepted);
        QCOMPARE(rejected.elapsed_ms, 0U);
        QVERIFY(rejected.answers.empty());
        QVERIFY(deck_payloads(model) == before);
        QVERIFY(model.timing() == timing);
        QVERIFY(model.current_quiz_batch() == batch);
        QVERIFY(!model.result());
    }
    QVERIFY(model.advance_time(10).accepted);
    QCOMPARE(
        model.timing().play_time_ms, std::numeric_limits<std::uint64_t>::max()
    );
    QVERIFY(model.finish());
    QCOMPARE(
        model.result()->timing.play_time_ms,
        std::numeric_limits<std::uint64_t>::max()
    );
    QCOMPARE(model.result()->timing.quiz_time_ms, 10U);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
