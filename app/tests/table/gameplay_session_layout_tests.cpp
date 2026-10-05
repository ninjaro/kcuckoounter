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

void gameplay_session_tests::slot_recommendations_follow_actual_packing() {
    using packing::extent;
    using packing::orientation_constraint;
    const extent item { 88, 63 }; // Existing fallback/native theme aspect.
    QCOMPARE(recommend_slots({ 640, 480 }, item)->bounds.recommended, 6U);
    QCOMPARE(recommend_slots({ 1000, 700 }, item)->bounds.recommended, 18U);
    QCOMPARE(recommend_slots({ 1920, 1000 }, item)->bounds.recommended, 50U);
    QCOMPARE(
        recommend_slots({ 2560, 1400 }, item)->bounds.recommended,
        desktop_slot_technical_cap
    );
    // Area alone would suggest seven readable slots in this too-short strip.
    const auto strip = recommend_slots({ 1800, 140 }, item);
    QVERIFY(strip);
    QCOMPARE(strip->bounds.recommended, 1U);
    QVERIFY(!strip->minimum_size_met);
    QCOMPARE(
        recommend_slots(
            { 180, 1400 }, item, orientation_constraint::horizontal_only
        )
            ->bounds.recommended,
        1U
    );
    QCOMPARE(
        recommend_slots(
            { 180, 1400 }, item, orientation_constraint::vertical_only
        )
            ->bounds.recommended,
        6U
    );

    for (const auto container : { extent { 640, 480 },
                                  { 1000, 700 },
                                  { 1920, 1000 },
                                  { 2560, 1400 },
                                  { 360, 800 },
                                  { 1800, 140 },
                                  { 80, 80 } }) {
        for (const auto aspect : { item, extent { 3, 1 } }) {
            for (const auto orientation :
                 { orientation_constraint::allow_rotation,
                   orientation_constraint::horizontal_only,
                   orientation_constraint::vertical_only }) {
                const auto result
                    = recommend_slots(container, aspect, orientation);
                QVERIFY(result);
                QCOMPARE(
                    result->bounds.technical_cap, desktop_slot_technical_cap
                );
                QVERIFY(result->bounds.recommended >= 1);
                QVERIFY(
                    result->bounds.recommended <= desktop_slot_technical_cap
                );
                const auto fits = [&](std::size_t count) {
                    const auto packed = packing::pack_equal_rectangles(
                        { container, aspect, count, orientation }
                    );
                    return packed.complete(count)
                        && std::all_of(
                               packed.rectangles.begin(),
                               packed.rectangles.end(),
                               [](const auto& rectangle) {
                                   return std::min(
                                              rectangle.width, rectangle.height
                                          )
                                       >= recommended_slot_short_side;
                               }
                        );
                };
                if (!result->minimum_size_met) {
                    QCOMPARE(result->bounds.recommended, 1U);
                    QVERIFY(!fits(1));
                } else {
                    for (std::size_t count = 1;
                         count <= result->bounds.recommended; ++count)
                        QVERIFY(fits(count));
                    if (result->bounds.recommended < desktop_slot_technical_cap)
                        QVERIFY(!fits(result->bounds.recommended + 1));
                }
            }
        }
    }
    const auto smaller_target = recommend_slots(
        { 640, 480 }, item, orientation_constraint::allow_rotation, 100
    );
    QVERIFY(smaller_target && smaller_target->minimum_size_met);
    QVERIFY(smaller_target->bounds.recommended > 6);
    auto settings = session_configuration {};
    settings.allow_extra_slots = true;
    const auto bounds = recommend_slots({ 2560, 1400 }, item)->bounds;
    QVERIFY(session::create(settings, configurations(64), bounds));
    QVERIFY(!session::create(settings, configurations(65), bounds));
}

