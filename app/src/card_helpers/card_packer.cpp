#include "card_helpers/card_packer.hpp"

#include "card_helpers/card_sheet.hpp"

#include <algorithm>
#include <cmath>

static double squared_distance_to_origin(const placed_card& card) {
    return card.x * card.x + card.y * card.y;
}

card_packer::card_packer(size_t card_count)
    : card_count(card_count)
    , index(0)
    , card_ratio(card_sheet_ratio())
    , scale(1.0)
    , cards(card_count) { }

std::tuple<double, std::vector<placed_card>>
card_packer::pack(double width, double height) {
    double right = max_scale(width, height, card_count);
    double left = 0.0;

    while (left + 0.1 < right) {
        scale = left + (right - left) / 2.0;
        index = 0;

        cards.resize(static_cast<size_t>(width * height / (scale * scale)));

        cards_init(width, height);

        if (index >= card_count) {
            left = scale;
        } else {
            right = scale;
        }
    }

    scale = left;
    index = 0;
    cards.resize(static_cast<size_t>(width * height / (scale * scale)));
    cards_init(width, height);

    cards.resize(index);

    const size_t need = std::min(index, card_count);
    if (index > need) {
        auto nth = cards.begin() + static_cast<std::ptrdiff_t>(need);
        std::ranges::nth_element(
            cards, nth, std::less<> {}, squared_distance_to_origin
        );
        cards.erase(nth, cards.end());
    }

    return { left, cards };
}

double card_packer::fill_vertical(
    double offset, double height, double card_width, double x, double y
) {
    double y_off = offset;
    if (index >= cards.size()) {
        return y_off;
    }
    for (; y_off + card_width <= height && index < cards.size();
         y_off += card_width) {
        cards[index++] = { x, y + y_off, true };
    }
    return y_off;
}

double card_packer::fill_horizontal(
    double offset, double width, double card_width, double x, double y
) {
    double x_off = offset;
    if (index >= cards.size()) {
        return x_off;
    }
    for (; x_off + card_width <= width && index < cards.size();
         x_off += card_width) {
        cards[index++] = { x + x_off, y, false };
    }
    return x_off;
}

void card_packer::cards_init(double width, double height, double x, double y) {
    if (index >= cards.size()) {
        return;
    }
    const double card_short = scale * card_ratio.second;
    const double card_long = scale * card_ratio.first;

    const bool fits_horizontal = (width >= card_long && height >= card_short);
    const bool fits_vertical = (width >= card_short && height >= card_long);
    if (!fits_horizontal && !fits_vertical) {
        return;
    }

    const double rows = std::floor(height / card_long);
    const double cols = std::floor(width / card_long);
    const double leftmost = height - rows * card_long;
    const double topmost = width - cols * card_long;

    if (leftmost >= card_short) {
        size_t first_idx = index;

        const double x_off = fill_horizontal(0.0, width, card_long, x, y);
        const double y_off = fill_vertical(card_short, height, card_long, x, y);

        if (x_off + card_short <= width) {
            fill_horizontal(
                card_short, width, card_long, x, y + y_off - card_short
            );
            fill_vertical(0.0, height, card_long, x + x_off, y);

            cards_init(
                width - 2.0 * card_short, height - 2.0 * card_short,
                x + card_short, y + card_short
            );

            const double x_shift = (width - x_off - card_short) / 2.0;
            double y_shift = (height - y_off) / 2.0;
            for (size_t i = first_idx; i < index; ++i) {
                cards[i].x += x_shift;
                cards[i].y += y_shift;
            }
        } else {
            cards_init(
                width - card_short, height - card_short, x + card_short,
                y + card_short
            );
        }
    } else if (topmost >= card_short) {
        fill_vertical(0.0, height, card_long, x, y);
        fill_horizontal(card_short, width, card_long, x, y);

        cards_init(
            width - card_short, height - card_short, x + card_short,
            y + card_short
        );
    } else if (topmost * height < leftmost * width) {
        const double off = topmost / 2.0;
        fill_horizontal(off, width, card_long, x, y);
        cards_init(width, height - card_short, x, y + card_short);
    } else {
        const double off = leftmost / 2.0;
        fill_vertical(off, height, card_long, x, y);
        cards_init(width - card_short, height, x + card_short, y);
    }
}

double card_packer::max_scale(double width, double height, size_t count) const {
    return std::sqrt(
        width * height / static_cast<double>(card_ratio.first)
        / static_cast<double>(card_ratio.second) / static_cast<double>(count)
    );
}
