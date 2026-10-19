#ifndef KCUCKOOUNTER_IMAGE_IMAGE_CACHER_HPP
#define KCUCKOOUNTER_IMAGE_IMAGE_CACHER_HPP

#include "image/image_sources.hpp"
#include <QPixmap>

// GUI presentation adapter. Rasterflow owns immutable sources, scheduling and
// shared QImage masters; this object retains only its displayed QPixmap.
class image_cacher : public QObject {
    Q_OBJECT
public:
    explicit image_cacher(
        const QString& source_path_value = {}, QObject* parent = nullptr
    );
    void set_source(const QString& new_source_path);
    void set_target_size(const QSize& new_target_size);
    void set_device_pixel_ratio(qreal ratio);
    [[nodiscard]] QSize display_size() const;
    [[nodiscard]] const QPixmap& pixmap() const;
    [[nodiscard]] bool is_ready() const;
    [[nodiscard]] bool has_source() const;
    [[nodiscard]] QString error() const;

signals:
    void pixmap_changed();

private:
    QString source_path;
    QSize target_size;
    qreal device_pixel_ratio = 1.0;
    QPixmap cached_pixmap;
    qint64 displayed_image_key = 0;
    image_source_request acquisition;
    rasterflow::qt::image_consumer* consumer;
    rasterflow::qt::image_request request;
    QString source_error;
    void update_desired();
    void publish_image();
};

#endif
