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
#include <utility>

#include "table/gameplay_session_fixture.hpp"

using namespace gameplay;
using namespace gameplay_test;

void gameplay_session_tests::
    infinite_chunks_are_balanced_bounded_and_replayable() {
    generation_policy policy;
    policy.infinite_chunk_decks = 3;
    for (const auto source :
         { quiz_source::virtual_interrupt, quiz_source::physical_joker }) {
        const std::size_t faces
            = source == quiz_source::physical_joker ? 54 : 52;
        for (std::uint64_t seed = 0; seed < 16; ++seed) {
            const auto generated
                = generate_infinite_shoe(source, seed, deck_id { 0 }, policy);
            const auto replay
                = generate_infinite_shoe(source, seed, deck_id { 0 }, policy);
            QVERIFY(generated && replay);
            QVERIFY(generated->cards == replay->cards);
            QVERIFY(generated->continuation == replay->continuation);
            QCOMPARE(generated->continuation.chunk, 0U);
            QCOMPARE(generated->continuation.seed, seed);
            QCOMPARE(generated->continuation.decks_per_chunk, 3U);
            for (const auto* chunk :
                 { &generated->cards, &generated->continuation.lookahead }) {
                QCOMPARE(chunk->size(), 3 * faces);
                std::array<std::size_t, 54> counts {};
                for (const auto face : *chunk) {
                    QVERIFY(face < faces);
                    ++counts[face];
                }
                for (std::size_t face = 0; face < faces; ++face) {
                    QCOMPARE(counts[face], 3U);
                }
            }
        }
    }
    const auto generated = generate_infinite_shoe(
        quiz_source::virtual_interrupt, 1234, deck_id { 0 }
    );
    QVERIFY(generated);
    QCOMPARE(generated->cards.size(), 5200U);
    QCOMPARE(generated->continuation.lookahead.size(), 5200U);
    const std::array<std::uint8_t, 12> current_prefix {
        36, 33, 51, 15, 18, 43, 0, 30, 48, 18, 2, 12
    };
    const std::array<std::uint8_t, 12> lookahead_prefix { 8,  18, 16, 50,
                                                          45, 23, 17, 23,
                                                          8,  21, 33, 47 };
    QVERIFY(
        std::ranges::equal(
            std::span(generated->cards).first(12), current_prefix
        )
    );
    QVERIFY(
        std::ranges::equal(
            std::span(generated->continuation.lookahead).first(12),
            lookahead_prefix
        )
    );
    QVERIFY(generated->cards != generated->continuation.lookahead);
    const auto other = generate_infinite_shoe(
        quiz_source::virtual_interrupt, 1234, deck_id { 1 }
    );
    QVERIFY(other);
    QVERIFY(generated->cards != other->cards);
    policy.maximum_cards = 311; // Two 156-card chunks cannot both fit.
    QVERIFY(!generate_infinite_shoe(
        quiz_source::virtual_interrupt, 0, deck_id { 0 }, policy
    ));
    policy.maximum_cards = 312;
    QVERIFY(generate_infinite_shoe(
        quiz_source::virtual_interrupt, 0, deck_id { 0 }, policy
    ));
    policy.infinite_chunk_decks = std::numeric_limits<std::size_t>::max();
    QVERIFY(!generate_infinite_shoe(
        quiz_source::virtual_interrupt, 0, deck_id { 0 }, policy
    ));
    QVERIFY(
        !generate_infinite_shoe(static_cast<quiz_source>(99), 0, deck_id { 0 })
    );
    policy = {};
    policy.infinite_chunk_decks = 0;
    QVERIFY(!generate_infinite_shoe(
        quiz_source::virtual_interrupt, 0, deck_id { 0 }, policy
    ));
}

