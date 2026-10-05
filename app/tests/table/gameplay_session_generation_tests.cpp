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

void gameplay_session_tests::
    strategy_counts_are_automatic_limits_are_advisory() {
    const auto& catalog = strategy_repository();
    QVERIFY2(catalog.is_valid(), qPrintable(catalog.diagnostic_summary()));
    const std::array<std::uint8_t, 4> dealt_cards { 0, 13, 26, 39 };
    for (const auto& strategy : catalog.strategies) {
        // Min/ideal metadata is advice, not a restriction or an implicit edit.
        for (const std::size_t count : { 1U, 6U, 32U }) {
            deck_configuration configuration {
                .strategy_slug = strategy.slug.toStdString(),
                .deck_count = count,
            };
            const auto parameters = resolve_strategy(catalog, configuration);
            QVERIFY(parameters);
            QCOMPARE(
                parameters->recommended_decks,
                static_cast<std::size_t>(strategy.min_decks)
            );
            QVERIFY(
                std::equal(
                    parameters->rank_weights.begin(),
                    parameters->rank_weights.end(), strategy.weights.begin()
                )
            );
            const auto expected = strategy.slug == QStringLiteral("uston_ss")
                ? -4 * static_cast<std::int64_t>(count)
                : 0;
            QCOMPARE(parameters->initial_running_count, expected);
            auto model
                = session::create({}, { configuration }, { 1, 64 }).value();
            QVERIFY(prepare_strategy_deck(
                model, deck_id { 0 }, catalog, dealt_cards
            ));
            const auto& state = *model.deck(deck_id { 0 });
            QVERIFY(state.configuration == configuration);
            QCOMPARE(state.running_count, expected);
            QCOMPARE(state.stream.initial_running_count, expected);
            QVERIFY(state.stream.rank_weights == parameters->rank_weights);
            QVERIFY(
                std::equal(
                    state.stream.cards.begin(), state.stream.cards.end(),
                    dealt_cards.begin()
                )
            );
            QCOMPARE(state.next_card, 0U);
        }
    }
}

void gameplay_session_tests::
    strategy_rejects_invalid_or_locked_edits_atomically() {
    const auto& catalog = strategy_repository();
    auto model = session::create(
                     {}, { { .strategy_slug = "uston_ss", .deck_count = 6 } },
                     { 1, 64 }
    )
                     .value();
    const std::array<std::uint8_t, 4> valid_cards { 0, 51, 7, 25 };
    const std::array<std::uint8_t, 1> invalid_card { 52 };
    QVERIFY(prepare_strategy_deck(model, deck_id { 0 }, catalog, valid_cards));
    const auto before = *model.deck(deck_id { 0 });
    QVERIFY(!prepare_strategy_deck(model, deck_id { 0 }, catalog, {}));
    QVERIFY(
        !prepare_strategy_deck(model, deck_id { 0 }, catalog, invalid_card)
    );
    QVERIFY(!prepare_strategy_deck(model, deck_id { 1 }, catalog, valid_cards));
    auto invalid_catalog = catalog;
    invalid_catalog.diagnostics.push_back(QStringLiteral("Bad formula"));
    QVERIFY(!prepare_strategy_deck(
        model, deck_id { 0 }, invalid_catalog, valid_cards
    ));
    QVERIFY(*model.deck(deck_id { 0 }) == before);
    QVERIFY(!resolve_strategy(catalog, { .strategy_slug = "not_a_strategy" }));
    QVERIFY(!resolve_strategy(
        catalog, { .strategy_slug = "uston_ss", .deck_count = 0 }
    ));
    if (std::numeric_limits<std::size_t>::max()
        > std::numeric_limits<std::int64_t>::max() / 4) {
        QVERIFY(!resolve_strategy(
            catalog,
            { .strategy_slug = "uston_ss",
              .deck_count = std::numeric_limits<std::size_t>::max() }
        ));
    }
    QVERIFY(model.set_traversal(std::array<std::size_t, 1> { 0 }));
    QVERIFY(model.start());
    QVERIFY(!prepare_strategy_deck(model, deck_id { 0 }, catalog, valid_cards));
    QVERIFY(model.pause());
    QVERIFY(!prepare_strategy_deck(model, deck_id { 0 }, catalog, valid_cards));
    QVERIFY(model.finish());
    QVERIFY(!prepare_strategy_deck(model, deck_id { 0 }, catalog, valid_cards));
    QVERIFY(*model.deck(deck_id { 0 }) == before);
}

