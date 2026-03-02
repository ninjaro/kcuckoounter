#include "include/resource_monitor_tests.hpp"

#include "monitor/resource_monitor.hpp"
#include "table/table.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

void resource_monitor_tests::collects_initial_snapshot_when_attached() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 8);

    collector.attach_cache_service(&cache_service);

    QVERIFY(collector.has_cache_snapshot());
    QCOMPARE(collector.timeline_size(), 1);

    const resource_monitor::cache_timeline_entry latest
        = collector.latest_cache_snapshot();
    QCOMPARE(latest.collector_sequence, 1);
    QCOMPARE(latest.cache_snapshot.ready_entries, 0);
}

void resource_monitor_tests::keeps_bounded_timeline_and_emits_updates() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 3);
    QSignalSpy spy(&collector, &resource_monitor::cache_snapshot_collected);

    collector.attach_cache_service(&cache_service);
    QCOMPARE(spy.count(), 1);

    for (int bucket = 100; bucket < 104; ++bucket) {
        const raster_cache::result result {
            .key =
                {
                    .name_space = raster_cache::cache_namespace::main,
                    .kind = raster_cache::resource_kind::single_svg,
                    .source_id = QStringLiteral("assets/cuckoo.svg"),
                    .render_scope = QStringLiteral("full"),
                    .target_bucket_px = bucket,
                },
            .raster_size = QSize(bucket, bucket),
            .generation = 1,
            .timestamp_ms = 1,
            .use_count = 0,
            .single_image =
                QImage(bucket, bucket, QImage::Format_ARGB32_Premultiplied),
            .face_images = {},
        };

        cache_service.insert_or_update_result(result);
    }

    QCOMPARE(spy.count(), 5);
    QCOMPARE(collector.timeline_size(), 3);

    const QVector<resource_monitor::cache_timeline_entry> timeline
        = collector.cache_timeline();
    QCOMPARE(timeline.size(), 3);
    QCOMPARE(timeline.front().collector_sequence, 3);
    QCOMPARE(timeline.back().collector_sequence, 5);
    QCOMPARE(timeline.back().cache_snapshot.ready_entries, 4);
}

void resource_monitor_tests::
    records_manual_markers_in_bounded_event_timeline() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 3);

    collector.attach_cache_service(&cache_service);
    collector.add_manual_marker(QStringLiteral("resize"));
    collector.add_manual_marker(QStringLiteral("theme_apply"));
    collector.add_manual_marker(QStringLiteral("start_game"));

    QCOMPARE(collector.event_timeline_size(), 3);

    const QVector<resource_monitor::event_timeline_entry> timeline
        = collector.event_timeline();
    QCOMPARE(timeline.size(), 3);
    QCOMPARE(
        timeline.front().kind,
        resource_monitor::event_timeline_entry::event_kind::manual_marker
    );
    QCOMPARE(timeline.front().label, QStringLiteral("resize"));
    QCOMPARE(timeline.back().label, QStringLiteral("start_game"));
}

void resource_monitor_tests::exports_snapshot_asynchronously() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 8);

    collector.attach_cache_service(&cache_service);
    collector.add_manual_marker(QStringLiteral("export_check"));

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString output_path
        = temp_dir.path() + QStringLiteral("/snapshot.json");

    QSignalSpy export_spy(
        &collector, &resource_monitor::snapshot_export_finished
    );

    collector.export_debug_snapshot_async(output_path);

    QTRY_COMPARE(export_spy.count(), 1);
    const QList<QVariant> signal_args = export_spy.takeFirst();
    QCOMPARE(signal_args.at(0).toString(), output_path);
    QCOMPARE(signal_args.at(1).toBool(), true);

    QFile exported_file(output_path);
    QVERIFY(exported_file.open(QIODevice::ReadOnly));
    const QJsonDocument document
        = QJsonDocument::fromJson(exported_file.readAll());
    QVERIFY(document.isObject());

    const QJsonObject root = document.object();
    QVERIFY(root.contains(QStringLiteral("cache_timeline")));
    QVERIFY(root.contains(QStringLiteral("event_timeline")));
    QCOMPARE(
        root.value(QStringLiteral("debug_cadence_mode")).toString(),
        QStringLiteral("realistic")
    );

    const resource_monitor::export_request_metadata metadata
        = collector.latest_export_metadata();
    QCOMPARE(
        root.value(QStringLiteral("collector_sequence")).toInteger(),
        metadata.collector_sequence
    );
}

