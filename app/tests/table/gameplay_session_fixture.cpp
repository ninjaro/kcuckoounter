#include "table/gameplay_session_fixture.hpp"

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
#include <initializer_list>
#include <numeric>
#include <type_traits>
#include <utility>

namespace gameplay_test {

using namespace gameplay;

static_assert(!std::is_convertible_v<deck_id, physical_slot_id>);
static_assert(!std::is_convertible_v<physical_slot_id, deck_id>);
static_assert(!std::is_convertible_v<std::size_t, deck_id>);
static_assert(
    session::minimum_pick_interval_ms
    == trainer_preferences::minimum_pickup_interval_ms
);
static_assert(
    session::maximum_pick_interval_ms
    == trainer_preferences::maximum_pickup_interval_ms
);

std::vector<deck_configuration> configurations(std::size_t count) {
    std::vector<deck_configuration> decks(count);
    for (std::size_t i = 0; i < count; ++i) {
        decks[i].strategy_slug = "strategy_" + std::to_string(i);
    }
    return decks;
}

prepared_deck cards(std::int64_t initial_count) {
    prepared_deck result;
    result.cards = { 0, 51, 7, 25 };
    result.rank_weights = { 1, 1, 1, 1, 1, 0, 0, 0, -1, -1, -1, -1, -1 };
    result.initial_running_count = initial_count;
    return result;
}

prepared_deck quiz_cards(std::vector<std::uint64_t> targets) {
    auto result = cards();
    result.cards.assign(8, 0);
    result.virtual_quiz_targets = std::move(targets);
    return result;
}

session quiz_session(
    session_configuration settings, const std::vector<prepared_deck>& streams,
    std::span<const std::uint64_t> table_targets,
    std::vector<deck_configuration> configured
) {
    if (configured.empty()) {
        configured = configurations(streams.size());
    }
    auto result
        = session::create(settings, configured, { streams.size(), 64 }).value();
    std::vector<std::size_t> order(streams.size());
    std::iota(order.begin(), order.end(), 0);
    if (!result.prepare_decks(streams, table_targets)
        || !result.set_traversal(order) || !result.start()) {
        qFatal("Invalid quiz fixture");
    }
    return result;
}

std::vector<deck_state> deck_payloads(const session& game) {
    std::vector<deck_state> result;
    for (std::size_t i = 0; i < game.size(); ++i) {
        result.push_back(*game.deck(deck_id { i }));
    }
    return result;
}

prepared_deck prepared_cards_for(
    const session& game, deck_id id, std::int64_t initial_count
) {
    auto prepared = cards(initial_count);
    if (!game.deck(id)->configuration.infinite) {
        return prepared;
    }
    generation_policy policy;
    policy.infinite_chunk_decks = 1;
    policy.minimum_quiz_gap = 1;
    policy.maximum_quiz_gap = 1;
    policy.quiz_page_targets = 2;
    auto shoe
        = generate_infinite_shoe(game.configuration().source, 0, id, policy)
              .value();
    prepared.cards = std::move(shoe.cards);
    prepared.infinite_cards = std::move(shoe.continuation);
    if (game.configuration().source == quiz_source::virtual_interrupt
        && game.configuration().scope == quiz_scope::single) {
        const auto page = generate_quiz_page(
                              { .seed = 0,
                                .minimum_gap = 1,
                                .maximum_gap = 1,
                                .page_size = 2 },
                              id, policy
        )
                              .value();
        prepared.virtual_quiz_targets = page.targets;
        prepared.quiz_continuation = page.continuation;
    }
    return prepared;
}

// Post-deal domain snapshots for the read-only cadence kernel, not restored
// checkpoints or an alternative mutable runtime model.
std::vector<deck_state>
exposure_decks(std::initializer_list<std::uint64_t> progress) {
    std::vector<deck_state> decks;
    decks.reserve(progress.size());
    for (const auto count : progress) {
        deck_state deck {};
        deck.id = deck_id { decks.size() };
        deck.configuration.strategy_slug = "hi_lo";
        deck.configuration.infinite = true;
        deck.dealt_physical_cards = count;
        decks.push_back(std::move(deck));
    }
    return decks;
}

session ready_session(failure_policy failure) {
    session_configuration settings;
    settings.failure = failure;
    auto decks = configurations(4);
    decks[1].training = true;
    decks[3].infinite = true;
    auto result = session::create(settings, decks, { 4, 64 }).value();
    if (!result.set_traversal(std::array<std::size_t, 4> { 0, 1, 2, 3 })) {
        qFatal("Invalid traversal fixture");
    }
    for (std::size_t i = 0; i < result.size(); ++i) {
        if (!result.prepare_deck(
                deck_id { i },
                prepared_cards_for(result, deck_id { i }, static_cast<int>(i))
            )) {
            qFatal("Invalid gameplay model fixture");
        }
    }
    return result;
}

packing::equal_packing_result layout(double width, double height) {
    return packing::pack_equal_rectangles(
        {
            .container = { width, height },
            .item = { 88.0, 63.0 },
            .count = 4,
        }
    );
}

bool same_geometry(
    const packing::rectangle& first, const packing::rectangle& second
) {
    return first.x == second.x && first.y == second.y
        && first.width == second.width && first.height == second.height;
}

bool same_motion(
    const rectangle_motion& first, const rectangle_motion& second
) {
    return same_geometry(first.from, second.from)
        && same_geometry(first.to, second.to)
        && first.from.rotated == second.from.rotated
        && first.to.rotated == second.to.rotated
        && first.from.source_index == second.from.source_index
        && first.to.source_index == second.to.source_index;
}

} // namespace gameplay_test
