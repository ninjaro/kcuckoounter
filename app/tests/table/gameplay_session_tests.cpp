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
#include <limits>

#include "table/gameplay_session_fixture.hpp"

using namespace gameplay;
using namespace gameplay_test;

void gameplay_session_tests::configuration_and_slot_limits() {
    session_configuration settings;
    QCOMPARE(settings.initial_lives, 3);
    for (const std::size_t count : { 1U, 4U, 24U, 32U, 64U }) {
        const auto result
            = session::create(settings, configurations(count), { count, 64 });
        QVERIFY(result);
        QCOMPARE(result->size(), count);
        QCOMPARE(result->remaining_lives(), 3);
        for (std::size_t i = 0; i < count; ++i) {
            QVERIFY(result->deck_at(physical_slot_id { i }) == deck_id { i });
            QVERIFY(result->slot_for(deck_id { i }) == physical_slot_id { i });
        }
    }
    auto decks = configurations(32);
    QVERIFY(!session::create(settings, decks, { 16, 64 }));
    settings.allow_extra_slots = true;
    QVERIFY(session::create(settings, decks, { 16, 64 }));
    QVERIFY(!session::create(settings, configurations(65), { 16, 64 }));
    QVERIFY(!session::create(settings, {}, { 16, 64 }));
    QVERIFY(!session::create(settings, decks, { 0, 64 }));
    QVERIFY(!session::create(settings, decks, { 64, 16 }));
    settings.sequential_count = 3; // 32 % 3 is irrelevant.
    QVERIFY(session::create(settings, decks, { 16, 64 }));
    settings.sequential_count = 32;
    QVERIFY(session::create(settings, decks, { 16, 64 }));
    settings.sequential_count = 33;
    QVERIFY(!session::create(settings, decks, { 16, 64 }));
}

void gameplay_session_tests::invalid_configuration_is_atomic() {
    auto model = ready_session(failure_policy::lives);
    const auto original = model.configuration();
    const auto deck_before = *model.deck(deck_id { 0 });
    std::vector<session_configuration> invalid;
    auto bad = original;
    bad.initial_lives = 0;
    invalid.push_back(bad);
    bad.initial_lives = 13;
    invalid.push_back(bad);
    bad = original;
    bad.global_quiz_pause = false;
    invalid.push_back(bad);
    bad = original;
    bad.failure = static_cast<failure_policy>(100);
    invalid.push_back(bad);
    bad = original;
    bad.source = static_cast<quiz_source>(100);
    invalid.push_back(bad);
    bad = original;
    bad.scope = static_cast<quiz_scope>(100);
    invalid.push_back(bad);
    bad = original;
    bad.dealing = static_cast<dealing_mode>(100);
    invalid.push_back(bad);
    bad = original;
    bad.sequential_count = 0;
    invalid.push_back(bad);
    for (const auto& configuration : invalid) {
        QVERIFY(!model.configure(configuration));
        QVERIFY(model.configuration() == original);
        QCOMPARE(model.remaining_lives(), 3);
        QVERIFY(*model.deck(deck_id { 0 }) == deck_before);
    }
    // Block permits no global pause for either scope; Multi is not a pause
    // rule.
    for (const auto scope : { quiz_scope::single, quiz_scope::multi }) {
        auto block = original;
        block.failure = failure_policy::block;
        block.global_quiz_pause = false;
        block.scope = scope;
        QVERIFY(model.configure(block));
    }
    const auto after_scope_edit = *model.deck(deck_id { 0 });
    for (const int lives : { 1, 12 }) {
        auto changed = original;
        changed.initial_lives = lives;
        QVERIFY(model.configure(changed));
        QCOMPARE(model.remaining_lives(), lives);
    }
    auto invalid_deck = deck_before.configuration;
    invalid_deck.deck_count = 0;
    QVERIFY(!model.configure_deck(deck_id { 0 }, invalid_deck));
    invalid_deck = deck_before.configuration;
    invalid_deck.strategy_slug.clear();
    QVERIFY(!model.configure_deck(deck_id { 0 }, invalid_deck));
    QVERIFY(*model.deck(deck_id { 0 }) == after_scope_edit);
}

