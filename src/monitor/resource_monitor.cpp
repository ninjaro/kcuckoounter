#include "monitor/resource_monitor.hpp"

#include "monitor/raster_cache_debug_strings.hpp"
#include "table/table.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QThreadPool>
#include <QtConcurrent>
#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace {

int size_to_int(qsizetype value) {
    return value > static_cast<qsizetype>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(value);
}

QString process_memory_report_trigger_to_string(
    resource_monitor::process_memory_report_trigger trigger
) {
    switch (trigger) {
    case resource_monitor::process_memory_report_trigger::threshold_rss_growth:
        return QStringLiteral("threshold_rss_growth");
    case resource_monitor::process_memory_report_trigger::manual_on_demand:
    default:
        return QStringLiteral("manual_on_demand_heavy_dump");
    }
}

QString cadence_mode_to_string(resource_monitor::debug_cadence_mode mode) {
    switch (mode) {
    case resource_monitor::debug_cadence_mode::instrumented:
        return QStringLiteral("instrumented");
    case resource_monitor::debug_cadence_mode::realistic:
    default:
        return QStringLiteral("realistic");
    }
}

QString process_memory_source_for_rss(qint64 rss_bytes);
QString process_memory_unavailable_reason();

QJsonObject size_to_json(const QSize& size) {
    QJsonObject object;
    object.insert(QStringLiteral("width"), size.width());
    object.insert(QStringLiteral("height"), size.height());
    return object;
}

QJsonObject geometry_snapshot_to_json(const geometry_debug_snapshot& snapshot) {
    QJsonObject object;
    object.insert(QStringLiteral("timestamp_ms"), snapshot.timestamp_ms);
    object.insert(QStringLiteral("slot_count"), snapshot.slot_count);
    object.insert(
        QStringLiteral("visible_slot_count"), snapshot.visible_slot_count
    );
    object.insert(
        QStringLiteral("window_size"), size_to_json(snapshot.window_size)
    );
    object.insert(
        QStringLiteral("layout_size"), size_to_json(snapshot.layout_size)
    );
    object.insert(
        QStringLiteral("display_card_size"),
        size_to_json(snapshot.display_card_size)
    );
    object.insert(
        QStringLiteral("display_card_need_short_px"),
        snapshot.display_card_need_short_px
    );
    object.insert(
        QStringLiteral("active_bucket_px"), snapshot.active_bucket_px
    );
    object.insert(
        QStringLiteral("warming_bucket_px"), snapshot.warming_bucket_px
    );
    object.insert(
        QStringLiteral("cache_raster_size"),
        size_to_json(snapshot.cache_raster_size)
    );
    object.insert(
        QStringLiteral("preloaded_raster_size"),
        size_to_json(snapshot.preloaded_raster_size)
    );
    object.insert(
        QStringLiteral("coverage_percent"), snapshot.coverage_percent
    );
    object.insert(
        QStringLiteral("coverage_window_ms"), snapshot.coverage_window_ms
    );
    object.insert(
        QStringLiteral("unique_size_buckets"), snapshot.unique_size_buckets
    );
    object.insert(
        QStringLiteral("prewarm_in_flight"), snapshot.prewarm_in_flight
    );
    object.insert(
        QStringLiteral("active_generation_id"), snapshot.active_generation_id
    );
    object.insert(
        QStringLiteral("warming_generation_id"), snapshot.warming_generation_id
    );
    return object;
}

QJsonObject resize_history_entry_to_json(
    const resource_monitor::resize_history_entry& entry
) {
    QJsonObject object;
    object.insert(
        QStringLiteral("collector_sequence"), entry.collector_sequence
    );
    object.insert(
        QStringLiteral("transition_start_timestamp_ms"),
        entry.transition_start_timestamp_ms
    );
    object.insert(
        QStringLiteral("transition_end_timestamp_ms"),
        entry.transition_end_timestamp_ms
    );
    object.insert(
        QStringLiteral("prewarm_completion_ms"), entry.prewarm_completion_ms
    );
    object.insert(
        QStringLiteral("old_window_size"), size_to_json(entry.old_window_size)
    );
    object.insert(
        QStringLiteral("new_window_size"), size_to_json(entry.new_window_size)
    );
    object.insert(
        QStringLiteral("old_active_bucket_px"), entry.old_active_bucket_px
    );
    object.insert(
        QStringLiteral("new_active_bucket_px"), entry.new_active_bucket_px
    );
    object.insert(
        QStringLiteral("old_warming_bucket_px"), entry.old_warming_bucket_px
    );
    object.insert(
        QStringLiteral("new_warming_bucket_px"), entry.new_warming_bucket_px
    );
    object.insert(
        QStringLiteral("geometry_after_resize"),
        geometry_snapshot_to_json(entry.geometry_after_resize)
    );
    object.insert(
        QStringLiteral("before_process_rss_bytes_measured"),
        entry.before_process_rss_bytes
    );
    object.insert(
        QStringLiteral("after_process_rss_bytes_measured"),
        entry.after_process_rss_bytes
    );
    object.insert(
        QStringLiteral("before_cache_accounted_ready_bytes"),
        entry.before_cache_accounted_ready_bytes
    );
    object.insert(
        QStringLiteral("after_cache_accounted_ready_bytes"),
        entry.after_cache_accounted_ready_bytes
    );
    object.insert(
        QStringLiteral("before_widget_local_display_bytes_estimated"),
        entry.before_widget_local_display_bytes_estimated
    );
    object.insert(
        QStringLiteral("after_widget_local_display_bytes_estimated"),
        entry.after_widget_local_display_bytes_estimated
    );
    object.insert(
        QStringLiteral("before_measured_accounted_gap_bytes_derived"),
        entry.before_measured_accounted_gap_bytes
    );
    object.insert(
        QStringLiteral("after_measured_accounted_gap_bytes_derived"),
        entry.after_measured_accounted_gap_bytes
    );
    return object;
}

QString resize_history_entry_to_jsonl_line(
    const resource_monitor::resize_history_entry& entry
) {
    const QJsonDocument document(resize_history_entry_to_json(entry));
    return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
}

