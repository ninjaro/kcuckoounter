#include "monitor/debug_probe_core.hpp"
#include "monitor/debug_probe_json_helpers.hpp"
#include "monitor/raster_cache_debug_strings.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

QJsonObject debug_probe_core::metric_catalog_entry(
    const QString& id, const QString& kind, const QString& provenance,
    const QString& unit, const QString& scope,
    const QString& cardinality_semantics, const QString& stability,
    const QString& additive_semantics, const QString& confidence,
    const QString& default_display_role, const QString& domain_namespace
) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("kind"), kind);
    object.insert(QStringLiteral("provenance"), provenance);
    object.insert(QStringLiteral("unit"), unit);
    object.insert(QStringLiteral("scope"), scope);
    object.insert(
        QStringLiteral("cardinality_semantics"), cardinality_semantics
    );
    object.insert(QStringLiteral("stability"), stability);
    object.insert(QStringLiteral("additive_semantics"), additive_semantics);
    object.insert(QStringLiteral("confidence"), confidence);
    object.insert(QStringLiteral("default_display_role"), default_display_role);
    object.insert(QStringLiteral("domain_namespace"), domain_namespace);
    return object;
}

QJsonObject debug_probe_core::protocol_identity_to_json(
    const QString& app_name, qint64 process_id, const QString& session_id,
    const QString& build_id, const QString& protocol_version,
    const QStringList& debug_flags, const QString& instrumentation_mode
) {
    QJsonObject identity;
    identity.insert(QStringLiteral("app"), app_name);
    identity.insert(QStringLiteral("pid"), process_id);
    identity.insert(QStringLiteral("session"), session_id);
    identity.insert(QStringLiteral("build"), build_id);
    identity.insert(QStringLiteral("protocol_version"), protocol_version);
    identity.insert(
        QStringLiteral("debug_flags"),
        debug_probe_json_helpers::string_list_to_json_array(debug_flags)
    );
    identity.insert(
        QStringLiteral("instrumentation_mode"), instrumentation_mode
    );
    return identity;
}

QJsonObject debug_probe_core::build_protocol_v1_envelope(
    const QString& message_family, const QString& app_name, qint64 process_id,
    const QString& session_id, const QString& build_id,
    const QString& protocol_version, const QStringList& debug_flags,
    const QString& instrumentation_mode
) {
    QJsonObject protocol;
    protocol.insert(QStringLiteral("message_family"), message_family);
    protocol.insert(
        QStringLiteral("version"), debug_probe_core::protocol_version_string()
    );
    protocol.insert(
        QStringLiteral("message_families"),
        debug_probe_core::protocol_message_families_v1()
    );
    protocol.insert(
        QStringLiteral("required_identity_fields"),
        debug_probe_core::protocol_required_identity_fields_v1()
    );
    protocol.insert(
        QStringLiteral("required_metric_hint_fields"),
        debug_probe_core::required_metric_hint_fields_v1()
    );
    protocol.insert(
        QStringLiteral("card_image_domain_hint_fields"),
        debug_probe_core::card_image_domain_hints_v1()
    );
    protocol.insert(
        QStringLiteral("capabilities"),
        debug_probe_core::protocol_capabilities_v1()
    );
    protocol.insert(
        QStringLiteral("identity"),
        protocol_identity_to_json(
            app_name, process_id, session_id, build_id,
            protocol_version.isEmpty()
                ? debug_probe_core::protocol_version_string()
                : protocol_version,
            debug_flags, instrumentation_mode
        )
    );
    return protocol;
}

qint64 debug_probe_core::integer_like_value(const QJsonValue& value) {
    if (value.isDouble()) {
        return static_cast<qint64>(std::llround(value.toDouble()));
    }
    return value.toInteger();
}

QString debug_probe_core::cadence_mode_to_string(debug_cadence_mode mode) {
    switch (mode) {
    case debug_cadence_mode::instrumented:
        return QStringLiteral("instrumented");
    case debug_cadence_mode::realistic:
    default:
        return QStringLiteral("realistic");
    }
}

qint64 debug_probe_core::sample_interval_ms_for_mode(debug_cadence_mode mode) {
    switch (mode) {
    case debug_cadence_mode::instrumented:
        return 1000;
    case debug_cadence_mode::realistic:
    default:
        return 5000;
    }
}

debug_probe_core::auto_process_report_policy
debug_probe_core::auto_report_policy_for_mode(debug_cadence_mode mode) {
    const qint64 one_hour_ms = 60 * 60 * 1000;
    switch (mode) {
    case debug_cadence_mode::instrumented:
        return {
            .rss_growth_threshold_bytes = 192 * 1024 * 1024,
            .cooldown_ms = 12 * 60 * 1000,
            .consecutive_growth_hits_required = 4,
            .window_ms = one_hour_ms,
            .window_max_exports = 4,
        };
    case debug_cadence_mode::realistic:
    default:
        return {
            .rss_growth_threshold_bytes = 96 * 1024 * 1024,
            .cooldown_ms = 8 * 60 * 1000,
            .consecutive_growth_hits_required = 3,
            .window_ms = one_hour_ms,
            .window_max_exports = 2,
        };
    }
}