void gameplay_session_tests::
    infinite_rollover_preserves_payload_and_continuation() {
    const auto model = ready_session();
    auto deck = *model.deck(deck_id { 3 });
    deck.next_card = deck.stream.cards.size();
    deck.running_count = 97;
    deck.latest_input = -8;
    deck.configuration.training = true;
    deck.show_count = true;
    deck.unverified_normal_cards = 17;
    deck.quiz = pending_quiz { 8, 97 };
    deck.statistics = { 100, 83, 2, 4, 7654, 321 };
    deck.dealt_physical_cards = 100;
    auto replay = deck; // Plain continuation snapshot, not a G8 codec.
    generation_policy policy;
    policy.infinite_chunk_decks = 9; // Existing recipe must stay frozen at 1.
    for (std::uint64_t ordinal = 1; ordinal <= 12; ++ordinal) {
        const auto before = deck;
        const auto expected_cards = before.stream.infinite_cards->lookahead;
        QVERIFY(
            rollover_infinite_shoe(deck, model.configuration().source, policy)
        );
        QVERIFY(
            rollover_infinite_shoe(replay, model.configuration().source, policy)
        );
        QVERIFY(deck == replay);
        QCOMPARE(deck.next_card, 0U);
        QCOMPARE(deck.stream.infinite_cards->chunk, ordinal);
        QCOMPARE(deck.stream.cards.size(), 52U);
        QCOMPARE(deck.stream.infinite_cards->lookahead.size(), 52U);
        QVERIFY(deck.stream.cards == expected_cards);
        std::array<int, 52> counts {};
        for (const auto face : deck.stream.infinite_cards->lookahead) {
            QVERIFY(face < 52);
            ++counts[face];
        }
        QVERIFY(std::ranges::all_of(counts, [](int count) {
            return count == 1;
        }));
        // Only buffers, ordinal and local card offset may change.
        auto normalized = deck;
        normalized.stream.cards = before.stream.cards;
        normalized.stream.infinite_cards = before.stream.infinite_cards;
        normalized.next_card = before.next_card;
        QVERIFY(normalized == before);
        deck.next_card = deck.stream.cards.size();
        replay.next_card = replay.stream.cards.size();
    }
    // Physical mode uses the same seam without hiding Joker faces.
    auto physical = *model.deck(deck_id { 3 });
    auto shoe
        = generate_infinite_shoe(quiz_source::physical_joker, 42, physical.id)
              .value();
    physical.stream.cards = std::move(shoe.cards);
    physical.stream.infinite_cards = std::move(shoe.continuation);
    physical.next_card = physical.stream.cards.size();
    QVERIFY(rollover_infinite_shoe(physical, quiz_source::physical_joker));
    QCOMPARE(std::ranges::count(physical.stream.cards, 52), 100);
    QCOMPARE(std::ranges::count(physical.stream.cards, 53), 100);
}

void gameplay_session_tests::
    infinite_rollover_rejects_invalid_boundaries_atomically() {
    const auto model = ready_session();
    const auto base = *model.deck(deck_id { 3 });
    for (int invalid = 0; invalid < 9; ++invalid) {
        auto deck = base;
        deck.next_card = deck.stream.cards.size();
        generation_policy policy;
        auto source = quiz_source::virtual_interrupt;
        switch (invalid) {
        case 0:
            --deck.next_card;
            break;
        case 1:
            ++deck.next_card;
            break;
        case 2:
            deck.configuration.infinite = false;
            break;
        case 3:
            deck.status = deck_status::failed;
            break;
        case 4:
            deck.stream.infinite_cards.reset();
            break;
        case 5:
            deck.stream.infinite_cards->lookahead.pop_back();
            break;
        case 6:
            deck.stream.infinite_cards->chunk
                = std::numeric_limits<std::uint64_t>::max() - 1;
            break;
        case 7:
            policy.maximum_cards = 103;
            break;
        case 8:
            source = static_cast<quiz_source>(99);
            break;
        }
        const auto before = deck;
        QVERIFY(!rollover_infinite_shoe(deck, source, policy));
        QVERIFY(deck == before);
    }
}

