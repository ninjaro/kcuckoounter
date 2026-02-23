#include "helpers/image_cacher.hpp"

#include <QDateTime>
#include <QPainter>
#include <QtGlobal>
#include <cmath>

namespace {

svg_raster_cache_service& shared_single_svg_cache_service() {
    static svg_raster_cache_service service;
    return service;
}

} // namespace

image_cacher::image_cacher(const QString& source_path)
    : source_path(source_path)
    , target_size()
    , cached_pixmap()
    , renderer()
    , base_scale(1.75)
    , min_short_px(63)
    , name_space(svg_raster_cache_service::cache_namespace::main) {
    if (!source_path.isEmpty()) {
        renderer.load(source_path);
    }
}

void image_cacher::set_source(const QString& new_source_path) {
    if (source_path == new_source_path) {
        return;
    }
    source_path = new_source_path;
    renderer.load(source_path);
    rasterize();
}

void image_cacher::set_target_size(const QSize& new_target_size) {
    if (target_size == new_target_size) {
        return;
    }
    target_size = new_target_size;
    rasterize();
}

void image_cacher::set_base_scale(qreal new_base_scale) {
    const qreal clamped = std::max<qreal>(0.1, new_base_scale);
    if (qFuzzyCompare(base_scale, clamped)) {
        return;
    }
    base_scale = clamped;
    rasterize();
}

void image_cacher::set_min_short_px(int new_min_short_px) {
    const int clamped = std::max(0, new_min_short_px);
    if (min_short_px == clamped) {
        return;
    }
    min_short_px = clamped;
    rasterize();
}

void image_cacher::set_cache_namespace(
    svg_raster_cache_service::cache_namespace new_namespace
) {
    if (name_space == new_namespace) {
        return;
    }
    name_space = new_namespace;
    rasterize();
}

svg_raster_cache_service::cache_namespace
image_cacher::cache_namespace() const {
    return name_space;
}

QSize image_cacher::display_size() const { return target_size; }

const QPixmap& image_cacher::pixmap() const { return cached_pixmap; }

bool image_cacher::is_ready() const { return !cached_pixmap.isNull(); }

bool image_cacher::has_source() const {
    return !source_path.isEmpty() && renderer.isValid();
}

QSize image_cacher::raster_cache_size(const QSize& desired_size) const {
    if (desired_size.isEmpty()) {
        return QSize();
    }
    const qreal min_side
        = std::min(desired_size.width(), desired_size.height());
    qreal scale = base_scale;
    if (min_side > 0.0 && min_short_px > 0) {
        scale = std::max(scale, static_cast<qreal>(min_short_px) / min_side);
    }
    const int width = std::max(
        1, static_cast<int>(std::ceil(desired_size.width() * scale))
    );
    const int height = std::max(
        1, static_cast<int>(std::ceil(desired_size.height() * scale))
    );
    return QSize(width, height);
}

void image_cacher::rasterize() {
    if (!renderer.isValid() || source_path.isEmpty() || target_size.isEmpty()) {
        cached_pixmap = QPixmap();
        return;
    }

    const QSize cache_size = raster_cache_size(target_size);
    const int bucket = std::min(cache_size.width(), cache_size.height());
    const svg_raster_cache_service::request req {
        .name_space = name_space,
        .kind = svg_raster_cache_service::resource_kind::single_svg,
        .source_id = source_path,
        .render_scope = QStringLiteral("full"),
        .need_short_px = std::min(target_size.width(), target_size.height()),
        .target_bucket_px = bucket,
        .high_priority = false,
        .interactive = true,
        .preview
        = name_space == svg_raster_cache_service::cache_namespace::settings,
    };
    svg_raster_cache_service& service = shared_single_svg_cache_service();
    const svg_raster_cache_service::submit_outcome outcome
        = service.submit_request(req);
    if (outcome.ready_result.has_value()
        && !outcome.ready_result->single_image.isNull()) {
        cached_pixmap = QPixmap::fromImage(outcome.ready_result->single_image);
        return;
    }

    QPixmap new_pixmap(cache_size);
    new_pixmap.fill(Qt::transparent);

    QPainter painter(&new_pixmap);
    renderer.render(
        &painter, QRectF(QPointF(0.0, 0.0), QSizeF(new_pixmap.size()))
    );
    cached_pixmap = new_pixmap;

    svg_raster_cache_service::result ready {
        .key = outcome.key,
        .raster_size = cache_size,
        .generation = 0,
        .timestamp_ms = QDateTime::currentMSecsSinceEpoch(),
        .use_count = 0,
        .single_image = new_pixmap.toImage(),
        .face_images = {},
    };
    service.insert_or_update_result(ready);

    const svg_raster_cache_service::family_key family {
        .name_space = outcome.key.name_space,
        .kind = outcome.key.kind,
        .source_id = outcome.key.source_id,
        .render_scope = outcome.key.render_scope,
    };
    service.finish_active_request(family, outcome.key);
}
