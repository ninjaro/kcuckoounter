#include "include/card_packer_tests.hpp"
#include "card_helpers/card_packer.hpp"

#include <QtTest/QtTest>

static constexpr double k_base_height = 88.0;
static constexpr double k_base_width = 63.0;
static constexpr double k_eps = 1e-6;

struct card_dims {
    double w;
    double h;
};

static card_dims card_dimensions(const placed_card& c, double scale) {
    card_dims d {};
    if (c.rotated) {
        d.w = scale * k_base_width;
        d.h = scale * k_base_height;
    } else {
        d.w = scale * k_base_height;
        d.h = scale * k_base_width;
    }
    return d;
}

static bool
cards_intersect(const placed_card& a, const placed_card& b, double scale) {
    card_dims da = card_dimensions(a, scale);
    card_dims db = card_dimensions(b, scale);

    double ax2 = a.x + da.w;
    double ay2 = a.y + da.h;
    double bx2 = b.x + db.w;
    double by2 = b.y + db.h;

    bool no_overlap = ax2 <= b.x + k_eps || bx2 <= a.x + k_eps
        || ay2 <= b.y + k_eps || by2 <= a.y + k_eps;

    return !no_overlap;
}

void card_packer_tests::simple_pack() {
    const double width = 500.0;
    const double height = 400.0;
    const int card_count = 10;

    card_packer packer(card_count);
    auto result = packer.pack(width, height);
    double scale = std::get<0>(result);
    const auto& cards = std::get<1>(result);

    QVERIFY(scale > 0.0);
    QCOMPARE(static_cast<int>(cards.size()), card_count);
}

void card_packer_tests::deck_52() {
    const double width = 800.0;
    const double height = 600.0;
    const int card_count = 52;

    card_packer packer(card_count);
    auto result = packer.pack(width, height);
    double scale = std::get<0>(result);
    const auto& cards = std::get<1>(result);

    QVERIFY(scale > 0.0);
    QCOMPARE(static_cast<int>(cards.size()), card_count);
}

void card_packer_tests::bounds_and_overlap() {
    const double width = 800.0;
    const double height = 600.0;
    const int card_count = 40;

    card_packer packer(card_count);
    auto result = packer.pack(width, height);
    double scale = std::get<0>(result);
    const auto& cards = std::get<1>(result);

    QVERIFY(scale > 0.0);
    QCOMPARE(static_cast<int>(cards.size()), card_count);

    for (const auto& c : cards) {
        card_dims d = card_dimensions(c, scale);

        QVERIFY2(c.x >= -k_eps, "x < 0");
        QVERIFY2(c.y >= -k_eps, "y < 0");
        QVERIFY2(c.x + d.w <= width + k_eps, "x + w > width");
        QVERIFY2(c.y + d.h <= height + k_eps, "y + h > height");
    }

    for (size_t i = 0; i < cards.size(); ++i) {
        for (size_t j = i + 1; j < cards.size(); ++j) {
            bool overlap = cards_intersect(cards[i], cards[j], scale);
            QVERIFY2(!overlap, "overlap");
        }
    }
}

void card_packer_tests::more_cards_smaller_scale() {
    const double width = 800.0;
    const double height = 600.0;

    card_packer packer_10(10);
    card_packer packer_100(100);

    auto result_10 = packer_10.pack(width, height);
    auto result_100 = packer_100.pack(width, height);

    double scale_10 = std::get<0>(result_10);
    double scale_100 = std::get<0>(result_100);

    const auto& cards_10 = std::get<1>(result_10);
    const auto& cards_100 = std::get<1>(result_100);

    QCOMPARE(static_cast<int>(cards_10.size()), 10);
    QCOMPARE(static_cast<int>(cards_100.size()), 100);

    QVERIFY2(scale_10 + k_eps >= scale_100, "scale(10) < scale(100)");
}
