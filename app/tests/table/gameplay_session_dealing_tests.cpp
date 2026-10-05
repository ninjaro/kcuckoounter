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
#include <numeric>

#include "table/gameplay_session_fixture.hpp"

using namespace gameplay;
using namespace gameplay_test;

void gameplay_session_tests::
    dealing_sequential_matches_live_traversal_oracle() {
    for (std::size_t m = 1; m <= 16; ++m) {
        for (std::size_t n = 1; n <= m; ++n) {
            for (const bool holes : { false, true }) {
                session_configuration settings;
                settings.failure = failure_policy::block;
                settings.sequential_count = n;
                auto configured = configurations(m);
                for (std::size_t i = 0; i < m; ++i) {
                    configured[i].training = i % 3 == 0;
                }
                auto model
                    = session::create(settings, configured, { m, 64 }).value();
                std::vector<std::size_t> order(m);
                std::iota(order.rbegin(), order.rend(), 0);
                QVERIFY(model.set_traversal(order));
                for (std::size_t i = 0; i < m; ++i) {
                    auto prepared = cards(static_cast<std::int64_t>(i));
                    prepared.cards = { 0, 0, 0, 0 };
                    QVERIFY(model.prepare_deck(deck_id { i }, prepared));
                }
                QVERIFY(model.start());
                std::vector<deck_status> expected_status(
                    m, deck_status::active
                );
                if (holes) {
                    for (std::size_t i = 0; i < m; ++i) {
                        if (i % 3 == 1) {
                            QVERIFY(model.fail_deck(deck_id { i }));
                            expected_status[i] = deck_status::failed;
                        } else if (i % 3 == 2) {
                            QVERIFY(model.complete_deck(deck_id { i }));
                            expected_status[i] = deck_status::completed;
                        }
                    }
                }
                std::vector<std::uint64_t> revealed(m, 0);
                std::optional<physical_slot_id> anchor;
                for (int step = 0; step < 10; ++step) {
                    std::vector<std::size_t> expected;
                    auto position = anchor
                        ? (static_cast<std::size_t>(
                               std::find(
                                   order.begin(), order.end(), anchor->value
                               )
                               - order.begin()
                           )
                           + 1)
                            % m
                        : 0;
                    for (std::size_t visited = 0; visited < m; ++visited) {
                        if (expected_status[order[position]]
                            == deck_status::active) {
                            expected.push_back(order[position]);
                            if (expected.size() == n) {
                                break;
                            }
                        }
                        position = (position + 1) % m;
                    }
                    const auto actual = model.deal_step();
                    QCOMPARE(
                        actual.status,
                        expected.empty() ? dealing_step_status::not_running
                                         : dealing_step_status::advanced
                    );
                    QCOMPARE(actual.cards.size(), expected.size());
                    for (std::size_t i = 0; i < expected.size(); ++i) {
                        const auto owner = expected[i];
                        ++revealed[owner];
                        QCOMPARE(actual.cards[i].slot.value, owner);
                        QCOMPARE(actual.cards[i].owner.value, owner);
                        QCOMPARE(actual.cards[i].face, 0);
                        QCOMPARE(
                            actual.cards[i].dealt_physical_cards,
                            revealed[owner]
                        );
                        QCOMPARE(
                            actual.cards[i].completed, revealed[owner] == 4
                        );
                        if (revealed[owner] == 4) {
                            expected_status[owner] = deck_status::completed;
                        }
                    }
                    if (!expected.empty()) {
                        anchor = physical_slot_id { expected.back() };
                    }
                    QVERIFY(model.sequential_anchor() == anchor);
                    for (std::size_t i = 0; i < m; ++i) {
                        const auto& deck = *model.deck(deck_id { i });
                        QCOMPARE(deck.status, expected_status[i]);
                        QCOMPARE(deck.next_card, revealed[i]);
                        QCOMPARE(deck.dealt_physical_cards, revealed[i]);
                        QCOMPARE(
                            deck.statistics.dealt_normal_cards, revealed[i]
                        );
                        QCOMPARE(deck.unverified_normal_cards, revealed[i]);
                        QCOMPARE(
                            deck.running_count,
                            static_cast<std::int64_t>(i + revealed[i])
                        );
                        QCOMPARE(deck.statistics.verified_cards, 0U);
                        QVERIFY(
                            model.slot_for(deck.id) == physical_slot_id { i }
                        );
                    }
                }
            }
        }
    }
}