void gameplay_session_tests::
    strategy_recommendations_are_explicit_fresh_preset_defaults() {
    const auto& catalog = strategy_repository();
    const deck_configuration chosen {
        .strategy_slug = "uston_ss",
        .deck_count = 1,
        .infinite = true,
        .training = true,
    };
    const auto recommended = recommended_configuration(catalog, chosen);
    QVERIFY(recommended);
    QCOMPARE(recommended->deck_count, 4U);
    QCOMPARE(recommended->strategy_slug, chosen.strategy_slug);
    QCOMPARE(recommended->infinite, chosen.infinite);
    QCOMPARE(recommended->training, chosen.training);
    QCOMPARE(chosen.deck_count, 1U);
    auto model = session::create({}, { *recommended }, { 1, 64 }).value();
    // Editing the preset back below its recommendation remains valid.
    QVERIFY(model.configure_deck(deck_id { 0 }, chosen));
    const std::array<std::uint8_t, 2> dealt_cards { 0, 51 };
    QVERIFY(!prepare_strategy_deck(model, deck_id { 0 }, catalog, dealt_cards));
    QVERIFY(prepare_generated_session(model, catalog, 1234));
    QCOMPARE(model.deck(deck_id { 0 })->running_count, -4);
    QCOMPARE(model.deck(deck_id { 0 })->configuration.deck_count, 1U);
    QVERIFY(
        !recommended_configuration(catalog, { .strategy_slug = "missing" })
    );
    QVERIFY(!recommended_configuration({}, chosen));
}

void gameplay_session_tests::
    fresh_presets_preserve_templates_and_validate_defaults() {
    const auto& catalog = strategy_repository();
    QVERIFY(catalog.is_valid());
    std::vector<deck_configuration> templates;
    for (const auto& strategy : catalog.strategies) {
        const auto index = templates.size();
        templates.push_back(
            { strategy.slug.toStdString(), index % 2 ? 17U : 1U, index % 2 == 0,
              index % 3 == 0 }
        );
    }
    const auto original = templates;
    auto settings = session_configuration {};
    settings.failure = failure_policy::block;
    settings.global_quiz_pause = false;
    settings.scope = quiz_scope::multi;
    settings.dealing = dealing_mode::random;
    settings.sequential_count = 3;
    settings.allow_skip = false;
    auto fresh = create_fresh_session(catalog, settings, templates, { 24, 64 });
    QVERIFY(fresh);
    QCOMPARE(fresh->configuration(), settings);
    QCOMPARE(fresh->slot_constraints(), (slot_limits { 24, 64 }));
    QCOMPARE(fresh->size(), templates.size());
    for (std::size_t index = 0; index < templates.size(); ++index) {
        const auto expected
            = recommended_configuration(catalog, templates[index]);
        QVERIFY(expected);
        const auto* deck = fresh->deck({ index });
        QVERIFY(deck);
        QCOMPARE(deck->configuration, *expected);
        QVERIFY(deck->stream.cards.empty());
        QCOMPARE(deck->running_count, 0);
        QVERIFY(!deck->show_count);
        QVERIFY(fresh->slot_for({ index }) == physical_slot_id { index });
    }
    QVERIFY(!fresh->start()); // defaults never generate/launch a session
    QCOMPARE(templates, original);

    // Existing choices use the ordinary constructor, not the opt-in factory.
    const auto existing = session::create(settings, templates, { 24, 64 });
    QVERIFY(existing);
    for (std::size_t index = 0; index < templates.size(); ++index)
        QCOMPARE(existing->deck({ index })->configuration, original[index]);
    auto changed = fresh->deck({ 0 })->configuration;
    changed.deck_count = 1;
    QVERIFY(fresh->configure_deck({ 0 }, changed));
    QCOMPARE(fresh->deck({ 0 })->configuration.deck_count, 1U);

    QVERIFY(!create_fresh_session(catalog, settings, {}, { 24, 64 }));
    QVERIFY(!create_fresh_session(catalog, settings, templates, { 1, 64 }));
    auto consent = settings;
    consent.allow_extra_slots = true;
    QVERIFY(create_fresh_session(catalog, consent, templates, { 1, 64 }));
    QVERIFY(!create_fresh_session(catalog, consent, templates, { 1, 19 }));
    auto invalid_settings = settings;
    invalid_settings.sequential_count = 0;
    QVERIFY(
        !create_fresh_session(catalog, invalid_settings, templates, { 24, 64 })
    );
    QVERIFY(!create_fresh_session({}, settings, templates, { 24, 64 }));
    auto invalid = templates;
    invalid.back().strategy_slug = "missing";
    QVERIFY(!create_fresh_session(catalog, settings, invalid, { 24, 64 }));
    QCOMPARE(invalid.back().strategy_slug, std::string("missing"));
    for (const auto malformed : { 0, 1, 2 }) {
        auto bad_catalog = catalog;
        auto& entry = bad_catalog.strategies.front();
        if (malformed == 0)
            entry.min_decks = 0;
        else if (malformed == 1)
            entry.weights.removeLast();
        else {
            entry.min_decks = 4;
            entry.initial_count = strategy_initial_count {
                std::numeric_limits<std::int64_t>::max(), true
            };
        }
        QVERIFY(!create_fresh_session(
            bad_catalog, {},
            std::span<const deck_configuration>(templates).first(1), { 1, 64 }
        ));
    }
    QCOMPARE(templates, original);
}

