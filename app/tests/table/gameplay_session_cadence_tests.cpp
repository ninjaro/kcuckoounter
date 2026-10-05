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

void gameplay_session_tests::multi_cadence_uses_minimum_for_ordered_modes() {
    auto decks = exposure_decks({ 30, 31, 60, 40 });
    for (const auto mode :
         { dealing_mode::sequential, dealing_mode::simultaneous }) {
        QVERIFY(multi_quiz_target_reached(decks, mode, 30));
        QVERIFY(!multi_quiz_target_reached(decks, mode, 31));
        const auto before = decks;
        std::reverse(decks.begin(), decks.end());
        QVERIFY(multi_quiz_target_reached(decks, mode, 30));
        QVERIFY(!multi_quiz_target_reached(decks, mode, 31));
        std::reverse(decks.begin(), decks.end());
        QVERIFY(decks == before);
    }
    // Equal Simultaneous exposure gives the same threshold as min/mean/max.
    decks = exposure_decks({ 40, 40, 40, 40 });
    for (const auto mode :
         { dealing_mode::sequential, dealing_mode::simultaneous,
           dealing_mode::random }) {
        QVERIFY(multi_quiz_target_reached(decks, mode, 40));
        QVERIFY(!multi_quiz_target_reached(decks, mode, 41));
    }
    // A partial Sequential N/M round (N=3, M=4) cannot quiz the fourth deck
    // early.
    decks[3].dealt_physical_cards = 39;
    QVERIFY(!multi_quiz_target_reached(decks, dealing_mode::sequential, 40));
    decks[3].dealt_physical_cards = 40;
    QVERIFY(multi_quiz_target_reached(decks, dealing_mode::sequential, 40));
}

void gameplay_session_tests::random_cadence_uses_average_and_lower_guard() {
    const auto mode = dealing_mode::random;
    QVERIFY(!multi_quiz_target_reached(exposure_decks({ 0, 100 }), mode, 40));
    QVERIFY(multi_quiz_target_reached(exposure_decks({ 20, 60 }), mode, 40));
    // Odd targets require ceil(target / 2), not a rounded-down guard.
    QVERIFY(!multi_quiz_target_reached(exposure_decks({ 15, 47 }), mode, 31));
    QVERIFY(multi_quiz_target_reached(exposure_decks({ 16, 46 }), mode, 31));
    // Mean 30.5 must not be rounded to 31; the lower guard already passes.
    QVERIFY(!multi_quiz_target_reached(exposure_decks({ 16, 45 }), mode, 31));
    QVERIFY(multi_quiz_target_reached(exposure_decks({ 16, 45 }), mode, 30));
    QVERIFY(!multi_quiz_target_reached(exposure_decks({ 0, 2 }), mode, 1));
    QVERIFY(multi_quiz_target_reached(exposure_decks({ 1, 1 }), mode, 1));
    // Absolute cumulative exposure, not verified score or a per-batch reset.
    auto decks = exposure_decks({ 60, 120 });
    decks[0].unverified_normal_cards = 0;
    decks[1].unverified_normal_cards = 0;
    decks[0].statistics.verified_cards = 999;
    QVERIFY(multi_quiz_target_reached(decks, mode, 90));
}

void gameplay_session_tests::
    cadence_includes_training_and_excludes_stopped_decks() {
    auto decks = exposure_decks(
        { 15, 45, 0, std::numeric_limits<std::uint64_t>::max() }
    );
    decks[0].configuration.training = true;
    decks[2].status = deck_status::completed;
    decks[3].status = deck_status::failed;
    const auto before = decks;
    // Excluding Training would incorrectly see only the 45-card challenge deck.
    QVERIFY(!multi_quiz_target_reached(decks, dealing_mode::sequential, 16));
    QVERIFY(multi_quiz_target_reached(decks, dealing_mode::sequential, 15));
    QVERIFY(multi_quiz_target_reached(decks, dealing_mode::random, 30));
    QVERIFY(!multi_quiz_target_reached(decks, dealing_mode::random, 31));
    QVERIFY(decks == before);
    decks[0].configuration.training = false;
    QVERIFY(multi_quiz_target_reached(decks, dealing_mode::random, 30));
    decks[1].status = deck_status::failed;
    QVERIFY(multi_quiz_target_reached(decks, dealing_mode::random, 15));
    QVERIFY(!multi_quiz_target_reached(decks, dealing_mode::random, 16));
    decks[0].status = deck_status::completed;
    for (const auto mode :
         { dealing_mode::sequential, dealing_mode::simultaneous,
           dealing_mode::random }) {
        QVERIFY(!multi_quiz_target_reached(decks, mode, 1));
        QVERIFY(!multi_quiz_target_reached({}, mode, 1));
    }
}