void gameplay_session_tests::repack_swap_keep_physical_anchor_and_counts() {
    auto model = ready_session();
    auto settings = model.configuration();
    settings.sequential_count = 2;
    QVERIFY(model.configure(settings));
    layout_transition presentation(model);
    const auto wide = layout(1200, 600);
    const auto tall = layout(600, 1200);
    QVERIFY(presentation.repack(wide));
    QVERIFY(model.start());
    const auto first = model.deal_step();
    QCOMPARE(first.status, dealing_step_status::advanced);
    QCOMPARE(first.cards[0].slot.value, wide.traversal[0]);
    QCOMPARE(first.cards[1].slot.value, wide.traversal[1]);
    const auto anchor = model.sequential_anchor().value();
    QVERIFY(model.pause());
    const auto before = deck_payloads(model);
    QVERIFY(presentation.swap_decks(deck_id { 0 }, deck_id { 3 }));
    QVERIFY(deck_payloads(model) == before);
    QVERIFY(model.sequential_anchor() == anchor);
    QVERIFY(presentation.repack(tall));
    QVERIFY(model.resume());
    const auto position = static_cast<std::size_t>(
        std::find(tall.traversal.begin(), tall.traversal.end(), anchor.value)
        - tall.traversal.begin()
    );
    const auto second = model.deal_step();
    QCOMPARE(second.status, dealing_step_status::advanced);
    for (std::size_t i = 0; i < second.cards.size(); ++i) {
        const auto& reveal = second.cards[i];
        QCOMPARE(
            reveal.slot.value, tall.traversal[(position + 1 + i) % model.size()]
        );
        QVERIFY(model.deck_at(reveal.slot) == reveal.owner);
        const auto& original = before[reveal.owner.value];
        QCOMPARE(reveal.face, original.stream.cards[original.next_card]);
        QCOMPARE(model.deck(reveal.owner)->next_card, original.next_card + 1);
    }
    const auto after = deck_payloads(model);
    QVERIFY(presentation.repack(tall));
    QVERIFY(deck_payloads(model) == after);
    // Finite stop/failure leaves packing, destinations and mappings intact.
    const auto held_order = std::vector<physical_slot_id>(
        model.traversal().begin(), model.traversal().end()
    );
    QVERIFY(model.fail_deck(deck_id { 0 }));
    const auto failed = *model.deck(deck_id { 0 });
    const auto next = model.deal_step();
    QVERIFY(std::ranges::none_of(next.cards, [](const auto& reveal) {
        return reveal.owner == deck_id { 0 };
    }));
    QVERIFY(*model.deck(deck_id { 0 }) == failed);
    QVERIFY(std::ranges::equal(model.traversal(), held_order));
    QVERIFY(model.slot_for(deck_id { 0 }) == physical_slot_id { 3 });
}

