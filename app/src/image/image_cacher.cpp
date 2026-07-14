#include "image/image_cacher.hpp"
#include "image/rasterization_runner.hpp"

#include <QDateTime>
#include <QPainter>
#include <QtGlobal>
#include <cmath>

static raster_cache& shared_single_svg_cache_service() {
    static raster_cache service;
    return service;
}

image_cacher::image_cacher(
    const QString& source_path_value, raster_cache* cache_service
)
    : source_path(source_path_value)
    , target_size()
    , cached_pixmap()
    , renderer()
    , base_scale(1.25)
    , min_short_px(63)
    , name_space(raster_cache::cache_namespace::main)
    , cache_service_value(cache_service)
    , displayed_entry_key(std::nullopt) {
    if (!source_path_value.isEmpty()) {
        renderer.load(source_path_value);
    }
}

image_cacher::~image_cacher() { clear_display_tracking(); }

void image_cacher::set_source(const QString& new_source_path) {
    if (source_path == new_source_path) {
        return;
    }
    source_path = new_source_path;
    renderer.load(source_path);
    rasterize(true);
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
    rasterize(true);
}

void image_cacher::set_min_short_px(int new_min_short_px) {
    const int clamped = std::max(0, new_min_short_px);
    if (min_short_px == clamped) {
        return;
    }
    min_short_px = clamped;
    rasterize(true);
}

void image_cacher::set_cache_namespace(
    raster_cache::cache_namespace new_namespace
) {
    if (name_space == new_namespace) {
        return;
    }
    name_space = new_namespace;
    rasterize(true);
}

raster_cache::cache_namespace image_cacher::cache_namespace() const {
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
        return {};
    }
    const int min_side = std::min(desired_size.width(), desired_size.height());
    const int policy_target
        = rasterization_runner::target_cache_px_for_need(min_side);
    const int configured_target = std::max(
        min_short_px,
        static_cast<int>(std::ceil(static_cast<qreal>(min_side) * base_scale))
    );
    const int target_short_px = std::max(policy_target, configured_target);
    const qreal scale = static_cast<qreal>(target_short_px) / min_side;
    const int width = std::max(
        1, static_cast<int>(std::ceil(desired_size.width() * scale))
    );
    const int height = std::max(
        1, static_cast<int>(std::ceil(desired_size.height() * scale))
    );
    return { width, height };
}

void image_cacher::rasterize(bool force) {
    if (!renderer.isValid() || source_path.isEmpty() || target_size.isEmpty()) {
        const std::optional<raster_cache::entry_key> previous_entry
            = displayed_entry_key;
        clear_display_tracking();
        if (previous_entry.has_value()) {
            cache_service().erase_result(*previous_entry);
        }
        cached_pixmap = QPixmap();
        return;
    }

    const int required_short_px
        = std::min(target_size.width(), target_size.height());
    const bool same_family = displayed_entry_key.has_value()
        && displayed_entry_key->name_space == name_space
        && displayed_entry_key->kind == raster_cache::resource_kind::single_svg
        && displayed_entry_key->source_id == source_path
        && displayed_entry_key->render_scope == QStringLiteral("full");
    if (same_family && !cached_pixmap.isNull() && !force) {
        const rasterization_runner::evaluation decision
            = rasterization_runner::evaluate_size_need(
                required_short_px, displayed_entry_key->target_bucket_px
            );
        if (!decision.rasterization_required) {
            cache_service().note_entry_displayed(
                *displayed_entry_key,
                raster_cache::debug_consumer_scope::image_cacher
            );
            return;
        }
    }

    const QSize cache_size = raster_cache_size(target_size);
    const int bucket = std::min(cache_size.width(), cache_size.height());
    const raster_cache::request req {
        .name_space = name_space,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = source_path,
        .render_scope = QStringLiteral("full"),
        .need_short_px = std::min(target_size.width(), target_size.height()),
        .target_bucket_px = bucket,
        .high_priority = false,
        .interactive = true,
        .preview = name_space == raster_cache::cache_namespace::settings,
    };
    raster_cache& service = cache_service();
    const std::optional<raster_cache::entry_key> previous_entry
        = displayed_entry_key;
    const raster_cache::submit_outcome outcome = service.submit_request(req);
    if (displayed_entry_key.has_value()
        && *displayed_entry_key != outcome.key) {
        clear_display_tracking();
    }

    if (outcome.ready_result.has_value()
        && !outcome.ready_result->single_image.isNull()) {
        service.note_entry_displayed(
            outcome.key, raster_cache::debug_consumer_scope::image_cacher
        );
        displayed_entry_key = outcome.key;
        cached_pixmap = QPixmap::fromImage(outcome.ready_result->single_image);
        if (previous_entry.has_value() && *previous_entry != outcome.key) {
            service.erase_result(*previous_entry);
        }
        return;
    }

    QPixmap new_pixmap(cache_size);
    new_pixmap.fill(Qt::transparent);

    QPainter painter(&new_pixmap);
    renderer.render(
        &painter, QRectF(QPointF(0.0, 0.0), QSizeF(new_pixmap.size()))
    );
    cached_pixmap = new_pixmap;

    raster_cache::result ready {
        .key = outcome.key,
        .raster_size = cache_size,
        .generation = 0,
        .timestamp_ms = QDateTime::currentMSecsSinceEpoch(),
        .use_count = 0,
        .single_image = new_pixmap.toImage(),
        .face_images = {},
    };
    service.insert_or_update_result(ready);
    service.note_entry_displayed(
        outcome.key, raster_cache::debug_consumer_scope::image_cacher
    );
    displayed_entry_key = outcome.key;

    const raster_cache::family_key family {
        .name_space = outcome.key.name_space,
        .kind = outcome.key.kind,
        .source_id = outcome.key.source_id,
        .render_scope = outcome.key.render_scope,
    };
    service.finish_active_request(family, outcome.key);
    if (previous_entry.has_value() && *previous_entry != outcome.key) {
        service.erase_result(*previous_entry);
    }
}

raster_cache& image_cacher::cache_service() const {
    return cache_service_value == nullptr ? shared_single_svg_cache_service()
                                          : *cache_service_value;
}

void image_cacher::clear_display_tracking() {
    if (!displayed_entry_key.has_value()) {
        return;
    }

    cache_service().note_entry_no_longer_displayed(
        *displayed_entry_key, raster_cache::debug_consumer_scope::image_cacher
    );
    displayed_entry_key.reset();
}
