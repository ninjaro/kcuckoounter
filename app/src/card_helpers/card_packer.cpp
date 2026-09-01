#include "card_helpers/card_packer.hpp"

#include "card_helpers/card_sheet.hpp"
#include "packing/equal_rectangles.hpp"

#include <utility>

card_packer::card_packer(const std::size_t count)
    : card_count(count) { }

std::tuple<double, std::vector<placed_card>>
card_packer::pack(const double width, const double height) const {
    const auto [card_long_side, card_short_side] = card_sheet_ratio();
    const packing::equal_packing_request request {
        .container = { width, height },
        .item = { static_cast<double>(card_long_side),
                  static_cast<double>(card_short_side) },
        .count = card_count,
        .orientation = packing::orientation_constraint::allow_rotation,
    };
    const packing::equal_packing_result result
        = packing::pack_equal_rectangles(request);

    std::vector<placed_card> cards;
    cards.reserve(result.rectangles.size());
    for (const packing::rectangle& rectangle : result.rectangles) {
        cards.push_back({ rectangle.x, rectangle.y, rectangle.rotated });
    }
    return { result.scale, std::move(cards) };
}
