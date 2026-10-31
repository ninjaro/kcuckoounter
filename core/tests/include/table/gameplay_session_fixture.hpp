#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_SESSION_FIXTURE_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_SESSION_FIXTURE_HPP

#include "packing/layout/equal_rectangles.hpp"
#include "table/gameplay_layout.hpp"
#include "table/gameplay_session.hpp"

#include <initializer_list>

namespace gameplay_test {

std::vector<gameplay::deck_configuration> configurations(std::size_t count);
gameplay::prepared_deck cards(std::int64_t initial_count = 0);
gameplay::prepared_deck
quiz_cards(std::vector<std::uint64_t> targets = { 1, 3 });
gameplay::session quiz_session(
    gameplay::session_configuration settings,
    const std::vector<gameplay::prepared_deck>& streams,
    std::span<const std::uint64_t> table_targets = {},
    std::vector<gameplay::deck_configuration> configured = {}
);
std::vector<gameplay::deck_state> deck_payloads(const gameplay::session& game);
gameplay::prepared_deck prepared_cards_for(
    const gameplay::session& game, gameplay::deck_id id,
    std::int64_t initial_count = 0
);
std::vector<gameplay::deck_state>
exposure_decks(std::initializer_list<std::uint64_t> progress);
gameplay::session ready_session(
    gameplay::failure_policy failure = gameplay::failure_policy::block
);
packing::equal_packing_result layout(double width, double height);
bool same_geometry(
    const packing::rectangle& first, const packing::rectangle& second
);
bool same_motion(
    const gameplay::rectangle_motion& first,
    const gameplay::rectangle_motion& second
);

} // namespace gameplay_test

#endif // KCUCKOOUNTER_TESTS_GAMEPLAY_SESSION_FIXTURE_HPP