void gameplay_session_tests::slot_recommendations_reject_invalid_geometry() {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto infinity = std::numeric_limits<double>::infinity();
    for (const auto invalid : { 0.0, -1.0, nan, infinity }) {
        QVERIFY(!recommend_slots({ invalid, 700 }, { 88, 63 }));
        QVERIFY(!recommend_slots({ 1000, invalid }, { 88, 63 }));
        QVERIFY(!recommend_slots({ 1000, 700 }, { invalid, 63 }));
        QVERIFY(!recommend_slots({ 1000, 700 }, { 88, invalid }));
        QVERIFY(!recommend_slots(
            { 1000, 700 }, { 88, 63 },
            packing::orientation_constraint::allow_rotation, invalid
        ));
    }
    QVERIFY(!recommend_slots(
        { 1000, 700 }, { 88, 63 },
        static_cast<packing::orientation_constraint>(100)
    ));
    // Positive but unrepresentable by the renderer's packing quantization.
    QVERIFY(!recommend_slots({ 1e-100, 1e-100 }, { 88, 63 }));
    QVERIFY(!recommend_slots(
        { std::numeric_limits<double>::max(),
          std::numeric_limits<double>::max() },
        { 88, 63 }
    ));
}

void gameplay_session_tests::
    slot_limit_refresh_preserves_payload_requires_consent() {
    auto model = ready_session();
    const auto original = model.configuration();
    const auto prepared = deck_payloads(model);
    const auto original_bounds = model.slot_constraints();
    for (const slot_limits invalid :
         { slot_limits { 0, 64 }, { 65, 64 }, { 1, 3 }, { 0, 0 } }) {
        QVERIFY(!model.update_slot_limits(invalid));
        QCOMPARE(model.slot_constraints(), original_bounds);
        QCOMPARE(deck_payloads(model), prepared);
        QCOMPARE(model.configuration(), original);
    }
    // An environmental update cannot discard four configured/prepared decks.
    QVERIFY(model.update_slot_limits({ 2, 64 }));
    QVERIFY(model.update_slot_limits({ 2, 64 })); // no-op
    QCOMPARE(model.slot_constraints(), (slot_limits { 2, 64 }));
    QCOMPARE(model.size(), 4U);
    QCOMPARE(model.configuration(), original);
    QCOMPARE(deck_payloads(model), prepared);
    QVERIFY(!model.start());
    QCOMPARE(model.phase(), session_phase::setup);
    QCOMPARE(deck_payloads(model), prepared);
    auto consent = original;
    consent.allow_extra_slots = true;
    QVERIFY(model.configure(consent));
    QCOMPARE(deck_payloads(model), prepared);
    QVERIFY(!model.configure(original)); // cannot silently uncheck above limit
    QVERIFY(model.update_slot_limits({ 4, 64 }));
    QVERIFY(
        model.configure(original)
    ); // recommendation grows, override optional
    QVERIFY(model.start());
    for (const auto phase : { session_phase::running, session_phase::paused,
                              session_phase::finished }) {
        QCOMPARE(model.phase(), phase);
        QVERIFY(!model.update_slot_limits({ 1, 64 }));
        QVERIFY(!model.update_slot_limits({ 4, 64 }));
        QCOMPARE(model.slot_constraints(), (slot_limits { 4, 64 }));
        QCOMPARE(deck_payloads(model), prepared);
        if (phase == session_phase::running)
            QVERIFY(model.pause());
        if (phase == session_phase::paused)
            QVERIFY(model.finish());
    }
    // Consent itself, rather than a larger recommendation, also permits Start.
    auto overridden = ready_session();
    QVERIFY(overridden.update_slot_limits({ 1, 64 }));
    auto configuration = overridden.configuration();
    configuration.allow_extra_slots = true;
    QVERIFY(overridden.configure(configuration));
    QVERIFY(overridden.start());
}