QString write_debug_snapshot_json(
    const QString& output_path,
    const resource_monitor::export_request_metadata& metadata,
    const QVector<resource_monitor::cache_timeline_entry>& cache_entries,
    const QVector<resource_monitor::event_timeline_entry>& event_entries,
    const QVector<geometry_debug_snapshot>& geometry_entries,
    const QVector<resource_monitor::resize_history_entry>& resize_entries,
    resource_monitor::debug_cadence_mode export_mode
) {
    QJsonObject root;
    root.insert(
        QStringLiteral("collector_sequence"), metadata.collector_sequence
    );
    root.insert(
        QStringLiteral("cache_timeline_size"), metadata.cache_timeline_size
    );
    root.insert(
        QStringLiteral("event_timeline_size"), metadata.event_timeline_size
    );
    root.insert(
        QStringLiteral("geometry_timeline_size"),
        metadata.geometry_timeline_size
    );
    root.insert(
        QStringLiteral("resize_history_size"), metadata.resize_history_size
    );
    root.insert(
        QStringLiteral("resize_history_log_path"),
        metadata.resize_history_log_path
    );
    root.insert(
        QStringLiteral("debug_cadence_mode"),
        cadence_mode_to_string(export_mode)
    );
    root.insert(
        QStringLiteral("process_memory_rss_bytes"),
        metadata.latest_process_rss_bytes
    );
    root.insert(
        QStringLiteral("process_memory_rss_source"),
        metadata.process_memory_source
    );
    root.insert(
        QStringLiteral("process_memory_rss_available"),
        metadata.latest_process_rss_bytes >= 0
    );
    if (metadata.latest_process_rss_bytes < 0) {
        root.insert(
            QStringLiteral("process_memory_rss_unavailable_reason"),
            metadata.process_memory_unavailable_reason
        );
    }
    root.insert(
        QStringLiteral("process_memory_sample_interval_ms"),
        metadata.process_memory_sample_interval_ms
    );
    root.insert(
        QStringLiteral("auto_process_report_rss_growth_threshold_bytes"),
        metadata.auto_process_report_rss_growth_threshold_bytes
    );
    root.insert(
        QStringLiteral("auto_process_report_cooldown_ms"),
        metadata.auto_process_report_cooldown_ms
    );
    root.insert(
        QStringLiteral("auto_process_report_baseline_rss_bytes"),
        metadata.auto_process_report_baseline_rss_bytes
    );
    root.insert(
        QStringLiteral("auto_process_report_rss_growth_since_baseline_bytes"),
        metadata.auto_process_report_rss_growth_since_baseline_bytes
    );
    root.insert(
        QStringLiteral("auto_process_report_last_trigger_utc_ms"),
        metadata.auto_process_report_last_trigger_utc_ms
    );
    root.insert(
        QStringLiteral("auto_process_report_cooldown_remaining_ms"),
        metadata.auto_process_report_cooldown_remaining_ms
    );
    root.insert(
        QStringLiteral("auto_process_report_consecutive_growth_hits_required"),
        metadata.auto_process_report_consecutive_growth_hits_required
    );
    root.insert(
        QStringLiteral("auto_process_report_consecutive_growth_hits_current"),
        metadata.auto_process_report_consecutive_growth_hits_current
    );
    root.insert(
        QStringLiteral("auto_process_report_window_ms"),
        metadata.auto_process_report_window_ms
    );
    root.insert(
        QStringLiteral("auto_process_report_window_max_exports"),
        metadata.auto_process_report_window_max_exports
    );
    root.insert(
        QStringLiteral("auto_process_report_window_exports_used"),
        metadata.auto_process_report_window_exports_used
    );
    QJsonObject telemetry_semantics;
    telemetry_semantics.insert(
        QStringLiteral("cache_accounted_ready_bytes"),
        QStringLiteral("accounted")
    );
    telemetry_semantics.insert(
        QStringLiteral("widget_local_display_bytes_estimated"),
        QStringLiteral("estimated")
    );
    telemetry_semantics.insert(
        QStringLiteral("process_memory_rss_bytes"), QStringLiteral("measured")
    );
    telemetry_semantics.insert(
        QStringLiteral("fallback_active_theme_keys_ready"),
        QStringLiteral("accounted_required_key_resolution")
    );
    telemetry_semantics.insert(
        QStringLiteral("fallback_default_theme_keys_ready"),
        QStringLiteral("accounted_required_key_resolution")
    );
    telemetry_semantics.insert(
        QStringLiteral("fallback_placeholder_keys_ready"),
        QStringLiteral("accounted_required_key_resolution")
    );
    telemetry_semantics.insert(
        QStringLiteral("process_memory_rss_source"),
        QStringLiteral("source_label")
    );
    telemetry_semantics.insert(
        QStringLiteral("process_memory_sample_interval_ms"),
        QStringLiteral("derived_from_debug_cadence_mode")
    );
    telemetry_semantics.insert(
        QStringLiteral("auto_process_report_rss_growth_threshold_bytes"),
        QStringLiteral("derived_from_debug_cadence_mode_unless_test_override")
    );
    telemetry_semantics.insert(
        QStringLiteral("auto_process_report_cooldown_ms"),
        QStringLiteral("derived_from_debug_cadence_mode_unless_test_override")
    );
    telemetry_semantics.insert(
        QStringLiteral("auto_process_report_baseline_rss_bytes"),
        QStringLiteral("collector_runtime_state")
    );
    telemetry_semantics.insert(
        QStringLiteral("auto_process_report_rss_growth_since_baseline_bytes"),
        QStringLiteral("collector_runtime_state")
    );
    telemetry_semantics.insert(
        QStringLiteral("auto_process_report_last_trigger_utc_ms"),
        QStringLiteral("collector_runtime_state")
    );
    telemetry_semantics.insert(
        QStringLiteral("auto_process_report_cooldown_remaining_ms"),
        QStringLiteral("collector_runtime_state")
    );
    telemetry_semantics.insert(
        QStringLiteral("auto_process_report_consecutive_growth_hits_required"),
        QStringLiteral("derived_from_debug_cadence_mode_unless_test_override")
    );
    telemetry_semantics.insert(
        QStringLiteral("auto_process_report_consecutive_growth_hits_current"),
        QStringLiteral("collector_runtime_state")
    );
    telemetry_semantics.insert(
        QStringLiteral("auto_process_report_window_ms"),
        QStringLiteral("derived_from_debug_cadence_mode_unless_test_override")
    );
    telemetry_semantics.insert(
        QStringLiteral("auto_process_report_window_max_exports"),
        QStringLiteral("derived_from_debug_cadence_mode_unless_test_override")
    );
    telemetry_semantics.insert(
        QStringLiteral("auto_process_report_window_exports_used"),
        QStringLiteral("collector_runtime_state")
    );
    telemetry_semantics.insert(
        QStringLiteral("displayed_recent_note"),
        QStringLiteral(
            "displayed_recent_* fields are window-based recent-use heuristics"
        )
    );
    telemetry_semantics.insert(
        QStringLiteral("geometry_debug_snapshot"),
        QStringLiteral("aggregated_table_geometry_telemetry")
    );
    telemetry_semantics.insert(
        QStringLiteral("resize_history_before_after_memory"),
        QStringLiteral("before_after_measured_accounted_estimated_derived")
    );
    telemetry_semantics.insert(
        QStringLiteral("resize_history_log_stream"),
        QStringLiteral("append_only_jsonl")
    );
    root.insert(QStringLiteral("telemetry_semantics"), telemetry_semantics);

    QJsonArray cache_array;
    for (const auto& entry : cache_entries) {
        QJsonObject object;
        object.insert(
            QStringLiteral("collector_sequence"), entry.collector_sequence
        );
        object.insert(
            QStringLiteral("snapshot_sequence"),
            entry.cache_snapshot.snapshot_sequence
        );
        object.insert(
            QStringLiteral("ready_entries"), entry.cache_snapshot.ready_entries
        );
        object.insert(
            QStringLiteral("ready_bytes"),
            static_cast<qint64>(entry.cache_snapshot.ready_bytes)
        );
        object.insert(
            QStringLiteral("cache_accounted_ready_bytes"),
            static_cast<qint64>(entry.cache_snapshot.ready_bytes)
        );
        object.insert(
            QStringLiteral("cache_accounted_ready_bytes_delta"),
            entry.cache_accounted_ready_bytes_delta
        );
        object.insert(
            QStringLiteral("cache_entries_added_interval"),
            entry.cache_entries_added_interval
        );
        object.insert(
            QStringLiteral("cache_entries_removed_interval"),
            entry.cache_entries_removed_interval
        );
        object.insert(
            QStringLiteral("cache_bytes_added_interval"),
            entry.cache_bytes_added_interval
        );
        object.insert(
            QStringLiteral("cache_bytes_removed_interval"),
            entry.cache_bytes_removed_interval
        );
        object.insert(
            QStringLiteral("cache_images_added_interval"),
            entry.cache_images_added_interval
        );
        object.insert(
            QStringLiteral("cache_images_removed_interval"),
            entry.cache_images_removed_interval
        );
        object.insert(
            QStringLiteral("widget_local_rasterized_bytes_estimated"),
            static_cast<qint64>(
                entry.cache_snapshot.widget_local_rasterized_bytes_estimated
            )
        );
        object.insert(
            QStringLiteral("widget_local_scaled_bytes_estimated"),
            static_cast<qint64>(
                entry.cache_snapshot.widget_local_scaled_bytes_estimated
            )
        );
        object.insert(
            QStringLiteral("widget_local_display_bytes_estimated"),
            static_cast<qint64>(
                entry.cache_snapshot.widget_local_display_bytes_estimated
            )
        );
        object.insert(
            QStringLiteral("fallback_active_theme_keys_ready"),
            entry.cache_snapshot.fallback_active_theme_keys_ready
        );
        object.insert(
            QStringLiteral("fallback_default_theme_keys_ready"),
            entry.cache_snapshot.fallback_default_theme_keys_ready
        );
        object.insert(
            QStringLiteral("fallback_placeholder_keys_ready"),
            entry.cache_snapshot.fallback_placeholder_keys_ready
        );
        object.insert(
            QStringLiteral("widget_local_display_bytes_estimated_delta"),
            entry.widget_local_display_bytes_estimated_delta
        );
        object.insert(
            QStringLiteral("widget_local_display_bytes_materialized_interval"),
            entry.widget_local_display_bytes_materialized_interval
        );
        object.insert(
            QStringLiteral("widget_local_display_bytes_released_interval"),
            entry.widget_local_display_bytes_released_interval
        );
        object.insert(
            QStringLiteral("process_memory_rss_bytes"), entry.process_rss_bytes
        );
        object.insert(
            QStringLiteral("process_memory_rss_bytes_delta"),
            entry.process_rss_bytes_delta
        );
        object.insert(
            QStringLiteral("process_memory_rss_bytes_growth_interval"),
            entry.process_rss_bytes_growth_interval
        );
        object.insert(
            QStringLiteral("process_memory_rss_bytes_drop_interval"),
            entry.process_rss_bytes_drop_interval
        );
        object.insert(
            QStringLiteral("ready_images"), entry.cache_snapshot.ready_images
        );
        object.insert(
            QStringLiteral("displayed_ready_entries"),
            entry.cache_snapshot.displayed_ready_entries
        );
        object.insert(
            QStringLiteral("displayed_recent_entries"),
            entry.cache_snapshot.displayed_ready_entries
        );
        object.insert(
            QStringLiteral("cached_only_ready_entries"),
            entry.cache_snapshot.cached_only_ready_entries
        );
        object.insert(
            QStringLiteral("displayed_ready_images"),
            entry.cache_snapshot.displayed_ready_images
        );
        object.insert(
            QStringLiteral("displayed_recent_images"),
            entry.cache_snapshot.displayed_ready_images
        );
        object.insert(
            QStringLiteral("cached_only_ready_images"),
            entry.cache_snapshot.cached_only_ready_images
        );
        object.insert(
            QStringLiteral("displayed_entry_window_ms"),
            static_cast<qint64>(entry.cache_snapshot.displayed_entry_window_ms)
        );
        object.insert(
            QStringLiteral("displayed_entry_coverage_percent"),
            entry.cache_snapshot.displayed_entry_coverage_percent
        );

        QJsonArray bucket_array;
        for (const auto& bucket : entry.cache_snapshot.size_buckets) {
            QJsonObject bucket_object;
            bucket_object.insert(
                QStringLiteral("target_bucket_px"), bucket.target_bucket_px
            );
            bucket_object.insert(
                QStringLiteral("entry_count"), bucket.entry_count
            );
            bucket_object.insert(
                QStringLiteral("total_bytes"),
                static_cast<qint64>(bucket.total_bytes)
            );
            bucket_array.push_back(bucket_object);
        }
        object.insert(
            QStringLiteral("unique_size_buckets"),
            entry.cache_snapshot.unique_size_buckets
        );
        object.insert(QStringLiteral("size_buckets"), bucket_array);

        QJsonArray largest_array;
        for (const auto& largest : entry.cache_snapshot.largest_entries) {
            QJsonObject largest_object;
            largest_object.insert(
                QStringLiteral("name_space"),
                cache_namespace_to_string(largest.name_space)
            );
            largest_object.insert(
                QStringLiteral("kind"), resource_kind_to_string(largest.kind)
            );
            largest_object.insert(
                QStringLiteral("source_id"), largest.source_id
            );
            largest_object.insert(
                QStringLiteral("render_scope"), largest.render_scope
            );
            largest_object.insert(
                QStringLiteral("target_bucket_px"), largest.target_bucket_px
            );
            largest_object.insert(
                QStringLiteral("estimated_bytes"),
                static_cast<qint64>(largest.estimated_bytes)
            );
            largest_array.push_back(largest_object);
        }
        object.insert(QStringLiteral("largest_entries"), largest_array);

        QJsonArray requested_array;
        for (const auto& requested :
             entry.cache_snapshot.top_requested_entries) {
            QJsonObject requested_object;
            requested_object.insert(
                QStringLiteral("name_space"),
                cache_namespace_to_string(requested.name_space)
            );
            requested_object.insert(
                QStringLiteral("kind"), resource_kind_to_string(requested.kind)
            );
            requested_object.insert(
                QStringLiteral("source_id"), requested.source_id
            );
            requested_object.insert(
                QStringLiteral("render_scope"), requested.render_scope
            );
            requested_object.insert(
                QStringLiteral("target_bucket_px"), requested.target_bucket_px
            );
            requested_object.insert(
                QStringLiteral("request_count"), requested.request_count
            );
            requested_array.push_back(requested_object);
        }
        object.insert(QStringLiteral("top_requested_entries"), requested_array);

        QJsonArray expensive_array;
        for (const auto& expensive : entry.cache_snapshot.top_expensive_tasks) {
            QJsonObject expensive_object;
            expensive_object.insert(
                QStringLiteral("stage"),
                expensive.stage
                        == raster_cache::debug_snapshot::timing_stage::
                            coalesced_wait
                    ? QStringLiteral("coalesced_wait")
                    : QStringLiteral("raster_lifecycle")
            );
            expensive_object.insert(
                QStringLiteral("name_space"),
                cache_namespace_to_string(expensive.name_space)
            );
            expensive_object.insert(
                QStringLiteral("kind"), resource_kind_to_string(expensive.kind)
            );
            expensive_object.insert(
                QStringLiteral("source_id"), expensive.source_id
            );
            expensive_object.insert(
                QStringLiteral("render_scope"), expensive.render_scope
            );
            expensive_object.insert(
                QStringLiteral("target_bucket_px"), expensive.target_bucket_px
            );
            expensive_object.insert(
                QStringLiteral("completed_samples"), expensive.completed_samples
            );
            expensive_object.insert(
                QStringLiteral("avg_elapsed_ms"),
                static_cast<qint64>(expensive.avg_elapsed_ms)
            );
            expensive_object.insert(
                QStringLiteral("max_elapsed_ms"),
                static_cast<qint64>(expensive.max_elapsed_ms)
            );
            expensive_array.push_back(expensive_object);
        }
        object.insert(QStringLiteral("top_expensive_tasks"), expensive_array);

        QJsonArray subsystem_array;
        for (const auto& subsystem : entry.cache_snapshot.subsystem_summaries) {
            QJsonObject subsystem_object;
            subsystem_object.insert(
                QStringLiteral("name_space"),
                cache_namespace_to_string(subsystem.name_space)
            );
            subsystem_object.insert(
                QStringLiteral("kind"), resource_kind_to_string(subsystem.kind)
            );
            subsystem_object.insert(
                QStringLiteral("ready_entries"), subsystem.ready_entries
            );
            subsystem_object.insert(
                QStringLiteral("ready_bytes"), subsystem.ready_bytes
            );
            subsystem_object.insert(
                QStringLiteral("request_samples"), subsystem.request_samples
            );
            subsystem_object.insert(
                QStringLiteral("timing_samples"), subsystem.timing_samples
            );
            subsystem_object.insert(
                QStringLiteral("timing_max_elapsed_ms"),
                subsystem.timing_max_elapsed_ms
            );
            subsystem_array.push_back(subsystem_object);
        }
        object.insert(QStringLiteral("subsystem_summaries"), subsystem_array);

        QJsonArray consumer_array;
        for (const auto& consumer : entry.cache_snapshot.consumer_summaries) {
            QJsonObject consumer_object;
            consumer_object.insert(
                QStringLiteral("consumer"),
                debug_consumer_scope_to_string(consumer.consumer)
            );
            consumer_object.insert(
                QStringLiteral("displayed_recent_entries"),
                consumer.displayed_recent_entries
            );
            consumer_object.insert(
                QStringLiteral("displayed_recent_images"),
                consumer.displayed_recent_images
            );
            consumer_object.insert(
                QStringLiteral("displayed_recent_ready_bytes"),
                consumer.displayed_recent_ready_bytes
            );
            consumer_object.insert(
                QStringLiteral("displayed_recent_widget_local_bytes_estimated"),
                consumer.displayed_recent_widget_local_bytes_estimated
            );
            consumer_array.push_back(consumer_object);
        }
        object.insert(QStringLiteral("consumer_summaries"), consumer_array);
        cache_array.push_back(object);
    }
    root.insert(QStringLiteral("cache_timeline"), cache_array);

    QJsonArray event_array;
    for (const auto& entry : event_entries) {
        QJsonObject object;
        object.insert(
            QStringLiteral("collector_sequence"), entry.collector_sequence
        );
        object.insert(QStringLiteral("timestamp_ms"), entry.timestamp_ms);
        object.insert(
            QStringLiteral("kind"),
            entry.kind
                    == resource_monitor::event_timeline_entry::event_kind::
                        cache_snapshot
                ? QStringLiteral("cache_snapshot")
                : QStringLiteral("manual_marker")
        );
        object.insert(QStringLiteral("label"), entry.label);
        event_array.push_back(object);
    }
    root.insert(QStringLiteral("event_timeline"), event_array);

    QJsonArray geometry_array;
    for (const auto& entry : geometry_entries) {
        geometry_array.push_back(geometry_snapshot_to_json(entry));
    }
    root.insert(QStringLiteral("geometry_timeline"), geometry_array);

    QJsonArray resize_array;
    for (const auto& entry : resize_entries) {
        resize_array.push_back(resize_history_entry_to_json(entry));
    }
    root.insert(QStringLiteral("resize_history_recent"), resize_array);

    const QJsonDocument document(root);
    QFile file(output_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QStringLiteral("unable to open output file");
    }

    const qint64 written = file.write(document.toJson(QJsonDocument::Indented));
    if (written < 0) {
        return QStringLiteral("unable to write output file");
    }

    return QString();
}

