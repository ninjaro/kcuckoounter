#include "include/resource_monitor_tests.hpp"

#include "monitor/resource_monitor.hpp"

#include <QFile>
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

    cache_service.insert_or_update_result(raster_cache::result {
        .key =
            {
                .name_space = raster_cache::cache_namespace::main,
                .kind = raster_cache::resource_kind::single_svg,
                .source_id = QStringLiteral("assets/cuckoo.svg"),
                .render_scope = QStringLiteral("full"),
                .target_bucket_px = 144,
            },
        .raster_size = QSize(144, 144),
        .generation = 1,
        .timestamp_ms = 0,
        .use_count = 1,
        .single_image = QImage(144, 144, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    });

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

    QVERIFY(latest.contains(QStringLiteral("displayed_ready_entries")));
    QVERIFY(latest.contains(QStringLiteral("cached_only_ready_entries")));
    QVERIFY(latest.contains(QStringLiteral("displayed_entry_window_ms")));
    QVERIFY(
        latest.contains(QStringLiteral("displayed_entry_coverage_percent"))
    );

    const QJsonArray requested_entries
        = latest.value(QStringLiteral("top_requested_entries")).toArray();
    QVERIFY(!requested_entries.isEmpty());
    QVERIFY(requested_entries.at(0).toObject().contains(
        QStringLiteral("request_count")
    ));

    const QJsonArray expensive_tasks
        = latest.value(QStringLiteral("top_expensive_tasks")).toArray();
    QVERIFY(!expensive_tasks.isEmpty());
    QVERIFY(expensive_tasks.at(0).toObject().contains(QStringLiteral("stage")));
    QVERIFY(expensive_tasks.at(0).toObject().contains(
        QStringLiteral("max_elapsed_ms")
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