void gameplay_session_tests::
    traversal_requires_a_complete_physical_permutation() {
    auto model = session::create({}, configurations(4), { 4, 64 }).value();
    for (std::size_t i = 0; i < model.size(); ++i) {
        QVERIFY(model.prepare_deck(deck_id { i }, cards()));
    }
    QVERIFY(!model.start()); // no widget-order fallback before packing
    QVERIFY(model.traversal().empty());
    QVERIFY(model.live_traversal().empty());
    const std::vector<std::size_t> order { 2, 0, 3, 1 };
    QVERIFY(model.set_traversal(order));
    const std::vector<physical_slot_id> expected { { 2 }, { 0 }, { 3 }, { 1 } };
    const std::vector<std::vector<std::size_t>> invalid {
        {},
        { 2, 0, 3 },
        { 2, 0, 3, 1, 4 },
        { 2, 0, 3, 3 },
        { 2, 0, 3, 4 },
        { 2, 0, 3, std::numeric_limits<std::size_t>::max() },
    };
    QVERIFY(model.start());
    QVERIFY(
        model.next_sequential_slots() == std::vector<physical_slot_id> { { 2 } }
    );
    for (const auto& bad : invalid) {
        QVERIFY(!model.set_traversal(bad));
        QVERIFY(std::ranges::equal(model.traversal(), expected));
        QVERIFY(model.sequential_anchor() == physical_slot_id { 2 });
    }
    QVERIFY(model.pause());
    QVERIFY(model.next_sequential_slots().empty());
    QVERIFY(model.sequential_anchor() == physical_slot_id { 2 });
    QVERIFY(model.finish());
    QVERIFY(!model.set_traversal(std::array<std::size_t, 4> { 0, 1, 2, 3 }));
    QVERIFY(model.next_sequential_slots().empty());
    QVERIFY(std::ranges::equal(model.traversal(), expected));
    for (const auto mode :
         { dealing_mode::random, dealing_mode::simultaneous }) {
        auto other = ready_session();
        auto settings = other.configuration();
        settings.dealing = mode;
        QVERIFY(other.configure(settings));
        QVERIFY(other.start());
        QVERIFY(other.next_sequential_slots().empty());
        QVERIFY(!other.sequential_anchor());
    }
}

void gameplay_session_tests::
    sequential_selection_handles_nondivisible_counts_and_holes() {
    // Compare to a simple filtered cyclic sequence over many M/N pairs. This
    // exercises the cursor only; card advancement/quiz blocking remains G4/G5.
    for (std::size_t m = 1; m <= 24; ++m) {
        for (std::size_t n = 1; n <= m; ++n) {
            for (bool holes : { false, true }) {
                session_configuration settings;
                settings.failure = failure_policy::block;
                settings.sequential_count = n;
                auto model
                    = session::create(settings, configurations(m), { m, m })
                          .value();
                std::vector<std::size_t> order(m);
                std::iota(order.rbegin(), order.rend(), std::size_t { 0 });
                QVERIFY(model.set_traversal(order));
                for (std::size_t i = 0; i < m; ++i) {
                    QVERIFY(model.prepare_deck(deck_id { i }, cards()));
                }
                QVERIFY(model.start());
                if (holes) {
                    for (std::size_t i = 0; i < m; i += 3) {
                        QVERIFY(model.complete_deck(deck_id { i }));
                    }
                    for (std::size_t i = 1; i < m; i += 3) {
                        QVERIFY(model.fail_deck(deck_id { i }));
                    }
                }
                std::vector<physical_slot_id> live;
                for (const auto index : order) {
                    if (!holes || index % 3 == 2) {
                        live.push_back(physical_slot_id { index });
                    }
                }
                QVERIFY(model.live_traversal() == live);
                std::size_t cursor = 0;
                for (std::size_t step = 0; step <= m; ++step) {
                    std::vector<physical_slot_id> expected;
                    for (std::size_t i = 0; i < std::min(n, live.size()); ++i) {
                        expected.push_back(live[cursor]);
                        cursor = (cursor + 1) % live.size();
                    }
                    QVERIFY(model.next_sequential_slots() == expected);
                    if (!expected.empty()) {
                        QVERIFY(model.sequential_anchor() == expected.back());
                    } else {
                        QVERIFY(!model.sequential_anchor());
                    }
                }
                for (std::size_t i = 0; i < m; ++i) {
                    QCOMPARE(model.deck(deck_id { i })->next_card, 0U);
                }
            }
        }
    }
}

