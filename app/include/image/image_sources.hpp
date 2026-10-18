#ifndef KCUCKOOUNTER_IMAGE_IMAGE_SOURCES_HPP
#define KCUCKOOUNTER_IMAGE_IMAGE_SOURCES_HPP

#include <rasterflow/qt/image_service.hpp>

// The application owns one service. Widgets own subscriptions, never workers or
// raster stores. Loading and decoding game files remains a client concern.
rasterflow::qt::image_service& application_image_service();
rasterflow::qt::source_handle
load_image_source(const QString& path, QString* error = nullptr);

#endif