void gameplay_session_tests::dealing_simultaneous_completes_each_deck_once() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.dealing = dealing_mode::simultaneous;
    auto configured = configurations(4);
    configured[3].training = true;
    auto model = session::create(settings, configured, { 4, 64 }).value();
    const std::array<std::size_t, 4> order { 2, 0, 3, 1 };
    QVERIFY(model.set_traversal(order));
    for (std::size_t i = 0; i < 4; ++i) {
        auto prepared = cards();
        prepared.cards.assign(i + 1, 0);
        QVERIFY(model.prepare_deck(deck_id { i }, prepared));
    }
    QVERIFY(model.set_show_count(deck_id { 3 }, true));
    QVERIFY(model.start());
    QVERIFY(model.fail_deck(deck_id { 2 }));
    const auto failed = *model.deck(deck_id { 2 });
    const auto first = model.deal_step();
    QCOMPARE(first.cards.size(), 3U);
    QCOMPARE(first.cards[0].owner.value, 0U);
    QCOMPARE(first.cards[1].owner.value, 3U);
    QCOMPARE(first.cards[2].owner.value, 1U);
    QVERIFY(first.cards[0].completed);
    const auto completed = *model.deck(deck_id { 0 });
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    }
    QVERIFY(*model.deck(deck_id { 0 }) == completed);
    QVERIFY(*model.deck(deck_id { 2 }) == failed);
    QCOMPARE(model.deck(deck_id { 1 })->statistics.dealt_normal_cards, 2U);
    QCOMPARE(model.deck(deck_id { 3 })->statistics.dealt_normal_cards, 4U);
    QCOMPARE(model.deck(deck_id { 3 })->statistics.verified_cards, 0U);
    QVERIFY(model.deck(deck_id { 3 })->show_count);
    const auto stopped = deck_payloads(model);
    for (int i = 0; i < 3; ++i) {
        const auto empty = model.deal_step();
        QCOMPARE(empty.status, dealing_step_status::not_running);
        QVERIFY(empty.cards.empty());
        QVERIFY(deck_payloads(model) == stopped);
    }
    QVERIFY(!model.sequential_anchor());
    QCOMPARE(model.random_dealing().next_step, 0U);
    QVERIFY(
        std::ranges::equal(
            model.traversal(),
            std::vector<physical_slot_id> { { 2 }, { 0 }, { 3 }, { 1 } }
        )
    );
}

void gameplay_session_tests::
    random_dealing_replays_filters_allows_immediate_repeats() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.dealing = dealing_mode::random;
    auto configured = configurations(4);
    configured[3].training = true;
    auto model = session::create(settings, configured, { 4, 64 }).value();
    for (std::size_t i = 0; i < 4; ++i) {
        auto prepared = cards();
        prepared.cards.assign(128, 0);
        QVERIFY(model.prepare_deck(deck_id { i }, prepared));
    }
    QVERIFY(model.set_traversal(std::array<std::size_t, 4> { 0, 1, 2, 3 }));
    auto replay = model;
    QVERIFY(replay.set_traversal(std::array<std::size_t, 4> { 3, 2, 1, 0 }));
    auto other_seed = model;
    constexpr std::uint64_t seed = (std::uint64_t { 1 } << 40) + 1234;
    QVERIFY(model.start(seed));
    QVERIFY(replay.start(seed));
    QVERIFY(other_seed.start(1234));
    std::vector<deck_id> sequence;
    std::vector<deck_id> other_sequence;
    for (int i = 0; i < 32; ++i) {
        const auto first = model.deal_step();
        const auto second = replay.deal_step();
        QCOMPARE(first.status, dealing_step_status::advanced);
        QVERIFY(first.cards == second.cards);
        sequence.push_back(first.cards.front().owner);
        other_sequence.push_back(other_seed.deal_step().cards.front().owner);
    }
    QVERIFY(sequence != other_sequence);
    const std::vector<deck_id> prefix {
        { 1 }, { 0 }, { 3 }, { 2 }, { 1 }, { 2 },
        { 3 }, { 2 }, { 3 }, { 2 }, { 2 }, { 2 },
    };
    QVERIFY(
        std::ranges::equal(std::span(sequence).first(prefix.size()), prefix)
    );
    QVERIFY(
        std::adjacent_find(sequence.begin(), sequence.end()) != sequence.end()
    );
    QVERIFY(std::ranges::find(sequence, deck_id { 3 }) != sequence.end());
    QCOMPARE(model.random_dealing().seed, seed);
    QCOMPARE(model.random_dealing().next_step, 32U);
    QVERIFY(!model.sequential_anchor());
    QVERIFY(model.pause());
    QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 1 }));
    QVERIFY(model.resume());
    QVERIFY(model.fail_deck(deck_id { 2 }));
    QVERIFY(replay.fail_deck(deck_id { 2 }));
    const auto failed = *model.deck(deck_id { 2 });
    for (int i = 0; i < 32; ++i) {
        const auto first = model.deal_step();
        const auto second = replay.deal_step();
        QCOMPARE(first.status, dealing_step_status::advanced);
        QCOMPARE(
            first.cards.front().owner.value, second.cards.front().owner.value
        );
        QVERIFY(first.cards.front().owner != deck_id { 2 });
        QVERIFY(
            model.slot_for(first.cards.front().owner)
            == first.cards.front().slot
        );
        QVERIFY(deck_payloads(model) == deck_payloads(replay));
    }
    QVERIFY(*model.deck(deck_id { 2 }) == failed);
    // A plain in-memory continuation copy produces the same future draws.
    // This is not a checkpoint codec or a restore workflow.
    auto continued = model;
    for (int i = 0; i < 16; ++i) {
        QVERIFY(model.deal_step().cards == continued.deal_step().cards);
    }
    QCOMPARE(model.random_dealing().next_step, 80U);
    QVERIFY(model.random_dealing() == continued.random_dealing());

    auto short_game = session::create(settings, configured, { 4, 64 }).value();
    auto one_card = cards();
    one_card.cards = { 0 };
    for (std::size_t i = 0; i < short_game.size(); ++i) {
        QVERIFY(short_game.prepare_deck(deck_id { i }, one_card));
    }
    QVERIFY(
        short_game.set_traversal(std::array<std::size_t, 4> { 2, 0, 3, 1 })
    );
    QVERIFY(short_game.start(seed));
    std::array<bool, 4> completed {};
    for (int i = 0; i < 4; ++i) {
        const auto step = short_game.deal_step();
        QCOMPARE(step.status, dealing_step_status::advanced);
        const auto& reveal = step.cards.front();
        QVERIFY(!completed[reveal.owner.value]);
        QVERIFY(reveal.completed);
        completed[reveal.owner.value] = true;
    }
    const auto exhausted = deck_payloads(short_game);
    QCOMPARE(short_game.deal_step().status, dealing_step_status::not_running);
    QVERIFY(deck_payloads(short_game) == exhausted);
    QCOMPARE(short_game.random_dealing().next_step, 4U);
}