void resource_monitor_tests::
    export_contains_size_bucket_and_largest_entry_diagnostics() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 8);
    collector.attach_cache_service(&cache_service);

    const raster_cache::entry_key displayed_key {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = QStringLiteral("assets/cuckoo.svg"),
        .render_scope = QStringLiteral("full"),
        .target_bucket_px = 144,
    };

    cache_service.insert_or_update_result(
        raster_cache::result {
            .key = displayed_key,
            .raster_size = QSize(144, 144),
            .generation = 1,
            .timestamp_ms = 0,
            .use_count = 1,
            .single_image
            = QImage(144, 144, QImage::Format_ARGB32_Premultiplied),
            .face_images = {},
        }
    );
    cache_service.note_entry_displayed(
        displayed_key, raster_cache::debug_consumer_scope::table_slots
    );

    const raster_cache::request req {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = QStringLiteral("assets/cuckoo.svg"),
        .render_scope = QStringLiteral("full"),
        .need_short_px = 144,
        .target_bucket_px = 144,
        .high_priority = false,
        .interactive = true,
        .preview = false,
    };
    const raster_cache::submit_outcome submitted
        = cache_service.submit_request(req);
    QVERIFY(
        submitted.state == raster_cache::request_state::cache_hit
        || submitted.state == raster_cache::request_state::start_async
    );
    if (submitted.state == raster_cache::request_state::start_async) {
        const raster_cache::family_key family {
            .name_space = submitted.key.name_space,
            .kind = submitted.key.kind,
            .source_id = submitted.key.source_id,
            .render_scope = submitted.key.render_scope,
        };
        QVERIFY(cache_service.finish_active_request(family, submitted.key)
                    .accepted_completion);
    }

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString output_path
        = temp_dir.path() + QStringLiteral("/snapshot_with_buckets.json");

    QSignalSpy export_spy(
        &collector, &resource_monitor::snapshot_export_finished
    );
    collector.export_debug_snapshot_async(output_path);
    QTRY_COMPARE(export_spy.count(), 1);

    QFile exported_file(output_path);
    QVERIFY(exported_file.open(QIODevice::ReadOnly));
    const QJsonDocument document
        = QJsonDocument::fromJson(exported_file.readAll());
    QVERIFY(document.isObject());

    const QJsonArray cache_timeline
        = document.object().value(QStringLiteral("cache_timeline")).toArray();
    QVERIFY(!cache_timeline.isEmpty());

    const QJsonObject latest
        = cache_timeline.at(cache_timeline.size() - 1).toObject();
    QVERIFY(latest.value(QStringLiteral("unique_size_buckets")).toInt() >= 1);

    const QJsonArray size_buckets
        = latest.value(QStringLiteral("size_buckets")).toArray();
    QVERIFY(!size_buckets.isEmpty());
    QVERIFY(size_buckets.at(0).toObject().contains(
        QStringLiteral("target_bucket_px")
    ));

    const QJsonArray largest_entries
        = latest.value(QStringLiteral("largest_entries")).toArray();
    QVERIFY(!largest_entries.isEmpty());
    QVERIFY(largest_entries.at(0).toObject().contains(
        QStringLiteral("estimated_bytes")
    ));
    QVERIFY(
        largest_entries.at(0).toObject().contains(QStringLiteral("name_space"))
    );
    QVERIFY(largest_entries.at(0).toObject().contains(QStringLiteral("kind")));
    QVERIFY(
        largest_entries.at(0).toObject().contains(QStringLiteral("source_id"))
    );
    QVERIFY(largest_entries.at(0).toObject().contains(
        QStringLiteral("render_scope")
    ));

    QVERIFY(latest.contains(QStringLiteral("displayed_ready_entries")));
    QVERIFY(latest.contains(QStringLiteral("displayed_recent_entries")));
    QVERIFY(latest.contains(QStringLiteral("cached_only_ready_entries")));
    QVERIFY(latest.contains(QStringLiteral("displayed_recent_images")));
    QVERIFY(latest.contains(QStringLiteral("displayed_entry_window_ms")));
    QVERIFY(
        latest.contains(QStringLiteral("displayed_entry_coverage_percent"))
    );

    const QJsonArray requested_entries
        = latest.value(QStringLiteral("top_requested_entries")).toArray();
    QVERIFY(!requested_entries.isEmpty());
    QVERIFY(requested_entries.at(0).toObject().contains(
        QStringLiteral("name_space")
    ));
    QVERIFY(
        requested_entries.at(0).toObject().contains(QStringLiteral("kind"))
    );
    QVERIFY(requested_entries.at(0).toObject().contains(
        QStringLiteral("request_count")
    ));

    const QJsonArray expensive_tasks
        = latest.value(QStringLiteral("top_expensive_tasks")).toArray();
    QVERIFY(!expensive_tasks.isEmpty());
    QVERIFY(expensive_tasks.at(0).toObject().contains(QStringLiteral("stage")));
    QVERIFY(
        expensive_tasks.at(0).toObject().contains(QStringLiteral("name_space"))
    );
    QVERIFY(expensive_tasks.at(0).toObject().contains(QStringLiteral("kind")));
    QVERIFY(expensive_tasks.at(0).toObject().contains(
        QStringLiteral("max_elapsed_ms")
    ));

    const QJsonArray subsystem_summaries
        = latest.value(QStringLiteral("subsystem_summaries")).toArray();
    QVERIFY(!subsystem_summaries.isEmpty());
    QVERIFY(subsystem_summaries.at(0).toObject().contains(
        QStringLiteral("name_space")
    ));
    QVERIFY(
        subsystem_summaries.at(0).toObject().contains(QStringLiteral("kind"))
    );
    QVERIFY(subsystem_summaries.at(0).toObject().contains(
        QStringLiteral("ready_entries")
    ));
    QVERIFY(subsystem_summaries.at(0).toObject().contains(
        QStringLiteral("ready_bytes")
    ));
    QVERIFY(subsystem_summaries.at(0).toObject().contains(
        QStringLiteral("request_samples")
    ));
    QVERIFY(subsystem_summaries.at(0).toObject().contains(
        QStringLiteral("timing_samples")
    ));
    QVERIFY(subsystem_summaries.at(0).toObject().contains(
        QStringLiteral("timing_max_elapsed_ms")
    ));

    const QJsonArray consumer_summaries
        = latest.value(QStringLiteral("consumer_summaries")).toArray();
    QVERIFY(!consumer_summaries.isEmpty());
    QVERIFY(
        consumer_summaries.at(0).toObject().contains(QStringLiteral("consumer"))
    );
    QVERIFY(consumer_summaries.at(0).toObject().contains(
        QStringLiteral("displayed_recent_entries")
    ));
    QVERIFY(consumer_summaries.at(0).toObject().contains(
        QStringLiteral("displayed_recent_images")
    ));
    QVERIFY(consumer_summaries.at(0).toObject().contains(
        QStringLiteral("displayed_recent_ready_bytes")
    ));
    QVERIFY(consumer_summaries.at(0).toObject().contains(
        QStringLiteral("displayed_recent_widget_local_bytes_estimated")
    ));
}

