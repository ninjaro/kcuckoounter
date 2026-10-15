// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/preferences_tests.hpp"

#include "settings/gameplay_drill_fixture.hpp"
#include "settings/preferences.hpp"
#include "table/gameplay_strategy.hpp"

#include <QtTest/QtTest>

#include <array>
#include <limits>

using namespace preferences_test;
using namespace gameplay;

void preferences_tests::gameplay_drills_restore_fresh_setup() {
    auto drill = sample_drill();
    const auto catalog = drill_catalog();
    QVERIFY(supports_drill(drill, catalog, { 1, 64 }));
    auto game = create_drill_session(drill, catalog, { 1, 64 });
    QVERIFY(game);
    QCOMPARE(game->phase(), session_phase::setup);
    QCOMPARE(game->configuration(), drill.configuration);
    QCOMPARE(game->pick_interval_ms(), drill.pickup_interval_ms);
    QCOMPARE(game->slot_constraints(), (slot_limits { 1, 64 }));
    QCOMPARE(game->statistics(), session_statistics {});
    QCOMPARE(game->timing(), session_timing {});
    QCOMPARE(game->remaining_lives(), 12);
    QVERIFY(!game->result() && !game->current_quiz_batch());
    QVERIFY(game->table_quiz_targets().empty());
    QVERIFY(game->traversal().empty());
    for (std::size_t index = 0; index < game->size(); ++index) {
        deck_state expected;
        expected.id = { index };
        expected.configuration = drill.decks[index].configuration;
        expected.show_count = drill.decks[index].show_count;
        QCOMPARE(*game->deck({ index }), expected);
        QCOMPARE(game->slot_for({ index })->value, index);
    }
    QCOMPARE(*capture_gameplay_drill(*game, drill.name, { 1, 64 }), drill);
    QVERIFY(!game->start()); // No generation is performed during import.
    // Loading an existing record never normalizes to the catalogue's ideal 8.
    QCOMPARE(game->deck({ 0 })->configuration.deck_count, 1U);
    QCOMPARE(game->deck({ 1 })->configuration.deck_count, 17U);
    const std::array<std::size_t, 2> order { 0, 1 };
    QVERIFY(game->set_traversal(order)); // Host recomputes Packing on import.
    QVERIFY(prepare_generated_session(*game, catalog, 123));
    QVERIFY(!game->start()); // Smaller actual geometry still needs consent.
    auto consent = game->configuration();
    consent.allow_extra_slots = true;
    QVERIFY(game->configure(consent));
    QVERIFY(game->start());

    drill.decks.assign(24, drill.decks.front());
    drill.configuration.sequential_count = 17;
    game = create_drill_session(drill, catalog, { 4, 64 });
    QVERIFY(game);
    QCOMPARE(game->size(), 24U);
    QCOMPARE(game->configuration().sequential_count, 17U);
    QVERIFY(!game->configuration().allow_extra_slots);
    QCOMPARE(game->slot_constraints(), (slot_limits { 4, 64 }));
    QVERIFY(!create_drill_session(drill, catalog, { 4, 16 }));
    QVERIFY(!create_drill_session(drill, catalog, { 0, 64 }));
    auto unavailable = drill;
    unavailable.decks.back().configuration.strategy_slug = "removed_strategy";
    QVERIFY(!supports_drill(unavailable, catalog, { 4, 64 }));
    QVERIFY(!create_drill_session(unavailable, catalog, { 4, 64 }));
    auto damaged_catalog = catalog;
    damaged_catalog.strategies.front().weights.clear();
    QVERIFY(!create_drill_session(drill, damaged_catalog, { 4, 64 }));
    drill.decks.front().configuration.deck_count
        = static_cast<std::size_t>(std::numeric_limits<int>::max());
    game = create_drill_session(drill, catalog, { 24, 64 });
    QVERIFY(game); // Large exact count is representable; generation is bounded.
    QVERIFY(!prepare_generated_session(*game, catalog, 123));
    QVERIFY(game->deck({ 0 })->stream.cards.empty());
    QCOMPARE(*capture_gameplay_drill(*game, drill.name, { 24, 64 }), drill);
}

void preferences_tests::gameplay_drills_capture_no_live_state() {
    auto drill = sample_drill();
    drill.configuration.allow_skip = true;
    drill.decks.back().configuration.infinite = false;
    auto game = create_drill_session(drill, drill_catalog(), { 2, 64 });
    QVERIFY(game);
    prepared_deck stream;
    stream.cards = { 0, 1, 2, 3, 4 };
    stream.rank_weights.fill(1);
    stream.initial_running_count = -7;
    stream.virtual_quiz_targets = { 1, 3 };
    const std::array streams { stream, stream };
    QVERIFY(game->prepare_decks(streams));
    const std::array<std::size_t, 2> traversal { 1, 0 };
    QVERIFY(game->set_traversal(traversal));
    QVERIFY(game->start(87654));
    QCOMPARE(game->deal_step().status, dealing_step_status::advanced);
    QVERIFY(game->current_quiz_batch());
    QVERIFY(game->edit_quiz_input({ 0 }, 888));
    QVERIFY(game->edit_quiz_input({ 1 }, -99));
    QVERIFY(game->advance_time(125).accepted);
    QVERIFY(game->check_quiz({ 0 }));
    QCOMPARE(game->deck({ 0 })->status, deck_status::failed);
    const auto before = *game->deck({ 1 });
    QCOMPARE(*capture_gameplay_drill(*game, drill.name, { 2, 64 }), drill);
    QCOMPARE(*game->deck({ 1 }), before); // Capture is observation only.
    QVERIFY(game->pause());
    QVERIFY(game->swap_decks({ 0 }, { 1 }));
    QVERIFY(game->set_show_count({ 1 }, false));
    drill.decks.back().show_count = false;
    QCOMPARE(game->slot_for({ 0 })->value, 1U);
    QCOMPARE(*capture_gameplay_drill(*game, drill.name, { 2, 64 }), drill);
    QVERIFY(!capture_gameplay_drill(*game, QString(), { 2, 64 }));
    QVERIFY(!capture_gameplay_drill(*game, drill.name, { 1, 1 }));
    QTemporaryDir directory;
    auto settings = temporary_settings(directory);
    gameplay_drill_service service(settings, { 2, 64 });
    QVERIFY(
        service.save({ *capture_gameplay_drill(*game, drill.name, { 2, 64 }) })
    );
    auto fresh = create_drill_session(
        service.load()->front(), drill_catalog(), { 2, 64 }
    );
    QVERIFY(fresh);
    QCOMPARE(fresh->phase(), session_phase::setup);
    QCOMPARE(fresh->slot_for({ 0 })->value, 0U);
    QCOMPARE(fresh->statistics(), session_statistics {});
    QCOMPARE(fresh->timing(), session_timing {});
    QVERIFY(!fresh->current_quiz_batch() && !fresh->result());
    for (std::size_t index = 0; index < fresh->size(); ++index) {
        deck_state expected;
        expected.id = { index };
        expected.configuration = drill.decks[index].configuration;
        expected.show_count = drill.decks[index].show_count;
        QCOMPARE(*fresh->deck({ index }), expected);
    }
    QVERIFY(game->finish());
    QVERIFY(game->result());
    QCOMPARE(*capture_gameplay_drill(*game, drill.name, { 2, 64 }), drill);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