void gameplay_session_tests::traversal_anchor_survives_swaps_and_repacking() {
    auto model = ready_session();
    const std::array<std::size_t, 4> order { 2, 0, 3, 1 };
    QVERIFY(model.set_traversal(order));
    QVERIFY(model.start());
    QVERIFY(
        model.next_sequential_slots() == std::vector<physical_slot_id> { { 2 } }
    );
    QVERIFY(model.complete_deck(deck_id { 2 })); // the physical anchor stops
    QVERIFY(
        model.next_sequential_slots() == std::vector<physical_slot_id> { { 0 } }
    );
    QVERIFY(model.fail_deck(deck_id { 0 }));
    QVERIFY(model.pause());
    const auto failed = *model.deck(deck_id { 0 });
    const auto training = *model.deck(deck_id { 1 });
    QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 1 }));
    QVERIFY(*model.deck(deck_id { 0 }) == failed);
    QVERIFY(*model.deck(deck_id { 1 }) == training);
    QVERIFY(model.sequential_anchor() == physical_slot_id { 0 });
    const std::vector<physical_slot_id> expected_live { { 0 }, { 3 } };
    QVERIFY(model.live_traversal() == expected_live); // Training now at slot 0
    QVERIFY(model.resume());
    QVERIFY(
        model.next_sequential_slots() == std::vector<physical_slot_id> { { 3 } }
    );
    const std::array<std::size_t, 4> repacked { 3, 2, 0, 1 };
    QVERIFY(model.set_traversal(repacked));
    QVERIFY(model.sequential_anchor() == physical_slot_id { 3 });
    QVERIFY(
        model.next_sequential_slots() == std::vector<physical_slot_id> { { 0 } }
    );
    // Same-layout notifications do not reset the cursor either.
    QVERIFY(model.set_traversal(repacked));
    QVERIFY(
        model.next_sequential_slots() == std::vector<physical_slot_id> { { 3 } }
    );
}

void gameplay_session_tests::
    packing_traversal_selects_slots_without_moving_decks() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.sequential_count = 3;
    auto model
        = session::create(settings, configurations(32), { 32, 64 }).value();
    const auto wide = packing::pack_equal_rectangles(
        {
            .container = { 1200.0, 600.0 },
            .item = { 88.0, 63.0 },
            .count = 32,
        }
    );
    QVERIFY(wide.complete(32));
    QVERIFY(model.set_traversal(wide.traversal));
    for (std::size_t i = 0; i < model.size(); ++i) {
        QVERIFY(model.prepare_deck(deck_id { i }, cards(static_cast<int>(i))));
    }
    QVERIFY(model.start());
    const auto selected = model.next_sequential_slots();
    QCOMPARE(selected.size(), 3U);
    for (std::size_t i = 0; i < selected.size(); ++i) {
        QCOMPARE(selected[i].value, wide.traversal[i]);
    }
    const auto anchor = *model.sequential_anchor();
    const auto tall = packing::pack_equal_rectangles(
        {
            .container = { 600.0, 1200.0 },
            .item = { 88.0, 63.0 },
            .count = 32,
        }
    );
    QVERIFY(tall.complete(32));
    QVERIFY(model.set_traversal(tall.traversal));
    const auto anchor_at
        = std::find(tall.traversal.begin(), tall.traversal.end(), anchor.value);
    QVERIFY(anchor_at != tall.traversal.end());
    const auto offset
        = static_cast<std::size_t>(anchor_at - tall.traversal.begin());
    const auto after_resize = model.next_sequential_slots();
    QCOMPARE(after_resize.size(), 3U);
    for (std::size_t i = 0; i < after_resize.size(); ++i) {
        QCOMPARE(
            after_resize[i].value,
            tall.traversal[(offset + 1 + i) % model.size()]
        );
    }
    for (std::size_t i = 0; i < model.size(); ++i) {
        QVERIFY(model.slot_for(deck_id { i }) == physical_slot_id { i });
        QCOMPARE(
            model.deck(deck_id { i })->running_count,
            static_cast<std::int64_t>(i)
        );
        QCOMPARE(wide.rectangles[i].source_index, i);
        QCOMPARE(tall.rectangles[i].source_index, i);
    }
}

