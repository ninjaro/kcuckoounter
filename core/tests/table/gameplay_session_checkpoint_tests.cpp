// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/gameplay_session_tests.hpp"

#include "table/gameplay_checkpoint.hpp"
#include "table/gameplay_session_fixture.hpp"

#include <QtTest/QtTest>
#include <array>
#include <limits>

using namespace gameplay;
using namespace gameplay_test;

void gameplay_session_tests::checkpoint_active_phases_restore_paused() {
    const std::array<std::size_t, 1> order { 0 };
    auto model = session::create({}, configurations(1), { 1, 4 }).value();
    QVERIFY(!capture_checkpoint(model));
    QVERIFY(model.prepare_deck(deck_id { 0 }, quiz_cards()));
    QVERIFY(model.set_traversal(order));
    QVERIFY(model.start(123456789));
    const auto running = capture_checkpoint(model);
    QVERIFY(running);
    QCOMPARE(model.phase(), session_phase::running);
    QCOMPARE(model.advance_time(123).elapsed_ms, 123U);
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    const auto input = std::numeric_limits<std::int64_t>::min();
    QVERIFY(model.edit_quiz_input(deck_id { 0 }, input));
    QCOMPARE(model.advance_time(789).elapsed_ms, 789U);
    const auto pending = capture_checkpoint(model);
    QVERIFY(pending);
    QVERIFY(model.pause());
    QVERIFY(capture_checkpoint(model) == pending);
    auto restored = restore_checkpoint(*pending, { 1, 4 }, order);
    QVERIFY(restored);
    QCOMPARE(restored->phase(), session_phase::paused);
    QVERIFY(!restored->result());
    QVERIFY(capture_checkpoint(*restored) == pending);
    QCOMPARE(restored->deal_step().status, dealing_step_status::not_running);
    QVERIFY(!restored->advance_time(1'000'000).accepted);
    QVERIFY(!restored->edit_quiz_input(deck_id { 0 }, 0));
    QVERIFY(!restored->check_quiz(deck_id { 0 }));
    QVERIFY(!restored->skip_quiz(deck_id { 0 }));
    QVERIFY(capture_checkpoint(*restored) == pending);
    QVERIFY(restored->resume());
    QVERIFY(model.resume());
    const auto remainder = *pending->batch->remaining_ms;
    const auto first = model.advance_time(remainder + 1000);
    const auto second = restored->advance_time(remainder + 1000);
    QCOMPARE(first.elapsed_ms, remainder);
    QCOMPARE(second.elapsed_ms, remainder);
    QVERIFY(first.answers == second.answers);
    QVERIFY(capture_checkpoint(*restored) == capture_checkpoint(model));
    QVERIFY(restored->finish());
    QVERIFY(model.finish());
    QVERIFY(restored->result() == model.result());
    QVERIFY(!capture_checkpoint(*restored));
}

void gameplay_session_tests::checkpoint_zero_lives_keeps_pending_answers() {
    session_configuration settings;
    settings.initial_lives = 1;
    settings.scope = quiz_scope::multi;
    settings.dealing = dealing_mode::simultaneous;
    auto configured = configurations(3);
    configured[2].training = true;
    const std::array<std::uint64_t, 2> targets { 1, 3 };
    const std::array<std::size_t, 3> order { 0, 1, 2 };
    auto model = quiz_session(
        settings, { quiz_cards({}), quiz_cards({}), quiz_cards({}) }, targets,
        configured
    );
    QVERIFY(model.pause());
    QVERIFY(model.set_show_count(deck_id { 2 }, true));
    QVERIFY(model.resume());
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QCOMPARE(model.check_quiz(deck_id { 0 })->outcome, quiz_outcome::wrong);
    QCOMPARE(model.remaining_lives(), 0);
    QCOMPARE(model.phase(), session_phase::running);
    QVERIFY(model.edit_quiz_input(deck_id { 1 }, 1));
    QVERIFY(model.edit_quiz_input(deck_id { 2 }, -99));
    const auto saved = capture_checkpoint(model);
    QVERIFY(saved);
    QCOMPARE(*saved->batch->remaining_ms, 70'000U);
    auto restored = restore_checkpoint(*saved, { 3, 3 }, order);
    QVERIFY(restored && restored->resume());
    QVERIFY(
        restored->check_quiz(deck_id { 1 }) == model.check_quiz(deck_id { 1 })
    );
    QCOMPARE(restored->phase(), session_phase::running);
    QVERIFY(
        restored->check_quiz(deck_id { 2 }) == model.check_quiz(deck_id { 2 })
    );
    QCOMPARE(restored->phase(), session_phase::finished);
    QVERIFY(restored->result() == model.result());
    QCOMPARE(restored->statistics().errors, 1U);
    QCOMPARE(restored->result()->decks[2].statistics.errors, 1U);
}

void gameplay_session_tests::checkpoint_repacking_preserves_slots_and_anchor() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.sequential_count = 2;
    auto model = quiz_session(settings, { cards(), cards(), cards(), cards() });
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QCOMPARE(model.sequential_anchor()->value, 1U);
    QVERIFY(model.complete_deck(deck_id { 1 })); // Trusted early completion.
    QVERIFY(model.fail_deck(deck_id { 2 })); // No fabricated answer/history.
    QVERIFY(model.pause());
    QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 3 }));
    QVERIFY(model.set_pick_interval_ms(777));
    const auto saved = capture_checkpoint(model);
    QVERIFY(saved);
    // New viewport recommendation is smaller; no active payload or consent
    // edit.
    const auto packed = layout(600.0, 900.0);
    QVERIFY(packed.complete(4));
    auto restored = restore_checkpoint(*saved, { 2, 4 }, packed.traversal);
    QVERIFY(restored);
    QVERIFY(capture_checkpoint(*restored) == saved);
    QVERIFY(!restored->configuration().allow_extra_slots);
    QCOMPARE(restored->slot_constraints().recommended, 2U);
    QVERIFY(restored->deck_at(physical_slot_id { 3 }) == deck_id { 0 });
    QVERIFY(restored->deck_at(physical_slot_id { 0 }) == deck_id { 3 });
    QCOMPARE(restored->traversal()[0].value, packed.traversal.front());
    layout_transition restored_geometry(*restored);
    layout_transition current_geometry(model);
    QVERIFY(restored_geometry.repack(packed));
    QVERIFY(current_geometry.repack(packed));
    QVERIFY(capture_checkpoint(*restored) == saved);
    QVERIFY(restored->resume() && model.resume());
    const auto first = model.deal_step();
    const auto second = restored->deal_step();
    QCOMPARE(first.status, second.status);
    QVERIFY(first.cards == second.cards);
    QVERIFY(capture_checkpoint(*restored) == capture_checkpoint(model));
    QVERIFY(restored->finish() && model.finish());
    QVERIFY(restored->result() == model.result());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