void gameplay_session_tests::
    multi_cadence_reevaluates_current_roster_without_consumption() {
    const auto targets = generate_quiz_targets(104, 1234, std::nullopt).value();
    QVERIFY(!targets.empty());
    const auto schedule_before = targets;
    const auto target = targets.front(); // Prepared table target 39.
    auto decks = exposure_decks({ 10, 80 });
    QVERIFY(!multi_quiz_target_reached(decks, dealing_mode::random, target));
    QVERIFY(
        !multi_quiz_target_reached(decks, dealing_mode::sequential, target)
    );
    // Removing the lagging deck changes readiness without moving the target.
    decks[0].status = deck_status::completed;
    const auto before = decks;
    for (int repeat = 0; repeat < 3; ++repeat) {
        QVERIFY(multi_quiz_target_reached(decks, dealing_mode::random, target));
        QVERIFY(
            multi_quiz_target_reached(decks, dealing_mode::sequential, target)
        );
    }
    QVERIFY(decks == before);
    QVERIFY(targets == schedule_before);
    // Removing the leading deck lowers exposure. No monotonic cached mean.
    decks[0].status = deck_status::active;
    decks[1].status = deck_status::failed;
    QVERIFY(!multi_quiz_target_reached(decks, dealing_mode::random, target));
    QVERIFY(
        !multi_quiz_target_reached(decks, dealing_mode::sequential, target)
    );
    decks[1].status = deck_status::active;
    decks[0].status = deck_status::failed;
    QVERIFY(multi_quiz_target_reached(decks, dealing_mode::random, target));
    std::reverse(decks.begin(), decks.end());
    QVERIFY(multi_quiz_target_reached(decks, dealing_mode::random, target));
    QVERIFY(targets == schedule_before);
    decks = exposure_decks({ 20, 60 });
    QVERIFY(multi_quiz_target_reached(decks, dealing_mode::random, target));
    decks[1].status = deck_status::failed;
    QVERIFY(!multi_quiz_target_reached(decks, dealing_mode::random, target));
}