void gameplay_session_tests::prepared_cards_and_setup_edits() {
    auto model = ready_session();
    const auto before = *model.deck(deck_id { 0 });
    QVERIFY(!model.prepare_deck(deck_id { 0 }, {}));
    auto invalid = cards();
    invalid.cards.push_back(52);
    QVERIFY(!model.prepare_deck(deck_id { 0 }, invalid));
    QVERIFY(*model.deck(deck_id { 0 }) == before);
    QVERIFY(model.configure_deck(deck_id { 0 }, before.configuration));
    QVERIFY(*model.deck(deck_id { 0 }) == before); // no-op retains preparation

    auto settings = model.configuration();
    settings.source = quiz_source::physical_joker;
    QVERIFY(model.configure(settings));
    QVERIFY(!model.start()); // source changes invalidate old shoes
    for (std::size_t i = 0; i < model.size(); ++i) {
        QVERIFY(model.deck(deck_id { i })->stream.cards.empty());
        QCOMPARE(model.deck(deck_id { i })->running_count, 0);
        QVERIFY(model.prepare_deck(
            deck_id { i },
            model.deck(deck_id { i })->configuration.infinite
                ? prepared_cards_for(model, deck_id { i })
                : invalid
        ));
    }
    invalid.cards.push_back(53);
    QVERIFY(model.prepare_deck(deck_id { 0 }, invalid));
    const auto physical = *model.deck(deck_id { 0 });
    invalid.cards.push_back(54);
    QVERIFY(!model.prepare_deck(deck_id { 0 }, invalid));
    QVERIFY(*model.deck(deck_id { 0 }) == physical);

    auto changed = model.deck(deck_id { 1 })->configuration;
    changed.deck_count = 1; // recommendation is not a minimum
    changed.strategy_slug = "new_strategy";
    QVERIFY(model.configure_deck(deck_id { 1 }, changed));
    QVERIFY(!model.start());
    QVERIFY(model.prepare_deck(deck_id { 1 }, cards(-8)));
    QCOMPARE(model.deck(deck_id { 1 })->running_count, -8);
    QVERIFY(model.start());
}

void gameplay_session_tests::gameplay_configuration_locks_after_start() {
    auto model = ready_session();
    const auto original = model.configuration();
    const auto deck_before = *model.deck(deck_id { 0 });
    std::vector<session_configuration> edits;
    auto changed = original;
    changed.failure = failure_policy::lives;
    edits.push_back(changed);
    changed = original;
    changed.initial_lives = 12;
    edits.push_back(changed);
    changed = original;
    changed.source = quiz_source::physical_joker;
    edits.push_back(changed);
    changed = original;
    changed.scope = quiz_scope::multi;
    edits.push_back(changed);
    changed = original;
    changed.global_quiz_pause = false;
    edits.push_back(changed);
    changed = original;
    changed.dealing = dealing_mode::random;
    edits.push_back(changed);
    changed.dealing = dealing_mode::simultaneous;
    edits.push_back(changed);
    changed = original;
    changed.sequential_count = 3;
    edits.push_back(changed);
    changed = original;
    changed.allow_skip = false;
    edits.push_back(changed);
    std::vector<deck_configuration> deck_edits;
    auto edited = deck_before.configuration;
    edited.strategy_slug = "different";
    deck_edits.push_back(edited);
    edited = deck_before.configuration;
    edited.infinite = true;
    deck_edits.push_back(edited);
    edited = deck_before.configuration;
    edited.deck_count = 1;
    deck_edits.push_back(edited);
    edited = deck_before.configuration;
    edited.training = true;
    deck_edits.push_back(edited);
    QVERIFY(model.start());
    for (int phase = 0; phase < 2; ++phase) {
        for (const auto& value : edits) {
            QVERIFY(!model.configure(value));
            QVERIFY(model.configuration() == original);
        }
        for (const auto& value : deck_edits) {
            QVERIFY(!model.configure_deck(deck_id { 0 }, value));
            QVERIFY(*model.deck(deck_id { 0 }) == deck_before);
        }
        QVERIFY(!model.prepare_deck(deck_id { 0 }, cards(99)));
        if (phase == 0) {
            QVERIFY(model.pause());
        }
    }
}

void gameplay_session_tests::swaps_preserve_identity_and_payload() {
    auto model = ready_session();
    QVERIFY(!model.swap_decks(deck_id { 0 }, deck_id { 1 }));
    QVERIFY(model.start());
    QVERIFY(!model.swap_decks(deck_id { 0 }, deck_id { 1 }));
    QVERIFY(model.complete_deck(deck_id { 0 }));
    QVERIFY(model.fail_deck(deck_id { 2 }));
    QVERIFY(model.pause());
    QVERIFY(model.set_show_count(deck_id { 1 }, true));
    std::vector<deck_state> original;
    for (std::size_t i = 0; i < model.size(); ++i) {
        original.push_back(*model.deck(deck_id { i }));
    }
    // Repeated arbitrary exchanges, including stopped decks and self-swaps.
    for (std::size_t a = 0; a < model.size(); ++a) {
        for (std::size_t b = 0; b < model.size(); ++b) {
            const auto first_slot = model.slot_for(deck_id { a });
            const auto second_slot = model.slot_for(deck_id { b });
            QVERIFY(model.swap_decks(deck_id { a }, deck_id { b }));
            QVERIFY(model.slot_for(deck_id { a }) == second_slot);
            QVERIFY(model.slot_for(deck_id { b }) == first_slot);
            for (std::size_t i = 0; i < model.size(); ++i) {
                QVERIFY(*model.deck(deck_id { i }) == original[i]);
                QVERIFY(
                    model.deck_at(*model.slot_for(deck_id { i }))
                    == deck_id { i }
                );
            }
        }
    }
    for (const auto invalid :
         { model.size(), std::numeric_limits<std::size_t>::max() }) {
        const auto slot_before = model.slot_for(deck_id { 0 });
        QVERIFY(!model.swap_decks(deck_id { 0 }, deck_id { invalid }));
        QVERIFY(!model.swap_decks(deck_id { invalid }, deck_id { 0 }));
        QVERIFY(model.slot_for(deck_id { 0 }) == slot_before);
        QVERIFY(!model.deck(deck_id { invalid }));
        QVERIFY(!model.slot_for(deck_id { invalid }));
        QVERIFY(!model.deck_at(physical_slot_id { invalid }));
    }
}