void gameplay_session_tests::
    repack_preserves_physical_ids_and_current_geometry() {
    auto model = ready_session();
    layout_transition presentation(model);
    QVERIFY(!presentation.slot_motion(physical_slot_id { 0 }));
    QVERIFY(!presentation.deck_motion(deck_id { 0 }));
    const auto wide = layout(1200.0, 600.0);
    const auto tall = layout(600.0, 1200.0);
    const auto smaller = layout(400.0, 400.0);
    QVERIFY(presentation.repack(wide));
    for (std::size_t i = 0; i < model.size(); ++i) {
        const auto* motion = presentation.slot_motion(physical_slot_id { i });
        QVERIFY(same_geometry(motion->from, wide.rectangles[i]));
        QVERIFY(same_geometry(motion->to, wide.rectangles[i]));
        QVERIFY(same_geometry(*motion->sample(0.0), wide.rectangles[i]));
    }
    QVERIFY(model.start());
    const auto selected = model.next_sequential_slots();
    QCOMPARE(selected.front().value, wide.traversal.front());
    const auto anchor = model.sequential_anchor();
    QVERIFY(presentation.repack(tall));
    for (std::size_t i = 0; i < model.size(); ++i) {
        const auto* motion = presentation.slot_motion(physical_slot_id { i });
        QVERIFY(same_geometry(motion->from, wide.rectangles[i]));
        QVERIFY(same_geometry(motion->to, tall.rectangles[i]));
        const auto middle = *motion->sample(0.5);
        QCOMPARE(middle.x, std::midpoint(motion->from.x, motion->to.x));
        QCOMPARE(middle.y, std::midpoint(motion->from.y, motion->to.y));
    }
    std::vector<packing::rectangle> interrupted;
    for (std::size_t i = 0; i < model.size(); ++i) {
        interrupted.push_back(
            *presentation.slot_motion(physical_slot_id { i })->sample(0.25)
        );
    }
    QVERIFY(presentation.repack(smaller, 0.25));
    QVERIFY(model.sequential_anchor() == anchor);
    for (std::size_t i = 0; i < model.size(); ++i) {
        const auto* slot = presentation.slot_motion(physical_slot_id { i });
        const auto* deck = presentation.deck_motion(deck_id { i });
        QVERIFY(same_geometry(*slot->sample(0.0), interrupted[i]));
        QVERIFY(same_geometry(*slot->sample(1.0), smaller.rectangles[i]));
        QVERIFY(same_motion(*slot, *deck));
        QVERIFY(model.slot_for(deck_id { i }) == physical_slot_id { i });
        QCOMPARE(model.traversal()[i].value, smaller.traversal[i]);
        QCOMPARE(
            model.deck(deck_id { i })->running_count,
            static_cast<std::int64_t>(i)
        );
    }
}