void gameplay_session_tests::
    infinite_quiz_pages_continue_independently_of_chunks() {
    const quiz_schedule_continuation initial { .seed = 1234 };
    const auto first = generate_quiz_page(initial, deck_id { 0 }).value();
    const auto replay = generate_quiz_page(initial, deck_id { 0 }).value();
    QVERIFY(first.targets == replay.targets);
    QVERIFY(first.continuation == replay.continuation);
    QCOMPARE(first.targets.size(), 64U);
    QCOMPARE(first.continuation.next_page, 1U);
    const auto next
        = generate_quiz_page(first.continuation, deck_id { 0 }).value();
    QCOMPARE(next.continuation.next_page, 2U);
    const std::array<std::uint64_t, 6> first_prefix {
        22, 47, 70, 92, 120, 140
    };
    const std::array<std::uint64_t, 6> next_prefix { 1876, 1898, 1922,
                                                     1959, 1992, 2026 };
    QVERIFY(
        std::ranges::equal(std::span(first.targets).first(6), first_prefix)
    );
    QVERIFY(std::ranges::equal(std::span(next.targets).first(6), next_prefix));
    const auto table = generate_quiz_page(initial, std::nullopt).value();
    const std::array<std::uint64_t, 6> table_prefix {
        35, 66, 89, 115, 143, 171
    };
    QVERIFY(
        std::ranges::equal(std::span(table.targets).first(6), table_prefix)
    );
    auto previous = std::uint64_t { 0 };
    for (const auto* targets : { &first.targets, &next.targets }) {
        for (const auto target : *targets) {
            QVERIFY(target - previous >= 20 && target - previous <= 40);
            previous = target;
        }
    }
    QCOMPARE(next.continuation.after_target, previous);
    QVERIFY(
        first.targets != generate_quiz_page(initial, deck_id { 1 })->targets
    );
    QVERIFY(
        first.targets != generate_quiz_page(initial, std::nullopt)->targets
    );
    // An interrupt may land exactly on a chunk boundary: only finite schedules
    // have an exclusive horizon. Absolute position never resets at rollover.
    const quiz_schedule_continuation fixed {
        .seed = 42,
        .minimum_gap = 52,
        .maximum_gap = 52,
        .page_size = 2,
    };
    const auto boundary = generate_quiz_page(fixed, deck_id { 0 }).value();
    QVERIFY(boundary.targets == std::vector<std::uint64_t>({ 52, 104 }));
    const auto after
        = generate_quiz_page(boundary.continuation, deck_id { 0 }).value();
    QVERIFY(after.targets == std::vector<std::uint64_t>({ 156, 208 }));
    const auto current_cards
        = generate_infinite_shoe(
              quiz_source::virtual_interrupt, 42, deck_id { 0 }
        )
              ->cards;
    // Generating schedules does not consume or reseed the independent shoe.
    QVERIFY(
        current_cards
        == generate_infinite_shoe(
               quiz_source::virtual_interrupt, 42, deck_id { 0 }
        )
               ->cards
    );
}

void gameplay_session_tests::
    infinite_pages_freeze_policy_and_reject_overflow() {
    const quiz_schedule_continuation initial { .seed = 1234 };
    const auto expected = generate_quiz_page(initial, std::nullopt).value();
    generation_policy changed;
    changed.minimum_quiz_gap = 1;
    changed.maximum_quiz_gap = 1;
    changed.quiz_page_targets = 1;
    const auto actual
        = generate_quiz_page(initial, std::nullopt, changed).value();
    QVERIFY(actual.targets == expected.targets);
    QVERIFY(actual.continuation == expected.continuation);
    for (int invalid = 0; invalid < 7; ++invalid) {
        auto continuation = initial;
        generation_policy policy;
        switch (invalid) {
        case 0:
            continuation.minimum_gap = 0;
            break;
        case 1:
            continuation.maximum_gap = 19;
            break;
        case 2:
            continuation.page_size = 0;
            break;
        case 3:
            policy.maximum_targets = 63;
            break;
        case 4:
            continuation.next_page = std::numeric_limits<std::uint64_t>::max();
            break;
        case 5:
            continuation.after_target
                = std::numeric_limits<std::uint64_t>::max() - 39;
            break;
        case 6:
            continuation.page_size = std::numeric_limits<std::size_t>::max();
            break;
        }
        const auto before = continuation;
        QVERIFY(!generate_quiz_page(continuation, std::nullopt, policy));
        QVERIFY(continuation == before);
    }
    const auto limit = std::numeric_limits<std::uint64_t>::max();
    const auto last = generate_quiz_page(
                          { .seed = 0,
                            .next_page = limit - 1,
                            .after_target = limit - 40,
                            .minimum_gap = 40,
                            .maximum_gap = 40,
                            .page_size = 1 },
                          std::nullopt
    )
                          .value();
    QCOMPARE(last.targets.back(), limit);
    QCOMPARE(last.continuation.next_page, limit);
    QVERIFY(!generate_quiz_page(last.continuation, std::nullopt));
}

