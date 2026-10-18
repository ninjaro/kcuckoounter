#include "card_helpers/card_sheet.hpp"
#include "table/card_widget.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"
#include <QPointer>
#include <cmath>
#include <utility>

namespace {
QSize physical_need(const card_widget& card, const QSize& logical) {
    const qreal dpr = card.devicePixelRatioF();
    return { static_cast<int>(std::ceil(logical.width() * dpr)),
             static_cast<int>(std::ceil(logical.height() * dpr)) };
}
}

void table::schedule_card_preload() {
    if (layout_demand_queued)
        return;
    layout_demand_queued = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            layout_demand_queued = false;
            update_card_image_demands();
        },
        Qt::QueuedConnection
    );
}

void table::prepare_cards_for_start() { update_card_image_demands(true); }

bool table::is_rasterization_busy() const { return rasterization_busy; }

const rasterflow::qt::image_service* table::shared_image_service() {
    return &application_image_service();
}

void table::update_card_image_demands(bool immediate, bool force) {
    if (gameplay_owner && gameplay_layout_progress < 1.0 && !force
        && !immediate)
        return;
    QMap<QString, QSize> needed;
    for (const auto* slot : slot_widgets) {
        const auto* card = slot ? slot->card_widget_internal : nullptr;
        if (!card)
            continue;
        const auto id = card->displayed_card_image_id();
        const auto logical = card->card_face_target_size();
        if (!id.isEmpty() && !logical.isEmpty())
            needed[id]
                = needed.value(id).expandedTo(physical_need(*card, logical));
    }
    const auto source = card_sheet_source_path();
    if (needed.isEmpty()) {
        card_images.replace_desired({});
        card_images.set_source(source);
        card_demand_active = false;
        desired_card_sizes.clear();
        desired_card_size = {};
        desired_card_source = source;
        displayed_card_images.clear();
        displayed_card_source.clear();
        if (!apply_displayed_card_images())
            return;
        refresh_rasterization_busy_state();
        return;
    }
    if (card_demand_active && desired_card_source == source
        && desired_card_sizes == needed) {
        on_card_images_changed();
        return;
    }
    card_demand_active = true;
    desired_card_sizes = needed;
    desired_card_size = {};
    for (const auto& size : std::as_const(needed))
        desired_card_size = desired_card_size.expandedTo(size);
    desired_card_source = source;
    card_images.set_source(source);
    std::vector<card_image_demand> desired;
    for (auto it = needed.cbegin(); it != needed.cend(); ++it)
        desired.push_back({ it.key(), it.value() });
    card_images.replace_desired(std::move(desired));
    on_card_images_changed();
}

void table::on_card_images_changed() {
    if (card_demand_active) {
        const bool complete = !card_images.is_pending();
        const bool source_cutover = !displayed_card_images.isEmpty()
            && displayed_card_source != desired_card_source;
        if (!source_cutover || complete) {
            auto next = source_cutover ? QHash<QString, QImage>()
                                       : displayed_card_images;
            for (auto it = next.begin(); it != next.end();) {
                if (!desired_card_sizes.contains(it.key()))
                    it = next.erase(it);
                else
                    ++it;
            }
            for (auto it = desired_card_sizes.cbegin();
                 it != desired_card_sizes.cend(); ++it) {
                const auto image = card_images.image_for(it.key());
                if (!image.isNull())
                    next.insert(it.key(), image);
                else if (complete)
                    next.remove(it.key());
            }
            displayed_card_images = std::move(next);
            displayed_card_source = desired_card_source;
        }
        if (!apply_displayed_card_images())
            return;
    }
    refresh_rasterization_busy_state(); // May destroy this table.
}

bool table::apply_displayed_card_images() {
    const QPointer<table> lifetime(this);
    for (auto* slot : slot_widgets) {
        auto* card = slot ? slot->card_widget_internal : nullptr;
        if (!card)
            continue;
        QHash<QString, QImage> presentation;
        const auto id = card->displayed_card_image_id();
        const auto image = displayed_card_images.value(id);
        const auto physical
            = physical_need(*card, card->card_face_target_size());
        // A retained older raster is never upscaled across a geometry cutover.
        if (!image.isNull()
            && rasterflow::satisfies(
                { image.width(), image.height() },
                { physical.width(), physical.height() }
            ))
            presentation.insert(id, image);
        slot->set_presented_card_images(presentation);
        if (!lifetime)
            return false;
    }
    return true;
}

void table::clear_presented_card_images() {
    card_demand_active = false;
    desired_card_sizes.clear();
    desired_card_size = {};
    card_images.clear();
    displayed_card_images.clear();
    displayed_card_source.clear();
    for (auto* slot : slot_widgets)
        if (slot)
            slot->clear_presented_card_images();
}

void table::on_slot_rasterization_busy_changed(bool busy) {
    auto* slot = qobject_cast<table_slot*>(sender());
    if (slot)
        update_rasterization_state(slot, busy);
}

void table::update_rasterization_state(table_slot* slot, bool busy) {
    if (!slot)
        return;
    if (busy)
        rasterizing_slots.insert(slot);
    else
        rasterizing_slots.remove(slot);
    refresh_rasterization_busy_state();
}

void table::refresh_rasterization_busy_state() {
    const bool busy = !rasterizing_slots.isEmpty()
        || (card_demand_active && card_images.is_pending());
    if (rasterization_busy == busy)
        return;
    rasterization_busy = busy;
    emit rasterization_busy_changed(busy);
}