void resource_monitor_tests::exports_active_debug_cadence_mode() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 8);

    collector.attach_cache_service(&cache_service);
    collector.set_debug_cadence_mode(
        resource_monitor::debug_cadence_mode::instrumented
    );
    QCOMPARE(
        collector.get_debug_cadence_mode(),
        resource_monitor::debug_cadence_mode::instrumented
    );

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString output_path
        = temp_dir.path() + QStringLiteral("/snapshot_mode.json");

    QSignalSpy export_spy(
        &collector, &resource_monitor::snapshot_export_finished
    );
    collector.export_debug_snapshot_async(output_path);
    QTRY_COMPARE(export_spy.count(), 1);

    QFile exported_file(output_path);
    QVERIFY(exported_file.open(QIODevice::ReadOnly));
    const QJsonDocument document
        = QJsonDocument::fromJson(exported_file.readAll());
    QVERIFY(document.isObject());

    QCOMPARE(
        document.object()
            .value(QStringLiteral("debug_cadence_mode"))
            .toString(),
        QStringLiteral("instrumented")
    );
}

void resource_monitor_tests::exports_process_sampling_interval_metadata() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 8);

    collector.attach_cache_service(&cache_service);
    QCOMPARE(collector.process_memory_sample_interval_ms(), 5000);

    collector.set_debug_cadence_mode(
        resource_monitor::debug_cadence_mode::instrumented
    );
    QCOMPARE(collector.process_memory_sample_interval_ms(), 1000);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString output_path
        = temp_dir.path() + QStringLiteral("/snapshot_interval.json");

    QString error_message;
    QVERIFY(collector.export_debug_snapshot_sync(output_path, &error_message));
    QVERIFY2(error_message.isEmpty(), qPrintable(error_message));

    QFile exported_file(output_path);
    QVERIFY(exported_file.open(QIODevice::ReadOnly));
    const QJsonDocument document
        = QJsonDocument::fromJson(exported_file.readAll());
    QVERIFY(document.isObject());

    const QJsonObject root = document.object();
    QCOMPARE(
        root.value(QStringLiteral("process_memory_sample_interval_ms"))
            .toInteger(),
        1000
    );
    QCOMPARE(
        root.value(
                QStringLiteral("auto_process_report_rss_growth_threshold_bytes")
        )
            .toInteger(),
        static_cast<qint64>(128 * 1024 * 1024)
    );
    QCOMPARE(
        root.value(QStringLiteral("auto_process_report_cooldown_ms"))
            .toInteger(),
        static_cast<qint64>(10 * 60 * 1000)
    );
    QVERIFY(
        root.contains(QStringLiteral("auto_process_report_baseline_rss_bytes"))
    );
    QVERIFY(root.contains(
        QStringLiteral("auto_process_report_rss_growth_since_baseline_bytes")
    ));
    QVERIFY(
        root.contains(QStringLiteral("auto_process_report_last_trigger_utc_ms"))
    );
    QVERIFY(root.contains(
        QStringLiteral("auto_process_report_cooldown_remaining_ms")
    ));
    QVERIFY(root.contains(
        QStringLiteral("auto_process_report_consecutive_growth_hits_required")
    ));
    QVERIFY(root.contains(
        QStringLiteral("auto_process_report_consecutive_growth_hits_current")
    ));
    QCOMPARE(
        root.value(QStringLiteral(
                       "auto_process_report_consecutive_growth_hits_required"
                   ))
            .toInteger(),
        static_cast<qint64>(3)
    );

    const QJsonObject semantics
        = root.value(QStringLiteral("telemetry_semantics")).toObject();
    QCOMPARE(
        semantics.value(QStringLiteral("process_memory_sample_interval_ms"))
            .toString(),
        QStringLiteral("derived_from_debug_cadence_mode")
    );
    QCOMPARE(
        semantics
            .value(
                QStringLiteral("auto_process_report_rss_growth_threshold_bytes")
            )
            .toString(),
        QStringLiteral("derived_from_debug_cadence_mode_unless_test_override")
    );
    QCOMPARE(
        semantics.value(QStringLiteral("auto_process_report_cooldown_ms"))
            .toString(),
        QStringLiteral("derived_from_debug_cadence_mode_unless_test_override")
    );
    QCOMPARE(
        semantics
            .value(QStringLiteral("auto_process_report_baseline_rss_bytes"))
            .toString(),
        QStringLiteral("collector_runtime_state")
    );
    QCOMPARE(
        semantics
            .value(QStringLiteral(
                "auto_process_report_consecutive_growth_hits_required"
            ))
            .toString(),
        QStringLiteral("derived_from_debug_cadence_mode_unless_test_override")
    );
    QCOMPARE(
        semantics
            .value(QStringLiteral(
                "auto_process_report_rss_growth_since_baseline_bytes"
            ))
            .toString(),
        QStringLiteral("collector_runtime_state")
    );
}

