#include "card_helpers/card_sheet.hpp"
#include "packing/layout/equal_rectangles.hpp"
#include "table/card_widget.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include <QRect>

#include <algorithm>

namespace table_layout {

packing::orientation_constraint
packing_orientation(card_orientation_mode orientation) noexcept {
    switch (orientation) {
    case card_orientation_mode::automatic:
        return packing::orientation_constraint::allow_rotation;
    case card_orientation_mode::vertical:
        return packing::orientation_constraint::vertical_only;
    case card_orientation_mode::horizontal:
        return packing::orientation_constraint::horizontal_only;
    }
    return packing::orientation_constraint::allow_rotation;
}

} // namespace table_layout

std::optional<packing::equal_packing_result>
table::pack_gameplay_slots(std::size_t count) const {
    if (width() <= 0 || height() <= 0)
        return std::nullopt;
    const auto [long_side, short_side] = card_sheet_ratio();
    auto result = packing::pack_equal_rectangles(
        { .container
          = { static_cast<double>(width()), static_cast<double>(height()) },
          .item
          = { static_cast<double>(long_side), static_cast<double>(short_side) },
          .count = count,
          .orientation = table_layout::packing_orientation(card_orientation) }
    );
    if (!result.complete(count)
        || std::ranges::any_of(result.rectangles, [](const auto& rectangle) {
               return rectangle.width < 1.0 || rectangle.height < 1.0;
           }))
        return std::nullopt;
    return result;
}

QList<shared_ui::qt::scene_track> table::layout_tracks() const {
    const auto box = [](const packing::rectangle& value) {
        return QRectF(value.x, value.y, value.width, value.height);
    };
    QList<shared_ui::qt::scene_track> tracks;
    for (std::size_t index = 0; index < slot_widgets.size(); ++index) {
        const auto* frame = gameplay_layout->slot_motion(
            *gameplay_owner->slot_for({ index })
        );
        const auto* content = gameplay_layout->deck_motion({ index });
        auto* slot = slot_widgets[index];
        tracks.append(
            { slot,
              slot->card_widget_internal,
              { box(frame->from), box(frame->to) },
              { box(content->from), box(content->to) },
              content->to.rotated ? 0.0 : 90.0 }
        );
    }
    return tracks;
}

void table::project_gameplay_layout() {
    if (!gameplay_layout)
        return;
    for (std::size_t index = 0; index < slot_widgets.size(); ++index) {
        auto* slot = slot_widgets[index];
        const auto pose = scene_motion->sample(static_cast<qsizetype>(index));
        slot->card_widget_internal->set_slot_number(
            static_cast<int>(gameplay_owner->slot_for({ index })->value) + 1
        );
        slot->set_rotated(gameplay_layout->deck_motion({ index })->to.rotated);
        // Set the pose before native resizing, so presentation-only frames do
        // not regenerate card jitter or watermark demand.
        slot->set_gameplay_layout_rotation(pose.rotation);
        slot->setGeometry(pose.frame);
        if (!scene_motion->active())
            slot->set_gameplay_layout_rotation(std::nullopt);
        slot->show();
    }
}

void table::update_layout() {
    if (gameplay_owner) {
        const auto packed = pack_gameplay_slots(gameplay_owner->size());
        if (!packed)
            return; // Keep the last usable paths when final boxes are subpixel.
        bool changed = false;
        for (std::size_t index = 0; index < packed->rectangles.size();
             ++index) {
            const auto& target = gameplay_layout->slot_motion({ index })->to;
            const auto& next = packed->rectangles[index];
            changed = changed || target.x != next.x || target.y != next.y
                || target.width != next.width || target.height != next.height
                || target.rotated != next.rotated;
        }
        if (!changed)
            return; // Repeated same-size notifications must not restart motion.
        if (gameplay_layout->repack(*packed, scene_motion->progress())) {
            scene_motion->transition(layout_tracks(), false);
            emit gameplay_layout_changed();
        }
        return;
    }
}