void gameplay_session_tests::
    generated_shoes_have_exact_composition_and_replay() {
    for (const auto source :
         { quiz_source::virtual_interrupt, quiz_source::physical_joker }) {
        const std::size_t faces
            = source == quiz_source::physical_joker ? 54 : 52;
        for (const std::size_t decks : { 1U, 4U, 32U }) {
            for (std::uint64_t seed = 0; seed < 32; ++seed) {
                const auto shoe
                    = generate_shoe(source, decks, seed, deck_id { 0 });
                QVERIFY(shoe);
                QCOMPARE(shoe->size(), faces * decks);
                std::array<std::size_t, 54> counts {};
                for (const auto face : *shoe) {
                    QVERIFY(face < faces);
                    ++counts[face];
                    if (face < 52) {
                        QCOMPARE(normal_card_rank(face).value(), face % 13U);
                    } else {
                        QVERIFY(!normal_card_rank(face));
                    }
                }
                for (std::size_t face = 0; face < counts.size(); ++face) {
                    QCOMPARE(counts[face], face < faces ? decks : 0U);
                }
                QVERIFY(
                    shoe == generate_shoe(source, decks, seed, deck_id { 0 })
                );
            }
        }
    }
    const auto shoe
        = generate_shoe(quiz_source::virtual_interrupt, 4, 1234, deck_id { 0 });
    const std::array<std::uint8_t, 12> replay_prefix { 46, 6,  3,  29, 16, 37,
                                                       13, 28, 36, 5,  38, 32 };
    QVERIFY(
        std::equal(replay_prefix.begin(), replay_prefix.end(), shoe->begin())
    );
    QVERIFY(
        shoe
        != generate_shoe(quiz_source::virtual_interrupt, 4, 1234, deck_id { 1 })
    );
    QVERIFY(
        shoe
        != generate_shoe(quiz_source::virtual_interrupt, 4, 1235, deck_id { 0 })
    );
    QVERIFY(
        shoe
        != generate_shoe(
            quiz_source::virtual_interrupt, 4,
            1234 + (std::uint64_t { 1 } << 32), deck_id { 0 }
        )
    );
    QVERIFY(!normal_card_rank(54));
    QVERIFY(!normal_card_rank(255));
}

