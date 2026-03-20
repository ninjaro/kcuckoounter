#ifndef KCUCKOOUNTER_MONITOR_GEOMETRY_DEBUG_TELEMETRY_HPP
#define KCUCKOOUNTER_MONITOR_GEOMETRY_DEBUG_TELEMETRY_HPP

#include <QMetaType>
#include <QSize>
#include <QtGlobal>

struct geometry_debug_snapshot {
    qint64 timestamp_ms = 0;
    int slot_count = 0;
    int visible_slot_count = 0;
    QSize window_size;
    QSize layout_size;
    QSize display_card_size;
    int display_card_need_short_px = 0;
    int active_bucket_px = 0;
    int warming_bucket_px = 0;
    QSize cache_raster_size;
    QSize preloaded_raster_size;
    int coverage_percent = 0;
    qint64 coverage_window_ms = 0;
    int unique_size_buckets = 0;
    bool prewarm_in_flight = false;
    qint64 active_generation_id = 0;
    qint64 warming_generation_id = 0;
};

struct resize_transition_debug_event {
    qint64 timestamp_ms = 0;
    QSize old_window_size;
    QSize new_window_size;
    int old_active_bucket_px = 0;
    int new_active_bucket_px = 0;
    int old_warming_bucket_px = 0;
    int new_warming_bucket_px = 0;
    geometry_debug_snapshot geometry_after_resize;
};

Q_DECLARE_METATYPE(geometry_debug_snapshot)
Q_DECLARE_METATYPE(resize_transition_debug_event)

#endif // KCUCKOOUNTER_MONITOR_GEOMETRY_DEBUG_TELEMETRY_HPP