void resource_monitor_tests::exposes_auto_process_report_runtime_state() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 8);

    collector.attach_cache_service(&cache_service);

    const auto realistic_state
        = collector.current_auto_process_report_runtime_state();
    QCOMPARE(
        realistic_state.rss_growth_threshold_bytes,
        static_cast<qint64>(64 * 1024 * 1024)
    );
    QCOMPARE(realistic_state.cooldown_ms, static_cast<qint64>(5 * 60 * 1000));
    QCOMPARE(realistic_state.consecutive_growth_hits_required, static_cast<qint64>(2));
    QCOMPARE(realistic_state.window_ms, static_cast<qint64>(60 * 60 * 1000));
    QCOMPARE(realistic_state.window_max_exports, static_cast<qint64>(3));

    collector.set_debug_cadence_mode(
        resource_monitor::debug_cadence_mode::instrumented
    );
    const auto instrumented_state
        = collector.current_auto_process_report_runtime_state();
    QCOMPARE(
        instrumented_state.rss_growth_threshold_bytes,
        static_cast<qint64>(128 * 1024 * 1024)
    );
    QCOMPARE(
        instrumented_state.cooldown_ms,
        static_cast<qint64>(10 * 60 * 1000)
    );
    QCOMPARE(
        instrumented_state.consecutive_growth_hits_required,
        static_cast<qint64>(3)
    );
    QCOMPARE(instrumented_state.window_ms, static_cast<qint64>(60 * 60 * 1000));
    QCOMPARE(instrumented_state.window_max_exports, static_cast<qint64>(6));

    collector.set_auto_process_report_policy_for_tests(4096, 777, 4);
    const auto override_state
        = collector.current_auto_process_report_runtime_state();
    QCOMPARE(override_state.rss_growth_threshold_bytes, static_cast<qint64>(4096));
    QCOMPARE(override_state.cooldown_ms, static_cast<qint64>(777));
    QCOMPARE(override_state.consecutive_growth_hits_required, static_cast<qint64>(4));
}

void resource_monitor_tests::samples_process_memory_and_exports_it() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 8);

    collector.attach_cache_service(&cache_service);

    const qint64 rss_bytes = collector.latest_process_rss_bytes();
    QVERIFY(rss_bytes >= -1);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString output_path
        = temp_dir.path() + QStringLiteral("/snapshot_rss.json");

    QSignalSpy export_spy(
        &collector, &resource_monitor::snapshot_export_finished
    );
    collector.export_debug_snapshot_async(output_path);
    QTRY_COMPARE(export_spy.count(), 1);

    QFile exported_file(output_path);
    QVERIFY(exported_file.open(QIODevice::ReadOnly));
    const QJsonDocument document
        = QJsonDocument::fromJson(exported_file.readAll());
    QVERIFY(document.isObject());

    const QJsonObject root = document.object();
    QVERIFY(root.contains(QStringLiteral("process_memory_rss_bytes")));
    QVERIFY(root.contains(QStringLiteral("process_memory_rss_source")));
    QVERIFY(root.contains(QStringLiteral("process_memory_rss_available")));
    QVERIFY(root.contains(QStringLiteral("process_memory_sample_interval_ms")));
    QVERIFY(root.contains(QStringLiteral("auto_process_report_window_ms")));
    QVERIFY(root.contains(QStringLiteral("auto_process_report_window_max_exports")));
    QVERIFY(root.contains(QStringLiteral("auto_process_report_window_exports_used")));
    QVERIFY(root.contains(QStringLiteral("telemetry_semantics")));

    const QJsonObject semantics
        = root.value(QStringLiteral("telemetry_semantics")).toObject();
    QCOMPARE(
        semantics.value(QStringLiteral("cache_accounted_ready_bytes"))
            .toString(),
        QStringLiteral("accounted")
    );
    QCOMPARE(
        semantics.value(QStringLiteral("widget_local_display_bytes_estimated"))
            .toString(),
        QStringLiteral("estimated")
    );
    QCOMPARE(
        semantics.value(QStringLiteral("process_memory_rss_bytes")).toString(),
        QStringLiteral("measured")
    );
    QCOMPARE(
        semantics.value(QStringLiteral("process_memory_rss_source")).toString(),
        QStringLiteral("source_label")
    );

    const bool rss_available
        = root.value(QStringLiteral("process_memory_rss_available")).toBool();
    const QString rss_source
        = root.value(QStringLiteral("process_memory_rss_source")).toString();
    if (rss_available) {
        QCOMPARE(rss_source, QStringLiteral("proc_status_vm_rss"));
    } else {
        QCOMPARE(rss_source, QStringLiteral("unavailable"));
        QVERIFY(root.contains(
            QStringLiteral("process_memory_rss_unavailable_reason")
        ));
    }

    const QJsonArray cache_timeline
        = root.value(QStringLiteral("cache_timeline")).toArray();
    QVERIFY(!cache_timeline.isEmpty());

    const QJsonObject latest
        = cache_timeline.at(cache_timeline.size() - 1).toObject();
    QVERIFY(latest.contains(QStringLiteral("cache_accounted_ready_bytes")));
    QVERIFY(latest.contains(
        QStringLiteral("widget_local_rasterized_bytes_estimated")
    ));
    QVERIFY(
        latest.contains(QStringLiteral("widget_local_scaled_bytes_estimated"))
    );
    QVERIFY(
        latest.contains(QStringLiteral("widget_local_display_bytes_estimated"))
    );
    QVERIFY(latest.contains(QStringLiteral("process_memory_rss_bytes")));
}

