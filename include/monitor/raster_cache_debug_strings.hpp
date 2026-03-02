#ifndef KCUCKOOUNTER_MONITOR_RASTER_CACHE_DEBUG_STRINGS_HPP
#define KCUCKOOUNTER_MONITOR_RASTER_CACHE_DEBUG_STRINGS_HPP

#include "image/raster_cache.hpp"

#include <QString>

QString cache_namespace_to_string(raster_cache::cache_namespace name_space);
QString resource_kind_to_string(raster_cache::resource_kind kind);
QString
debug_consumer_scope_to_string(raster_cache::debug_consumer_scope scope);

#endif // KCUCKOOUNTER_MONITOR_RASTER_CACHE_DEBUG_STRINGS_HPP
