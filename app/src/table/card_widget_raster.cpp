#include "table/card_widget.hpp"

#include "arch/asset_locator.hpp"
#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "settings/theme_settings.hpp"
#include <QFutureWatcher>
#include <QImage>
#include <QSize>
#include <QString>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <utility>

void card_rasterize_watcher::waitForFinished() {
    QFutureWatcher<QVector<QImage>>::waitForFinished();
    if (isFinished()) {
        Q_EMIT finished();
    }
}

void card_widget::rasterize_card_faces(
    QPromise<QVector<QImage>>& promise, const QString& source,
    const QSize& raster_size
) {
    if (raster_size.isEmpty() || promise.isCanceled()) {
        return;
    }
    card_sheet_render_context context(source);
    const QStringList ids = required_card_ids_with_back();
    QVector<QImage> images;
    images.reserve(ids.size());
    for (const auto& id : ids) {
        if (promise.isCanceled()) {
            return;
        }
        images.append(context.rasterize_face(id, raster_size));
    }
    // Keep local publication atomic, just like the shared table deck.
    promise.addResult(std::move(images));
}

void card_widget::prepare_card_faces() {
    const QSize target_size = card_face_target_size();
    if (target_size.isEmpty()) {
        return;
    }
    update_card_faces(target_size);
}

void card_widget::set_shared_card_faces(
    const QVector<QImage>& face_images, const QSize& raster_size
) {
    if (face_images.isEmpty() || raster_size.isEmpty()) {
        return;
    }

    apply_rasterized_images(face_images, raster_size);
    shared_card_faces_active = true;
    rasterize_watcher.cancel();
    // Retire the observer too; canceled futures may still retain old images.
    rasterize_watcher.setFuture(QFuture<QVector<QImage>>());
    card_sheet_renderer.reset();

    if (rasterizing) {
        pending_raster_size = QSize();
        set_rasterizing(false);
    }

    if (!card_face_size.isEmpty()) {
        update_card_faces(card_face_size);
    }
    update();
}

void card_widget::clear_shared_card_faces() {
    shared_card_faces_active = false;
}

bool card_widget::has_shared_card_faces() const {
    return shared_card_faces_active && !card_faces_rasterized.isEmpty();
}

void card_widget::set_shared_card_faces_mode(bool enabled) {
    if (shared_card_faces_mode == enabled) {
        return;
    }

    shared_card_faces_mode = enabled;
    if (shared_card_faces_mode) {
        rasterize_watcher.cancel();
        rasterize_watcher.setFuture(QFuture<QVector<QImage>>());
        card_sheet_renderer.reset();
    }
    if (shared_card_faces_mode && rasterizing) {
        pending_raster_size = QSize();
        set_rasterizing(false);
    }
    if (!card_face_size.isEmpty()) {
        update_card_faces(card_face_size);
    }
}

void card_widget::sync_card_sheet_source() {
    const QString next_source = card_sheet_source_path();
    if (card_sheet_source == next_source) {
        return;
    }

    card_sheet_source = next_source;
    card_sheet_renderer.reset();
    invalidate_selected_card_face();
    card_faces_rasterized.clear();
    card_face_raster_size = QSize();
    picks_since_rasterize = 0;

    if (rasterizing) {
        // Keep one local job until completion, but stop painting an old theme.
        rasterize_watcher.cancel();
        pending_raster_size = raster_cache_size(card_face_size);
    }

    if (!card_face_size.isEmpty()) {
        update_card_faces(card_face_size);
    }
    update();
}

int card_widget::card_face_target_short_px() const {
    const QSize target_size = card_face_target_size();
    if (target_size.isEmpty()) {
        return 0;
    }

    return std::min(target_size.width(), target_size.height());
}

QSize card_widget::card_face_target_size() const {
    const QRectF card_rect = layout_geometry().card;
    if (card_rect.isEmpty()) {
        return {};
    }
    return card_rect.size().toSize().expandedTo(QSize(1, 1));
}

QSize card_widget::raster_cache_size(const QSize& target_size) {
    if (target_size.isEmpty()) {
        return {};
    }
    const qreal base_scale = 1.75;
    const qreal min_side = std::min(target_size.width(), target_size.height());
    qreal scale = base_scale;
    if (min_side > 0.0) {
        scale = std::max(scale, 63.0 / min_side);
    }
    const int width
        = std::max(1, static_cast<int>(std::ceil(target_size.width() * scale)));
    const int height = std::max(
        1, static_cast<int>(std::ceil(target_size.height() * scale))
    );
    return { width, height };
}