void resource_monitor_tests::exports_process_memory_detail_report_on_demand() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 8);

    collector.attach_cache_service(&cache_service);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString output_path
        = temp_dir.path() + QStringLiteral("/process_memory_detail.json");

    QString error_message;
    QVERIFY(
        collector.export_process_memory_report_sync(output_path, &error_message)
    );
    QVERIFY2(error_message.isEmpty(), qPrintable(error_message));

    QFile exported_file(output_path);
    QVERIFY(exported_file.open(QIODevice::ReadOnly));
    const QJsonDocument document
        = QJsonDocument::fromJson(exported_file.readAll());
    QVERIFY(document.isObject());

    const QJsonObject root = document.object();
    QCOMPARE(
        root.value(QStringLiteral("report_kind")).toString(),
        QStringLiteral("process_memory_detail_on_demand")
    );
    QVERIFY(root.contains(QStringLiteral("status_bytes")));
    QVERIFY(root.contains(QStringLiteral("smaps_rollup_bytes")));
    QVERIFY(
        root.contains(QStringLiteral("collector_latest_process_rss_source"))
    );
    QVERIFY(
        root.contains(QStringLiteral("collector_latest_process_rss_available"))
    );
    QVERIFY(root.contains(
        QStringLiteral("auto_process_report_rss_growth_threshold_bytes")
    ));
    QVERIFY(root.contains(QStringLiteral("auto_process_report_cooldown_ms")));
    QVERIFY(
        root.contains(QStringLiteral("auto_process_report_baseline_rss_bytes"))
    );
    QVERIFY(root.contains(
        QStringLiteral("auto_process_report_rss_growth_since_baseline_bytes")
    ));
    QVERIFY(
        root.contains(QStringLiteral("auto_process_report_last_trigger_utc_ms"))
    );
    QVERIFY(root.contains(
        QStringLiteral("auto_process_report_cooldown_remaining_ms")
    ));
    QVERIFY(root.contains(
        QStringLiteral("auto_process_report_consecutive_growth_hits_required")
    ));
    QVERIFY(root.contains(
        QStringLiteral("auto_process_report_consecutive_growth_hits_current")
    ));
    QVERIFY(root.contains(QStringLiteral("status_bytes_available")));
    QVERIFY(root.contains(QStringLiteral("smaps_rollup_bytes_available")));

    const QJsonObject status
        = root.value(QStringLiteral("status_bytes")).toObject();
    QVERIFY(status.contains(QStringLiteral("VmRSS")));
    QVERIFY(status.contains(QStringLiteral("VmHWM")));

    const QJsonObject semantics
        = root.value(QStringLiteral("telemetry_semantics")).toObject();
    QCOMPARE(
        semantics.value(QStringLiteral("report_trigger")).toString(),
        QStringLiteral("manual_on_demand_heavy_dump")
    );
    QCOMPARE(
        semantics.value(QStringLiteral("collector_latest_process_rss_source"))
            .toString(),
        QStringLiteral("source_label")
    );

    const bool collector_rss_available
        = root.value(QStringLiteral("collector_latest_process_rss_available"))
              .toBool();
    const QString collector_rss_source
        = root.value(QStringLiteral("collector_latest_process_rss_source"))
              .toString();
    if (collector_rss_available) {
        QCOMPARE(collector_rss_source, QStringLiteral("proc_status_vm_rss"));
    } else {
        QCOMPARE(collector_rss_source, QStringLiteral("unavailable"));
        QVERIFY(root.contains(
            QStringLiteral("collector_latest_process_rss_unavailable_reason")
        ));
    }
}

void resource_monitor_tests::
    auto_exports_process_memory_detail_when_rss_growth_threshold_is_hit() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 8);
    collector.attach_cache_service(&cache_service);
    collector.set_auto_process_report_policy_for_tests(1, 0);

    QSignalSpy process_export_spy(
        &collector, &resource_monitor::process_memory_report_export_finished
    );

    cache_service.insert_or_update_result(raster_cache::result {
        .key =
            {
                .name_space = raster_cache::cache_namespace::main,
                .kind = raster_cache::resource_kind::single_svg,
                .source_id = QStringLiteral("assets/cuckoo.svg"),
                .render_scope = QStringLiteral("full"),
                .target_bucket_px = 320,
            },
        .raster_size = QSize(320, 320),
        .generation = 1,
        .timestamp_ms = 10,
        .use_count = 1,
        .single_image = QImage(320, 320, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    });

    QTRY_VERIFY(process_export_spy.count() >= 1);
    const QList<QVariant> args = process_export_spy.takeFirst();
    const QString output_path = args.at(0).toString();
    QCOMPARE(args.at(1).toBool(), true);
    QVERIFY(output_path.contains(QStringLiteral("monitor_auto_dumps/")));

    QFile exported_file(output_path);
    QVERIFY(exported_file.open(QIODevice::ReadOnly));
    const QJsonDocument document
        = QJsonDocument::fromJson(exported_file.readAll());
    QVERIFY(document.isObject());

    const QJsonObject semantics
        = document.object()
              .value(QStringLiteral("telemetry_semantics"))
              .toObject();
    QCOMPARE(
        semantics.value(QStringLiteral("report_trigger")).toString(),
        QStringLiteral("threshold_rss_growth")
    );
}