struct process_status_sample {
    qint64 vm_rss_bytes = -1;
    qint64 vm_hwm_bytes = -1;
    qint64 vm_size_bytes = -1;
    qint64 vm_swap_bytes = -1;
};

struct process_memory_sample_result {
    qint64 rss_bytes = -1;
    QString source;
    QString unavailable_reason;
};

QString process_memory_source_for_rss(qint64 rss_bytes) {
    return rss_bytes >= 0 ? QStringLiteral("proc_status_vm_rss")
                          : QStringLiteral("unavailable");
}

QString process_memory_unavailable_reason() {
#if defined(Q_OS_LINUX)
    return QStringLiteral("proc_status_vm_rss_unreadable");
#else
    return QStringLiteral("proc_status_vm_rss_unsupported_platform");
#endif
}

qint64 parse_status_kb_line(const QString& line, const QString& key) {
    if (!line.startsWith(key)) {
        return -1;
    }

    const QString value_text = line.mid(key.size()).trimmed();
    const QStringList parts
        = value_text.split(QRegularExpression(QStringLiteral("\\s+")));
    if (parts.isEmpty()) {
        return -1;
    }

    bool ok = false;
    const qint64 value_kb = parts.first().toLongLong(&ok);
    if (!ok || value_kb < 0) {
        return -1;
    }

    return value_kb * 1024;
}

process_status_sample read_process_status_sample() {
    process_status_sample sample;
    QFile status_file(QStringLiteral("/proc/self/status"));
    if (!status_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return sample;
    }

    QTextStream stream(&status_file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (sample.vm_rss_bytes < 0) {
            sample.vm_rss_bytes
                = parse_status_kb_line(line, QStringLiteral("VmRSS:"));
        }
        if (sample.vm_hwm_bytes < 0) {
            sample.vm_hwm_bytes
                = parse_status_kb_line(line, QStringLiteral("VmHWM:"));
        }
        if (sample.vm_size_bytes < 0) {
            sample.vm_size_bytes
                = parse_status_kb_line(line, QStringLiteral("VmSize:"));
        }
        if (sample.vm_swap_bytes < 0) {
            sample.vm_swap_bytes
                = parse_status_kb_line(line, QStringLiteral("VmSwap:"));
        }
    }

    return sample;
}