void gameplay_session_tests::
    generated_targets_are_independent_end_before_exhaustion() {
    const auto first = generate_quiz_targets(208, 1234, deck_id { 0 });
    const auto second = generate_quiz_targets(208, 1234, deck_id { 1 });
    const auto table = generate_quiz_targets(208, 1234, std::nullopt);
    QVERIFY(first && second && table);
    QVERIFY(
        first.value()
        == std::vector<std::uint64_t>({ 30, 59, 96, 132, 166, 197 })
    );
    QVERIFY(
        second.value()
        == std::vector<std::uint64_t>({ 37, 67, 87, 121, 145, 175, 203 })
    );
    QVERIFY(
        table.value()
        == std::vector<std::uint64_t>({ 39, 72, 110, 134, 162, 182 })
    );
    QVERIFY(first != second);
    QVERIFY(first != table);
    QVERIFY(second != table);
    // Another owner/card generation and a shorter horizon cannot consume this
    // schedule's random stream. A longer horizon retains the same prefix.
    const auto longer = generate_quiz_targets(520, 1234, deck_id { 0 });
    QVERIFY(longer);
    QVERIFY(std::equal(first->begin(), first->end(), longer->begin()));
    QVERIFY(
        generate_shoe(quiz_source::virtual_interrupt, 4, 1234, deck_id { 0 })
    );
    QVERIFY(first == generate_quiz_targets(208, 1234, deck_id { 0 }));
    std::vector<std::uint64_t> gaps;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const auto targets = generate_quiz_targets(520, seed, deck_id { 3 });
        QVERIFY(targets);
        std::uint64_t previous = 0;
        for (const auto target : *targets) {
            QVERIFY(target > previous && target < 520);
            const auto gap = target - previous;
            QVERIFY(gap >= 20 && gap <= 40);
            gaps.push_back(gap);
            previous = target;
        }
    }
    QVERIFY(
        *std::min_element(gaps.begin(), gaps.end())
        != *std::max_element(gaps.begin(), gaps.end())
    );
    generation_policy boundary;
    boundary.minimum_quiz_gap = 1;
    boundary.maximum_quiz_gap = 1;
    QVERIFY(generate_quiz_targets(1, 0, deck_id { 0 }, boundary)->empty());
    QVERIFY(
        generate_quiz_targets(4, 0, deck_id { 0 }, boundary).value()
        == std::vector<std::uint64_t>({ 1, 2, 3 })
    );
    QVERIFY(generate_quiz_targets(20, 1234, deck_id { 0 })->empty());
}

void gameplay_session_tests::generation_rejects_invalid_inputs_and_bounds() {
    const auto source = quiz_source::virtual_interrupt;
    const deck_id id { 0 };
    QVERIFY(!generate_shoe(source, 0, 0, id));
    QVERIFY(
        !generate_shoe(source, std::numeric_limits<std::size_t>::max(), 0, id)
    );
    QVERIFY(!generate_shoe(static_cast<quiz_source>(99), 1, 0, id));
    QVERIFY(!generate_quiz_targets(0, 0, id));
    QVERIFY(
        !generate_quiz_targets(std::numeric_limits<std::uint64_t>::max(), 0, id)
    );
    generation_policy policy;
    policy.maximum_cards = 52;
    QVERIFY(generate_shoe(source, 1, 0, id, policy));
    QVERIFY(!generate_shoe(quiz_source::physical_joker, 1, 0, id, policy));
    QVERIFY(!generate_shoe(source, 2, 0, id, policy));
    QVERIFY(!generate_quiz_targets(53, 0, id, policy));
    policy.maximum_targets = 1;
    QVERIFY(!generate_quiz_targets(52, 0, id, policy));
    QVERIFY(generate_quiz_targets(40, 0, id, policy));
    for (int invalid = 0; invalid < 4; ++invalid) {
        policy = {};
        if (invalid == 0)
            policy.minimum_quiz_gap = 0;
        if (invalid == 1)
            policy.maximum_quiz_gap = policy.minimum_quiz_gap - 1;
        if (invalid == 2)
            policy.maximum_cards = 0;
        if (invalid == 3)
            policy.maximum_targets = 0;
        QVERIFY(!generate_shoe(source, 1, 0, id, policy));
        QVERIFY(!generate_quiz_targets(52, 0, id, policy));
    }
    policy = {};
    policy.minimum_quiz_gap = std::numeric_limits<std::uint32_t>::max();
    policy.maximum_quiz_gap = policy.minimum_quiz_gap;
    QVERIFY(generate_quiz_targets(52, 0, id, policy)->empty());
}