void gameplay_session_tests::layout_rejects_invalid_updates_atomically() {
    auto model = ready_session();
    layout_transition presentation(model);
    const auto valid = layout(800.0, 600.0);
    QVERIFY(presentation.repack(valid));
    const auto before = *presentation.slot_motion(physical_slot_id { 0 });
    const auto before_order = std::vector<physical_slot_id>(
        model.traversal().begin(), model.traversal().end()
    );
    std::vector<packing::equal_packing_result> invalid;
    invalid.push_back({});
    auto bad = valid;
    bad.rectangles.pop_back();
    invalid.push_back(bad);
    bad = valid;
    bad.scale = std::numeric_limits<double>::infinity();
    invalid.push_back(bad);
    bad = valid;
    bad.rectangles[1].x = std::numeric_limits<double>::quiet_NaN();
    invalid.push_back(bad);
    bad = valid;
    bad.rectangles[1].width = 0.0;
    invalid.push_back(bad);
    bad = valid;
    bad.rectangles[1].x = std::numeric_limits<double>::max();
    bad.rectangles[1].width = std::numeric_limits<double>::max();
    invalid.push_back(bad);
    bad = valid;
    bad.traversal[1] = bad.traversal[0];
    invalid.push_back(bad);
    bad = valid;
    bad.traversal.clear();
    invalid.push_back(bad);
    for (const auto& result : invalid) {
        QVERIFY(!presentation.repack(result, 0.5));
        QVERIFY(same_motion(
            before, *presentation.slot_motion(physical_slot_id { 0 })
        ));
        QVERIFY(
            std::equal(
                before_order.begin(), before_order.end(),
                model.traversal().begin()
            )
        );
    }
    for (const auto progress : {
             -0.1,
             1.1,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(),
         }) {
        QVERIFY(!presentation.repack(valid, progress));
        QVERIFY(!before.sample(progress));
    }
    QVERIFY(!presentation.slot_motion(physical_slot_id { 4 }));
    QVERIFY(!presentation.deck_motion(deck_id { 4 }));
    QVERIFY(!presentation.swap_decks(deck_id { 0 }, deck_id { 1 }));
    QVERIFY(model.start());
    QVERIFY(!presentation.swap_decks(deck_id { 0 }, deck_id { 1 }));
    QVERIFY(model.pause());
    QVERIFY(!presentation.swap_decks(deck_id { 0 }, deck_id { 4 }));
    QVERIFY(!presentation.swap_decks(deck_id { 0 }, deck_id { 1 }, -0.1));
    QVERIFY(model.slot_for(deck_id { 0 }) == physical_slot_id { 0 });
    QVERIFY(
        same_motion(before, *presentation.slot_motion(physical_slot_id { 0 }))
    );
    QVERIFY(model.finish());
    QVERIFY(
        presentation.repack(valid)
    ); // geometry only; terminal game stays frozen
    QVERIFY(!presentation.swap_decks(deck_id { 0 }, deck_id { 1 }));
}

void gameplay_session_tests::
    layout_terminal_repack_preserves_frozen_gameplay() {
    auto model = ready_session();
    layout_transition presentation(model);
    const auto wide = layout(1200.0, 600.0);
    QVERIFY(presentation.repack(wide));
    QVERIFY(model.start());
    QVERIFY(model.pause());
    QVERIFY(presentation.swap_decks({ 0 }, { 3 }));
    QVERIFY(model.finish());
    const auto result = model.result();
    const auto first = *model.deck({ 0 });
    const std::vector<physical_slot_id> order(
        model.traversal().begin(), model.traversal().end()
    );
    const auto tall = layout(600.0, 900.0);
    QVERIFY(presentation.repack(tall));
    const auto& destination = presentation.deck_motion({ 0 })->to;
    QCOMPARE(destination.x, tall.rectangles[3].x);
    QCOMPARE(destination.width, tall.rectangles[3].width);
    QCOMPARE(model.result(), result);
    QCOMPARE(*model.deck({ 0 }), first);
    QVERIFY(std::ranges::equal(model.traversal(), order));
    QVERIFY(!model.set_traversal(tall.traversal));
    const auto motion = *presentation.deck_motion({ 0 });
    auto invalid = tall;
    invalid.traversal[1] = invalid.traversal[0];
    invalid.rectangles[3].x += 10.0;
    QVERIFY(!presentation.repack(invalid));
    invalid.traversal.clear();
    QVERIFY(!presentation.repack(invalid));
    QVERIFY(!presentation.swap_decks({ 0 }, { 1 }));
    QVERIFY(same_motion(motion, *presentation.deck_motion({ 0 })));
    QVERIFY(std::ranges::equal(model.traversal(), order));
    QCOMPARE(model.result(), result);
    QCOMPARE(*model.deck({ 0 }), first);
}

