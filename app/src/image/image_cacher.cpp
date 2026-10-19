#include "image/image_cacher.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
QSize physical_size(const QSize& logical, qreal ratio) {
    if (logical.isEmpty())
        return {};
    const auto width = std::ceil(logical.width() * ratio);
    const auto height = std::ceil(logical.height() * ratio);
    if (!std::isfinite(width) || !std::isfinite(height)
        || width > std::numeric_limits<int>::max()
        || height > std::numeric_limits<int>::max())
        throw std::invalid_argument(
            "image dimensions exceed physical pixel range"
        );
    return { static_cast<int>(width), static_cast<int>(height) };
}
}

image_cacher::image_cacher(const QString& source_path_value, QObject* parent)
    : QObject(parent)
    , consumer(application_image_service().create_consumer(this)) {
    connect(
        &acquisition, &image_source_request::finished, this,
        &image_cacher::update_desired
    );
    connect(
        consumer, &rasterflow::qt::image_consumer::images_changed, this,
        &image_cacher::publish_image
    );
    connect(
        consumer, &rasterflow::qt::image_consumer::image_failed, this,
        [this](const QString&, const QString& reason) { source_error = reason; }
    );
    set_source(source_path_value);
}

void image_cacher::set_source(const QString& new_source_path) {
    if (source_path == new_source_path)
        return;
    source_path = new_source_path;
    source_error.clear();
    acquisition.load(source_path);
    cached_pixmap = {};
    displayed_image_key = 0;
    update_desired();
}

void image_cacher::set_target_size(const QSize& new_target_size) {
    if (target_size == new_target_size)
        return;
    (void)physical_size(new_target_size, device_pixel_ratio);
    target_size = new_target_size;
    update_desired();
}

void image_cacher::set_device_pixel_ratio(qreal ratio) {
    if (!std::isfinite(ratio) || ratio <= 0)
        throw std::invalid_argument("invalid image device pixel ratio");
    if (device_pixel_ratio == ratio)
        return;
    (void)physical_size(target_size, ratio);
    device_pixel_ratio = ratio;
    update_desired();
}

QSize image_cacher::display_size() const { return target_size; }

const QPixmap& image_cacher::pixmap() const { return cached_pixmap; }

bool image_cacher::is_ready() const { return !cached_pixmap.isNull(); }

bool image_cacher::has_source() const {
    return acquisition.source().is_valid() && acquisition.error().isEmpty();
}

QString image_cacher::error() const {
    return source_error.isEmpty() ? acquisition.error() : source_error;
}

void image_cacher::update_desired() {
    const auto source = acquisition.source();
    if (!source.is_valid() || !source.error().isEmpty()
        || target_size.isEmpty()) {
        consumer->replace_desired({});
        request = {};
        cached_pixmap = {};
        displayed_image_key = 0;
        emit pixmap_changed();
        return;
    }
    request = { .source = source,
                .element_id = {},
                .physical_size = physical_size(target_size, device_pixel_ratio),
                .required = true,
                .priority = 0,
                .deadline_us = {},
                .viewport = {} };
    consumer->replace_desired({ request });
    publish_image();
}

void image_cacher::publish_image() {
    if (!request.source.is_valid() || target_size.isEmpty())
        return;
    const auto image = consumer->image_for(request);
    if (image.isNull()
        || (image.cacheKey() == displayed_image_key
            && cached_pixmap.devicePixelRatio() == device_pixel_ratio))
        return;
    cached_pixmap = QPixmap::fromImage(image);
    cached_pixmap.setDevicePixelRatio(device_pixel_ratio);
    displayed_image_key = image.cacheKey();
    emit pixmap_changed();
}