void gameplay_session_tests::physical_jokers_index_without_count_or_segment() {
    session_configuration settings;
    settings.source = quiz_source::physical_joker;
    auto model
        = session::create(settings, configurations(1), { 1, 64 }).value();
    auto prepared = cards(10);
    prepared.cards = { 52, 0, 53, 5, 51, 13 };
    QVERIFY(model.prepare_deck(deck_id { 0 }, prepared));
    QVERIFY(model.set_traversal(std::array<std::size_t, 1> { 0 }));
    QVERIFY(model.start());
    const std::array<std::int64_t, 6> counts { 10, 11, 11, 11, 10, 11 };
    const std::array<std::uint64_t, 6> normal { 0, 1, 1, 2, 3, 4 };
    std::uint64_t segment_start = 0;
    for (std::size_t i = 0; i < prepared.cards.size(); ++i) {
        const auto step = model.deal_step();
        QCOMPARE(step.status, dealing_step_status::advanced);
        QCOMPARE(step.cards.size(), 1U);
        QCOMPARE(step.cards.front().face, prepared.cards[i]);
        QCOMPARE(step.cards.front().dealt_physical_cards, i + 1);
        QCOMPARE(step.cards.front().running_count, counts[i]);
        QCOMPARE(step.cards.front().completed, i == 5);
        const auto& deck = *model.deck(deck_id { 0 });
        QCOMPARE(deck.next_card, i + 1);
        QCOMPARE(deck.unverified_normal_cards, normal[i] - segment_start);
        QCOMPARE(deck.statistics.dealt_normal_cards, normal[i]);
        QCOMPARE(deck.statistics.verified_cards, 0U);
        QCOMPARE(deck.quiz.has_value(), prepared.cards[i] >= 52);
        if (deck.quiz) {
            const auto answer = model.skip_quiz(deck_id { 0 });
            QVERIFY(answer);
            QCOMPARE(answer->expected_count, counts[i]);
            segment_start = normal[i];
        }
    }
    const auto end = *model.deck(deck_id { 0 });
    QCOMPARE(end.status, deck_status::completed);
    QCOMPARE(end.running_count, 11);
    QCOMPARE(model.phase(), session_phase::finished);
    QCOMPARE(model.deal_step().status, dealing_step_status::not_running);
    QVERIFY(*model.deck(deck_id { 0 }) == end);
    // A literal last-card Joker is retained in the reveal/completion result;
    // this does not invent a final virtual event or score the untested tail.
    auto last_joker
        = session::create(settings, configurations(1), { 1, 64 }).value();
    prepared.cards = { 0, 52 };
    QVERIFY(last_joker.prepare_deck(deck_id { 0 }, prepared));
    QVERIFY(last_joker.set_traversal(std::array<std::size_t, 1> { 0 }));
    QVERIFY(last_joker.start());
    QCOMPARE(last_joker.deal_step().cards.front().face, 0);
    const auto final = last_joker.deal_step();
    QCOMPARE(final.cards.front().face, 52);
    QVERIFY(final.cards.front().completed);
    QVERIFY(!last_joker.deck(deck_id { 0 })->quiz);
    QVERIFY(!last_joker.current_quiz_batch());
    QCOMPARE(last_joker.deck(deck_id { 0 })->unverified_normal_cards, 1U);
}