void resource_monitor_tests::auto_export_requires_configured_consecutive_growth_hits() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 8);
    collector.attach_cache_service(&cache_service);
    collector.set_auto_process_report_policy_for_tests(1, 0, 3);

    QSignalSpy process_export_spy(
        &collector, &resource_monitor::process_memory_report_export_finished
    );

    auto insert_svg_result = [&cache_service](int bucket_px, int generation) {
        cache_service.insert_or_update_result(raster_cache::result {
            .key =
                {
                    .name_space = raster_cache::cache_namespace::main,
                    .kind = raster_cache::resource_kind::single_svg,
                    .source_id = QStringLiteral("assets/cuckoo.svg"),
                    .render_scope = QStringLiteral("full"),
                    .target_bucket_px = bucket_px,
                },
            .raster_size = QSize(bucket_px, bucket_px),
            .generation = generation,
            .timestamp_ms = generation,
            .use_count = 1,
            .single_image = QImage(
                bucket_px,
                bucket_px,
                QImage::Format_ARGB32_Premultiplied
            ),
            .face_images = {},
        });
    };

    insert_svg_result(96, 1);
    QTest::qWait(50);
    QCOMPARE(process_export_spy.count(), 0);

    insert_svg_result(128, 2);
    QTest::qWait(50);
    QCOMPARE(process_export_spy.count(), 0);

    insert_svg_result(160, 3);
    QTRY_VERIFY(process_export_spy.count() >= 1);

    const QList<QVariant> args = process_export_spy.takeFirst();
    QCOMPARE(args.at(1).toBool(), true);

    QFile exported_file(args.at(0).toString());
    QVERIFY(exported_file.open(QIODevice::ReadOnly));
    const QJsonDocument document
        = QJsonDocument::fromJson(exported_file.readAll());
    QVERIFY(document.isObject());

    const QJsonObject root = document.object();
    QCOMPARE(
        root.value(
                QStringLiteral(
                    "auto_process_report_consecutive_growth_hits_required"
                )
        )
            .toInteger(),
        static_cast<qint64>(3)
    );
}

void resource_monitor_tests::auto_export_is_rate_limited_by_mode_window() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 16);
    collector.attach_cache_service(&cache_service);
    collector.set_debug_cadence_mode(
        resource_monitor::debug_cadence_mode::realistic
    );
    collector.set_auto_process_report_policy_for_tests(1, 0, 1);

    QSignalSpy process_export_spy(
        &collector, &resource_monitor::process_memory_report_export_finished
    );

    auto insert_svg_result = [&cache_service](int bucket_px, int generation) {
        cache_service.insert_or_update_result(raster_cache::result {
            .key =
                {
                    .name_space = raster_cache::cache_namespace::main,
                    .kind = raster_cache::resource_kind::single_svg,
                    .source_id = QStringLiteral("assets/cuckoo.svg"),
                    .render_scope = QStringLiteral("full"),
                    .target_bucket_px = bucket_px,
                },
            .raster_size = QSize(bucket_px, bucket_px),
            .generation = generation,
            .timestamp_ms = generation,
            .use_count = 1,
            .single_image = QImage(
                bucket_px,
                bucket_px,
                QImage::Format_ARGB32_Premultiplied
            ),
            .face_images = {},
        });
    };

    for (int index = 0; index < 3; ++index) {
        insert_svg_result(100 + index * 16, index + 1);
        QTRY_VERIFY(process_export_spy.count() >= (index + 1));
    }

    const qsizetype rate_limit_count = process_export_spy.count();
    QCOMPARE(rate_limit_count, static_cast<qsizetype>(3));

    insert_svg_result(200, 10);
    QTest::qWait(120);
    insert_svg_result(220, 11);
    QTest::qWait(120);

    QCOMPARE(process_export_spy.count(), rate_limit_count);

    const auto state = collector.current_auto_process_report_runtime_state();
    QCOMPARE(state.window_max_exports, static_cast<qint64>(3));
    QCOMPARE(state.window_exports_used, static_cast<qint64>(3));
    QCOMPARE(state.window_ms, static_cast<qint64>(60 * 60 * 1000));
}