void gameplay_session_tests::
    mixed_preparation_owns_bounded_buffers_and_schedules() {
    const auto& catalog = strategy_repository();
    const std::vector<deck_configuration> decks {
        { .strategy_slug = "hi_lo", .deck_count = 1 },
        { .strategy_slug = "uston_ss",
          .deck_count = 1,
          .infinite = true,
          .training = true },
    };
    for (const auto source :
         { quiz_source::virtual_interrupt, quiz_source::physical_joker }) {
        for (const auto scope : { quiz_scope::single, quiz_scope::multi }) {
            session_configuration settings;
            settings.source = source;
            settings.scope = scope;
            auto model = session::create(settings, decks, { 2, 64 }).value();
            const auto* address = model.deck(deck_id { 1 });
            QVERIFY(prepare_generated_session(model, catalog, 1234));
            QCOMPARE(model.deck(deck_id { 1 }), address);
            const auto& finite = model.deck(deck_id { 0 })->stream;
            const auto& infinite = model.deck(deck_id { 1 })->stream;
            QVERIFY(
                finite.cards
                == generate_shoe(source, 1, 1234, deck_id { 0 }).value()
            );
            QVERIFY(!finite.infinite_cards && !finite.quiz_continuation);
            QVERIFY(infinite.infinite_cards);
            QCOMPARE(
                infinite.cards.size(),
                source == quiz_source::physical_joker ? 5400U : 5200U
            );
            QCOMPARE(
                infinite.infinite_cards->lookahead.size(), infinite.cards.size()
            );
            QCOMPARE(
                infinite.initial_running_count, -4
            ); // chosen count, not 100-deck capacity
            QCOMPARE(model.deck(deck_id { 1 })->running_count, -4);
            const bool single = source == quiz_source::virtual_interrupt
                && scope == quiz_scope::single;
            const bool multi = source == quiz_source::virtual_interrupt
                && scope == quiz_scope::multi;
            QCOMPARE(infinite.quiz_continuation.has_value(), single);
            QCOMPARE(infinite.virtual_quiz_targets.size(), single ? 64U : 0U);
            QCOMPARE(model.table_quiz_continuation().has_value(), multi);
            QCOMPARE(model.table_quiz_targets().size(), multi ? 64U : 0U);
            if (single) {
                QVERIFY(
                    finite.virtual_quiz_targets
                    == generate_quiz_targets(52, 1234, deck_id { 0 }).value()
                );
            } else {
                QVERIFY(finite.virtual_quiz_targets.empty());
            }
            QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 1, 0 }));
            QVERIFY(model.start());
            QVERIFY(model.pause());
            const auto first = *model.deck(deck_id { 0 });
            const auto second = *model.deck(deck_id { 1 });
            const auto continuation = model.table_quiz_continuation();
            QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 1 }));
            QVERIFY(*model.deck(deck_id { 0 }) == first);
            QVERIFY(*model.deck(deck_id { 1 }) == second);
            QVERIFY(model.table_quiz_continuation() == continuation);
        }
    }
}

void gameplay_session_tests::
    mixed_preparation_budget_and_lifecycle_are_atomic() {
    const auto& catalog = strategy_repository();
    const std::vector<deck_configuration> decks {
        { .strategy_slug = "hi_lo", .deck_count = 1 },
        { .strategy_slug = "uston_ss", .deck_count = 1, .infinite = true },
    };
    auto model = session::create({}, decks, { 2, 64 }).value();
    generation_policy policy;
    policy.infinite_chunk_decks = 1;
    policy.maximum_cards = 156; // finite 52 + infinite current/lookahead 104
    policy.maximum_targets = 66; // finite upper bound 2 + infinite page 64
    QVERIFY(prepare_generated_session(model, catalog, 7, policy));
    const auto first = *model.deck(deck_id { 0 });
    const auto second = *model.deck(deck_id { 1 });
    --policy.maximum_cards;
    QVERIFY(!prepare_generated_session(model, catalog, 1234, policy));
    ++policy.maximum_cards;
    --policy.maximum_targets;
    QVERIFY(!prepare_generated_session(model, catalog, 1234, policy));
    ++policy.maximum_targets;
    QVERIFY(!prepare_finite_session(model, catalog, 1234, policy));
    auto invalid_catalog = catalog;
    invalid_catalog.diagnostics.push_back(QStringLiteral("Invalid catalogue"));
    QVERIFY(!prepare_generated_session(model, invalid_catalog, 1234, policy));
    QVERIFY(*model.deck(deck_id { 0 }) == first);
    QVERIFY(*model.deck(deck_id { 1 }) == second);
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    QVERIFY(model.start());
    QVERIFY(!prepare_generated_session(model, catalog, 1234, policy));
    QVERIFY(model.pause());
    QVERIFY(!prepare_generated_session(model, catalog, 1234, policy));
    QVERIFY(model.finish());
    QVERIFY(!prepare_generated_session(model, catalog, 1234, policy));
    QVERIFY(*model.deck(deck_id { 0 }) == first);
    QVERIFY(*model.deck(deck_id { 1 }) == second);

    session_configuration settings;
    settings.scope = quiz_scope::multi;
    auto multi = session::create(settings, decks, { 2, 64 }).value();
    policy.maximum_targets
        = 64; // One page for entire table, no finite per-deck targets.
    QVERIFY(prepare_generated_session(multi, catalog, 7, policy));
    const auto targets = std::vector<std::uint64_t>(
        multi.table_quiz_targets().begin(), multi.table_quiz_targets().end()
    );
    const auto continuation = multi.table_quiz_continuation();
    --policy.maximum_targets;
    QVERIFY(!prepare_generated_session(multi, catalog, 1234, policy));
    QVERIFY(std::ranges::equal(multi.table_quiz_targets(), targets));
    QVERIFY(multi.table_quiz_continuation() == continuation);

    // Large allowed rosters still fit the bounded default recipe.
    std::vector<deck_configuration> large(64, decks[1]);
    auto many = session::create({}, large, { 64, 64 }).value();
    QVERIFY(prepare_generated_session(many, catalog, 7));
    std::size_t resident_cards = 0;
    for (std::size_t i = 0; i < many.size(); ++i) {
        const auto& stream = many.deck(deck_id { i })->stream;
        resident_cards
            += stream.cards.size() + stream.infinite_cards->lookahead.size();
    }
    QCOMPARE(resident_cards, 665600U);
}

