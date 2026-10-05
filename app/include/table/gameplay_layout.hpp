#ifndef KCUCKOOUNTER_TABLE_GAMEPLAY_LAYOUT_HPP
#define KCUCKOOUNTER_TABLE_GAMEPLAY_LAYOUT_HPP

#include "packing/layout/equal_rectangles.hpp"
#include "table/gameplay_session.hpp"

#include <optional>
#include <vector>

namespace gameplay {

// Provisional R-006 desktop policy, not a fundamental domain/storage limit.
// Logical slot short side leaves room for the existing frame/inset treatment.
inline constexpr double recommended_slot_short_side = 160.0;
inline constexpr std::size_t desktop_slot_technical_cap = 64;

struct slot_recommendation {
    slot_limits bounds;
    // False for the one-slot fallback when even one slot is below the target.
    bool minimum_size_met;
};

// Setup-only bounded search over actual Packing output. The caller supplies
// usable table geometry and the active theme aspect in the same logical units
// as the renderer; do not multiply by DPR or use inline settings-panel size.
[[nodiscard]] std::optional<slot_recommendation> recommend_slots(
    packing::extent usable_table, packing::extent item,
    packing::orientation_constraint orientation
    = packing::orientation_constraint::allow_rotation,
    double minimum_short_side = recommended_slot_short_side
);

struct rectangle_motion {
    packing::rectangle from;
    packing::rectangle to;

    // Linear box geometry only. Metadata describes the target; the renderer
    // can inspect both endpoints to choose its rotation/easing treatment.
    // Nonfinite/out-of-range progress is rejected, never extrapolated.
    [[nodiscard]] std::optional<packing::rectangle>
    sample(double progress) const;
};

// Application-owned, ephemeral presentation history, NOT gameplay state or
// Packing history. The bound session must outlive this object without moving;
// all presented swaps must go through this seam, not directly through session.
class layout_transition {
public:
    explicit layout_transition(session& active_session);
    layout_transition(const layout_transition&) = delete;
    layout_transition& operator=(const layout_transition&) = delete;

    // Retarget from the currently displayed progress, then reset the caller's
    // timeline to zero on success. The first layout installs both endpoints
    // immediately. Invalid results leave history AND traversal untouched.
    // Finished sessions allow geometry-only repack, preserving frozen gameplay
    // and traversal; Swap still requires manual Pause.
    [[nodiscard]] bool repack(
        const packing::equal_packing_result& result,
        double current_progress = 1.0
    );
    // Physical destinations/traversal stay fixed. Session owns and validates
    // the complete mapping change; no deck payload is copied into this object.
    [[nodiscard]] bool
    swap_decks(deck_id first, deck_id second, double current_progress = 1.0);

    [[nodiscard]] const rectangle_motion*
    slot_motion(physical_slot_id id) const;
    [[nodiscard]] const rectangle_motion* deck_motion(deck_id id) const;

private:
    session& game;
    std::vector<rectangle_motion> slot_paths;
    std::vector<rectangle_motion> deck_paths;
};

} // namespace gameplay

#endif // KCUCKOOUNTER_TABLE_GAMEPLAY_LAYOUT_HPP