void gameplay_session_tests::
    finite_preparation_resolves_strategies_and_preserves_identity() {
    auto decks = std::vector<deck_configuration> {
        { .strategy_slug = "uston_ss", .deck_count = 1, .training = true },
        { .strategy_slug = "hi_lo", .deck_count = 32 },
    };
    const auto& catalog = strategy_repository();
    auto model = session::create({}, decks, { 2, 64 }).value();
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 1, 0 }));
    QVERIFY(model.set_show_count(deck_id { 0 }, true));
    const auto* stable = model.deck(deck_id { 0 });
    QVERIFY(prepare_finite_session(model, catalog, 1234));
    QCOMPARE(stable, model.deck(deck_id { 0 }));
    QCOMPARE(stable->running_count, -4);
    QVERIFY(stable->show_count);
    QVERIFY(stable->configuration == decks[0]);
    QVERIFY(
        stable->stream.cards
        == generate_shoe(quiz_source::virtual_interrupt, 1, 1234, deck_id { 0 })
               .value()
    );
    QVERIFY(
        stable->stream.virtual_quiz_targets
        == generate_quiz_targets(52, 1234, deck_id { 0 }).value()
    );
    QVERIFY(
        stable->stream.rank_weights
        == resolve_strategy(catalog, decks[0])->rank_weights
    );
    QCOMPARE(model.deck(deck_id { 1 })->stream.cards.size(), 32U * 52);
    QVERIFY(model.table_quiz_targets().empty());
    const auto payload = *stable;
    QVERIFY(prepare_finite_session(model, catalog, 1234));
    QVERIFY(*stable == payload);
    auto replay = session::create({}, decks, { 2, 64 }).value();
    QVERIFY(replay.set_show_count(deck_id { 0 }, true));
    QVERIFY(prepare_finite_session(replay, catalog, 1234));
    QVERIFY(*replay.deck(deck_id { 0 }) == payload);
    QVERIFY(model.start());
    QVERIFY(model.pause());
    QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 1 }));
    QVERIFY(*stable == payload); // schedule travels with deck, never its slot
    QVERIFY(model.slot_for(deck_id { 0 }) == physical_slot_id { 1 });
}

void gameplay_session_tests::
    finite_multi_preparation_owns_one_table_schedule() {
    session_configuration settings;
    settings.scope = quiz_scope::multi;
    std::vector<deck_configuration> decks {
        { .strategy_slug = "hi_lo", .deck_count = 1 },
        { .strategy_slug = "uston_ss", .deck_count = 4, .training = true },
    };
    const auto& catalog = strategy_repository();
    auto model = session::create(settings, decks, { 2, 64 }).value();
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    QVERIFY(prepare_finite_session(model, catalog, 1234));
    for (std::size_t i = 0; i < model.size(); ++i) {
        QVERIFY(model.deck(deck_id { i })->stream.virtual_quiz_targets.empty());
    }
    const auto targets = generate_quiz_targets(208, 1234, std::nullopt).value();
    QVERIFY(std::ranges::equal(model.table_quiz_targets(), targets));
    // Table ownership is unaffected by the order/identity of the longest shoe.
    std::reverse(decks.begin(), decks.end());
    auto reordered = session::create(settings, decks, { 2, 64 }).value();
    QVERIFY(prepare_finite_session(reordered, catalog, 1234));
    QVERIFY(
        std::ranges::equal(
            model.table_quiz_targets(), reordered.table_quiz_targets()
        )
    );
    const auto first = *model.deck(deck_id { 0 });
    QVERIFY(model.start());
    QVERIFY(model.pause());
    QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 1 }));
    QVERIFY(*model.deck(deck_id { 0 }) == first);
    QVERIFY(std::ranges::equal(model.table_quiz_targets(), targets));

    settings.source = quiz_source::physical_joker;
    auto physical = session::create(settings, decks, { 2, 64 }).value();
    QVERIFY(prepare_finite_session(physical, catalog, 1234));
    QVERIFY(physical.table_quiz_targets().empty());
    for (std::size_t i = 0; i < physical.size(); ++i) {
        QVERIFY(
            physical.deck(deck_id { i })->stream.virtual_quiz_targets.empty()
        );
        QCOMPARE(
            physical.deck(deck_id { i })->stream.cards.size(),
            decks[i].deck_count * 54
        );
    }
}

