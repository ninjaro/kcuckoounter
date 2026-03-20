#ifndef KCUCKOOUNTER_IMAGE_IMAGE_CACHER_HPP
#define KCUCKOOUNTER_IMAGE_IMAGE_CACHER_HPP

#include <QPixmap>
#include <QSize>
#include <QString>
#include <QSvgRenderer>

#include "image/raster_cache.hpp"

class image_cacher {
public:
    explicit image_cacher(const QString& source_path_value = QString());
    ~image_cacher();

    void set_source(const QString& new_source_path);
    void set_target_size(const QSize& new_target_size);
    void set_base_scale(qreal new_base_scale);
    void set_min_short_px(int new_min_short_px);
    void set_cache_namespace(raster_cache::cache_namespace new_namespace);
    raster_cache::cache_namespace cache_namespace() const;

    QSize display_size() const;
    const QPixmap& pixmap() const;
    bool is_ready() const;
    bool has_source() const;

private:
    QString source_path;
    QSize target_size;
    QPixmap cached_pixmap;
    QSvgRenderer renderer;
    qreal base_scale;
    int min_short_px;
    raster_cache::cache_namespace name_space;
    std::optional<raster_cache::entry_key> displayed_entry_key;

    QSize raster_cache_size(const QSize& desired_size) const;
    void clear_display_tracking();
    void rasterize();
};

#endif // KCUCKOOUNTER_IMAGE_IMAGE_CACHER_HPP