void gameplay_session_tests::
    infinite_preparation_validates_metadata_and_invalidation() {
    const auto& catalog = strategy_repository();
    const deck_configuration configured { .strategy_slug = "hi_lo",
                                          .deck_count = 1,
                                          .infinite = true };
    auto model = session::create({}, { configured }, { 1, 64 }).value();
    generation_policy policy;
    policy.infinite_chunk_decks = 1;
    QVERIFY(prepare_generated_session(model, catalog, 7, policy));
    const auto before = *model.deck(deck_id { 0 });
    QVERIFY(
        before.stream.virtual_quiz_targets.back() > before.stream.cards.size()
    );
    for (int invalid = 0; invalid < 9; ++invalid) {
        auto prepared = before.stream;
        switch (invalid) {
        case 0:
            prepared.infinite_cards.reset();
            break;
        case 1:
            prepared.infinite_cards->decks_per_chunk = 0;
            break;
        case 2:
            prepared.infinite_cards->lookahead.pop_back();
            break;
        case 3:
            prepared.infinite_cards->lookahead[0] = 52;
            break;
        case 4:
            prepared.quiz_continuation.reset();
            break;
        case 5:
            ++prepared.quiz_continuation->after_target;
            break;
        case 6:
            prepared.quiz_continuation->next_page = 0;
            break;
        case 7:
            prepared.quiz_continuation->minimum_gap = 0;
            break;
        case 8:
            prepared.virtual_quiz_targets.clear();
            break;
        }
        QVERIFY(!model.prepare_deck(deck_id { 0 }, prepared));
        QVERIFY(!model.prepare_decks(std::array { prepared }));
        QVERIFY(*model.deck(deck_id { 0 }) == before);
    }
    auto finite
        = session::create({}, { { .strategy_slug = "hi_lo" } }, { 1, 64 })
              .value();
    QVERIFY(!finite.prepare_deck(deck_id { 0 }, before.stream));
    auto settings = model.configuration();
    settings.scope = quiz_scope::multi;
    QVERIFY(model.configure(settings));
    QVERIFY(prepare_generated_session(model, catalog, 7, policy));
    const auto prepared = model.deck(deck_id { 0 })->stream;
    const auto targets = std::vector<std::uint64_t>(
        model.table_quiz_targets().begin(), model.table_quiz_targets().end()
    );
    const auto continuation = model.table_quiz_continuation();
    QVERIFY(continuation);
    QVERIFY(!model.prepare_decks(std::array { prepared }, targets));
    QVERIFY(model.table_quiz_continuation() == continuation);
    QVERIFY(model.configure(settings)); // No-op preserves recipe.
    QVERIFY(model.configure_deck(deck_id { 0 }, configured));
    QVERIFY(model.table_quiz_continuation() == continuation);
    QVERIFY(model.prepare_deck(deck_id { 0 }, prepared));
    QVERIFY(!model.table_quiz_continuation());
    QVERIFY(model.table_quiz_targets().empty());
    QVERIFY(prepare_generated_session(model, catalog, 7, policy));
    auto changed = configured;
    changed.training = true;
    QVERIFY(model.configure_deck(deck_id { 0 }, changed));
    QVERIFY(!model.table_quiz_continuation());
    QVERIFY(prepare_generated_session(model, catalog, 7, policy));
    settings.source = quiz_source::physical_joker;
    QVERIFY(model.configure(settings));
    QVERIFY(!model.table_quiz_continuation());
    QVERIFY(prepare_generated_session(model, catalog, 7, policy));
    auto physical = model.deck(deck_id { 0 })->stream;
    physical.quiz_continuation = continuation;
    QVERIFY(!model.prepare_deck(deck_id { 0 }, physical));
    physical.quiz_continuation.reset();
    QVERIFY(
        !model.prepare_decks(std::array { physical }, targets, continuation)
    );
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