QJsonObject read_smaps_rollup_bytes() {
    QJsonObject rollup;
    QFile smaps_rollup_file(QStringLiteral("/proc/self/smaps_rollup"));
    if (!smaps_rollup_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return rollup;
    }

    QTextStream stream(&smaps_rollup_file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        const int separator = static_cast<int>(line.indexOf(QLatin1Char(':')));
        if (separator <= 0) {
            continue;
        }

        const QString key = line.left(separator).trimmed();
        const qint64 value_bytes
            = parse_status_kb_line(line, QStringLiteral("%1:").arg(key));
        if (value_bytes >= 0) {
            rollup.insert(key, value_bytes);
        }
    }

    return rollup;
}

QString write_process_memory_report_json(
    const QString& output_path,
    resource_monitor::debug_cadence_mode cadence_mode,
    qint64 process_sample_interval_ms,
    qint64 auto_process_report_rss_growth_threshold_bytes,
    qint64 auto_process_report_cooldown_ms,
    qint64 auto_process_report_baseline_rss_bytes,
    qint64 auto_process_report_rss_growth_since_baseline_bytes,
    qint64 auto_process_report_last_trigger_utc_ms,
    qint64 auto_process_report_cooldown_remaining_ms,
    qint64 auto_process_report_consecutive_growth_hits_required,
    qint64 auto_process_report_consecutive_growth_hits_current,
    qint64 latest_process_rss_bytes, const QString& latest_process_rss_source,
    const QString& latest_process_rss_unavailable_reason,
    resource_monitor::process_memory_report_trigger trigger
) {
    const process_status_sample status_sample = read_process_status_sample();
    const QJsonObject smaps_rollup = read_smaps_rollup_bytes();
    const bool has_status = status_sample.vm_rss_bytes >= 0
        || status_sample.vm_hwm_bytes >= 0 || status_sample.vm_size_bytes >= 0
        || status_sample.vm_swap_bytes >= 0;
    const bool has_smaps_rollup = !smaps_rollup.isEmpty();

    QJsonObject root;
    root.insert(
        QStringLiteral("report_kind"),
        QStringLiteral("process_memory_detail_on_demand")
    );
    root.insert(
        QStringLiteral("captured_at_utc_ms"),
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()
    );
    root.insert(
        QStringLiteral("debug_cadence_mode"),
        cadence_mode_to_string(cadence_mode)
    );
    root.insert(
        QStringLiteral("process_memory_sample_interval_ms"),
        process_sample_interval_ms
    );
    root.insert(
        QStringLiteral("collector_latest_process_rss_bytes"),
        latest_process_rss_bytes
    );
    root.insert(
        QStringLiteral("auto_process_report_rss_growth_threshold_bytes"),
        auto_process_report_rss_growth_threshold_bytes
    );
    root.insert(
        QStringLiteral("auto_process_report_cooldown_ms"),
        auto_process_report_cooldown_ms
    );
    root.insert(
        QStringLiteral("auto_process_report_baseline_rss_bytes"),
        auto_process_report_baseline_rss_bytes
    );
    root.insert(
        QStringLiteral("auto_process_report_rss_growth_since_baseline_bytes"),
        auto_process_report_rss_growth_since_baseline_bytes
    );
    root.insert(
        QStringLiteral("auto_process_report_last_trigger_utc_ms"),
        auto_process_report_last_trigger_utc_ms
    );
    root.insert(
        QStringLiteral("auto_process_report_cooldown_remaining_ms"),
        auto_process_report_cooldown_remaining_ms
    );
    root.insert(
        QStringLiteral("auto_process_report_consecutive_growth_hits_required"),
        auto_process_report_consecutive_growth_hits_required
    );
    root.insert(
        QStringLiteral("auto_process_report_consecutive_growth_hits_current"),
        auto_process_report_consecutive_growth_hits_current
    );
    root.insert(
        QStringLiteral("collector_latest_process_rss_source"),
        latest_process_rss_source
    );
    root.insert(
        QStringLiteral("collector_latest_process_rss_available"),
        latest_process_rss_bytes >= 0
    );
    if (latest_process_rss_bytes < 0) {
        root.insert(
            QStringLiteral("collector_latest_process_rss_unavailable_reason"),
            latest_process_rss_unavailable_reason
        );
    }

    QJsonObject status_object;
    status_object.insert(QStringLiteral("VmRSS"), status_sample.vm_rss_bytes);
    status_object.insert(QStringLiteral("VmHWM"), status_sample.vm_hwm_bytes);
    status_object.insert(QStringLiteral("VmSize"), status_sample.vm_size_bytes);
    status_object.insert(QStringLiteral("VmSwap"), status_sample.vm_swap_bytes);
    root.insert(QStringLiteral("status_bytes"), status_object);
    root.insert(QStringLiteral("status_bytes_available"), has_status);
    if (!has_status) {
        root.insert(
            QStringLiteral("status_bytes_unavailable_reason"),
            process_memory_unavailable_reason()
        );
    }

    root.insert(QStringLiteral("smaps_rollup_bytes"), smaps_rollup);
    root.insert(
        QStringLiteral("smaps_rollup_bytes_available"), has_smaps_rollup
    );
    if (!has_smaps_rollup) {
        root.insert(
            QStringLiteral("smaps_rollup_bytes_unavailable_reason"),
            QStringLiteral("proc_smaps_rollup_unreadable_or_unsupported")
        );
    }

    QJsonObject semantics;
    semantics.insert(
        QStringLiteral("report_trigger"),
        process_memory_report_trigger_to_string(trigger)
    );
    semantics.insert(
        QStringLiteral("collector_latest_process_rss_bytes"),
        QStringLiteral("lightweight_sampled")
    );
    semantics.insert(
        QStringLiteral("collector_latest_process_rss_source"),
        QStringLiteral("source_label")
    );
    semantics.insert(
        QStringLiteral("status_bytes"), QStringLiteral("measured_proc_status")
    );
    semantics.insert(
        QStringLiteral("smaps_rollup_bytes"),
        QStringLiteral("measured_proc_smaps_rollup_on_demand")
    );
    semantics.insert(
        QStringLiteral("auto_process_report_rss_growth_threshold_bytes"),
        QStringLiteral("derived_from_debug_cadence_mode_unless_test_override")
    );
    semantics.insert(
        QStringLiteral("auto_process_report_cooldown_ms"),
        QStringLiteral("derived_from_debug_cadence_mode_unless_test_override")
    );
    semantics.insert(
        QStringLiteral("auto_process_report_baseline_rss_bytes"),
        QStringLiteral("collector_runtime_state")
    );
    semantics.insert(
        QStringLiteral("auto_process_report_rss_growth_since_baseline_bytes"),
        QStringLiteral("collector_runtime_state")
    );
    semantics.insert(
        QStringLiteral("auto_process_report_last_trigger_utc_ms"),
        QStringLiteral("collector_runtime_state")
    );
    semantics.insert(
        QStringLiteral("auto_process_report_cooldown_remaining_ms"),
        QStringLiteral("collector_runtime_state")
    );
    semantics.insert(
        QStringLiteral("auto_process_report_consecutive_growth_hits_required"),
        QStringLiteral("derived_from_debug_cadence_mode_unless_test_override")
    );
    semantics.insert(
        QStringLiteral("auto_process_report_consecutive_growth_hits_current"),
        QStringLiteral("collector_runtime_state")
    );
    root.insert(QStringLiteral("telemetry_semantics"), semantics);

    const QJsonDocument document(root);
    QFile file(output_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QStringLiteral("unable to open output file");
    }

    const qint64 written = file.write(document.toJson(QJsonDocument::Indented));
    if (written < 0) {
        return QStringLiteral("unable to write output file");
    }

    return QString();
}

} // namespace

resource_monitor::resource_monitor(QObject* parent, int max_timeline_entries)
    : QObject(parent)
    , observed_cache_service(nullptr)
    , observed_table_service(nullptr)
    , timeline_limit(std::max(1, max_timeline_entries))
    , collector_sequence(0)
    , cache_timeline_entries()
    , event_timeline_entries()
    , geometry_timeline_entries()
    , resize_history_entries()
    , pending_resize_transition_state(std::nullopt)
    , resize_history_log_stream_path()
    , last_export_metadata()
    , active_export_watcher(nullptr)
    , active_process_report_export_watcher(nullptr)
    , cadence_mode(debug_cadence_mode::realistic)
    , last_process_sample_ms(0)
    , current_process_rss_bytes(-1)
    , current_process_rss_source(process_memory_source_for_rss(-1))
    , current_process_rss_unavailable_reason(
          process_memory_unavailable_reason()
      )
    , auto_process_dump_rss_growth_threshold_bytes(96 * 1024 * 1024)
    , auto_process_dump_cooldown_ms(8 * 60 * 1000)
    , auto_process_dump_policy_override_for_tests(false)
    , auto_process_dump_consecutive_growth_hits_required_override(1)
    , auto_process_dump_last_trigger_ms(0)
    , auto_process_dump_baseline_rss_bytes(-1)
    , auto_process_dump_consecutive_growth_hits(0)
    , auto_process_dump_window_start_ms(0)
    , auto_process_dump_window_exports_used(0) {
    cache_timeline_entries.reserve(timeline_limit);
    event_timeline_entries.reserve(timeline_limit);
    geometry_timeline_entries.reserve(timeline_limit);
    resize_history_entries.reserve(timeline_limit);
}