QJsonObject debug_probe_core::build_protocol_message_v1(
    const QString& message_family, const protocol_identity& identity,
    qint64 monotonic_timestamp_ms, const QJsonObject& payload
) {
    QJsonObject root = payload;
    root.insert(
        QStringLiteral("monotonic_timestamp_ms"), monotonic_timestamp_ms
    );
    root.insert(
        QStringLiteral("protocol_v1"),
        build_protocol_v1_envelope(
            message_family, identity.app_name, identity.process_id,
            identity.session_id, identity.build_id, identity.protocol_version,
            identity.debug_flags, identity.instrumentation_mode
        )
    );
    return root;
}

QString debug_probe_core::protocol_version_string() {
    return QStringLiteral("debug_telemetry.v1");
}

QJsonArray debug_probe_core::protocol_message_families_v1() {
    return debug_probe_json_helpers::string_list_to_json_array(
        {
            QStringLiteral("hello"),
            QStringLiteral("capabilities"),
            QStringLiteral("sample_batch"),
            QStringLiteral("event_batch"),
            QStringLiteral("snapshot"),
            QStringLiteral("marker"),
            QStringLiteral("warning"),
            QStringLiteral("goodbye"),
        }
    );
}

QJsonArray debug_probe_core::protocol_required_identity_fields_v1() {
    return debug_probe_json_helpers::string_list_to_json_array(
        {
            QStringLiteral("app"),
            QStringLiteral("pid"),
            QStringLiteral("session"),
            QStringLiteral("build"),
            QStringLiteral("protocol_version"),
            QStringLiteral("debug_flags"),
            QStringLiteral("instrumentation_mode"),
        }
    );
}

QJsonArray debug_probe_core::required_metric_hint_fields_v1() {
    return debug_probe_json_helpers::string_list_to_json_array(
        {
            QStringLiteral("kind"),
            QStringLiteral("provenance"),
            QStringLiteral("unit"),
            QStringLiteral("scope"),
            QStringLiteral("cardinality_semantics"),
            QStringLiteral("stability"),
            QStringLiteral("additive_semantics"),
            QStringLiteral("confidence"),
            QStringLiteral("default_display_role"),
            QStringLiteral("domain_namespace"),
        }
    );
}

QJsonArray debug_probe_core::card_image_domain_hints_v1() {
    return debug_probe_json_helpers::string_list_to_json_array(
        {
            QStringLiteral("cache_namespace"),
            QStringLiteral("logical_card_key_coverage"),
            QStringLiteral("resolved_source_id"),
            QStringLiteral("fallback_source"),
            QStringLiteral("active_generation_id"),
            QStringLiteral("warming_generation_id"),
            QStringLiteral("target_display_size"),
            QStringLiteral("active_size_bucket_px"),
            QStringLiteral("cache_raster_size"),
            QStringLiteral("coverage_interval_ms"),
            QStringLiteral("slot_count"),
            QStringLiteral("layout_geometry"),
            QStringLiteral("resize_prewarm_timing"),
            QStringLiteral("theme_switch_marker"),
        }
    );
}

