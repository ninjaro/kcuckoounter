#include "monitor/raster_cache_debug_strings.hpp"

QString cache_namespace_to_string(raster_cache::cache_namespace name_space) {
    switch (name_space) {
    case raster_cache::cache_namespace::settings:
        return QStringLiteral("settings");
    case raster_cache::cache_namespace::main:
    default:
        return QStringLiteral("main");
    }
}

QString resource_kind_to_string(raster_cache::resource_kind kind) {
    switch (kind) {
    case raster_cache::resource_kind::card_sheet_faces:
        return QStringLiteral("card_sheet_faces");
    case raster_cache::resource_kind::single_svg:
    default:
        return QStringLiteral("single_svg");
    }
}

QString
debug_consumer_scope_to_string(raster_cache::debug_consumer_scope scope) {
    switch (scope) {
    case raster_cache::debug_consumer_scope::table_slots:
        return QStringLiteral("table_slots");
    case raster_cache::debug_consumer_scope::settings_theme_carousel:
        return QStringLiteral("settings_theme_carousel");
    case raster_cache::debug_consumer_scope::settings_strategy_preview:
        return QStringLiteral("settings_strategy_preview");
    case raster_cache::debug_consumer_scope::image_cacher:
        return QStringLiteral("image_cacher");
    case raster_cache::debug_consumer_scope::unknown:
    default:
        return QStringLiteral("unknown");
    }
}