void resource_monitor::attach_cache_service(raster_cache* cache_service) {
    if (observed_cache_service == cache_service) {
        return;
    }

    if (observed_cache_service != nullptr) {
        QObject::disconnect(
            observed_cache_service, &raster_cache::debug_snapshot_updated, this,
            &resource_monitor::on_cache_snapshot_updated
        );
    }

    observed_cache_service = cache_service;
    cache_timeline_entries.clear();
    event_timeline_entries.clear();
    pending_resize_transition_state.reset();
    auto_process_dump_baseline_rss_bytes = -1;
    auto_process_dump_last_trigger_ms = 0;
    auto_process_dump_consecutive_growth_hits = 0;
    auto_process_dump_window_start_ms = 0;
    auto_process_dump_window_exports_used = 0;

    if (observed_cache_service == nullptr) {
        return;
    }

    QObject::connect(
        observed_cache_service, &raster_cache::debug_snapshot_updated, this,
        &resource_monitor::on_cache_snapshot_updated
    );

    on_cache_snapshot_updated(observed_cache_service->get_debug_snapshot());
}

void resource_monitor::attach_table_service(QObject* table_service) {
    if (observed_table_service == table_service) {
        return;
    }

    table* previous_table = qobject_cast<table*>(observed_table_service);
    if (previous_table != nullptr) {
        QObject::disconnect(
            previous_table, &table::debug_geometry_snapshot_updated, this,
            &resource_monitor::on_geometry_debug_snapshot_updated
        );
        QObject::disconnect(
            previous_table, &table::debug_resize_transition_recorded, this,
            &resource_monitor::on_resize_transition_recorded
        );
    }

    observed_table_service = table_service;
    geometry_timeline_entries.clear();
    resize_history_entries.clear();
    pending_resize_transition_state.reset();
    resize_history_log_stream_path.clear();

    table* observed_table = qobject_cast<table*>(observed_table_service);
    if (observed_table == nullptr) {
        return;
    }

    QObject::connect(
        observed_table, &table::debug_geometry_snapshot_updated, this,
        &resource_monitor::on_geometry_debug_snapshot_updated
    );
    QObject::connect(
        observed_table, &table::debug_resize_transition_recorded, this,
        &resource_monitor::on_resize_transition_recorded
    );

    on_geometry_debug_snapshot_updated(
        observed_table->current_geometry_debug_snapshot()
    );
}

int resource_monitor::max_timeline_entries() const { return timeline_limit; }

int resource_monitor::timeline_size() const {
    return size_to_int(cache_timeline_entries.size());
}

int resource_monitor::event_timeline_size() const {
    return size_to_int(event_timeline_entries.size());
}

int resource_monitor::geometry_timeline_size() const {
    return size_to_int(geometry_timeline_entries.size());
}

int resource_monitor::resize_history_size() const {
    return size_to_int(resize_history_entries.size());
}

bool resource_monitor::has_cache_snapshot() const {
    return !cache_timeline_entries.isEmpty();
}

bool resource_monitor::has_geometry_snapshot() const {
    return !geometry_timeline_entries.isEmpty();
}

resource_monitor::cache_timeline_entry
resource_monitor::latest_cache_snapshot() const {
    if (cache_timeline_entries.isEmpty()) {
        return cache_timeline_entry();
    }

    return cache_timeline_entries.constLast();
}

geometry_debug_snapshot resource_monitor::latest_geometry_snapshot() const {
    if (geometry_timeline_entries.isEmpty()) {
        return geometry_debug_snapshot();
    }

    return geometry_timeline_entries.constLast();
}

QVector<resource_monitor::cache_timeline_entry>
resource_monitor::cache_timeline() const {
    return cache_timeline_entries;
}

QVector<resource_monitor::event_timeline_entry>
resource_monitor::event_timeline() const {
    return event_timeline_entries;
}

QVector<geometry_debug_snapshot> resource_monitor::geometry_timeline() const {
    return geometry_timeline_entries;
}

QVector<resource_monitor::resize_history_entry>
resource_monitor::resize_history() const {
    return resize_history_entries;
}

QString resource_monitor::resize_history_log_path() const {
    return resize_history_log_stream_path;
}

void resource_monitor::add_manual_marker(const QString& label) {
    push_event_entry(event_timeline_entry::event_kind::manual_marker, label);
}

void resource_monitor::set_debug_cadence_mode(debug_cadence_mode mode) {
    cadence_mode = mode;
    last_process_sample_ms = 0;
    auto_process_dump_consecutive_growth_hits = 0;
    auto_process_dump_window_start_ms = 0;
    auto_process_dump_window_exports_used = 0;
    refresh_process_memory_sample_if_needed();
    maybe_trigger_auto_process_report(0);
}

resource_monitor::debug_cadence_mode
resource_monitor::get_debug_cadence_mode() const {
    return cadence_mode;
}

resource_monitor::export_request_metadata
resource_monitor::latest_export_metadata() const {
    return last_export_metadata;
}

resource_monitor::auto_process_report_runtime_state
resource_monitor::current_auto_process_report_runtime_state() const {
    return auto_process_report_runtime_state {
        .rss_growth_threshold_bytes
        = auto_process_dump_rss_growth_threshold_bytes_effective(),
        .cooldown_ms = auto_process_dump_cooldown_ms_effective(),
        .baseline_rss_bytes = auto_process_dump_baseline_rss_bytes,
        .rss_growth_since_baseline_bytes
        = auto_process_dump_rss_growth_since_baseline_bytes(),
        .last_trigger_utc_ms = auto_process_dump_last_trigger_ms,
        .cooldown_remaining_ms = auto_process_dump_cooldown_remaining_ms(
            QDateTime::currentMSecsSinceEpoch()
        ),
        .consecutive_growth_hits_required
        = auto_process_dump_consecutive_growth_hits_required(),
        .consecutive_growth_hits_current
        = auto_process_dump_consecutive_growth_hits,
        .window_ms = auto_process_dump_window_ms_effective(),
        .window_max_exports = auto_process_dump_window_max_exports_effective(),
        .window_exports_used = auto_process_dump_window_exports_used,
    };
}

qint64 resource_monitor::latest_process_rss_bytes() const {
    return current_process_rss_bytes;
}

qint64 resource_monitor::process_memory_sample_interval_ms() const {
    return process_sample_interval_ms_for_mode();
}

void resource_monitor::set_auto_process_report_policy_for_tests(
    qint64 rss_growth_threshold_bytes, qint64 cooldown_ms,
    qint64 consecutive_growth_hits_required
) {
    auto_process_dump_policy_override_for_tests = true;
    auto_process_dump_rss_growth_threshold_bytes
        = std::max<qint64>(0, rss_growth_threshold_bytes);
    auto_process_dump_cooldown_ms = std::max<qint64>(0, cooldown_ms);
    auto_process_dump_consecutive_growth_hits_required_override
        = std::max<qint64>(1, consecutive_growth_hits_required);
    last_process_sample_ms = 0;
    refresh_process_memory_sample_if_needed();
    auto_process_dump_baseline_rss_bytes = current_process_rss_bytes;
    auto_process_dump_consecutive_growth_hits = 0;
    auto_process_dump_window_start_ms = 0;
    auto_process_dump_window_exports_used = 0;
}

void resource_monitor::export_debug_snapshot_async(const QString& output_path) {
    const export_request_metadata metadata {
        .collector_sequence = collector_sequence,
        .cache_timeline_size = size_to_int(cache_timeline_entries.size()),
        .event_timeline_size = size_to_int(event_timeline_entries.size()),
        .geometry_timeline_size = size_to_int(geometry_timeline_entries.size()),
        .resize_history_size = size_to_int(resize_history_entries.size()),
        .resize_history_log_path = resize_history_log_stream_path,
        .latest_process_rss_bytes = current_process_rss_bytes,
        .process_memory_source = current_process_rss_source,
        .process_memory_unavailable_reason
        = current_process_rss_unavailable_reason,
        .process_memory_sample_interval_ms
        = process_sample_interval_ms_for_mode(),
        .auto_process_report_rss_growth_threshold_bytes
        = auto_process_dump_rss_growth_threshold_bytes_effective(),
        .auto_process_report_cooldown_ms
        = auto_process_dump_cooldown_ms_effective(),
        .auto_process_report_baseline_rss_bytes
        = auto_process_dump_baseline_rss_bytes,
        .auto_process_report_rss_growth_since_baseline_bytes
        = auto_process_dump_rss_growth_since_baseline_bytes(),
        .auto_process_report_last_trigger_utc_ms
        = auto_process_dump_last_trigger_ms,
        .auto_process_report_cooldown_remaining_ms
        = auto_process_dump_cooldown_remaining_ms(
            QDateTime::currentMSecsSinceEpoch()
        ),
        .auto_process_report_consecutive_growth_hits_required
        = auto_process_dump_consecutive_growth_hits_required(),
        .auto_process_report_consecutive_growth_hits_current
        = auto_process_dump_consecutive_growth_hits,
        .auto_process_report_window_ms
        = auto_process_dump_window_ms_effective(),
        .auto_process_report_window_max_exports
        = auto_process_dump_window_max_exports_effective(),
        .auto_process_report_window_exports_used
        = auto_process_dump_window_exports_used,
    };
    last_export_metadata = metadata;

    const QVector<cache_timeline_entry> cache_entries = cache_timeline_entries;
    const QVector<event_timeline_entry> event_entries = event_timeline_entries;
    const QVector<geometry_debug_snapshot> geometry_entries
        = geometry_timeline_entries;
    const QVector<resize_history_entry> resize_entries = resize_history_entries;

    if (active_export_watcher != nullptr) {
        active_export_watcher->deleteLater();
        active_export_watcher = nullptr;
    }

    active_export_watcher = new QFutureWatcher<QString>(this);
    QObject::connect(
        active_export_watcher, &QFutureWatcher<QString>::finished, this,
        [this, output_path]() {
            if (active_export_watcher == nullptr) {
                emit snapshot_export_finished(
                    output_path, false, QStringLiteral("export watcher lost")
                );
                return;
            }

            const QString error_message
                = active_export_watcher->future().result();
            const bool success = error_message.isEmpty();

            active_export_watcher->deleteLater();
            active_export_watcher = nullptr;
            emit snapshot_export_finished(output_path, success, error_message);
        }
    );

    const debug_cadence_mode export_mode = cadence_mode;
    active_export_watcher->setFuture(
        QtConcurrent::run([output_path, metadata, cache_entries, event_entries,
                           geometry_entries, resize_entries, export_mode]() {
            return write_debug_snapshot_json(
                output_path, metadata, cache_entries, event_entries,
                geometry_entries, resize_entries, export_mode
            );
        })
    );
}

