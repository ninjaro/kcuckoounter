#include "table/gameplay_layout.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gameplay {
namespace {

    bool valid_progress(double progress) {
        return std::isfinite(progress) && progress >= 0.0 && progress <= 1.0;
    }

    bool valid_rectangle(const packing::rectangle& rectangle) {
        return packing::is_finite(rectangle)
            && std::isfinite(rectangle.x + rectangle.width)
            && std::isfinite(rectangle.y + rectangle.height);
    }

} // namespace

std::optional<slot_recommendation> recommend_slots(
    packing::extent usable_table, packing::extent item,
    packing::orientation_constraint orientation, double minimum_short_side
) {
    if (!packing::is_positive_finite(usable_table)
        || !packing::is_positive_finite(item)
        || !std::isfinite(minimum_short_side) || minimum_short_side <= 0.0
        || (orientation != packing::orientation_constraint::allow_rotation
            && orientation != packing::orientation_constraint::horizontal_only
            && orientation != packing::orientation_constraint::vertical_only)) {
        return std::nullopt;
    }
    slot_recommendation recommendation { { 1, desktop_slot_technical_cap },
                                         false };
    for (std::size_t count = 1; count <= desktop_slot_technical_cap; ++count) {
        const auto packed = packing::pack_equal_rectangles(
            {
                .container = usable_table,
                .item = item,
                .count = count,
                .orientation = orientation,
            }
        );
        if (!packed.complete(count)) {
            if (count == 1)
                return std::nullopt;
            break;
        }
        const bool meets_minimum = std::all_of(
            packed.rectangles.begin(), packed.rectangles.end(),
            [minimum_short_side](const auto& rectangle) {
                return std::min(rectangle.width, rectangle.height)
                    >= minimum_short_side;
            }
        );
        // Recommend a prefix: every allowed smaller count was also checked.
        // Do not assume an arbitrary packing policy is monotone or use area
        // alone to promise that a very narrow container fits readable slots.
        if (!meets_minimum)
            break;
        recommendation.bounds.recommended = count;
        recommendation.minimum_size_met = true;
    }
    return recommendation;
}

std::optional<packing::rectangle>
rectangle_motion::sample(double progress) const {
    if (!valid_progress(progress)) {
        return std::nullopt;
    }
    auto rectangle = to;
    rectangle.x = std::lerp(from.x, to.x, progress);
    rectangle.y = std::lerp(from.y, to.y, progress);
    rectangle.width = std::lerp(from.width, to.width, progress);
    rectangle.height = std::lerp(from.height, to.height, progress);
    return rectangle;
}

layout_transition::layout_transition(session& active_session)
    : game(active_session) { }

bool layout_transition::repack(
    const packing::equal_packing_result& result, double current_progress
) {
    if (!valid_progress(current_progress) || !result.complete(game.size())
        || !std::isfinite(result.scale)
        || !std::all_of(
            result.rectangles.begin(), result.rectangles.end(), valid_rectangle
        )) {
        return false;
    }

    std::vector<rectangle_motion> next_slots;
    std::vector<rectangle_motion> next_decks;
    next_slots.reserve(game.size());
    next_decks.reserve(game.size());
    for (std::size_t i = 0; i < game.size(); ++i) {
        const auto& slot_target = result.rectangles[i];
        const auto& deck_target
            = result.rectangles[game.slot_for(deck_id { i })->value];
        next_slots.push_back(
            {
                slot_paths.empty() ? slot_target
                                   : *slot_paths[i].sample(current_progress),
                slot_target,
            }
        );
        next_decks.push_back(
            {
                deck_paths.empty() ? deck_target
                                   : *deck_paths[i].sample(current_progress),
                deck_target,
            }
        );
    }
    // All history allocation/validation precedes the session mutation. Its
    // permutation replacement is atomic; committing paths cannot allocate.
    if (game.phase() == session_phase::finished) {
        // Final results/gameplay stay frozen; window geometry is still mutable.
        // Validate Packing's order without replacing the terminal traversal.
        if (result.traversal.size() != game.size())
            return false;
        std::vector<bool> seen(game.size(), false);
        for (const auto index : result.traversal) {
            if (index >= game.size() || seen[index])
                return false;
            seen[index] = true;
        }
    } else if (!game.set_traversal(result.traversal)) {
        return false;
    }
    slot_paths = std::move(next_slots);
    deck_paths = std::move(next_decks);
    return true;
}

bool layout_transition::swap_decks(
    deck_id first, deck_id second, double current_progress
) {
    if (!valid_progress(current_progress) || slot_paths.empty()
        || game.phase() != session_phase::paused || !game.deck(first)
        || !game.deck(second)) {
        return false;
    }
    auto next_slots = slot_paths;
    auto next_decks = deck_paths;
    const auto first_slot = *game.slot_for(first);
    const auto second_slot = *game.slot_for(second);
    for (std::size_t i = 0; i < game.size(); ++i) {
        next_slots[i].from = *slot_paths[i].sample(current_progress);
        next_decks[i].from = *deck_paths[i].sample(current_progress);
        auto destination = *game.slot_for(deck_id { i });
        if (i == first.value) {
            destination = second_slot;
        } else if (i == second.value) {
            destination = first_slot;
        }
        next_decks[i].to = slot_paths[destination.value].to;
    }
    if (!game.swap_decks(first, second)) {
        return false;
    }
    slot_paths = std::move(next_slots);
    deck_paths = std::move(next_decks);
    return true;
}

const rectangle_motion*
layout_transition::slot_motion(physical_slot_id id) const {
    return id.value < slot_paths.size() ? &slot_paths[id.value] : nullptr;
}

const rectangle_motion* layout_transition::deck_motion(deck_id id) const {
    return id.value < deck_paths.size() ? &deck_paths[id.value] : nullptr;
}

} // namespace gameplay