void gameplay_session_tests::dealing_phase_and_arithmetic_guards_are_atomic() {
    auto model = ready_session();
    const auto before = deck_payloads(model);
    QCOMPARE(model.deal_step().status, dealing_step_status::not_running);
    QVERIFY(deck_payloads(model) == before);
    QVERIFY(model.start(17));
    QVERIFY(!model.start(99));
    QCOMPARE(model.random_dealing().seed, 17U);
    QVERIFY(model.pause());
    const auto anchor = model.sequential_anchor();
    QCOMPARE(model.deal_step().status, dealing_step_status::not_running);
    QVERIFY(deck_payloads(model) == before);
    QVERIFY(model.sequential_anchor() == anchor);
    QVERIFY(model.resume());
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(model.finish());
    const auto finished = deck_payloads(model);
    QCOMPARE(model.deal_step().status, dealing_step_status::not_running);
    QVERIFY(deck_payloads(model) == finished);

    for (const auto mode : { dealing_mode::sequential, dealing_mode::random,
                             dealing_mode::simultaneous }) {
        session_configuration settings;
        settings.dealing = mode;
        const std::size_t size = mode == dealing_mode::random ? 1 : 2;
        settings.sequential_count = size;
        for (const bool negative : { false, true }) {
            auto bad
                = session::create(settings, configurations(size), { size, 64 })
                      .value();
            std::vector<std::size_t> order(size);
            std::iota(order.begin(), order.end(), 0);
            QVERIFY(bad.set_traversal(order));
            for (std::size_t i = 0; i < size; ++i) {
                auto prepared = cards();
                prepared.cards = { 0 };
                if (i == size - 1) {
                    prepared.initial_running_count = negative
                        ? std::numeric_limits<std::int64_t>::min()
                        : std::numeric_limits<std::int64_t>::max();
                    prepared.rank_weights[0] = negative
                        ? std::numeric_limits<int>::min()
                        : std::numeric_limits<int>::max();
                }
                QVERIFY(bad.prepare_deck(deck_id { i }, prepared));
            }
            QVERIFY(bad.start(7));
            const auto retained = deck_payloads(bad);
            const auto random = bad.random_dealing();
            for (int retry = 0; retry < 2; ++retry) {
                const auto step = bad.deal_step();
                QCOMPARE(step.status, dealing_step_status::arithmetic_limit);
                QVERIFY(step.cards.empty());
                QVERIFY(deck_payloads(bad) == retained);
                QVERIFY(!bad.sequential_anchor());
                QVERIFY(bad.random_dealing() == random);
            }
        }
    }
    for (const auto face :
         { std::uint8_t { 0 }, std::uint8_t { 51 }, std::uint8_t { 52 } }) {
        session_configuration settings;
        settings.source = quiz_source::physical_joker;
        auto edge
            = session::create(settings, configurations(1), { 1, 64 }).value();
        const auto limit = face == 51
            ? std::numeric_limits<std::int64_t>::min()
            : std::numeric_limits<std::int64_t>::max();
        auto prepared
            = cards(face == 52 ? limit : limit + (face == 51 ? 1 : -1));
        prepared.cards = { face };
        QVERIFY(edge.prepare_deck(deck_id { 0 }, prepared));
        QVERIFY(edge.set_traversal(std::array<std::size_t, 1> { 0 }));
        QVERIFY(edge.start());
        const auto step = edge.deal_step();
        QCOMPARE(step.status, dealing_step_status::advanced);
        QCOMPARE(step.cards.front().running_count, limit);
    }
}