void resource_monitor::export_process_memory_report_async(
    const QString& output_path
) {
    export_process_memory_report_async(
        output_path, process_memory_report_trigger::manual_on_demand
    );
}

void resource_monitor::export_process_memory_report_async(
    const QString& output_path, process_memory_report_trigger trigger
) {
    if (active_process_report_export_watcher != nullptr) {
        active_process_report_export_watcher->deleteLater();
        active_process_report_export_watcher = nullptr;
    }

    active_process_report_export_watcher = new QFutureWatcher<QString>(this);
    QObject::connect(
        active_process_report_export_watcher,
        &QFutureWatcher<QString>::finished, this, [this, output_path]() {
            if (active_process_report_export_watcher == nullptr) {
                emit process_memory_report_export_finished(
                    output_path, false,
                    QStringLiteral("process-report export watcher lost")
                );
                return;
            }

            const QString error_message
                = active_process_report_export_watcher->future().result();
            const bool success = error_message.isEmpty();

            active_process_report_export_watcher->deleteLater();
            active_process_report_export_watcher = nullptr;
            emit process_memory_report_export_finished(
                output_path, success, error_message
            );
        }
    );

    const debug_cadence_mode export_mode = cadence_mode;
    const qint64 sample_interval_ms = process_sample_interval_ms_for_mode();
    const qint64 auto_rss_growth_threshold_bytes
        = auto_process_dump_rss_growth_threshold_bytes_effective();
    const qint64 auto_cooldown_ms = auto_process_dump_cooldown_ms_effective();
    const qint64 auto_baseline_rss_bytes = auto_process_dump_baseline_rss_bytes;
    const qint64 auto_rss_growth_since_baseline_bytes
        = auto_process_dump_rss_growth_since_baseline_bytes();
    const qint64 auto_last_trigger_utc_ms = auto_process_dump_last_trigger_ms;
    const qint64 auto_cooldown_remaining_ms
        = auto_process_dump_cooldown_remaining_ms(
            QDateTime::currentMSecsSinceEpoch()
        );
    const qint64 auto_consecutive_growth_hits_required
        = auto_process_dump_consecutive_growth_hits_required();
    const qint64 auto_consecutive_growth_hits_current
        = auto_process_dump_consecutive_growth_hits;
    const qint64 latest_rss = current_process_rss_bytes;
    const QString latest_rss_source = current_process_rss_source;
    const QString latest_rss_unavailable_reason
        = current_process_rss_unavailable_reason;
    active_process_report_export_watcher->setFuture(
        QtConcurrent::run([output_path, export_mode, sample_interval_ms,
                           auto_rss_growth_threshold_bytes, auto_cooldown_ms,
                           auto_baseline_rss_bytes,
                           auto_rss_growth_since_baseline_bytes,
                           auto_last_trigger_utc_ms, auto_cooldown_remaining_ms,
                           auto_consecutive_growth_hits_required,
                           auto_consecutive_growth_hits_current, latest_rss,
                           latest_rss_source, latest_rss_unavailable_reason,
                           trigger]() {
            return write_process_memory_report_json(
                output_path, export_mode, sample_interval_ms,
                auto_rss_growth_threshold_bytes, auto_cooldown_ms,
                auto_baseline_rss_bytes, auto_rss_growth_since_baseline_bytes,
                auto_last_trigger_utc_ms, auto_cooldown_remaining_ms,
                auto_consecutive_growth_hits_required,
                auto_consecutive_growth_hits_current, latest_rss,
                latest_rss_source, latest_rss_unavailable_reason, trigger
            );
        })
    );
}

bool resource_monitor::export_debug_snapshot_sync(
    const QString& output_path, QString* error_message
) const {
    const export_request_metadata metadata {
        .collector_sequence = collector_sequence,
        .cache_timeline_size = size_to_int(cache_timeline_entries.size()),
        .event_timeline_size = size_to_int(event_timeline_entries.size()),
        .geometry_timeline_size = size_to_int(geometry_timeline_entries.size()),
        .resize_history_size = size_to_int(resize_history_entries.size()),
        .resize_history_log_path = resize_history_log_stream_path,
        .latest_process_rss_bytes = current_process_rss_bytes,
        .process_memory_source = current_process_rss_source,
        .process_memory_unavailable_reason
        = current_process_rss_unavailable_reason,
        .process_memory_sample_interval_ms
        = process_sample_interval_ms_for_mode(),
        .auto_process_report_rss_growth_threshold_bytes
        = auto_process_dump_rss_growth_threshold_bytes_effective(),
        .auto_process_report_cooldown_ms
        = auto_process_dump_cooldown_ms_effective(),
        .auto_process_report_baseline_rss_bytes
        = auto_process_dump_baseline_rss_bytes,
        .auto_process_report_rss_growth_since_baseline_bytes
        = auto_process_dump_rss_growth_since_baseline_bytes(),
        .auto_process_report_last_trigger_utc_ms
        = auto_process_dump_last_trigger_ms,
        .auto_process_report_cooldown_remaining_ms
        = auto_process_dump_cooldown_remaining_ms(
            QDateTime::currentMSecsSinceEpoch()
        ),
        .auto_process_report_consecutive_growth_hits_required
        = auto_process_dump_consecutive_growth_hits_required(),
        .auto_process_report_consecutive_growth_hits_current
        = auto_process_dump_consecutive_growth_hits,
        .auto_process_report_window_ms
        = auto_process_dump_window_ms_effective(),
        .auto_process_report_window_max_exports
        = auto_process_dump_window_max_exports_effective(),
        .auto_process_report_window_exports_used
        = auto_process_dump_window_exports_used,
    };

    const QString error = write_debug_snapshot_json(
        output_path, metadata, cache_timeline_entries, event_timeline_entries,
        geometry_timeline_entries, resize_history_entries, cadence_mode
    );
    if (error_message != nullptr) {
        *error_message = error;
    }
    return error.isEmpty();
}

bool resource_monitor::export_process_memory_report_sync(
    const QString& output_path, QString* error_message
) const {
    const QString error = write_process_memory_report_json(
        output_path, cadence_mode, process_sample_interval_ms_for_mode(),
        auto_process_dump_rss_growth_threshold_bytes_effective(),
        auto_process_dump_cooldown_ms_effective(),
        auto_process_dump_baseline_rss_bytes,
        auto_process_dump_rss_growth_since_baseline_bytes(),
        auto_process_dump_last_trigger_ms,
        auto_process_dump_cooldown_remaining_ms(
            QDateTime::currentMSecsSinceEpoch()
        ),
        auto_process_dump_consecutive_growth_hits_required(),
        auto_process_dump_consecutive_growth_hits, current_process_rss_bytes,
        current_process_rss_source, current_process_rss_unavailable_reason,
        process_memory_report_trigger::manual_on_demand
    );
    if (error_message != nullptr) {
        *error_message = error;
    }
    return error.isEmpty();
}