QJsonArray debug_probe_core::protocol_metric_catalog_v1() {
    QJsonArray metrics;
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cache_accounted_ready_bytes"), QStringLiteral("memory"),
        QStringLiteral("accounted"), QStringLiteral("bytes"),
        QStringLiteral("cache"), QStringLiteral("stock"),
        QStringLiteral("stable"), QStringLiteral("additive_within_scope"),
        QStringLiteral("exact"), QStringLiteral("primary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("widget_local_display_bytes_estimated"),
        QStringLiteral("memory"), QStringLiteral("estimated"),
        QStringLiteral("bytes"), QStringLiteral("widget"),
        QStringLiteral("stock"), QStringLiteral("recent_window"),
        QStringLiteral("non_additive_overlap_possible"),
        QStringLiteral("heuristic"), QStringLiteral("primary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("process_memory_rss_bytes"), QStringLiteral("memory"),
        QStringLiteral("measured"), QStringLiteral("bytes"),
        QStringLiteral("process"), QStringLiteral("stock"),
        QStringLiteral("stable"), QStringLiteral("non_additive_system_total"),
        QStringLiteral("exact"), QStringLiteral("primary"),
        QStringLiteral("system.process")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("process_memory_rss_bytes_delta"),
        QStringLiteral("memory"), QStringLiteral("derived"),
        QStringLiteral("bytes"), QStringLiteral("process"),
        QStringLiteral("delta"), QStringLiteral("noisy"),
        QStringLiteral("comparison_only"), QStringLiteral("exact"),
        QStringLiteral("secondary"), QStringLiteral("system.process")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("measured_accounted_gap_bytes_derived"),
        QStringLiteral("memory"), QStringLiteral("derived"),
        QStringLiteral("bytes"), QStringLiteral("comparison"),
        QStringLiteral("comparison"), QStringLiteral("stable"),
        QStringLiteral("comparison_only"), QStringLiteral("exact"),
        QStringLiteral("primary_overlay"), QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("accounted_to_measured_ratio_percent_derived"),
        QStringLiteral("ratio"), QStringLiteral("derived"),
        QStringLiteral("percent"), QStringLiteral("comparison"),
        QStringLiteral("comparison"), QStringLiteral("stable"),
        QStringLiteral("comparison_only"), QStringLiteral("exact"),
        QStringLiteral("secondary"), QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("displayed_recent_entries"), QStringLiteral("count"),
        QStringLiteral("accounted"), QStringLiteral("count"),
        QStringLiteral("cache"), QStringLiteral("stock_subset"),
        QStringLiteral("recent_window"),
        QStringLiteral("non_additive_overlap_possible"),
        QStringLiteral("best_effort"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cached_only_ready_entries"), QStringLiteral("count"),
        QStringLiteral("accounted"), QStringLiteral("count"),
        QStringLiteral("cache"), QStringLiteral("stock_subset"),
        QStringLiteral("recent_window"),
        QStringLiteral("non_additive_overlap_possible"),
        QStringLiteral("best_effort"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("active_generation_id"), QStringLiteral("state"),
        QStringLiteral("accounted"), QStringLiteral("id"),
        QStringLiteral("cache_generation"), QStringLiteral("state"),
        QStringLiteral("stable"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("warming_generation_id"), QStringLiteral("state"),
        QStringLiteral("accounted"), QStringLiteral("id"),
        QStringLiteral("cache_generation"), QStringLiteral("state"),
        QStringLiteral("stable"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cache_ready_entries"), QStringLiteral("count"),
        QStringLiteral("accounted"), QStringLiteral("count"),
        QStringLiteral("cache"), QStringLiteral("stock"),
        QStringLiteral("stable"), QStringLiteral("additive_within_scope"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cache_ready_images"), QStringLiteral("count"),
        QStringLiteral("accounted"), QStringLiteral("count"),
        QStringLiteral("cache"), QStringLiteral("stock"),
        QStringLiteral("stable"), QStringLiteral("additive_within_scope"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cache_in_flight_families"), QStringLiteral("count"),
        QStringLiteral("accounted"), QStringLiteral("count"),
        QStringLiteral("cache"), QStringLiteral("stock"),
        QStringLiteral("noisy"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cache_pending_families"), QStringLiteral("count"),
        QStringLiteral("accounted"), QStringLiteral("count"),
        QStringLiteral("cache"), QStringLiteral("stock"),
        QStringLiteral("noisy"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cache_entries_added_interval"), QStringLiteral("count"),
        QStringLiteral("accounted"), QStringLiteral("count"),
        QStringLiteral("cache"), QStringLiteral("delta"),
        QStringLiteral("stable"), QStringLiteral("additive_within_scope"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cache_entries_removed_interval"),
        QStringLiteral("count"), QStringLiteral("accounted"),
        QStringLiteral("count"), QStringLiteral("cache"),
        QStringLiteral("delta"), QStringLiteral("stable"),
        QStringLiteral("additive_within_scope"), QStringLiteral("exact"),
        QStringLiteral("secondary"), QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cache_bytes_added_interval"), QStringLiteral("memory"),
        QStringLiteral("accounted"), QStringLiteral("bytes"),
        QStringLiteral("cache"), QStringLiteral("delta"),
        QStringLiteral("stable"), QStringLiteral("additive_within_scope"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cache_bytes_removed_interval"),
        QStringLiteral("memory"), QStringLiteral("accounted"),
        QStringLiteral("bytes"), QStringLiteral("cache"),
        QStringLiteral("delta"), QStringLiteral("stable"),
        QStringLiteral("additive_within_scope"), QStringLiteral("exact"),
        QStringLiteral("secondary"), QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cache_images_added_interval"), QStringLiteral("count"),
        QStringLiteral("accounted"), QStringLiteral("count"),
        QStringLiteral("cache"), QStringLiteral("delta"),
        QStringLiteral("stable"), QStringLiteral("additive_within_scope"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("cache_images_removed_interval"),
        QStringLiteral("count"), QStringLiteral("accounted"),
        QStringLiteral("count"), QStringLiteral("cache"),
        QStringLiteral("delta"), QStringLiteral("stable"),
        QStringLiteral("additive_within_scope"), QStringLiteral("exact"),
        QStringLiteral("secondary"), QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("raster_lifecycle_completed_samples"),
        QStringLiteral("count"), QStringLiteral("measured"),
        QStringLiteral("count"), QStringLiteral("raster_pipeline"),
        QStringLiteral("cumulative"), QStringLiteral("stable"),
        QStringLiteral("additive_within_scope"), QStringLiteral("exact"),
        QStringLiteral("secondary"), QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("raster_lifecycle_average_ms"),
        QStringLiteral("duration"), QStringLiteral("measured"),
        QStringLiteral("ms"), QStringLiteral("raster_pipeline"),
        QStringLiteral("lifetime_summary"), QStringLiteral("noisy"),
        QStringLiteral("comparison_only"), QStringLiteral("best_effort"),
        QStringLiteral("secondary"), QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("raster_lifecycle_max_ms"), QStringLiteral("duration"),
        QStringLiteral("measured"), QStringLiteral("ms"),
        QStringLiteral("raster_pipeline"), QStringLiteral("lifetime_summary"),
        QStringLiteral("noisy"), QStringLiteral("comparison_only"),
        QStringLiteral("best_effort"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_slot_count"), QStringLiteral("count"),
        QStringLiteral("accounted"), QStringLiteral("count"),
        QStringLiteral("layout"), QStringLiteral("state"),
        QStringLiteral("stable"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_required_short_px"), QStringLiteral("size"),
        QStringLiteral("accounted"), QStringLiteral("px"),
        QStringLiteral("layout"), QStringLiteral("state"),
        QStringLiteral("stable"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_device_pixel_ratio"), QStringLiteral("ratio"),
        QStringLiteral("accounted"), QStringLiteral("ratio"),
        QStringLiteral("layout"), QStringLiteral("state"),
        QStringLiteral("stable"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_cache_window_minimum_need_px"),
        QStringLiteral("size"), QStringLiteral("accounted"),
        QStringLiteral("px"), QStringLiteral("layout"), QStringLiteral("state"),
        QStringLiteral("stable"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_cache_window_maximum_need_px"),
        QStringLiteral("size"), QStringLiteral("accounted"),
        QStringLiteral("px"), QStringLiteral("layout"), QStringLiteral("state"),
        QStringLiteral("stable"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_requested_target_bucket_px"),
        QStringLiteral("size"), QStringLiteral("accounted"),
        QStringLiteral("px"), QStringLiteral("cache_generation"),
        QStringLiteral("state"), QStringLiteral("stable"),
        QStringLiteral("non_additive"), QStringLiteral("exact"),
        QStringLiteral("secondary"), QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_active_bucket_px"), QStringLiteral("size"),
        QStringLiteral("accounted"), QStringLiteral("px"),
        QStringLiteral("cache_generation"), QStringLiteral("state"),
        QStringLiteral("stable"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_warming_bucket_px"), QStringLiteral("size"),
        QStringLiteral("accounted"), QStringLiteral("px"),
        QStringLiteral("cache_generation"), QStringLiteral("state"),
        QStringLiteral("stable"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_cache_raster_width_px"), QStringLiteral("size"),
        QStringLiteral("accounted"), QStringLiteral("px"),
        QStringLiteral("cache_generation"), QStringLiteral("state"),
        QStringLiteral("stable"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_cache_raster_height_px"), QStringLiteral("size"),
        QStringLiteral("accounted"), QStringLiteral("px"),
        QStringLiteral("cache_generation"), QStringLiteral("state"),
        QStringLiteral("stable"), QStringLiteral("non_additive"),
        QStringLiteral("exact"), QStringLiteral("secondary"),
        QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_preloaded_raster_width_px"),
        QStringLiteral("size"), QStringLiteral("accounted"),
        QStringLiteral("px"), QStringLiteral("cache_generation"),
        QStringLiteral("state"), QStringLiteral("stable"),
        QStringLiteral("non_additive"), QStringLiteral("exact"),
        QStringLiteral("secondary"), QStringLiteral("cards.image")
    ));
    metrics.push_back(metric_catalog_entry(
        QStringLiteral("layout_preloaded_raster_height_px"),
        QStringLiteral("size"), QStringLiteral("accounted"),
        QStringLiteral("px"), QStringLiteral("cache_generation"),
        QStringLiteral("state"), QStringLiteral("stable"),
        QStringLiteral("non_additive"), QStringLiteral("exact"),
        QStringLiteral("secondary"), QStringLiteral("cards.image")
    ));
    return metrics;
}

QJsonObject debug_probe_core::metric_hint_for_id_v1(const QString& metric_id) {
    if (metric_id.isEmpty()) {
        return {};
    }

    const QJsonArray catalog = protocol_metric_catalog_v1();
    for (const auto& value : catalog) {
        const QJsonObject metric = value.toObject();
        if (metric.value(QStringLiteral("id")).toString() == metric_id) {
            return metric;
        }
    }
    return {};
}

QJsonObject debug_probe_core::protocol_capabilities_v1() {
    QJsonObject capabilities;
    QJsonObject cards_image_extension;
    cards_image_extension.insert(
        QStringLiteral("id"), QStringLiteral("cards.image.cache_layout")
    );
    cards_image_extension.insert(QStringLiteral("schema_version"), 1);
    QJsonArray extensions;
    extensions.push_back(cards_image_extension);
    capabilities.insert(QStringLiteral("extensions"), extensions);
    capabilities.insert(QStringLiteral("metric_catalog_revision"), 2);
    capabilities.insert(
        QStringLiteral("domains"),
        debug_probe_json_helpers::string_list_to_json_array(
            {
                QStringLiteral("cards.image"),
                QStringLiteral("system.process"),
            }
        )
    );
    capabilities.insert(
        QStringLiteral("optional_streams"),
        debug_probe_json_helpers::string_list_to_json_array(
            {
                QStringLiteral("sample_batch"),
                QStringLiteral("event_batch"),
                QStringLiteral("snapshot"),
                QStringLiteral("marker"),
                QStringLiteral("warning"),
            }
        )
    );
    capabilities.insert(
        QStringLiteral("metric_catalog"), protocol_metric_catalog_v1()
    );
    capabilities.insert(
        QStringLiteral("required_metric_hint_fields"),
        required_metric_hint_fields_v1()
    );
    capabilities.insert(
        QStringLiteral("card_image_domain_hint_fields"),
        card_image_domain_hints_v1()
    );
    capabilities.insert(
        QStringLiteral("structured_payloads"),
        debug_probe_json_helpers::string_list_to_json_array(
            {
                QStringLiteral("cache_detail"),
                QStringLiteral("geometry"),
                QStringLiteral("resize_transition"),
                QStringLiteral("process_memory"),
            }
        )
    );
    return capabilities;
}

debug_probe_core::metric_point_v1
debug_probe_core::point_from_sample_batch_v1(const QJsonArray& samples) {
    metric_point_v1 point;
    for (const auto& sample_value : samples) {
        const QJsonObject sample = sample_value.toObject();
        const QString metric_id
            = sample.value(QStringLiteral("metric_id")).toString();
        const qint64 value
            = integer_like_value(sample.value(QStringLiteral("value")));
        if (metric_id == QStringLiteral("cache_accounted_ready_bytes")) {
            point.cache_accounted_ready_bytes = value;
        } else if (
            metric_id == QStringLiteral("widget_local_display_bytes_estimated")
        ) {
            point.widget_local_display_bytes_estimated = value;
        } else if (metric_id == QStringLiteral("process_memory_rss_bytes")) {
            point.process_memory_rss_bytes = value;
        } else if (
            metric_id == QStringLiteral("measured_accounted_gap_bytes_derived")
        ) {
            point.measured_accounted_gap_bytes_derived = value;
        }
    }
    return point;
}

debug_probe_core::metric_point_v1
debug_probe_core::point_from_snapshot_payload_v1(
    const QJsonObject& snapshot_payload
) {
    metric_point_v1 point;
    if (snapshot_payload.contains(
            QStringLiteral("cache_accounted_ready_bytes")
        )) {
        point.cache_accounted_ready_bytes
            = integer_like_value(snapshot_payload.value(
                QStringLiteral("cache_accounted_ready_bytes")
            ));
    }
    if (snapshot_payload.contains(
            QStringLiteral("widget_local_display_bytes_estimated")
        )) {
        point.widget_local_display_bytes_estimated
            = integer_like_value(snapshot_payload.value(
                QStringLiteral("widget_local_display_bytes_estimated")
            ));
    }
    if (snapshot_payload.contains(QStringLiteral("process_memory_rss_bytes"))) {
        point.process_memory_rss_bytes = integer_like_value(
            snapshot_payload.value(QStringLiteral("process_memory_rss_bytes"))
        );
    }
    if (snapshot_payload.contains(
            QStringLiteral("measured_accounted_gap_bytes_derived")
        )) {
        point.measured_accounted_gap_bytes_derived
            = integer_like_value(snapshot_payload.value(
                QStringLiteral("measured_accounted_gap_bytes_derived")
            ));
    } else if (
        point.cache_accounted_ready_bytes >= 0
        && point.process_memory_rss_bytes >= 0
    ) {
        point.measured_accounted_gap_bytes_derived
            = point.process_memory_rss_bytes
            - point.cache_accounted_ready_bytes;
    }
    return point;
}

void debug_probe_core::merge_metric_point_v1(
    metric_point_v1* target, const metric_point_v1& update
) {
    if (target == nullptr) {
        return;
    }

    if (update.cache_accounted_ready_bytes >= 0) {
        target->cache_accounted_ready_bytes
            = update.cache_accounted_ready_bytes;
    }
    if (update.widget_local_display_bytes_estimated >= 0) {
        target->widget_local_display_bytes_estimated
            = update.widget_local_display_bytes_estimated;
    }
    if (update.process_memory_rss_bytes >= 0) {
        target->process_memory_rss_bytes = update.process_memory_rss_bytes;
    }
    if (update.measured_accounted_gap_bytes_derived >= 0) {
        target->measured_accounted_gap_bytes_derived
            = update.measured_accounted_gap_bytes_derived;
    }
}

QJsonObject debug_probe_core::geometry_snapshot_to_json(
    const geometry_debug_snapshot& snapshot
) {
    QJsonObject object;
    object.insert(QStringLiteral("timestamp_ms"), snapshot.timestamp_ms);
    object.insert(QStringLiteral("slot_count"), snapshot.slot_count);
    object.insert(
        QStringLiteral("visible_slot_count"), snapshot.visible_slot_count
    );
    object.insert(
        QStringLiteral("window_size"),
        debug_probe_json_helpers::size_to_json(snapshot.window_size)
    );
    object.insert(
        QStringLiteral("layout_size"),
        debug_probe_json_helpers::size_to_json(snapshot.layout_size)
    );
    object.insert(
        QStringLiteral("display_card_size"),
        debug_probe_json_helpers::size_to_json(snapshot.display_card_size)
    );
    object.insert(
        QStringLiteral("display_card_need_short_px"),
        snapshot.display_card_need_short_px
    );
    object.insert(
        QStringLiteral("device_pixel_ratio"), snapshot.device_pixel_ratio
    );
    object.insert(
        QStringLiteral("active_bucket_px"), snapshot.active_bucket_px
    );
    object.insert(
        QStringLiteral("warming_bucket_px"), snapshot.warming_bucket_px
    );
    object.insert(
        QStringLiteral("cache_window_minimum_need_px"),
        snapshot.cache_window_minimum_need_px
    );
    object.insert(
        QStringLiteral("cache_window_maximum_need_px"),
        snapshot.cache_window_maximum_need_px
    );
    object.insert(
        QStringLiteral("requested_target_bucket_px"),
        snapshot.requested_target_bucket_px
    );
    object.insert(QStringLiteral("cache_decision"), snapshot.cache_decision);
    object.insert(QStringLiteral("cache_trigger"), snapshot.cache_trigger);
    object.insert(
        QStringLiteral("cache_raster_size"),
        debug_probe_json_helpers::size_to_json(snapshot.cache_raster_size)
    );
    object.insert(
        QStringLiteral("preloaded_raster_size"),
        debug_probe_json_helpers::size_to_json(snapshot.preloaded_raster_size)
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

QJsonObject debug_probe_core::cache_snapshot_to_live_json(
    const raster_cache::debug_snapshot& snapshot
) {
    QJsonObject ready;
    ready.insert(QStringLiteral("entries"), snapshot.ready_entries);
    ready.insert(QStringLiteral("images"), snapshot.ready_images);
    ready.insert(QStringLiteral("accounted_bytes"), snapshot.ready_bytes);
    ready.insert(
        QStringLiteral("high_water_entries"), snapshot.high_water_ready_entries
    );
    ready.insert(
        QStringLiteral("high_water_images"), snapshot.high_water_ready_images
    );
    ready.insert(
        QStringLiteral("high_water_accounted_bytes"),
        snapshot.high_water_ready_bytes
    );

    QJsonObject work;
    work.insert(
        QStringLiteral("in_flight_families"), snapshot.in_flight_families
    );
    work.insert(QStringLiteral("pending_families"), snapshot.pending_families);

    QJsonObject interval_changes;
    interval_changes.insert(
        QStringLiteral("entries_added"), snapshot.interval_deltas.entries_added
    );
    interval_changes.insert(
        QStringLiteral("entries_removed"),
        snapshot.interval_deltas.entries_removed
    );
    interval_changes.insert(
        QStringLiteral("bytes_added"), snapshot.interval_deltas.bytes_added
    );
    interval_changes.insert(
        QStringLiteral("bytes_removed"), snapshot.interval_deltas.bytes_removed
    );
    interval_changes.insert(
        QStringLiteral("images_added"), snapshot.interval_deltas.images_added
    );
    interval_changes.insert(
        QStringLiteral("images_removed"),
        snapshot.interval_deltas.images_removed
    );

    QJsonObject raster_lifecycle;
    raster_lifecycle.insert(
        QStringLiteral("completed_samples"), snapshot.raster_timing_samples
    );
    raster_lifecycle.insert(
        QStringLiteral("average_ms"), snapshot.raster_timing_avg_ms
    );
    raster_lifecycle.insert(
        QStringLiteral("maximum_ms"), snapshot.raster_timing_max_ms
    );

    QJsonObject coalesced_wait;
    coalesced_wait.insert(
        QStringLiteral("completed_samples"), snapshot.coalesced_wait_samples
    );
    coalesced_wait.insert(
        QStringLiteral("average_ms"), snapshot.coalesced_wait_avg_ms
    );
    coalesced_wait.insert(
        QStringLiteral("maximum_ms"), snapshot.coalesced_wait_max_ms
    );

    QJsonObject deadline_readiness;
    deadline_readiness.insert(
        QStringLiteral("samples"), snapshot.deadline_readiness_samples
    );
    deadline_readiness.insert(
        QStringLiteral("ready_early"), snapshot.deadline_ready_early
    );
    deadline_readiness.insert(
        QStringLiteral("ready_on_time"), snapshot.deadline_ready_on_time
    );
    deadline_readiness.insert(
        QStringLiteral("ready_late"), snapshot.deadline_ready_late
    );

    QJsonObject display;
    display.insert(
        QStringLiteral("recent_ready_entries"), snapshot.displayed_ready_entries
    );
    display.insert(
        QStringLiteral("cached_only_ready_entries"),
        snapshot.cached_only_ready_entries
    );
    display.insert(
        QStringLiteral("recent_ready_images"), snapshot.displayed_ready_images
    );
    display.insert(
        QStringLiteral("cached_only_ready_images"),
        snapshot.cached_only_ready_images
    );
    display.insert(
        QStringLiteral("widget_local_bytes_estimated"),
        snapshot.widget_local_display_bytes_estimated
    );
    display.insert(
        QStringLiteral("coverage_window_ms"), snapshot.displayed_entry_window_ms
    );
    display.insert(
        QStringLiteral("coverage_percent"),
        snapshot.displayed_entry_coverage_percent
    );

    QJsonObject fallback;
    fallback.insert(
        QStringLiteral("active_theme_keys_ready"),
        snapshot.fallback_active_theme_keys_ready
    );
    fallback.insert(
        QStringLiteral("default_theme_keys_ready"),
        snapshot.fallback_default_theme_keys_ready
    );
    fallback.insert(
        QStringLiteral("placeholder_keys_ready"),
        snapshot.fallback_placeholder_keys_ready
    );

    constexpr qsizetype maximum_live_size_buckets = 32;
    QJsonArray size_buckets;
    for (qsizetype index = 0; index < snapshot.size_buckets.size()
         && index < maximum_live_size_buckets;
         ++index) {
        const auto& bucket = snapshot.size_buckets.at(index);
        QJsonObject bucket_json;
        bucket_json.insert(
            QStringLiteral("target_bucket_px"), bucket.target_bucket_px
        );
        bucket_json.insert(QStringLiteral("ready_entries"), bucket.entry_count);
        bucket_json.insert(
            QStringLiteral("accounted_bytes"), bucket.total_bytes
        );
        size_buckets.push_back(bucket_json);
    }

    QJsonArray subsystems;
    for (const auto& subsystem : snapshot.subsystem_summaries) {
        QJsonObject subsystem_json;
        subsystem_json.insert(
            QStringLiteral("name_space"),
            cache_namespace_to_string(subsystem.name_space)
        );
        subsystem_json.insert(
            QStringLiteral("kind"), resource_kind_to_string(subsystem.kind)
        );
        subsystem_json.insert(
            QStringLiteral("ready_entries"), subsystem.ready_entries
        );
        subsystem_json.insert(
            QStringLiteral("accounted_bytes"), subsystem.ready_bytes
        );
        subsystem_json.insert(
            QStringLiteral("request_samples"), subsystem.request_samples
        );
        subsystem_json.insert(
            QStringLiteral("timing_samples"), subsystem.timing_samples
        );
        subsystem_json.insert(
            QStringLiteral("timing_maximum_ms"), subsystem.timing_max_elapsed_ms
        );
        subsystems.push_back(subsystem_json);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(
        QStringLiteral("snapshot_sequence"), snapshot.snapshot_sequence
    );
    root.insert(QStringLiteral("timestamp_ms"), snapshot.timestamp_ms);
    root.insert(QStringLiteral("ready"), ready);
    root.insert(QStringLiteral("work"), work);
    root.insert(QStringLiteral("interval_changes"), interval_changes);
    root.insert(QStringLiteral("raster_lifecycle"), raster_lifecycle);
    root.insert(QStringLiteral("coalesced_wait"), coalesced_wait);
    root.insert(QStringLiteral("deadline_readiness"), deadline_readiness);
    root.insert(QStringLiteral("display"), display);
    root.insert(QStringLiteral("fallback"), fallback);
    root.insert(
        QStringLiteral("size_bucket_total_count"), snapshot.size_buckets.size()
    );
    root.insert(
        QStringLiteral("size_bucket_omitted_count"),
        std::max<qsizetype>(
            0, snapshot.size_buckets.size() - maximum_live_size_buckets
        )
    );
    root.insert(QStringLiteral("size_buckets"), size_buckets);
    root.insert(QStringLiteral("subsystems"), subsystems);
    return root;
}

QJsonObject debug_probe_core::resize_transition_to_live_json(
    const resize_transition_debug_event& transition
) {
    QJsonObject object;
    object.insert(QStringLiteral("schema_version"), 1);
    object.insert(QStringLiteral("timestamp_ms"), transition.timestamp_ms);
    object.insert(
        QStringLiteral("old_window_size"),
        debug_probe_json_helpers::size_to_json(transition.old_window_size)
    );
    object.insert(
        QStringLiteral("new_window_size"),
        debug_probe_json_helpers::size_to_json(transition.new_window_size)
    );
    object.insert(
        QStringLiteral("old_active_bucket_px"), transition.old_active_bucket_px
    );
    object.insert(
        QStringLiteral("new_active_bucket_px"), transition.new_active_bucket_px
    );
    object.insert(
        QStringLiteral("old_warming_bucket_px"),
        transition.old_warming_bucket_px
    );
    object.insert(
        QStringLiteral("new_warming_bucket_px"),
        transition.new_warming_bucket_px
    );
    object.insert(
        QStringLiteral("geometry_after_resize"),
        geometry_snapshot_to_json(transition.geometry_after_resize)
    );
    return object;
}

QJsonObject debug_probe_core::resize_history_entry_to_json(
    const resize_history_entry& entry
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
        QStringLiteral("old_window_size"),
        debug_probe_json_helpers::size_to_json(entry.old_window_size)
    );
    object.insert(
        QStringLiteral("new_window_size"),
        debug_probe_json_helpers::size_to_json(entry.new_window_size)
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

QString debug_probe_core::resize_entry_to_jsonl_line(
    const resize_history_entry& entry
) {
    const QJsonDocument document(resize_history_entry_to_json(entry));
    return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
}

QJsonObject debug_probe_core::build_snapshot_export_json(
    const export_request_metadata& metadata,
    const QVector<cache_timeline_entry>& cache_entries,
    const QVector<event_timeline_entry>& event_entries,
    const QVector<geometry_debug_snapshot>& geometry_entries,
    const QVector<resize_history_entry>& resize_entries,
    debug_cadence_mode export_mode
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
    const QString instrumentation_mode
        = metadata.protocol_instrumentation_mode.isEmpty()
        ? cadence_mode_to_string(export_mode)
        : metadata.protocol_instrumentation_mode;
    root.insert(
        QStringLiteral("protocol_v1"),
        build_protocol_v1_envelope(
            QStringLiteral("snapshot"), metadata.protocol_app_name,
            metadata.protocol_process_id, metadata.protocol_session_id,
            metadata.protocol_build_id, metadata.protocol_version,
            metadata.protocol_debug_flags, instrumentation_mode
        )
    );
    root.insert(
        QStringLiteral("telemetry_semantics"), snapshot_telemetry_semantics()
    );

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
            QStringLiteral("cache_ready_entries"),
            entry.cache_snapshot.ready_entries
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
            QStringLiteral("cache_ready_images"),
            entry.cache_snapshot.ready_images
        );
        object.insert(
            QStringLiteral("cache_in_flight_families"),
            entry.cache_snapshot.in_flight_families
        );
        object.insert(
            QStringLiteral("cache_pending_families"),
            entry.cache_snapshot.pending_families
        );
        object.insert(
            QStringLiteral("raster_lifecycle_completed_samples"),
            entry.cache_snapshot.raster_timing_samples
        );
        object.insert(
            QStringLiteral("raster_lifecycle_average_ms"),
            entry.cache_snapshot.raster_timing_avg_ms
        );
        object.insert(
            QStringLiteral("raster_lifecycle_max_ms"),
            entry.cache_snapshot.raster_timing_max_ms
        );
        object.insert(
            QStringLiteral("cache_detail"),
            cache_snapshot_to_live_json(entry.cache_snapshot)
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
            entry.kind == event_timeline_entry::event_kind::cache_snapshot
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

    return root;
}

QJsonObject debug_probe_core::snapshot_telemetry_semantics() {
    QJsonObject telemetry_semantics;
    const QJsonArray metric_catalog = protocol_metric_catalog_v1();
    for (const auto& metric_value : metric_catalog) {
        const QJsonObject metric_object = metric_value.toObject();
        const QString metric_id
            = metric_object.value(QStringLiteral("id")).toString();
        if (metric_id.isEmpty()) {
            continue;
        }
        telemetry_semantics.insert(
            metric_id,
            metric_object.value(QStringLiteral("provenance")).toString()
        );
    }
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
    return telemetry_semantics;
}

QJsonObject debug_probe_core::process_memory_report_telemetry_semantics() {
    QJsonObject semantics;
    const QJsonArray metric_catalog = protocol_metric_catalog_v1();
    for (const auto& metric_value : metric_catalog) {
        const QJsonObject metric_object = metric_value.toObject();
        const QString metric_id
            = metric_object.value(QStringLiteral("id")).toString();
        if (metric_id.isEmpty()) {
            continue;
        }
        semantics.insert(
            metric_id,
            metric_object.value(QStringLiteral("provenance")).toString()
        );
    }
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
    return semantics;
}

QJsonObject debug_probe_core::build_process_memory_report_json(
    const process_memory_report_inputs& inputs
) {
    QJsonObject root;
    root.insert(
        QStringLiteral("report_kind"),
        QStringLiteral("process_memory_detail_on_demand")
    );
    root.insert(
        QStringLiteral("captured_at_utc_ms"), inputs.captured_at_utc_ms
    );
    root.insert(
        QStringLiteral("debug_cadence_mode"),
        cadence_mode_to_string(inputs.cadence_mode)
    );
    root.insert(
        QStringLiteral("process_memory_sample_interval_ms"),
        inputs.process_memory_sample_interval_ms
    );
    root.insert(
        QStringLiteral("collector_latest_process_rss_bytes"),
        inputs.latest_process_rss_bytes
    );
    root.insert(
        QStringLiteral("auto_process_report_rss_growth_threshold_bytes"),
        inputs.auto_process_report_rss_growth_threshold_bytes
    );
    root.insert(
        QStringLiteral("auto_process_report_cooldown_ms"),
        inputs.auto_process_report_cooldown_ms
    );
    root.insert(
        QStringLiteral("auto_process_report_baseline_rss_bytes"),
        inputs.auto_process_report_baseline_rss_bytes
    );
    root.insert(
        QStringLiteral("auto_process_report_rss_growth_since_baseline_bytes"),
        inputs.auto_process_report_rss_growth_since_baseline_bytes
    );
    root.insert(
        QStringLiteral("auto_process_report_last_trigger_utc_ms"),
        inputs.auto_process_report_last_trigger_utc_ms
    );
    root.insert(
        QStringLiteral("auto_process_report_cooldown_remaining_ms"),
        inputs.auto_process_report_cooldown_remaining_ms
    );
    root.insert(
        QStringLiteral("auto_process_report_consecutive_growth_hits_required"),
        inputs.auto_process_report_consecutive_growth_hits_required
    );
    root.insert(
        QStringLiteral("auto_process_report_consecutive_growth_hits_current"),
        inputs.auto_process_report_consecutive_growth_hits_current
    );
    root.insert(
        QStringLiteral("collector_latest_process_rss_source"),
        inputs.latest_process_rss_source
    );
    root.insert(
        QStringLiteral("collector_latest_process_rss_available"),
        inputs.latest_process_rss_bytes >= 0
    );
    if (inputs.latest_process_rss_bytes < 0) {
        root.insert(
            QStringLiteral("collector_latest_process_rss_unavailable_reason"),
            inputs.latest_process_rss_unavailable_reason
        );
    }

    QJsonObject status_object;
    status_object.insert(QStringLiteral("VmRSS"), inputs.status_vm_rss_bytes);
    status_object.insert(QStringLiteral("VmHWM"), inputs.status_vm_hwm_bytes);
    status_object.insert(QStringLiteral("VmSize"), inputs.status_vm_size_bytes);
    status_object.insert(QStringLiteral("VmSwap"), inputs.status_vm_swap_bytes);
    root.insert(QStringLiteral("status_bytes"), status_object);
    root.insert(
        QStringLiteral("status_bytes_available"), inputs.status_bytes_available
    );
    if (!inputs.status_bytes_available) {
        root.insert(
            QStringLiteral("status_bytes_unavailable_reason"),
            inputs.status_bytes_unavailable_reason
        );
    }

    root.insert(
        QStringLiteral("smaps_rollup_bytes"), inputs.smaps_rollup_bytes
    );
    root.insert(
        QStringLiteral("smaps_rollup_bytes_available"),
        inputs.smaps_rollup_bytes_available
    );
    if (!inputs.smaps_rollup_bytes_available) {
        root.insert(
            QStringLiteral("smaps_rollup_bytes_unavailable_reason"),
            inputs.smaps_rollup_bytes_unavailable_reason
        );
    }

    const QString instrumentation_mode
        = inputs.protocol_instrumentation_mode.isEmpty()
        ? cadence_mode_to_string(inputs.cadence_mode)
        : inputs.protocol_instrumentation_mode;
    root.insert(
        QStringLiteral("protocol_v1"),
        build_protocol_v1_envelope(
            QStringLiteral("snapshot"), inputs.protocol_app_name,
            inputs.protocol_process_id, inputs.protocol_session_id,
            inputs.protocol_build_id, inputs.protocol_version,
            inputs.protocol_debug_flags, instrumentation_mode
        )
    );

    QJsonObject semantics = process_memory_report_telemetry_semantics();
    semantics.insert(
        QStringLiteral("report_trigger"), inputs.report_trigger_label
    );
    root.insert(QStringLiteral("telemetry_semantics"), semantics);
    return root;
}