void gameplay_session_tests::finite_preparation_failure_is_atomic_and_locked() {
    const auto& catalog = strategy_repository();
    const auto decks = std::vector<deck_configuration> {
        { .strategy_slug = "hi_lo", .deck_count = 1 },
        { .strategy_slug = "uston_ss", .deck_count = 4 },
    };
    session_configuration settings;
    settings.scope = quiz_scope::multi;
    auto model = session::create(settings, decks, { 2, 64 }).value();
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    QVERIFY(prepare_finite_session(model, catalog, 7));
    const auto first = *model.deck(deck_id { 0 });
    const auto second = *model.deck(deck_id { 1 });
    const std::vector<std::uint64_t> targets(
        model.table_quiz_targets().begin(), model.table_quiz_targets().end()
    );
    generation_policy policy;
    policy.maximum_cards = 259; // each fits separately; entire roster does not
    QVERIFY(!prepare_finite_session(model, catalog, 1234, policy));
    policy.maximum_cards = 260;
    QVERIFY(prepare_finite_session(model, catalog, 7, policy));
    policy.maximum_targets = 1;
    QVERIFY(!prepare_finite_session(model, catalog, 1234, policy));
    auto invalid_catalog = catalog;
    const auto invalid_strategy = std::find_if(
        invalid_catalog.strategies.begin(), invalid_catalog.strategies.end(),
        [](const strategy_data& entry) {
            return entry.slug == QStringLiteral("uston_ss");
        }
    );
    QVERIFY(invalid_strategy != invalid_catalog.strategies.end());
    invalid_strategy->initial_count
        = strategy_initial_count { std::numeric_limits<std::int64_t>::max(),
                                   true };
    // The later deck fails count arithmetic after the first strategy resolves.
    QVERIFY(!prepare_finite_session(model, invalid_catalog, 1234));
    invalid_catalog = catalog;
    invalid_catalog.diagnostics.push_back(QStringLiteral("Invalid catalogue"));
    QVERIFY(!prepare_finite_session(model, invalid_catalog, 1234));
    QVERIFY(*model.deck(deck_id { 0 }) == first);
    QVERIFY(*model.deck(deck_id { 1 }) == second);
    QVERIFY(std::ranges::equal(model.table_quiz_targets(), targets));
    for (const auto& invalid : std::vector<deck_configuration> {
             { .strategy_slug = "not_a_strategy", .deck_count = 4 },
             { .strategy_slug = "hi_lo", .deck_count = 4, .infinite = true },
             { .strategy_slug = "hi_lo",
               .deck_count = std::numeric_limits<std::size_t>::max() },
         }) {
        auto bad = model;
        QVERIFY(bad.configure_deck(deck_id { 1 }, invalid));
        const auto retained = *bad.deck(deck_id { 0 });
        const auto edited = *bad.deck(deck_id { 1 });
        QVERIFY(!prepare_finite_session(bad, catalog, 1234));
        QVERIFY(*bad.deck(deck_id { 0 }) == retained);
        QVERIFY(*bad.deck(deck_id { 1 }) == edited);
        QVERIFY(bad.table_quiz_targets().empty());
    }
    QVERIFY(model.start());
    QVERIFY(!prepare_finite_session(model, catalog, 1234));
    QVERIFY(model.pause());
    QVERIFY(!prepare_finite_session(model, catalog, 1234));
    QVERIFY(model.finish());
    QVERIFY(!prepare_finite_session(model, catalog, 1234));
    QVERIFY(*model.deck(deck_id { 0 }) == first);
    QVERIFY(*model.deck(deck_id { 1 }) == second);
    QVERIFY(std::ranges::equal(model.table_quiz_targets(), targets));

    settings.scope = quiz_scope::single;
    auto single = session::create(settings, decks, { 2, 64 }).value();
    policy = {};
    policy.maximum_targets = 10; // bounds: 2 + 10, even though each fits alone
    QVERIFY(!prepare_finite_session(single, catalog, 1234, policy));
    QVERIFY(single.deck(deck_id { 0 })->stream.cards.empty());
    policy.maximum_targets = 12;
    QVERIFY(prepare_finite_session(single, catalog, 1234, policy));
}

