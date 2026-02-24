#include "card_helpers/card_sheet.hpp"

#include "arch/str_label.hpp"

#include <QRectF>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kRanksPerSuit = 13;
constexpr int kSuitsCount = 4;
constexpr int kStandardDeckCount = kRanksPerSuit * kSuitsCount;
constexpr int kJokerCount = 2;

const QStringList& rank_labels() {
    static const QStringList labels
        = { str_label("A"), str_label("2"),  str_label("3"), str_label("4"),
            str_label("5"), str_label("6"),  str_label("7"), str_label("8"),
            str_label("9"), str_label("10"), str_label("J"), str_label("Q"),
            str_label("K") };
    return labels;
}

const QStringList& suit_labels() {
    static const QStringList labels
        = { str_label("clubs"), str_label("diamonds"), str_label("hearts"),
            str_label("spades") };
    return labels;
}

const QStringList& suit_ids() {
    static const QStringList ids = { str_label("club"), str_label("diamond"),
                                     str_label("heart"), str_label("spade") };
    return ids;
}

QString rank_suit_element_id(int rank_index, int suit_index) {
    const auto& suit_list = suit_ids();
    if (rank_index < 0 || rank_index >= kRanksPerSuit) {
        return {};
    }
    if (suit_index < 0 || suit_index >= suit_list.size()) {
        return {};
    }

    const QString& suit_id = suit_list.at(suit_index);
    if (rank_index <= 9) {
        return str_label("%1_%2").arg(suit_id).arg(rank_index + 1);
    }
    if (rank_index == 10) {
        return str_label("jack_%1").arg(suit_id);
    }
    if (rank_index == 11) {
        return str_label("queen_%1").arg(suit_id);
    }
    if (rank_index == 12) {
        return str_label("king_%1").arg(suit_id);
    }
    return {};
}

struct card_sheet_cache {
    bool valid;
    std::pair<int, int> ratio;
};

card_sheet_cache build_card_sheet_cache() {
    const std::pair<int, int> fallback_ratio { 88, 63 };
    QSvgRenderer renderer(card_sheet_source_path());
    if (!renderer.isValid()) {
        return { false, fallback_ratio };
    }

    const auto& ids = card_element_ids();
    for (const QString& id : ids) {
        if (id.isEmpty() || !renderer.elementExists(id)) {
            continue;
        }
        const QRectF bounds = renderer.boundsOnElement(id);
        if (!bounds.isValid() || bounds.width() <= 0.0
            || bounds.height() <= 0.0) {
            continue;
        }

        const int width = static_cast<int>(std::lround(bounds.width()));
        const int height = static_cast<int>(std::lround(bounds.height()));
        if (width > 0 && height > 0) {
            const int long_side = std::max(width, height);
            const int short_side = std::min(width, height);
            return { true, { long_side, short_side } };
        }
    }

    return { true, fallback_ratio };
}

const card_sheet_cache& cached_card_sheet() {
    static const card_sheet_cache cache = build_card_sheet_cache();
    return cache;
}

}

QString card_sheet_source_path() { return str_label("assets/cards_0.svg"); }

bool preload_card_sheet() {
    const auto& cache = cached_card_sheet();
    return cache.valid;
}

std::pair<int, int> card_sheet_ratio() {
    const auto& cache = cached_card_sheet();
    return cache.ratio;
}

QString card_label_from_index(int index) {
    if (index < 0) {
        return {};
    }
    if (index >= kStandardDeckCount) {
        return str_label("Joker");
    }

    const int rank_index = index % kRanksPerSuit;
    const int suit_index = index / kRanksPerSuit;
    const auto& ranks = rank_labels();
    const auto& suits = suit_labels();
    if (rank_index < 0 || rank_index >= ranks.size() || suit_index < 0
        || suit_index >= suits.size()) {
        return {};
    }

    return ranks.at(rank_index) + str_label(" of ") + suits.at(suit_index);
}

QString card_element_id_from_index(int index) {
    const auto& ids = card_element_ids();
    if (index < 0 || index >= ids.size()) {
        return {};
    }
    return ids.at(index);
}

const QStringList& card_element_ids() {
    static const QStringList ids = [] {
        QStringList list;
        list.reserve(kStandardDeckCount + kJokerCount);
        for (int suit_index = 0; suit_index < kSuitsCount; ++suit_index) {
            for (int rank_index = 0; rank_index < kRanksPerSuit; ++rank_index) {
                const QString element_id
                    = rank_suit_element_id(rank_index, suit_index);
                list.append(element_id);
            }
        }
        list.append(str_label("joker_black"));
        list.append(str_label("joker_red"));
        return list;
    }();
    return ids;
}

QString card_back_element_id() { return str_label("back"); }