void gameplay_session_tests::completion_failure_and_training_are_distinct() {
    auto model = ready_session();
    QVERIFY(!model.complete_deck(deck_id { 0 }));
    QVERIFY(!model.fail_deck(deck_id { 0 }));
    QVERIFY(model.start());
    QVERIFY(model.complete_deck(deck_id { 0 }));
    QVERIFY(!model.complete_deck(deck_id { 0 }));
    QVERIFY(!model.fail_deck(deck_id { 0 }));
    QVERIFY(!model.fail_deck(deck_id { 1 })); // Training immunity
    QVERIFY(model.complete_deck(deck_id { 1 })); // natural end still applies
    QVERIFY(model.fail_deck(deck_id { 2 }));
    QVERIFY(!model.complete_deck(deck_id { 2 }));
    QVERIFY(!model.fail_deck(deck_id { 2 }));
    QVERIFY(!model.complete_deck(deck_id { 3 })); // infinite stream != complete
    QCOMPARE(model.deck(deck_id { 0 })->status, deck_status::completed);
    QCOMPARE(model.deck(deck_id { 1 })->status, deck_status::completed);
    QCOMPARE(model.deck(deck_id { 2 })->status, deck_status::failed);
    QCOMPARE(model.deck(deck_id { 3 })->status, deck_status::active);
    for (std::size_t i = 0; i < model.size(); ++i) {
        QVERIFY(model.slot_for(deck_id { i }) == physical_slot_id { i });
    }
    QVERIFY(!model.complete_deck(deck_id { 99 }));
    QVERIFY(!model.fail_deck(deck_id { 99 }));
    auto lives = ready_session(failure_policy::lives);
    QVERIFY(lives.start());
    QVERIFY(!lives.fail_deck(deck_id { 0 }));
    QCOMPARE(lives.remaining_lives(), 3); // failure != life/error accounting
}

void gameplay_session_tests::pause_and_finish_guard_mutations() {
    auto model = ready_session();
    QVERIFY(!model.pause());
    QVERIFY(!model.resume());
    QVERIFY(!model.finish());
    QVERIFY(!model.set_show_count(deck_id { 0 }, true));
    QVERIFY(model.set_show_count(deck_id { 1 }, true));
    auto challenge = model.deck(deck_id { 1 })->configuration;
    challenge.training = false;
    QVERIFY(model.configure_deck(deck_id { 1 }, challenge));
    QVERIFY(!model.deck(deck_id { 1 })->show_count);
    challenge.training = true;
    QVERIFY(model.configure_deck(deck_id { 1 }, challenge));
    QVERIFY(model.prepare_deck(deck_id { 1 }, cards()));
    QVERIFY(model.start());
    QVERIFY(!model.start());
    QVERIFY(!model.set_show_count(deck_id { 1 }, true));
    QVERIFY(model.set_pick_interval_ms(100)); // preserve live manual speed
    QVERIFY(model.pause());
    QVERIFY(!model.pause());
    QVERIFY(model.set_show_count(deck_id { 1 }, true));
    QVERIFY(!model.set_show_count(deck_id { 99 }, true));
    QVERIFY(!model.complete_deck(deck_id { 0 }));
    QVERIFY(!model.fail_deck(deck_id { 0 }));
    QVERIFY(model.set_pick_interval_ms(1000));
    QVERIFY(!model.set_pick_interval_ms(99));
    QVERIFY(!model.set_pick_interval_ms(1001));
    QCOMPARE(model.pick_interval_ms(), 1000);
    QVERIFY(model.resume());
    QVERIFY(!model.resume());
    const auto before = *model.deck(deck_id { 1 });
    QVERIFY(model.finish());
    QCOMPARE(model.phase(), session_phase::finished);
    QVERIFY(!model.finish());
    QVERIFY(!model.start());
    QVERIFY(!model.resume());
    QVERIFY(!model.pause());
    QVERIFY(!model.set_show_count(deck_id { 1 }, false));
    QVERIFY(!model.swap_decks(deck_id { 0 }, deck_id { 1 }));
    QVERIFY(!model.set_pick_interval_ms(300));
    QVERIFY(!model.complete_deck(deck_id { 0 }));
    QVERIFY(!model.fail_deck(deck_id { 0 }));
    QVERIFY(*model.deck(deck_id { 1 }) == before);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