void resource_monitor_tests::tracks_memory_class_deltas_between_snapshots() {
    raster_cache cache_service;
    resource_monitor collector(nullptr, 8);
    collector.attach_cache_service(&cache_service);

    cache_service.insert_or_update_result(raster_cache::result {
        .key =
            {
                .name_space = raster_cache::cache_namespace::main,
                .kind = raster_cache::resource_kind::single_svg,
                .source_id = QStringLiteral("assets/cuckoo.svg"),
                .render_scope = QStringLiteral("full"),
                .target_bucket_px = 200,
            },
        .raster_size = QSize(200, 200),
        .generation = 1,
        .timestamp_ms = 10,
        .use_count = 1,
        .single_image = QImage(200, 200, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    });

    const QVector<resource_monitor::cache_timeline_entry> timeline
        = collector.cache_timeline();
    QVERIFY(timeline.size() >= 2);

    const resource_monitor::cache_timeline_entry latest = timeline.constLast();
    QVERIFY(latest.cache_accounted_ready_bytes_delta > 0);
    QVERIFY(latest.widget_local_display_bytes_estimated_delta > 0);
    QVERIFY(latest.cache_entries_added_interval > 0);
    QVERIFY(latest.cache_entries_removed_interval >= 0);
    QVERIFY(latest.cache_images_added_interval > 0);
    QVERIFY(latest.cache_images_removed_interval >= 0);
    QVERIFY(latest.cache_bytes_added_interval > 0);
    QVERIFY(latest.cache_bytes_removed_interval >= 0);
    QVERIFY(latest.widget_local_display_bytes_materialized_interval > 0);
    QVERIFY(latest.widget_local_display_bytes_released_interval >= 0);
    QVERIFY(latest.process_rss_bytes_growth_interval >= 0);
    QVERIFY(latest.process_rss_bytes_drop_interval >= 0);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString output_path
        = temp_dir.path() + QStringLiteral("/snapshot_delta.json");

    QString error_message;
    QVERIFY(collector.export_debug_snapshot_sync(output_path, &error_message));
    QVERIFY2(error_message.isEmpty(), qPrintable(error_message));

    QFile exported_file(output_path);
    QVERIFY(exported_file.open(QIODevice::ReadOnly));
    const QJsonDocument document
        = QJsonDocument::fromJson(exported_file.readAll());
    QVERIFY(document.isObject());

    const QJsonArray cache_timeline
        = document.object().value(QStringLiteral("cache_timeline")).toArray();
    QVERIFY(!cache_timeline.isEmpty());

    const QJsonObject latest_json
        = cache_timeline.at(cache_timeline.size() - 1).toObject();
    QVERIFY(latest_json.contains(
        QStringLiteral("cache_accounted_ready_bytes_delta")
    ));
    QVERIFY(latest_json.contains(
        QStringLiteral("widget_local_display_bytes_estimated_delta")
    ));
    QVERIFY(latest_json.contains(
        QStringLiteral("cache_entries_added_interval")
    ));
    QVERIFY(latest_json.contains(
        QStringLiteral("cache_entries_removed_interval")
    ));
    QVERIFY(latest_json.contains(
        QStringLiteral("cache_images_added_interval")
    ));
    QVERIFY(latest_json.contains(
        QStringLiteral("cache_images_removed_interval")
    ));
    QVERIFY(
        latest_json.contains(QStringLiteral("cache_bytes_added_interval"))
    );
    QVERIFY(latest_json.contains(QStringLiteral("cache_bytes_removed_interval")));
    QVERIFY(latest_json.contains(QStringLiteral(
        "widget_local_display_bytes_materialized_interval"
    )));
    QVERIFY(latest_json.contains(QStringLiteral(
        "widget_local_display_bytes_released_interval"
    )));
    QVERIFY(
        latest_json.contains(QStringLiteral("process_memory_rss_bytes_delta"))
    );
    QVERIFY(latest_json.contains(QStringLiteral(
        "process_memory_rss_bytes_growth_interval"
    )));
    QVERIFY(latest_json.contains(QStringLiteral(
        "process_memory_rss_bytes_drop_interval"
    )));
}

void resource_monitor_tests::
    collects_geometry_snapshots_and_resize_history_from_table() {
    table table_widget;
    table_widget.set_slot_count(0);
    table_widget.resize(640, 400);

    resource_monitor collector(nullptr, 32);
    collector.attach_cache_service(table_widget.shared_raster_cache_service());
    collector.attach_table_service(&table_widget);

    QSignalSpy geometry_spy(
        &collector, &resource_monitor::geometry_snapshot_collected
    );
    QSignalSpy resize_spy(
        &collector, &resource_monitor::resize_history_recorded
    );

    QVERIFY(collector.has_geometry_snapshot());
    QVERIFY(collector.geometry_timeline_size() >= 1);
    QCOMPARE(
        collector.latest_geometry_snapshot().window_size,
        QSize(640, 400)
    );

    geometry_debug_snapshot geometry_update
        = collector.latest_geometry_snapshot();
    geometry_update.timestamp_ms += 42;
    geometry_update.window_size = QSize(960, 620);
    geometry_update.layout_size = QSize(940, 600);
    geometry_update.display_card_size = QSize(180, 260);
    geometry_update.cache_raster_size = QSize(224, 224);
    geometry_update.preloaded_raster_size = QSize();
    geometry_update.active_bucket_px = 224;
    geometry_update.warming_bucket_px = 0;
    geometry_update.active_generation_id = 3;
    geometry_update.warming_generation_id = 0;
    geometry_update.prewarm_in_flight = false;
    QVERIFY(QMetaObject::invokeMethod(
        &collector, "on_geometry_debug_snapshot_updated",
        Qt::DirectConnection, Q_ARG(geometry_debug_snapshot, geometry_update)
    ));

    QCOMPARE(
        collector.latest_geometry_snapshot().window_size,
        QSize(960, 620)
    );

    const resize_transition_debug_event transition {
        .timestamp_ms = geometry_update.timestamp_ms,
        .old_window_size = QSize(640, 400),
        .new_window_size = QSize(960, 620),
        .old_active_bucket_px = 0,
        .new_active_bucket_px = 224,
        .old_warming_bucket_px = 0,
        .new_warming_bucket_px = 0,
        .geometry_after_resize = geometry_update,
    };
    QVERIFY(QMetaObject::invokeMethod(
        &collector, "on_resize_transition_recorded", Qt::DirectConnection,
        Q_ARG(resize_transition_debug_event, transition)
    ));

    QTRY_VERIFY(resize_spy.count() >= 1);
    QVERIFY(collector.resize_history_size() >= 1);

    const QVector<resource_monitor::resize_history_entry> history
        = collector.resize_history();
    QVERIFY(!history.isEmpty());
    const auto& latest_entry = history.constLast();
    QCOMPARE(latest_entry.old_window_size, QSize(640, 400));
    QCOMPARE(latest_entry.new_window_size, QSize(960, 620));
    QVERIFY(latest_entry.prewarm_completion_ms >= 0);
    QVERIFY(!collector.resize_history_log_path().isEmpty());

    QVERIFY(geometry_spy.count() >= 1);
}

void resource_monitor_tests::exports_geometry_and_resize_history_sections() {
    table table_widget;
    table_widget.set_slot_count(0);
    table_widget.resize(700, 420);

    resource_monitor collector(nullptr, 32);
    collector.attach_cache_service(table_widget.shared_raster_cache_service());
    collector.attach_table_service(&table_widget);

    geometry_debug_snapshot geometry_update
        = collector.latest_geometry_snapshot();
    geometry_update.timestamp_ms += 77;
    geometry_update.window_size = QSize(980, 640);
    geometry_update.layout_size = QSize(960, 620);
    geometry_update.display_card_size = QSize(200, 280);
    geometry_update.cache_raster_size = QSize(240, 240);
    geometry_update.active_bucket_px = 240;
    geometry_update.warming_bucket_px = 0;
    geometry_update.prewarm_in_flight = false;
    QVERIFY(QMetaObject::invokeMethod(
        &collector, "on_geometry_debug_snapshot_updated",
        Qt::DirectConnection, Q_ARG(geometry_debug_snapshot, geometry_update)
    ));

    const resize_transition_debug_event transition {
        .timestamp_ms = geometry_update.timestamp_ms,
        .old_window_size = QSize(700, 420),
        .new_window_size = QSize(980, 640),
        .old_active_bucket_px = 0,
        .new_active_bucket_px = 240,
        .old_warming_bucket_px = 0,
        .new_warming_bucket_px = 0,
        .geometry_after_resize = geometry_update,
    };
    QVERIFY(QMetaObject::invokeMethod(
        &collector, "on_resize_transition_recorded", Qt::DirectConnection,
        Q_ARG(resize_transition_debug_event, transition)
    ));

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString output_path
        = temp_dir.path() + QStringLiteral("/snapshot_geometry_resize.json");

    QString error_message;
    QVERIFY(collector.export_debug_snapshot_sync(output_path, &error_message));
    QVERIFY2(error_message.isEmpty(), qPrintable(error_message));

    QFile exported_file(output_path);
    QVERIFY(exported_file.open(QIODevice::ReadOnly));
    const QJsonDocument document
        = QJsonDocument::fromJson(exported_file.readAll());
    QVERIFY(document.isObject());

    const QJsonObject root = document.object();
    QVERIFY(root.contains(QStringLiteral("geometry_timeline")));
    QVERIFY(root.contains(QStringLiteral("resize_history_recent")));
    QVERIFY(root.contains(QStringLiteral("geometry_timeline_size")));
    QVERIFY(root.contains(QStringLiteral("resize_history_size")));
    QVERIFY(root.contains(QStringLiteral("resize_history_log_path")));

    const QJsonArray geometry_timeline
        = root.value(QStringLiteral("geometry_timeline")).toArray();
    QVERIFY(!geometry_timeline.isEmpty());
    const QJsonObject latest_geometry
        = geometry_timeline.at(geometry_timeline.size() - 1).toObject();
    QVERIFY(latest_geometry.contains(QStringLiteral("window_size")));
    QVERIFY(latest_geometry.contains(QStringLiteral("layout_size")));
    QVERIFY(latest_geometry.contains(QStringLiteral("coverage_percent")));
    QVERIFY(latest_geometry.contains(QStringLiteral("active_bucket_px")));

    const QJsonArray resize_history
        = root.value(QStringLiteral("resize_history_recent")).toArray();
    QVERIFY(!resize_history.isEmpty());
    const QJsonObject latest_resize
        = resize_history.at(resize_history.size() - 1).toObject();
    QVERIFY(latest_resize.contains(QStringLiteral("old_window_size")));
    QVERIFY(latest_resize.contains(QStringLiteral("new_window_size")));
    QVERIFY(latest_resize.contains(
        QStringLiteral("before_process_rss_bytes_measured")
    ));
    QVERIFY(latest_resize.contains(
        QStringLiteral("after_cache_accounted_ready_bytes")
    ));

    const QJsonObject semantics
        = root.value(QStringLiteral("telemetry_semantics")).toObject();
    QCOMPARE(
        semantics.value(QStringLiteral("geometry_debug_snapshot")).toString(),
        QStringLiteral("aggregated_table_geometry_telemetry")
    );
    QCOMPARE(
        semantics.value(QStringLiteral("resize_history_log_stream")).toString(),
        QStringLiteral("append_only_jsonl")
    );
}