void gameplay_session_tests::
    dealing_infinite_rollover_preserves_owned_payload() {
    const auto& catalog = strategy_repository();
    session_configuration settings;
    settings.dealing = dealing_mode::simultaneous;
    const std::vector<deck_configuration> configured {
        { .strategy_slug = "hi_lo", .deck_count = 1 },
        { .strategy_slug = "uston_ss",
          .deck_count = 1,
          .infinite = true,
          .training = true },
    };
    auto model = session::create(settings, configured, { 2, 64 }).value();
    generation_policy policy;
    policy.infinite_chunk_decks = 1;
    // This regression isolates card rollover. G5's separate tests below
    // deliberately cross chunk boundaries with active schedule consumption.
    policy.minimum_quiz_gap = 1000;
    policy.maximum_quiz_gap = 1000;
    QVERIFY(prepare_generated_session(model, catalog, 42, policy));
    auto finite = model.deck(deck_id { 0 })->stream;
    finite.cards.resize(2);
    finite.virtual_quiz_targets.clear();
    QVERIFY(model.prepare_deck(deck_id { 0 }, finite));
    QVERIFY(model.set_show_count(deck_id { 1 }, true));
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    const auto* address = model.deck(deck_id { 1 });
    const auto original = *address;
    auto reference = original; // G3 helper already has independent
                               // composition/golden tests.
    QVERIFY(model.start());
    auto count = original.running_count;
    std::optional<deck_state> completed;
    for (std::uint64_t revealed = 1; revealed <= 160; ++revealed) {
        if (reference.next_card == reference.stream.cards.size()) {
            QVERIFY(rollover_infinite_shoe(reference, settings.source, policy));
        }
        const auto expected = reference.stream.cards[reference.next_card++];
        count += original.stream.rank_weights[expected % 13];
        const auto step = model.deal_step();
        QCOMPARE(step.status, dealing_step_status::advanced);
        QCOMPARE(step.cards.size(), revealed <= 2 ? 2U : 1U);
        const auto& event = step.cards.back();
        QVERIFY(event.owner == deck_id { 1 });
        QCOMPARE(event.face, expected);
        QCOMPARE(event.running_count, count);
        QCOMPARE(event.dealt_physical_cards, revealed);
        QVERIFY(!event.completed);
        const auto& deck = *model.deck(deck_id { 1 });
        QCOMPARE(&deck, address);
        QCOMPARE(deck.status, deck_status::active);
        QCOMPARE(deck.stream.infinite_cards->chunk, (revealed - 1) / 52);
        QCOMPARE(deck.next_card, (revealed - 1) % 52 + 1);
        QCOMPARE(deck.statistics.dealt_normal_cards, revealed);
        QCOMPARE(deck.unverified_normal_cards, revealed);
        QCOMPARE(deck.statistics.verified_cards, 0U);
        QCOMPARE(deck.latest_input, original.latest_input);
        QVERIFY(deck.show_count);
        QVERIFY(
            deck.stream.quiz_continuation == original.stream.quiz_continuation
        );
        QVERIFY(
            deck.stream.virtual_quiz_targets
            == original.stream.virtual_quiz_targets
        );
        QCOMPARE(deck.stream.cards.size(), 52U);
        QCOMPARE(deck.stream.infinite_cards->lookahead.size(), 52U);
        if (revealed == 52) {
            QCOMPARE(count, 0);
        }
        if (revealed == 104) {
            QCOMPARE(count, 4);
        }
        if (revealed == 2) {
            completed = *model.deck(deck_id { 0 });
        }
        if (completed) {
            QVERIFY(*model.deck(deck_id { 0 }) == *completed);
        }
    }
}