void gameplay_session_tests::
    multi_cadence_integer_boundaries_and_reference_oracle() {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    auto decks = exposure_decks({ maximum, maximum });
    QVERIFY(multi_quiz_target_reached(decks, dealing_mode::random, maximum));
    decks[1].dealt_physical_cards = maximum - 1;
    QVERIFY(!multi_quiz_target_reached(decks, dealing_mode::random, maximum));
    QVERIFY(
        multi_quiz_target_reached(decks, dealing_mode::random, maximum - 1)
    );
    decks = exposure_decks({ maximum, maximum, maximum - 2 });
    QVERIFY(!multi_quiz_target_reached(decks, dealing_mode::random, maximum));
    QVERIFY(
        multi_quiz_target_reached(decks, dealing_mode::random, maximum - 1)
    );
    QVERIFY(!multi_quiz_target_reached(decks, dealing_mode::random, 0));
    QVERIFY(
        !multi_quiz_target_reached(decks, static_cast<dealing_mode>(99), 1)
    );
    // Exhaustive small reference: ordinary sum is safe here and independent
    // from the production quotient/remainder accumulation.
    decks = exposure_decks({ 0, 0, 0 });
    for (std::uint64_t first = 0; first <= 8; ++first) {
        for (std::uint64_t second = 0; second <= 8; ++second) {
            for (std::uint64_t third = 0; third <= 8; ++third) {
                decks[0].dealt_physical_cards = first;
                decks[1].dealt_physical_cards = second;
                decks[2].dealt_physical_cards = third;
                const auto minimum = std::min({ first, second, third });
                const auto mean = (first + second + third) / 3;
                for (std::uint64_t target = 1; target <= 8; ++target) {
                    QCOMPARE(
                        multi_quiz_target_reached(
                            decks, dealing_mode::random, target
                        ),
                        mean >= target && minimum >= (target + 1) / 2
                    );
                    QCOMPARE(
                        multi_quiz_target_reached(
                            decks, dealing_mode::sequential, target
                        ),
                        minimum >= target
                    );
                    QCOMPARE(
                        multi_quiz_target_reached(
                            decks, dealing_mode::simultaneous, target
                        ),
                        minimum >= target
                    );
                }
            }
        }
    }
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

void gameplay_session_tests::
    cadence_query_preserves_preparation_and_lifecycle() {
    const auto& catalog = strategy_repository();
    session_configuration settings;
    settings.scope = quiz_scope::multi;
    settings.dealing = dealing_mode::random;
    settings.failure = failure_policy::block;
    const std::vector<deck_configuration> decks {
        { .strategy_slug = "hi_lo", .deck_count = 4 },
        { .strategy_slug = "uston_ss", .deck_count = 4, .training = true },
    };
    auto model = session::create(settings, decks, { 2, 64 }).value();
    QVERIFY(!model.multi_quiz_target_reached(0));
    QVERIFY(prepare_finite_session(model, catalog, 1234));
    QVERIFY(!model.table_quiz_targets().empty());
    const std::vector<std::uint64_t> targets(
        model.table_quiz_targets().begin(), model.table_quiz_targets().end()
    );
    const auto first = *model.deck(deck_id { 0 });
    const auto second = *model.deck(deck_id { 1 });
    QVERIFY(!model.multi_quiz_target_reached(0)); // Setup never creates events.
    QVERIFY(model.set_traversal(std::array<std::size_t, 2> { 1, 0 }));
    QVERIFY(model.start());
    // G4 has not dealt cards; evaluation must not manufacture exposure.
    QVERIFY(!model.multi_quiz_target_reached(0));
    QVERIFY(!model.multi_quiz_target_reached(targets.size()));
    QVERIFY(!model.multi_quiz_target_reached(
        std::numeric_limits<std::size_t>::max()
    ));
    QVERIFY(*model.deck(deck_id { 0 }) == first);
    QVERIFY(*model.deck(deck_id { 1 }) == second);
    QVERIFY(model.pause());
    QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 1 }));
    QVERIFY(!model.multi_quiz_target_reached(0));
    QVERIFY(std::ranges::equal(model.table_quiz_targets(), targets));
    QVERIFY(model.resume());
    QVERIFY(model.fail_deck(deck_id { 0 }));
    QVERIFY(model.complete_deck(deck_id { 1 }));
    QVERIFY(
        !model.multi_quiz_target_reached(0)
    ); // No live decks, no final quiz.
    QVERIFY(std::ranges::equal(model.table_quiz_targets(), targets));
    QCOMPARE(model.phase(), session_phase::finished);
    QVERIFY(model.result());
    QVERIFY(!model.finish());
    QVERIFY(!model.multi_quiz_target_reached(0));
    for (const auto source :
         { quiz_source::virtual_interrupt, quiz_source::physical_joker }) {
        for (const auto scope : { quiz_scope::single, quiz_scope::multi }) {
            settings.source = source;
            settings.scope = scope;
            auto other = session::create(settings, decks, { 2, 64 }).value();
            QVERIFY(prepare_finite_session(other, catalog, 1234));
            QVERIFY(other.set_traversal(std::array<std::size_t, 2> { 0, 1 }));
            QVERIFY(other.start());
            QVERIFY(!other.multi_quiz_target_reached(0));
        }
    }
}
