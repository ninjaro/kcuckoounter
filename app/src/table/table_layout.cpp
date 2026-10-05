#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "packing/layout/equal_rectangles.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_settings.hpp"
#include "table/card_widget.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QRect>
#include <QStyle>

#include <algorithm>
#include <functional>
#include <utility>

namespace table_layout {

// One non-interactive layer below native controls, not a snapshot or a second
// renderer. The callback borrows the table's current capped scene only.
class motion_layer final : public BaseWidget {
public:
    motion_layer(BaseWidget* parent, std::function<void(QPainter&)> paint)
        : BaseWidget(parent)
        , paint_scene(std::move(paint)) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        paint_scene(painter);
    }

private:
    std::function<void(QPainter&)> paint_scene;
};

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

void table::project_gameplay_layout() {
    if (!gameplay_layout)
        return;
    if (gameplay_layout_progress == 1.0)
        retire_gameplay_content_motion();
    if (gameplay_content_motion && !gameplay_motion_layer)
        gameplay_motion_layer
            = new table_layout::motion_layer(this, [this](QPainter& painter) {
                  paint_gameplay_motion(painter);
              });
    for (std::size_t index = 0; index < slot_widgets.size(); ++index) {
        const auto* motion = gameplay_layout->deck_motion({ index });
        // A bound widget keeps deck identity, but its native controls occupy
        // the committed physical destination. Only content follows deck paths.
        const auto rectangle
            = *gameplay_layout
                   ->slot_motion(*gameplay_owner->slot_for({ index }))
                   ->sample(gameplay_layout_progress);
        const auto rotation = gameplay_rotation_paths[index];
        auto* slot = slot_widgets[index];
        slot->card_widget_internal->gameplay_paint_external
            = gameplay_content_motion;
        slot->set_rotated(motion->to.rotated); // Controls stay upright/native.
        slot->set_gameplay_layout_rotation(
            std::lerp(rotation.from, rotation.to, gameplay_layout_progress)
        );
        slot->setGeometry(
            static_cast<int>(rectangle.x), static_cast<int>(rectangle.y),
            static_cast<int>(rectangle.width),
            static_cast<int>(rectangle.height)
        );
        if (gameplay_layout_progress == 1.0)
            slot->set_gameplay_layout_rotation(std::nullopt);
        slot->show();
    }
    if (gameplay_content_motion) {
        gameplay_motion_layer->setGeometry(rect());
        gameplay_motion_layer->lower(); // Every native control stays above it.
        gameplay_motion_layer->show();
        gameplay_motion_layer->update();
    }
}

void table::paint_gameplay_motion(QPainter& painter) {
    if (!gameplay_content_motion || !gameplay_layout)
        return;
    // All fixed-slot frames first, then borrowed deck paint. Temporary content
    // overlap is permitted; a later frame must not erase another deck's card.
    for (const bool content : { false, true }) {
        for (std::size_t index = 0; index < slot_widgets.size(); ++index) {
            const auto* motion = content
                ? gameplay_layout->deck_motion({ index })
                : gameplay_layout->slot_motion(
                      *gameplay_owner->slot_for({ index })
                  );
            const auto rectangle = *motion->sample(gameplay_layout_progress);
            auto* card = slot_widgets[index]->card_widget_internal;
            // Match native logical-pixel rounding at both ends of the layer
            // interval; G2 retains its full-precision path independently.
            const QRect box(
                static_cast<int>(rectangle.x), static_cast<int>(rectangle.y),
                static_cast<int>(rectangle.width),
                static_cast<int>(rectangle.height)
            );
            painter.save();
            painter.translate(box.topLeft());
            const QSizeF size(box.size());
            painter.setClipRect(QRectF(QPointF(), size), Qt::IntersectClip);
            painter.setFont(card->font());
            const auto geometry = card->layout_geometry(size);
            if (content)
                card->paint_deck_content(painter, geometry);
            else
                card->paint_slot_frame(painter, geometry);
            painter.restore();
        }
    }
}

void table::retire_gameplay_content_motion() {
    gameplay_content_motion = false;
    if (gameplay_motion_layer)
        gameplay_motion_layer->hide();
    for (auto* slot : slot_widgets) {
        auto* card = slot->card_widget_internal;
        if (card->gameplay_paint_external) {
            card->gameplay_paint_external = false;
            card->update();
        }
    }
}

void table::finish_gameplay_layout_transition() {
    gameplay_layout_animation.stop();
    gameplay_layout_progress = 1.0;
    project_gameplay_layout();
    if (gameplay_owner)
        update_shared_card_face_need();
}

void table::restart_gameplay_layout_transition() {
    gameplay_layout_animation.stop();
    int duration = 0;
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    if (isVisible())
        duration = std::clamp(
            style()->styleHint(
                QStyle::SH_Widget_Animation_Duration, nullptr, this
            ),
            0, 250
        );
#endif
    if (duration == 0) {
        finish_gameplay_layout_transition();
        return;
    }
    gameplay_layout_progress = 0.0;
    // Reset old elapsed time before changing duration. A shorter new native
    // hint must not emit progress=1 for the newly installed paths and retire
    // their content layer before the timeline starts.
    gameplay_layout_animation.setCurrentTime(0);
    gameplay_layout_animation.setDuration(duration);
    project_gameplay_layout();
    gameplay_layout_animation.start();
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
        auto rotations = gameplay_rotation_paths;
        for (auto& rotation : rotations)
            rotation.from = std::lerp(
                rotation.from, rotation.to, gameplay_layout_progress
            );
        if (gameplay_layout->repack(*packed, gameplay_layout_progress)) {
            for (std::size_t index = 0; index < rotations.size(); ++index)
                rotations[index].to
                    = gameplay_layout->deck_motion({ index })->to.rotated
                    ? 0.0
                    : 90.0;
            gameplay_rotation_paths = std::move(rotations);
            restart_gameplay_layout_transition();
            emit gameplay_layout_changed();
        }
        return;
    }
    const size_t slot_count = slot_widgets.size();
    if (slot_count == 0) {
        return;
    }

    const int field_width = width();
    const int field_height = height();
    if (field_width <= 0 || field_height <= 0) {
        return;
    }

    const auto [card_long_side, card_short_side] = card_sheet_ratio();
    const packing::equal_packing_result result = packing::pack_equal_rectangles(
        {
            .container = { static_cast<double>(field_width),
                           static_cast<double>(field_height) },
            .item = { static_cast<double>(card_long_side),
                      static_cast<double>(card_short_side) },
            .count = slot_count,
            .orientation = table_layout::packing_orientation(card_orientation),
        }
    );

    const size_t mapped_count = std::min(slot_count, result.rectangles.size());

    for (size_t i = 0; i < mapped_count; ++i) {
        const packing::rectangle& card = result.rectangles[i];

        table_slot* slot = slot_widgets[i];
        slot->set_rotated(card.rotated);
        slot->setGeometry(
            static_cast<int>(card.x), static_cast<int>(card.y),
            static_cast<int>(card.width), static_cast<int>(card.height)
        );
        slot->show();
    }

    for (size_t i = mapped_count; i < slot_count; ++i) {
        slot_widgets[i]->hide();
    }
}