void resource_monitor::on_cache_snapshot_updated(
    const raster_cache::debug_snapshot& snapshot
) {
    if (auto_process_dump_policy_override_for_tests) {
        last_process_sample_ms = 0;
    }
    refresh_process_memory_sample_if_needed();

    const cache_timeline_entry previous_entry = cache_timeline_entries.isEmpty()
        ? cache_timeline_entry()
        : cache_timeline_entries.constLast();

    const qint64 cache_ready_bytes_delta = cache_timeline_entries.isEmpty()
        ? 0
        : snapshot.ready_bytes - previous_entry.cache_snapshot.ready_bytes;
    maybe_trigger_auto_process_report(cache_ready_bytes_delta);
    const qint64 widget_local_display_delta = cache_timeline_entries.isEmpty()
        ? 0
        : snapshot.widget_local_display_bytes_estimated
            - previous_entry.cache_snapshot
                  .widget_local_display_bytes_estimated;
    const qint64 widget_local_display_bytes_materialized_interval
        = std::max<qint64>(0, widget_local_display_delta);
    const qint64 widget_local_display_bytes_released_interval
        = std::max<qint64>(0, -widget_local_display_delta);

    qint64 process_rss_bytes_delta = 0;
    if (!cache_timeline_entries.isEmpty()
        && previous_entry.process_rss_bytes >= 0
        && current_process_rss_bytes >= 0) {
        process_rss_bytes_delta
            = current_process_rss_bytes - previous_entry.process_rss_bytes;
    }

    const cache_timeline_entry entry {
        .collector_sequence = ++collector_sequence,
        .cache_snapshot = snapshot,
        .cache_entries_added_interval = snapshot.interval_deltas.entries_added,
        .cache_entries_removed_interval
        = snapshot.interval_deltas.entries_removed,
        .cache_bytes_added_interval = snapshot.interval_deltas.bytes_added,
        .cache_bytes_removed_interval = snapshot.interval_deltas.bytes_removed,
        .cache_images_added_interval = snapshot.interval_deltas.images_added,
        .cache_images_removed_interval
        = snapshot.interval_deltas.images_removed,
        .cache_accounted_ready_bytes_delta = cache_ready_bytes_delta,
        .widget_local_display_bytes_estimated_delta
        = widget_local_display_delta,
        .widget_local_display_bytes_materialized_interval
        = widget_local_display_bytes_materialized_interval,
        .widget_local_display_bytes_released_interval
        = widget_local_display_bytes_released_interval,
        .process_rss_bytes = current_process_rss_bytes,
        .process_rss_bytes_delta = process_rss_bytes_delta,
        .process_rss_bytes_growth_interval
        = std::max<qint64>(0, process_rss_bytes_delta),
        .process_rss_bytes_drop_interval
        = std::max<qint64>(0, -process_rss_bytes_delta),
    };

    cache_timeline_entries.push_back(entry);
    while (cache_timeline_entries.size() > timeline_limit) {
        cache_timeline_entries.removeFirst();
    }

    push_event_entry(
        event_timeline_entry::event_kind::cache_snapshot, QString()
    );

    if (!geometry_timeline_entries.isEmpty()) {
        maybe_finalize_pending_resize_transition(
            geometry_timeline_entries.constLast()
        );
    }

    emit cache_snapshot_collected(entry);
}

void resource_monitor::on_geometry_debug_snapshot_updated(
    const geometry_debug_snapshot& snapshot
) {
    geometry_timeline_entries.push_back(snapshot);
    while (geometry_timeline_entries.size() > timeline_limit) {
        geometry_timeline_entries.removeFirst();
    }

    maybe_finalize_pending_resize_transition(snapshot);
    emit geometry_snapshot_collected(snapshot);
}

void resource_monitor::on_resize_transition_recorded(
    const resize_transition_debug_event& event
) {
    if (pending_resize_transition_state.has_value()) {
        finalize_pending_resize_transition(event.timestamp_ms, -1);
    }

    const cache_timeline_entry latest_cache = has_cache_snapshot()
        ? latest_cache_snapshot()
        : cache_timeline_entry();
    const qint64 before_cache_accounted_ready_bytes
        = latest_cache.cache_snapshot.ready_bytes;
    const qint64 before_widget_local_display_bytes_estimated
        = latest_cache.cache_snapshot.widget_local_display_bytes_estimated;
    const qint64 before_process_rss_bytes = current_process_rss_bytes;
    const qint64 before_measured_accounted_gap_bytes
        = before_process_rss_bytes >= 0
        ? before_process_rss_bytes - before_cache_accounted_ready_bytes
        : 0;

    pending_resize_transition_state = pending_resize_transition {
        .transition = event,
        .before_process_rss_bytes = before_process_rss_bytes,
        .before_cache_accounted_ready_bytes
        = before_cache_accounted_ready_bytes,
        .before_widget_local_display_bytes_estimated
        = before_widget_local_display_bytes_estimated,
        .before_measured_accounted_gap_bytes
        = before_measured_accounted_gap_bytes,
    };

    maybe_finalize_pending_resize_transition(event.geometry_after_resize);
}

void resource_monitor::maybe_finalize_pending_resize_transition(
    const geometry_debug_snapshot& snapshot
) {
    if (!pending_resize_transition_state.has_value()) {
        return;
    }

    const pending_resize_transition& pending = *pending_resize_transition_state;
    const bool window_size_applied
        = !pending.transition.new_window_size.isValid()
        || snapshot.window_size == pending.transition.new_window_size;
    const bool active_bucket_applied
        = pending.transition.new_active_bucket_px <= 0
        || snapshot.active_bucket_px == pending.transition.new_active_bucket_px;
    const bool no_prewarm_expected
        = pending.transition.new_warming_bucket_px <= 0
        && pending.transition.new_active_bucket_px
            == pending.transition.old_active_bucket_px;
    const bool prewarm_drained = snapshot.warming_generation_id <= 0
        && snapshot.warming_bucket_px <= 0;

    if (!window_size_applied || !active_bucket_applied
        || (!no_prewarm_expected && !prewarm_drained)) {
        return;
    }

    const qint64 transition_end_timestamp_ms = snapshot.timestamp_ms > 0
        ? snapshot.timestamp_ms
        : QDateTime::currentMSecsSinceEpoch();
    const qint64 prewarm_completion_ms = no_prewarm_expected
        ? 0
        : std::max<qint64>(
              0, transition_end_timestamp_ms - pending.transition.timestamp_ms
          );
    finalize_pending_resize_transition(
        transition_end_timestamp_ms, prewarm_completion_ms
    );
}

void resource_monitor::finalize_pending_resize_transition(
    qint64 transition_end_timestamp_ms, qint64 prewarm_completion_ms
) {
    if (!pending_resize_transition_state.has_value()) {
        return;
    }

    const pending_resize_transition pending = *pending_resize_transition_state;
    pending_resize_transition_state.reset();

    const cache_timeline_entry latest_cache = has_cache_snapshot()
        ? latest_cache_snapshot()
        : cache_timeline_entry();
    const qint64 after_cache_accounted_ready_bytes
        = latest_cache.cache_snapshot.ready_bytes;
    const qint64 after_widget_local_display_bytes_estimated
        = latest_cache.cache_snapshot.widget_local_display_bytes_estimated;
    const qint64 after_process_rss_bytes = current_process_rss_bytes;
    const qint64 after_measured_accounted_gap_bytes
        = after_process_rss_bytes >= 0
        ? after_process_rss_bytes - after_cache_accounted_ready_bytes
        : 0;

    const qint64 normalized_transition_end = transition_end_timestamp_ms > 0
        ? transition_end_timestamp_ms
        : QDateTime::currentMSecsSinceEpoch();

    const resize_history_entry entry {
        .collector_sequence = collector_sequence,
        .transition_start_timestamp_ms = pending.transition.timestamp_ms,
        .transition_end_timestamp_ms = normalized_transition_end,
        .prewarm_completion_ms = prewarm_completion_ms,
        .old_window_size = pending.transition.old_window_size,
        .new_window_size = pending.transition.new_window_size,
        .old_active_bucket_px = pending.transition.old_active_bucket_px,
        .new_active_bucket_px = pending.transition.new_active_bucket_px,
        .old_warming_bucket_px = pending.transition.old_warming_bucket_px,
        .new_warming_bucket_px = pending.transition.new_warming_bucket_px,
        .geometry_after_resize = pending.transition.geometry_after_resize,
        .before_process_rss_bytes = pending.before_process_rss_bytes,
        .after_process_rss_bytes = after_process_rss_bytes,
        .before_cache_accounted_ready_bytes
        = pending.before_cache_accounted_ready_bytes,
        .after_cache_accounted_ready_bytes = after_cache_accounted_ready_bytes,
        .before_widget_local_display_bytes_estimated
        = pending.before_widget_local_display_bytes_estimated,
        .after_widget_local_display_bytes_estimated
        = after_widget_local_display_bytes_estimated,
        .before_measured_accounted_gap_bytes
        = pending.before_measured_accounted_gap_bytes,
        .after_measured_accounted_gap_bytes
        = after_measured_accounted_gap_bytes,
    };

    resize_history_entries.push_back(entry);
    while (resize_history_entries.size() > timeline_limit) {
        resize_history_entries.removeFirst();
    }

    append_resize_history_entry_async(entry);
    push_event_entry(
        event_timeline_entry::event_kind::manual_marker,
        QStringLiteral("resize_transition_recorded")
    );
    emit resize_history_recorded(entry);
}

void resource_monitor::append_resize_history_entry_async(
    const resize_history_entry& entry
) {
    initialize_resize_history_log_stream_path_if_needed();
    if (resize_history_log_stream_path.isEmpty()) {
        return;
    }

    const QString output_path = resize_history_log_stream_path;
    const QByteArray line
        = (resize_history_entry_to_jsonl_line(entry) + QLatin1Char('\n'))
              .toUtf8();
    QThreadPool::globalInstance()->start([output_path, line]() {
        QFile file(output_path);
        if (!file.open(
                QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text
            )) {
            return;
        }
        file.write(line);
        file.flush();
    });
}

void resource_monitor::initialize_resize_history_log_stream_path_if_needed() {
    if (!resize_history_log_stream_path.isEmpty()) {
        return;
    }

    const QString app_data_dir
        = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString temp_location_dir
        = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString temp_path_dir = QDir::tempPath();

    QStringList candidate_base_dirs;
    if (!app_data_dir.isEmpty()) {
        candidate_base_dirs.push_back(app_data_dir);
    }
    if (!temp_location_dir.isEmpty()
        && !candidate_base_dirs.contains(temp_location_dir)) {
        candidate_base_dirs.push_back(temp_location_dir);
    }
    if (!temp_path_dir.isEmpty()
        && !candidate_base_dirs.contains(temp_path_dir)) {
        candidate_base_dirs.push_back(temp_path_dir);
    }

    for (const QString& candidate_dir : candidate_base_dirs) {
        QDir dir(candidate_dir);
        if (!dir.mkpath(QStringLiteral("monitor_resize_history"))) {
            continue;
        }

        const QString timestamp = QDateTime::currentDateTimeUtc().toString(
            QStringLiteral("yyyyMMdd_hhmmss")
        );
        resize_history_log_stream_path = dir.filePath(
            QStringLiteral("monitor_resize_history/resize_history_%1.jsonl")
                .arg(timestamp)
        );
        return;
    }
}