void gameplay_session_tests::
    prepared_schedules_validate_ownership_and_invalidation() {
    auto model
        = session::create({}, { { .strategy_slug = "hi_lo" } }, { 1, 64 })
              .value();
    auto prepared = cards();
    prepared.virtual_quiz_targets = { 1, 3 };
    QVERIFY(model.prepare_decks(std::array { prepared }));
    const auto before = *model.deck(deck_id { 0 });
    for (const auto& invalid : std::vector<std::vector<std::uint64_t>> {
             { 0 },
             { 1, 1 },
             { 3, 2 },
             { 4 },
             { std::numeric_limits<std::uint64_t>::max() } }) {
        auto bad = prepared;
        bad.virtual_quiz_targets = invalid;
        QVERIFY(!model.prepare_deck(deck_id { 0 }, bad));
        QVERIFY(!model.prepare_decks(std::array { bad }));
        QVERIFY(*model.deck(deck_id { 0 }) == before);
    }
    QVERIFY(!model.prepare_decks({}));
    QVERIFY(!model.prepare_decks(
        std::array { prepared }, std::array<std::uint64_t, 1> { 1 }
    ));
    QVERIFY(*model.deck(deck_id { 0 }) == before);
    auto settings = model.configuration();
    settings.scope = quiz_scope::multi;
    QVERIFY(model.configure(settings));
    QVERIFY(model.deck(deck_id { 0 })->stream.cards.empty());
    QVERIFY(!model.prepare_deck(deck_id { 0 }, prepared)); // Single schedule
    prepared.virtual_quiz_targets.clear();
    QVERIFY(model.prepare_deck(deck_id { 0 }, prepared));
    QVERIFY(model.set_traversal(std::array<std::size_t, 1> { 0 }));
    QVERIFY(
        !model.start()
    ); // per-deck preparation cannot certify table cadence
    const std::array<std::uint64_t, 2> targets { 1, 3 };
    QVERIFY(model.prepare_decks(std::array { prepared }, targets));
    QVERIFY(!model.prepare_decks(
        std::array { prepared }, std::array<std::uint64_t, 1> { 4 }
    ));
    QVERIFY(std::ranges::equal(model.table_quiz_targets(), targets));
    QVERIFY(model.configure(settings)); // no-op preserves table preparation
    QVERIFY(model.configure_deck(
        deck_id { 0 }, model.deck(deck_id { 0 })->configuration
    ));
    QVERIFY(std::ranges::equal(model.table_quiz_targets(), targets));
    // Replacing just one shoe invalidates table targets even if its size
    // agrees.
    QVERIFY(model.prepare_deck(deck_id { 0 }, prepared));
    QVERIFY(model.table_quiz_targets().empty());
    QVERIFY(!model.start());
    QVERIFY(model.prepare_decks(std::array { prepared }, targets));
    auto changed = model.deck(deck_id { 0 })->configuration;
    changed.deck_count = 6;
    QVERIFY(model.configure_deck(deck_id { 0 }, changed));
    QVERIFY(model.table_quiz_targets().empty());
    QVERIFY(!model.start());
    // An explicitly prepared empty schedule is legitimate for a short shoe.
    QVERIFY(model.prepare_decks(std::array { prepared }));
    QVERIFY(model.start());
    const auto running = *model.deck(deck_id { 0 });
    QVERIFY(!model.prepare_decks(std::array { cards(99) }, targets));
    QVERIFY(*model.deck(deck_id { 0 }) == running);

    settings.source = quiz_source::physical_joker;
    auto physical = session::create(settings, { changed }, { 1, 64 }).value();
    prepared.virtual_quiz_targets = { 1 };
    QVERIFY(!physical.prepare_decks(std::array { prepared }));
    prepared.virtual_quiz_targets.clear();
    QVERIFY(!physical.prepare_decks(std::array { prepared }, targets));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