void gameplay_session_tests::
    late_rollover_failure_keeps_whole_step_unchanged() {
    session_configuration settings;
    settings.dealing = dealing_mode::simultaneous;
    auto configured = configurations(2);
    for (auto& deck : configured) {
        deck.infinite = true;
    }
    auto model = session::create(settings, configured, { 2, 64 }).value();
    for (std::size_t i = 0; i < 2; ++i) {
        auto prepared = prepared_cards_for(model, deck_id { i });
        if (i == 1) {
            // Setup checks shape, not an externally restored recipe. Force
            // the representable-counter boundary without millions of draws.
            prepared.infinite_cards->chunk
                = std::numeric_limits<std::uint64_t>::max() - 1;
        }
        QVERIFY(model.prepare_deck(deck_id { i }, prepared));
    }
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    QVERIFY(model.start());
    for (int i = 0; i < 52; ++i) {
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
        for (const auto id : model.unresolved_quizzes()) {
            QVERIFY(model.skip_quiz(id));
        }
    }
    const auto before = deck_payloads(model);
    const auto random = model.random_dealing();
    for (int retry = 0; retry < 2; ++retry) {
        const auto rejected = model.deal_step();
        QCOMPARE(rejected.status, dealing_step_status::invalid_stream);
        QVERIFY(rejected.cards.empty());
        QVERIFY(deck_payloads(model) == before);
        QVERIFY(model.random_dealing() == random);
        QVERIFY(!model.sequential_anchor());
    }
}

void gameplay_session_tests::
    dealing_consumes_actual_multi_cadence_targets_once() {
    for (const auto mode :
         { dealing_mode::sequential, dealing_mode::simultaneous,
           dealing_mode::random }) {
        session_configuration settings;
        settings.failure = failure_policy::block;
        settings.scope = quiz_scope::multi;
        settings.dealing = mode;
        settings.sequential_count = 2;
        auto configured = configurations(2);
        configured[1].training = true;
        auto model = session::create(settings, configured, { 2, 64 }).value();
        auto prepared = cards();
        prepared.cards.assign(32, 0);
        const std::array<std::uint64_t, 2> targets { 2, 4 };
        QVERIFY(
            model.prepare_decks(std::array { prepared, prepared }, targets)
        );
        QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 1, 0 }));
        QVERIFY(model.start(1234));
        bool reached = false;
        std::size_t batches = 0;
        for (int i = 0; i < 16; ++i) {
            QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
            const auto a = model.deck(deck_id { 0 })->dealt_physical_cards;
            const auto b = model.deck(deck_id { 1 })->dealt_physical_cards;
            const auto expected = mode == dealing_mode::random
                ? ((a + b) / 2 >= 2 && std::min(a, b) >= 1)
                : std::min(a, b) >= 2;
            QCOMPARE(model.multi_quiz_target_reached(0), expected);
            QCOMPARE(model.multi_quiz_target_reached(0), expected);
            reached = reached || expected;
            QVERIFY(std::ranges::equal(model.table_quiz_targets(), targets));
            const auto previous_cursor = batches;
            const auto ready_count
                = static_cast<std::size_t>(model.multi_quiz_target_reached(0))
                + static_cast<std::size_t>(model.multi_quiz_target_reached(1));
            QCOMPARE(model.next_table_quiz_target(), ready_count);
            if (ready_count != previous_cursor) {
                QVERIFY(model.deck(deck_id { 0 })->quiz);
                QVERIFY(model.deck(deck_id { 1 })->quiz);
                ++batches;
                for (const auto id : model.unresolved_quizzes()) {
                    QVERIFY(model.skip_quiz(id));
                }
            } else {
                QVERIFY(!model.current_quiz_batch());
            }
        }
        QVERIFY(reached);
        QCOMPARE(batches, 2U);
    }
    // Natural completion of a shorter deck removes it from future exposure.
    session_configuration settings;
    settings.scope = quiz_scope::multi;
    settings.dealing = dealing_mode::simultaneous;
    auto model
        = session::create(settings, configurations(2), { 2, 64 }).value();
    auto short_shoe = cards();
    short_shoe.cards = { 0 };
    const auto longer = cards();
    QVERIFY(model.prepare_decks(
        std::array { short_shoe, longer }, std::array<std::uint64_t, 1> { 2 }
    ));
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    QVERIFY(model.start());
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(!model.multi_quiz_target_reached(0));
    QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
    QVERIFY(model.multi_quiz_target_reached(0));
    QCOMPARE(model.deck(deck_id { 0 })->status, deck_status::completed);
    QCOMPARE(model.deck(deck_id { 0 })->dealt_physical_cards, 1U);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