void card_widget::update_card_faces(const QSize& target_size) {
    if (target_size.isEmpty()) {
        // No demand remains for the local worker or its queued completion.
        rasterize_watcher.cancel();
        rasterize_watcher.setFuture(QFuture<QVector<QImage>>());
        invalidate_selected_card_face();
        card_face_size = QSize();
        card_faces_rasterized.clear();
        card_face_raster_size = QSize();
        picks_since_rasterize = 0;
        pending_raster_size = QSize();
        set_rasterizing(false);
        return;
    }

    const bool allow_local_rasterization
        = !shared_card_faces_mode && !shared_card_faces_active;
    if (allow_local_rasterization) {
        // Prepared shared pixels have no dependency on a widget-local SVG.
        if (!card_sheet_renderer) {
            card_sheet_renderer = std::make_unique<QSvgRenderer>();
        }
        if (!card_sheet_renderer->isValid()) {
            card_sheet_renderer->load(card_sheet_source);
        }
        if (!card_sheet_renderer->isValid()) {
            rasterize_watcher.cancel();
            rasterize_watcher.setFuture(QFuture<QVector<QImage>>());
            invalidate_selected_card_face();
            card_face_size = QSize();
            card_faces_rasterized.clear();
            card_face_raster_size = QSize();
            picks_since_rasterize = 0;
            pending_raster_size = QSize();
            set_rasterizing(false);
            return;
        }
    }

    const bool size_changed = card_face_size != target_size;
    const QSize raster_target_size = raster_cache_size(target_size);
    const bool raster_size_changed
        = card_face_raster_size != raster_target_size;
    const bool raster_cache_ready
        = !card_faces_rasterized.isEmpty() && !card_face_raster_size.isEmpty();
    const bool should_rasterize = !raster_cache_ready
        || (allow_local_rasterization && raster_size_changed
            && picks_since_rasterize >= 3);

    if (allow_local_rasterization) {
        if (rasterizing) {
            // Re-entry into ready or in-flight pixels supersedes a queued
            // resize.
            pending_raster_size = should_rasterize
                    && (raster_task_size != raster_target_size
                        || raster_task_source != card_sheet_source)
                ? raster_target_size
                : QSize();
        } else if (should_rasterize) {
            start_rasterization(raster_target_size);
        }
    }

    if (!size_changed && !should_rasterize) {
        return;
    }

    if (size_changed) {
        invalidate_selected_card_face();
    }
    card_face_size = target_size;
}

void card_widget::invalidate_selected_card_face() {
    selected_card_face = QPixmap();
    selected_card_face_index = -1;
}

const QPixmap& card_widget::card_face_pixmap(int index) {
    if (index < 0 || index >= card_faces_rasterized.size()
        || card_face_size.isEmpty()) {
        invalidate_selected_card_face();
        return selected_card_face;
    }
    if (selected_card_face_index != index) {
        // Widgets share immutable raster handles. Only the face being painted
        // needs a platform pixmap at this widget's current display size.
        selected_card_face
            = QPixmap::fromImage(card_faces_rasterized.at(index));
        if (!selected_card_face.isNull()) {
            selected_card_face = selected_card_face.scaled(
                card_face_size, Qt::IgnoreAspectRatio, Qt::FastTransformation
            );
        }
        selected_card_face_index = index;
    }
    return selected_card_face;
}

void card_widget::start_rasterization(const QSize& target_size) {
    if (target_size.isEmpty()) {
        return;
    }

    raster_task_size = target_size;
    raster_task_source = card_sheet_source;
    pending_raster_size = QSize();
    set_rasterizing(true);

    const QString source = raster_task_source;
    const QSize raster_size = target_size;

    rasterize_watcher.setFuture(QtConcurrent::run(
        &card_widget::rasterize_card_faces, source, raster_size
    ));
}

void card_widget::apply_rasterized_images(
    const QVector<QImage>& images, const QSize& target_size
) {
    card_faces_rasterized = images;
    card_face_raster_size = target_size;
    invalidate_selected_card_face();
    picks_since_rasterize = 0;
}

void card_widget::set_rasterizing(bool active) {
    if (rasterizing == active) {
        return;
    }

    rasterizing = active;
    emit rasterization_busy_changed(active);
}

void card_widget::on_rasterization_finished() {
    // Cancellation invalidates result access, but an owned job still needs
    // cleanup and dispatch of the latest demand. Idle notifications do not.
    if (!rasterizing
        || (!rasterize_watcher.future().isValid()
            && !rasterize_watcher.isCanceled())) {
        return;
    }
    // Shared ownership can change before a local job's notification arrives.
    if (shared_card_faces_mode || shared_card_faces_active) {
        rasterize_watcher.setFuture(QFuture<QVector<QImage>>());
        pending_raster_size = QSize();
        set_rasterizing(false);
        return;
    }

    const QString finished_source = raster_task_source;
    const bool canceled = rasterize_watcher.isCanceled();
    const QVector<QImage> images
        = canceled ? QVector<QImage>() : rasterize_watcher.result();
    // The widget owns accepted rasters; an idle observer need not retain them.
    rasterize_watcher.setFuture(QFuture<QVector<QImage>>());
    const bool source_still_current = finished_source == card_sheet_source;

    if (!canceled && source_still_current) {
        apply_rasterized_images(images, raster_task_size);
    }

    if ((canceled || !source_still_current) && pending_raster_size.isEmpty()
        && !card_face_size.isEmpty()) {
        pending_raster_size = raster_cache_size(card_face_size);
    }

    if (!pending_raster_size.isEmpty()
        && (canceled || pending_raster_size != raster_task_size
            || !source_still_current)) {
        const QSize next_size = pending_raster_size;
        start_rasterization(next_size);
        update();
        return;
    }

    set_rasterizing(false);
    update();
}