void gameplay_session_tests::layout_swap_moves_decks_between_fixed_slots() {
    auto model = ready_session();
    layout_transition presentation(model);
    const auto result = layout(1200.0, 600.0);
    QVERIFY(presentation.repack(result));
    QVERIFY(model.set_show_count(deck_id { 1 }, true));
    QVERIFY(model.start());
    QVERIFY(model.fail_deck(deck_id { 0 }));
    QVERIFY(model.complete_deck(deck_id { 2 }));
    const auto selected = model.next_sequential_slots();
    QVERIFY(!selected.empty());
    const auto anchor = model.sequential_anchor();
    QVERIFY(model.pause());
    std::vector<deck_state> payloads;
    for (std::size_t i = 0; i < model.size(); ++i) {
        payloads.push_back(*model.deck(deck_id { i }));
    }
    QVERIFY(presentation.swap_decks(deck_id { 0 }, deck_id { 1 }));
    QVERIFY(model.sequential_anchor() == anchor);
    QVERIFY(model.slot_for(deck_id { 0 }) == physical_slot_id { 1 });
    QVERIFY(model.slot_for(deck_id { 1 }) == physical_slot_id { 0 });
    const auto* first = presentation.deck_motion(deck_id { 0 });
    const auto* second = presentation.deck_motion(deck_id { 1 });
    QVERIFY(same_geometry(first->from, result.rectangles[0]));
    QVERIFY(same_geometry(first->to, result.rectangles[1]));
    QVERIFY(same_geometry(second->from, result.rectangles[1]));
    QVERIFY(same_geometry(second->to, result.rectangles[0]));
    // Intersections are deliberately not sent through final-packing validation.
    QVERIFY(packing::intersects(*first->sample(0.5), *second->sample(0.5)));
    for (std::size_t i = 0; i < model.size(); ++i) {
        const auto* slot = presentation.slot_motion(physical_slot_id { i });
        QVERIFY(same_geometry(slot->from, result.rectangles[i]));
        QVERIFY(same_geometry(slot->to, result.rectangles[i]));
        QVERIFY(*model.deck(deck_id { i }) == payloads[i]);
        QCOMPARE(model.traversal()[i].value, result.traversal[i]);
    }
    QVERIFY(presentation.swap_decks(deck_id { 2 }, deck_id { 3 }));
    QVERIFY(model.slot_for(deck_id { 2 }) == physical_slot_id { 3 });
    QVERIFY(model.slot_for(deck_id { 3 }) == physical_slot_id { 2 });
    QVERIFY(*model.deck(deck_id { 2 }) == payloads[2]);
    QVERIFY(*model.deck(deck_id { 3 }) == payloads[3]);
}

void gameplay_session_tests::
    layout_interleaved_swap_and_resize_are_continuous() {
    auto model = ready_session();
    layout_transition presentation(model);
    const auto wide = layout(1200.0, 600.0);
    const auto tall = layout(600.0, 1200.0);
    QVERIFY(presentation.repack(wide));
    QVERIFY(model.start());
    QVERIFY(model.pause());
    QVERIFY(presentation.swap_decks(deck_id { 0 }, deck_id { 1 }));
    const auto moving_deck
        = *presentation.deck_motion(deck_id { 0 })->sample(0.3);
    QVERIFY(presentation.repack(tall, 0.3));
    QVERIFY(same_geometry(
        presentation.deck_motion(deck_id { 0 })->from, moving_deck
    ));
    QVERIFY(same_geometry(
        presentation.deck_motion(deck_id { 0 })->to, tall.rectangles[1]
    ));
    const auto moving_slot
        = *presentation.slot_motion(physical_slot_id { 0 })->sample(0.4);
    const auto moving_back
        = *presentation.deck_motion(deck_id { 0 })->sample(0.4);
    QVERIFY(presentation.swap_decks(deck_id { 0 }, deck_id { 1 }, 0.4));
    QVERIFY(same_geometry(
        presentation.slot_motion(physical_slot_id { 0 })->from, moving_slot
    ));
    QVERIFY(same_geometry(
        presentation.slot_motion(physical_slot_id { 0 })->to, tall.rectangles[0]
    ));
    QVERIFY(same_geometry(
        presentation.deck_motion(deck_id { 0 })->from, moving_back
    ));
    QVERIFY(same_geometry(
        presentation.deck_motion(deck_id { 0 })->to, tall.rectangles[0]
    ));
    QVERIFY(model.slot_for(deck_id { 0 }) == physical_slot_id { 0 });
    QVERIFY(model.slot_for(deck_id { 1 }) == physical_slot_id { 1 });
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
