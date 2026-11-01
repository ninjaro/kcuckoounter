#include "table/card_widget.hpp"

#include "card_helpers/card_sheet.hpp"
#include <QPointer>
#include <algorithm>
#include <shared_ui/graphics/qt/svg_image_view.hpp>

namespace {
bool same_images(
    const QHash<QString, QImage>& first, const QHash<QString, QImage>& second
) {
    if (first.size() != second.size())
        return false;
    for (auto it = first.cbegin(); it != first.cend(); ++it) {
        const auto found = second.constFind(it.key());
        if (found == second.cend() || found->cacheKey() != it->cacheKey())
            return false;
    }
    return true;
}
}

void card_widget::schedule_card_preparation() {
    if (preparation_queued)
        return;
    preparation_queued = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            preparation_queued = false;
            prepare_card_faces();
        },
        Qt::QueuedConnection
    );
}

QString card_widget::displayed_card_image_id() const {
    if (!isVisible() || !has_cards()
        || (display_hidden() && !deck_frame_enabled))
        return {};
    // The deck frame shows the back while the face is hidden.
    if (display_hidden() || !display_running() || display_card_index() < 0)
        return card_back_element_id();
    return card_element_id_from_index(std::min(
        display_card_index(), static_cast<int>(card_element_ids().size()) - 1
    ));
}

void card_widget::prepare_card_faces() {
    const QPointer<card_widget> lifetime(this);
    auto marking_target = table_marking.target();
    marking_target.device_pixel_ratio = devicePixelRatioF();
    table_marking.set_target(marking_target);
    if (!lifetime)
        return;
    const auto target = card_face_target_size();
    const bool presented = presented_image_mode;
    update_card_faces(target);
    // Table-owned intent follows state/geometry, never painting or publication.
    if (lifetime && presented)
        emit image_demand_changed();
}

void card_widget::set_presented_card_images(const QHash<QString, QImage>& images
) {
    presented_image_mode = true;
    card_images.reset();
    const auto target = card_face_target_size();
    const bool changed = !same_images(images, displayed_card_images)
        || target != card_face_size;
    card_face_size = target;
    if (changed) {
        displayed_card_images = images;
        update();
    }
    set_rasterizing(false);
}

void card_widget::clear_presented_card_images() {
    set_presented_card_images({});
}

bool card_widget::has_presented_card_images() const {
    return presented_image_mode && !displayed_card_images.isEmpty();
}

void card_widget::set_presented_image_mode(bool enabled) {
    if (presented_image_mode == enabled)
        return;
    presented_image_mode = enabled;
    displayed_card_images.clear();
    update();
    if (enabled) {
        card_images.reset();
        set_rasterizing(false);
    } else {
        schedule_card_preparation();
    }
}

void card_widget::sync_card_sheet_source() {
    const auto next_source = card_sheet_source_path();
    if (card_sheet_source == next_source)
        return;
    card_sheet_source = next_source;
    if (presented_image_mode) {
        // The table owns the coherent visible-subset source transition.
        update();
        return;
    }
    card_images.reset();
    displayed_card_images.clear();
    update();
    schedule_card_preparation();
}

int card_widget::card_face_target_short_px() const {
    const auto target = card_face_target_size();
    return target.isEmpty() ? 0 : std::min(target.width(), target.height());
}

QSize card_widget::card_face_target_size() const {
    const auto rect = layout_geometry().item;
    return rect.isEmpty() ? QSize()
                          : rect.size().toSize().expandedTo(QSize(1, 1));
}

void card_widget::update_card_faces(const QSize& target_size) {
    if (card_face_size != target_size) {
        update();
    }
    card_face_size = target_size;
    if (presented_image_mode)
        return;
    const auto visible = displayed_card_image_id();
    if (target_size.isEmpty())
        card_images.reset();
    if (target_size.isEmpty() || visible.isEmpty()) {
        if (card_images)
            card_images->replace_desired({});
        displayed_card_images.clear();
        set_rasterizing(false);
        return;
    }
    if (!card_images) {
        card_images = std::make_unique<card_image_consumer>();
        connect(
            card_images.get(), &card_image_consumer::images_changed, this,
            &card_widget::on_card_images_changed, Qt::QueuedConnection
        );
    }
    card_images->set_source(card_sheet_source);
    const auto physical
        = shared_ui::qt::raster_physical_size(target_size, devicePixelRatioF());
    card_images->replace_desired({ { visible, physical } });
    on_card_images_changed();
}

QImage card_widget::card_face_image(int index) const {
    if (card_face_size.isEmpty())
        return {};
    const auto id = index == card_element_ids().size()
        ? card_back_element_id()
        : card_element_id_from_index(index);
    return presented_image_mode
        ? displayed_card_images.value(id)
        : (card_images ? card_images->image_for(id) : QImage());
}

void card_widget::set_rasterizing(bool active) {
    if (rasterizing == active)
        return;
    rasterizing = active;
    emit rasterization_busy_changed(active);
}

void card_widget::on_card_images_changed() {
    if (!card_images || presented_image_mode || card_face_size.isEmpty())
        return;
    if (card_images->preferred_source_failed()) {
        card_images.reset();
        displayed_card_images.clear();
        card_face_size = {};
        update();
    } else {
        if (card_images->is_pending()) {
            set_rasterizing(true);
            return;
        }
        const auto id = displayed_card_image_id();
        const auto image = card_images->image_for(id);
        QHash<QString, QImage> next;
        if (!image.isNull())
            next.insert(id, image);
        if (!same_images(next, displayed_card_images)) {
            displayed_card_images = std::move(next);
            update();
        }
    }
    set_rasterizing(false); // Observers may destroy this widget.
}