void resource_monitor::push_event_entry(
    event_timeline_entry::event_kind kind, const QString& label
) {
    const event_timeline_entry entry {
        .collector_sequence = collector_sequence,
        .kind = kind,
        .timestamp_ms = QDateTime::currentMSecsSinceEpoch(),
        .label = label,
    };

    event_timeline_entries.push_back(entry);
    while (event_timeline_entries.size() > timeline_limit) {
        event_timeline_entries.removeFirst();
    }

    emit event_recorded(entry);
}

QString resource_monitor::cadence_mode_to_string(debug_cadence_mode mode) {
    return ::cadence_mode_to_string(mode);
}

void resource_monitor::maybe_trigger_auto_process_report(
    qint64 cache_accounted_ready_bytes_delta_hint
) {
    const bool has_measured_rss = current_process_rss_bytes >= 0;
    qint64 effective_growth_bytes = -1;
    if (has_measured_rss) {
        if (auto_process_dump_baseline_rss_bytes < 0) {
            auto_process_dump_baseline_rss_bytes = current_process_rss_bytes;
        } else if (current_process_rss_bytes
                   < auto_process_dump_baseline_rss_bytes) {
            auto_process_dump_baseline_rss_bytes = current_process_rss_bytes;
        }

        effective_growth_bytes = std::max<qint64>(
            0, current_process_rss_bytes - auto_process_dump_baseline_rss_bytes
        );
    } else {
        auto_process_dump_baseline_rss_bytes = -1;
    }

    if (auto_process_dump_policy_override_for_tests
        && cache_accounted_ready_bytes_delta_hint > 0) {
        effective_growth_bytes = std::max(
            effective_growth_bytes, cache_accounted_ready_bytes_delta_hint
        );
    }

    if (effective_growth_bytes
        < auto_process_dump_rss_growth_threshold_bytes_effective()) {
        auto_process_dump_consecutive_growth_hits = 0;
        return;
    }

    ++auto_process_dump_consecutive_growth_hits;
    if (auto_process_dump_consecutive_growth_hits
        < auto_process_dump_consecutive_growth_hits_required()) {
        return;
    }

    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    if (auto_process_dump_last_trigger_ms > 0
        && (now_ms - auto_process_dump_last_trigger_ms)
            < auto_process_dump_cooldown_ms_effective()) {
        auto_process_dump_consecutive_growth_hits = 0;
        return;
    }

    const qint64 export_window_ms = auto_process_dump_window_ms_effective();
    if (auto_process_dump_window_start_ms <= 0
        || (now_ms - auto_process_dump_window_start_ms) >= export_window_ms) {
        auto_process_dump_window_start_ms = now_ms;
        auto_process_dump_window_exports_used = 0;
    }

    if (auto_process_dump_window_exports_used
        >= auto_process_dump_window_max_exports_effective()) {
        auto_process_dump_consecutive_growth_hits = 0;
        return;
    }

    if (active_process_report_export_watcher != nullptr) {
        return;
    }

    const QString app_data_dir
        = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString temp_location_dir
        = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString temp_path_dir = QDir::tempPath();

    QString output_base_dir;
    QStringList candidate_base_dirs;
    if (!app_data_dir.isEmpty()) {
        candidate_base_dirs.push_back(app_data_dir);
    }
    if (!temp_location_dir.isEmpty()
        && !candidate_base_dirs.contains(temp_location_dir)) {
        candidate_base_dirs.push_back(temp_location_dir);
    }
    if (!temp_path_dir.isEmpty()
        && !candidate_base_dirs.contains(temp_path_dir)) {
        candidate_base_dirs.push_back(temp_path_dir);
    }

    for (const QString& candidate_dir : candidate_base_dirs) {
        QDir dir(candidate_dir);
        if (!dir.mkpath(QStringLiteral("monitor_auto_dumps"))) {
            continue;
        }
        output_base_dir = candidate_dir;
        break;
    }

    if (output_base_dir.isEmpty()) {
        auto_process_dump_consecutive_growth_hits = 0;
        return;
    }

    QDir dir(output_base_dir);
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd_hhmmss_zzz")
    );
    const QString output_path = dir.filePath(
        QStringLiteral("monitor_auto_dumps/process_memory_auto_%1.json")
            .arg(timestamp)
    );

    auto_process_dump_last_trigger_ms = now_ms;
    if (has_measured_rss) {
        auto_process_dump_baseline_rss_bytes = current_process_rss_bytes;
    }
    auto_process_dump_consecutive_growth_hits = 0;
    ++auto_process_dump_window_exports_used;
    push_event_entry(
        event_timeline_entry::event_kind::manual_marker,
        QStringLiteral("auto_export_process_memory_detail")
    );
    export_process_memory_report_async(
        output_path, process_memory_report_trigger::threshold_rss_growth
    );
}

void resource_monitor::refresh_process_memory_sample_if_needed() {
    const qint64 process_sample_interval_ms
        = process_sample_interval_ms_for_mode();
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    if (last_process_sample_ms > 0
        && (now_ms - last_process_sample_ms) < process_sample_interval_ms) {
        return;
    }

    last_process_sample_ms = now_ms;

    const process_memory_sample_result sample = []() {
        process_memory_sample_result result;
        QFile status_file(QStringLiteral("/proc/self/status"));
        if (!status_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            result.unavailable_reason = process_memory_unavailable_reason();
            return result;
        }

        QTextStream stream(&status_file);
        while (!stream.atEnd()) {
            const QString line = stream.readLine();
            if (!line.startsWith(QStringLiteral("VmRSS:"))) {
                continue;
            }

            const qint64 rss_bytes
                = parse_status_kb_line(line, QStringLiteral("VmRSS:"));
            if (rss_bytes < 0) {
                result.unavailable_reason = process_memory_unavailable_reason();
                return result;
            }

            result.rss_bytes = rss_bytes;
            result.source = QStringLiteral("proc_status_vm_rss");
            return result;
        }

        result.unavailable_reason = process_memory_unavailable_reason();
        return result;
    }();

    if (sample.rss_bytes < 0) {
        current_process_rss_bytes = -1;
        current_process_rss_source = process_memory_source_for_rss(-1);
        current_process_rss_unavailable_reason = sample.unavailable_reason;
        return;
    }

    current_process_rss_bytes = sample.rss_bytes;
    current_process_rss_source = sample.source;
    current_process_rss_unavailable_reason.clear();
}

qint64 resource_monitor::process_sample_interval_ms_for_mode() const {
    switch (cadence_mode) {
    case debug_cadence_mode::instrumented:
        return 1000;
    case debug_cadence_mode::realistic:
    default:
        return 5000;
    }
}

qint64
resource_monitor::auto_process_dump_rss_growth_since_baseline_bytes() const {
    if (auto_process_dump_baseline_rss_bytes < 0
        || current_process_rss_bytes < 0) {
        return 0;
    }

    return std::max<qint64>(
        0, current_process_rss_bytes - auto_process_dump_baseline_rss_bytes
    );
}

qint64
resource_monitor::auto_process_dump_cooldown_remaining_ms(qint64 now_ms) const {
    if (auto_process_dump_last_trigger_ms <= 0) {
        return 0;
    }

    const qint64 elapsed_ms
        = std::max<qint64>(0, now_ms - auto_process_dump_last_trigger_ms);
    const qint64 cooldown_ms = auto_process_dump_cooldown_ms_effective();
    return std::max<qint64>(0, cooldown_ms - elapsed_ms);
}

qint64
resource_monitor::auto_process_dump_consecutive_growth_hits_required() const {
    if (auto_process_dump_policy_override_for_tests) {
        return auto_process_dump_consecutive_growth_hits_required_override;
    }

    switch (cadence_mode) {
    case debug_cadence_mode::instrumented:
        return 4;
    case debug_cadence_mode::realistic:
    default:
        return 3;
    }
}

qint64 resource_monitor::auto_process_dump_window_ms_effective() const {
    return 60 * 60 * 1000;
}

qint64
resource_monitor::auto_process_dump_window_max_exports_effective() const {
    switch (cadence_mode) {
    case debug_cadence_mode::instrumented:
        return 4;
    case debug_cadence_mode::realistic:
    default:
        return 2;
    }
}

qint64 resource_monitor::
    auto_process_dump_rss_growth_threshold_bytes_effective() const {
    if (auto_process_dump_policy_override_for_tests) {
        return auto_process_dump_rss_growth_threshold_bytes;
    }

    switch (cadence_mode) {
    case debug_cadence_mode::instrumented:
        return 192 * 1024 * 1024;
    case debug_cadence_mode::realistic:
    default:
        return 96 * 1024 * 1024;
    }
}

qint64 resource_monitor::auto_process_dump_cooldown_ms_effective() const {
    if (auto_process_dump_policy_override_for_tests) {
        return auto_process_dump_cooldown_ms;
    }

    switch (cadence_mode) {
    case debug_cadence_mode::instrumented:
        return 12 * 60 * 1000;
    case debug_cadence_mode::realistic:
    default:
        return 8 * 60 * 1000;
    }
}
