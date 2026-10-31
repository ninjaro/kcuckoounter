// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/gameplay_session_tests.hpp"

#include "settings/strategy_data.hpp"
#include "table/gameplay_checkpoint.hpp"
#include "table/gameplay_session_fixture.hpp"
#include "table/gameplay_strategy.hpp"

#include <QtTest/QtTest>
#include <array>

using namespace gameplay;
using namespace gameplay_test;

namespace {

// Compare transitions as well as resulting payloads. End results are not
// checkpoints; stopped/infinite tail state must match there too.
bool continue_pair(session& first, session& second, std::size_t steps) {
    for (std::size_t tick = 0;
         tick < steps && first.phase() == session_phase::running; ++tick) {
        const auto time_a = first.advance_time(17 + tick);
        const auto time_b = second.advance_time(17 + tick);
        if (time_a.accepted != time_b.accepted
            || time_a.elapsed_ms != time_b.elapsed_ms
            || time_a.answers != time_b.answers) {
            return false;
        }
        const auto step_a = first.deal_step();
        const auto step_b = second.deal_step();
        if (step_a.status != step_b.status || step_a.cards != step_b.cards) {
            return false;
        }
        for (const auto id : first.unresolved_quizzes()) {
            // Correct, wrong and Skip are all represented. Wrong Training
            // stays live; wrong Challenge may remove a Block participant.
            const auto desired = (tick + id.value) % 3 == 1
                ? first.deck(id)->quiz->expected_count + 1
                : first.deck(id)->quiz->expected_count;
            if (!first.edit_quiz_input(id, desired)
                || !second.edit_quiz_input(id, desired)) {
                return false;
            }
            const auto skip = first.configuration().allow_skip
                && (tick + id.value) % 3 == 2;
            const auto answer_a
                = skip ? first.skip_quiz(id) : first.check_quiz(id);
            const auto answer_b
                = skip ? second.skip_quiz(id) : second.check_quiz(id);
            if (!answer_a || answer_a != answer_b) {
                return false;
            }
        }
        if (first.phase() != second.phase()
            || first.result() != second.result()) {
            return false;
        }
        if (first.phase() == session_phase::running) {
            const auto state_a = capture_checkpoint(first);
            const auto state_b = capture_checkpoint(second);
            if (!state_a || state_a != state_b) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

void gameplay_session_tests::checkpoint_finite_policies_continue_identically() {
    const std::array<std::size_t, 4> order { 3, 1, 0, 2 };
    std::size_t cases = 0;
    for (const auto failure :
         { failure_policy::lives, failure_policy::block }) {
        for (const auto source :
             { quiz_source::virtual_interrupt, quiz_source::physical_joker }) {
            for (const auto scope : { quiz_scope::single, quiz_scope::multi }) {
                for (const auto global_pause : { false, true }) {
                    if (failure == failure_policy::lives && !global_pause) {
                        continue;
                    }
                    for (const auto dealing :
                         { dealing_mode::sequential, dealing_mode::random,
                           dealing_mode::simultaneous }) {
                        session_configuration settings;
                        settings.failure = failure;
                        settings.initial_lives = 12;
                        settings.source = source;
                        settings.scope = scope;
                        settings.global_quiz_pause = global_pause;
                        settings.dealing = dealing;
                        settings.sequential_count = 2;
                        settings.allow_skip = dealing != dealing_mode::random;
                        auto configured = configurations(4);
                        configured[3].training = true;
                        auto model
                            = session::create(settings, configured, { 4, 4 });
                        QVERIFY(model);
                        auto stream = cards(-17);
                        stream.cards = source == quiz_source::physical_joker
                            ? std::vector<std::uint8_t> { 0, 52, 1, 53,
                                                          2, 3,  4, 5 }
                            : std::vector<std::uint8_t>(8, 0);
                        if (source == quiz_source::virtual_interrupt
                            && scope == quiz_scope::single) {
                            stream.virtual_quiz_targets = { 1, 3, 5, 7 };
                        }
                        std::vector<std::uint64_t> targets;
                        if (source == quiz_source::virtual_interrupt
                            && scope == quiz_scope::multi) {
                            targets = { 1, 3, 5, 7 };
                        }
                        QVERIFY(model->prepare_decks(
                            std::vector<prepared_deck>(4, stream), targets
                        ));
                        QVERIFY(model->set_show_count(deck_id { 3 }, true));
                        QVERIFY(model->set_traversal(order));
                        QVERIFY(model->start(0xFEDCBA9876543210ULL));
                        for (std::size_t warmup = 0; warmup < 2; ++warmup) {
                            (void)model->advance_time(23);
                            (void)model->deal_step();
                        }
                        for (const auto id : model->unresolved_quizzes()) {
                            QVERIFY(model->edit_quiz_input(id, -9));
                        }
                        QVERIFY(model->pause());
                        QVERIFY(
                            model->swap_decks(deck_id { 0 }, deck_id { 3 })
                        );
                        const auto saved = capture_checkpoint(*model);
                        QVERIFY(saved);
                        auto restored
                            = restore_checkpoint(*saved, { 4, 4 }, order);
                        QVERIFY(
                            restored && restored->resume() && model->resume()
                        );
                        QVERIFY(continue_pair(*model, *restored, 80));
                        QVERIFY(model->phase() == session_phase::finished);
                        QVERIFY(restored->result() == model->result());
                        ++cases;
                    }
                }
            }
        }
    }
    QCOMPARE(cases, 36U);
}

void gameplay_session_tests::checkpoint_mixed_streams_continue_across_pages() {
    const auto& catalog = strategy_repository();
    QVERIFY(catalog.is_valid());
    const std::array<std::size_t, 3> order { 2, 0, 1 };
    std::size_t cases = 0;
    for (const auto source :
         { quiz_source::virtual_interrupt, quiz_source::physical_joker }) {
        for (const auto scope : { quiz_scope::single, quiz_scope::multi }) {
            for (const auto dealing :
                 { dealing_mode::sequential, dealing_mode::random,
                   dealing_mode::simultaneous }) {
                session_configuration settings;
                settings.failure = failure_policy::block;
                settings.source = source;
                settings.scope = scope;
                settings.dealing = dealing;
                settings.global_quiz_pause = false;
                settings.sequential_count = 2;
                std::vector<deck_configuration> configured {
                    { "hi_lo", 1, false, false },
                    { "hi_lo", 3, true, true },
                    { "hi_lo", 5, true, true },
                };
                auto model = session::create(settings, configured, { 3, 3 });
                QVERIFY(model);
                generation_policy policy;
                policy.infinite_chunk_decks = 1;
                policy.minimum_quiz_gap = 1;
                policy.maximum_quiz_gap = 2;
                policy.quiz_page_targets = 3;
                QVERIFY(prepare_generated_session(
                    *model, catalog, 987654321, policy
                ));
                QVERIFY(model->set_traversal(order));
                QVERIFY(model->start(123));
                // Cross at least one infinite buffer/page before capture. Both
                // Training decks survive wrong answers and keep informational
                // stats.
                auto before = *model;
                QVERIFY(continue_pair(*model, before, 400));
                QVERIFY(
                    model->deck(deck_id { 1 })->stream.infinite_cards->chunk > 0
                );
                QVERIFY(model->pause());
                QVERIFY(model->set_show_count(deck_id { 2 }, true));
                QVERIFY(model->swap_decks(deck_id { 0 }, deck_id { 2 }));
                const auto saved = capture_checkpoint(*model);
                QVERIFY(saved);
                // Capacity fields are bounds, not a replacement generation
                // recipe.
                generation_policy changed;
                changed.infinite_chunk_decks = 7;
                changed.minimum_quiz_gap = 20;
                changed.maximum_quiz_gap = 40;
                changed.quiz_page_targets = 64;
                auto restored
                    = restore_checkpoint(*saved, { 3, 3 }, order, changed);
                QVERIFY(restored && model->resume() && restored->resume());
                QVERIFY(continue_pair(*model, *restored, 400));
                const auto continued = capture_checkpoint(*model);
                QVERIFY(continued);
                auto second_restore
                    = restore_checkpoint(*continued, { 3, 3 }, order);
                QVERIFY(second_restore && second_restore->resume());
                QVERIFY(continue_pair(*model, *second_restore, 32));
                QVERIFY(model->finish() && second_restore->finish());
                QVERIFY(model->result() == second_restore->result());
                ++cases;
            }
        }
    }
    QCOMPARE(cases, 12U);
}

void gameplay_session_tests::checkpoint_preserves_custom_preparation() {
    // Preparation accepts synthetic bounded streams, not only
    // catalogue-generated balanced shoes. Its initial recipe ordinal need not
    // be zero.
    const std::array<std::size_t, 1> order { 0 };
    auto configured = configurations(1);
    configured[0].infinite = true;
    configured[0].training = true;
    auto model = session::create({}, configured, { 1, 1 });
    QVERIFY(model);
    auto stream = prepared_cards_for(*model, deck_id { 0 }, -7);
    stream.cards.assign(52, 0);
    stream.infinite_cards->lookahead.assign(52, 5);
    stream.infinite_cards->chunk = 19;
    QVERIFY(model->prepare_deck(deck_id { 0 }, stream));
    QVERIFY(model->set_traversal(order));
    QVERIFY(model->start());
    const auto saved = capture_checkpoint(*model);
    QVERIFY(saved);
    QCOMPARE(saved->decks[0].stream.infinite_cards->chunk, 19U);
    QCOMPARE(saved->decks[0].dealt_physical_cards, 0U);
    auto restored = restore_checkpoint(*saved, { 1, 1 }, order);
    QVERIFY(restored && restored->resume());
    QVERIFY(continue_pair(*model, *restored, 160));
    QCOMPARE(model->deck(deck_id { 0 })->stream.infinite_cards->chunk, 22U);
    QCOMPARE(model->deck(deck_id { 0 })->dealt_physical_cards, 160U);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
