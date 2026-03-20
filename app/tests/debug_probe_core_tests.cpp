#include "include/debug_probe_core_tests.hpp"

#include "monitor/debug_probe_core.hpp"

#include <QJsonDocument>
#include <QStringList>
#include <QtTest/QtTest>

static QJsonObject make_sample(const QString& metric_id, qint64 value) {
    QJsonObject sample;
    sample.insert(QStringLiteral("metric_id"), metric_id);
    sample.insert(QStringLiteral("value"), value);
    sample.insert(
        QStringLiteral("metric_hint"),
        debug_probe_core::protocol_metric_hint_for_id_v1(metric_id)
    );
    return sample;
}

void debug_probe_core_tests::cadence_mode_labels_are_stable() {
    QCOMPARE(
        debug_probe_core::cadence_mode_to_string(
            debug_probe_core::debug_cadence_mode::realistic
        ),
        QStringLiteral("realistic")
    );
    QCOMPARE(
        debug_probe_core::cadence_mode_to_string(
            debug_probe_core::debug_cadence_mode::instrumented
        ),
        QStringLiteral("instrumented")
    );
}

void debug_probe_core_tests::process_sample_interval_matches_cadence_mode() {
    QCOMPARE(
        debug_probe_core::process_sample_interval_ms_for_mode(
            debug_probe_core::debug_cadence_mode::realistic
        ),
        qint64(5000)
    );
    QCOMPARE(
        debug_probe_core::process_sample_interval_ms_for_mode(
            debug_probe_core::debug_cadence_mode::instrumented
        ),
        qint64(1000)
    );
}

void debug_probe_core_tests::auto_process_report_policy_matches_contract() {
    const debug_probe_core::auto_process_report_policy realistic
        = debug_probe_core::auto_process_report_policy_for_mode(
            debug_probe_core::debug_cadence_mode::realistic
        );
    QCOMPARE(realistic.rss_growth_threshold_bytes, qint64(96 * 1024 * 1024));
    QCOMPARE(realistic.cooldown_ms, qint64(8 * 60 * 1000));
    QCOMPARE(realistic.consecutive_growth_hits_required, qint64(3));
    QCOMPARE(realistic.window_ms, qint64(60 * 60 * 1000));
    QCOMPARE(realistic.window_max_exports, qint64(2));

    const debug_probe_core::auto_process_report_policy instrumented
        = debug_probe_core::auto_process_report_policy_for_mode(
            debug_probe_core::debug_cadence_mode::instrumented
        );
    QCOMPARE(
        instrumented.rss_growth_threshold_bytes, qint64(192 * 1024 * 1024)
    );
    QCOMPARE(instrumented.cooldown_ms, qint64(12 * 60 * 1000));
    QCOMPARE(instrumented.consecutive_growth_hits_required, qint64(4));
    QCOMPARE(instrumented.window_ms, qint64(60 * 60 * 1000));
    QCOMPARE(instrumented.window_max_exports, qint64(4));
}

void debug_probe_core_tests::protocol_v1_contract_freezes_core_fields() {
    QCOMPARE(
        debug_probe_core::protocol_version_string(),
        QStringLiteral("debug_telemetry.v1")
    );

    const QJsonArray message_families
        = debug_probe_core::protocol_message_families_v1();
    QCOMPARE(message_families.size(), 8);
    QCOMPARE(message_families.at(0).toString(), QStringLiteral("hello"));
    QCOMPARE(message_families.at(1).toString(), QStringLiteral("capabilities"));
    QCOMPARE(message_families.at(2).toString(), QStringLiteral("sample_batch"));
    QCOMPARE(message_families.at(3).toString(), QStringLiteral("event_batch"));
    QCOMPARE(message_families.at(4).toString(), QStringLiteral("snapshot"));
    QCOMPARE(message_families.at(5).toString(), QStringLiteral("marker"));
    QCOMPARE(message_families.at(6).toString(), QStringLiteral("warning"));
    QCOMPARE(message_families.at(7).toString(), QStringLiteral("goodbye"));

    const QJsonArray identity_fields
        = debug_probe_core::protocol_required_identity_fields_v1();
    QCOMPARE(identity_fields.size(), 7);
    QVERIFY(identity_fields.contains(QStringLiteral("app")));
    QVERIFY(identity_fields.contains(QStringLiteral("pid")));
    QVERIFY(identity_fields.contains(QStringLiteral("session")));
    QVERIFY(identity_fields.contains(QStringLiteral("build")));
    QVERIFY(identity_fields.contains(QStringLiteral("protocol_version")));
    QVERIFY(identity_fields.contains(QStringLiteral("debug_flags")));
    QVERIFY(identity_fields.contains(QStringLiteral("instrumentation_mode")));

    const QJsonArray metric_hint_fields
        = debug_probe_core::protocol_required_metric_hint_fields_v1();
    QCOMPARE(metric_hint_fields.size(), 10);
    QVERIFY(metric_hint_fields.contains(QStringLiteral("kind")));
    QVERIFY(metric_hint_fields.contains(QStringLiteral("provenance")));
    QVERIFY(metric_hint_fields.contains(QStringLiteral("unit")));
    QVERIFY(metric_hint_fields.contains(QStringLiteral("scope")));
    QVERIFY(
        metric_hint_fields.contains(QStringLiteral("cardinality_semantics"))
    );
    QVERIFY(metric_hint_fields.contains(QStringLiteral("stability")));
    QVERIFY(metric_hint_fields.contains(QStringLiteral("additive_semantics")));
    QVERIFY(metric_hint_fields.contains(QStringLiteral("confidence")));
    QVERIFY(
        metric_hint_fields.contains(QStringLiteral("default_display_role"))
    );
    QVERIFY(metric_hint_fields.contains(QStringLiteral("domain_namespace")));

    const QJsonArray domain_hints
        = debug_probe_core::protocol_card_image_domain_hint_fields_v1();
    QVERIFY(domain_hints.contains(QStringLiteral("active_generation_id")));
    QVERIFY(domain_hints.contains(QStringLiteral("warming_generation_id")));
    QVERIFY(domain_hints.contains(QStringLiteral("resize_prewarm_timing")));
    QVERIFY(domain_hints.contains(QStringLiteral("theme_switch_marker")));
}

void debug_probe_core_tests::protocol_v1_capabilities_expose_metric_catalog() {
    const QJsonObject capabilities
        = debug_probe_core::protocol_capabilities_v1();
    QVERIFY(capabilities.contains(QStringLiteral("domains")));
    QVERIFY(capabilities.contains(QStringLiteral("metric_catalog")));
    QVERIFY(
        capabilities.contains(QStringLiteral("required_metric_hint_fields"))
    );
    QVERIFY(
        capabilities.contains(QStringLiteral("card_image_domain_hint_fields"))
    );

    const QJsonArray metric_catalog
        = capabilities.value(QStringLiteral("metric_catalog")).toArray();
    QVERIFY(metric_catalog.size() >= 6);

    const QJsonObject first_metric = metric_catalog.at(0).toObject();
    QVERIFY(first_metric.contains(QStringLiteral("id")));
    QVERIFY(first_metric.contains(QStringLiteral("kind")));
    QVERIFY(first_metric.contains(QStringLiteral("provenance")));
    QVERIFY(first_metric.contains(QStringLiteral("unit")));
    QVERIFY(first_metric.contains(QStringLiteral("scope")));
    QVERIFY(first_metric.contains(QStringLiteral("cardinality_semantics")));
    QVERIFY(first_metric.contains(QStringLiteral("stability")));
    QVERIFY(first_metric.contains(QStringLiteral("additive_semantics")));
    QVERIFY(first_metric.contains(QStringLiteral("confidence")));
    QVERIFY(first_metric.contains(QStringLiteral("default_display_role")));
    QVERIFY(first_metric.contains(QStringLiteral("domain_namespace")));
}

void debug_probe_core_tests::
    protocol_metric_hint_lookup_returns_catalog_entry() {
    const QJsonObject hint = debug_probe_core::protocol_metric_hint_for_id_v1(
        QStringLiteral("cache_accounted_ready_bytes")
    );
    QVERIFY(!hint.isEmpty());
    QCOMPARE(
        hint.value(QStringLiteral("id")).toString(),
        QStringLiteral("cache_accounted_ready_bytes")
    );
    QCOMPARE(
        hint.value(QStringLiteral("provenance")).toString(),
        QStringLiteral("accounted")
    );
    QVERIFY(
        debug_probe_core::protocol_metric_hint_for_id_v1(
            QStringLiteral("bogus")
        )
            .isEmpty()
    );
}

void debug_probe_core_tests::
    metric_point_helpers_extract_and_merge_primary_metrics() {
    QJsonArray samples;

    samples.push_back(
        make_sample(QStringLiteral("cache_accounted_ready_bytes"), 10)
    );
    samples.push_back(
        make_sample(QStringLiteral("widget_local_display_bytes_estimated"), 11)
    );
    samples.push_back(
        make_sample(QStringLiteral("process_memory_rss_bytes"), 12)
    );

    const debug_probe_core::metric_point_v1 sample_point
        = debug_probe_core::metric_point_from_sample_batch_v1(samples);
    QCOMPARE(sample_point.cache_accounted_ready_bytes, qint64(10));
    QCOMPARE(sample_point.widget_local_display_bytes_estimated, qint64(11));
    QCOMPARE(sample_point.process_memory_rss_bytes, qint64(12));
    QCOMPARE(sample_point.measured_accounted_gap_bytes_derived, qint64(-1));

    QJsonObject snapshot;
    snapshot.insert(QStringLiteral("cache_accounted_ready_bytes"), 100);
    snapshot.insert(QStringLiteral("process_memory_rss_bytes"), 150);
    const debug_probe_core::metric_point_v1 snapshot_point
        = debug_probe_core::metric_point_from_snapshot_payload_v1(snapshot);
    QCOMPARE(snapshot_point.cache_accounted_ready_bytes, qint64(100));
    QCOMPARE(snapshot_point.process_memory_rss_bytes, qint64(150));
    QCOMPARE(snapshot_point.measured_accounted_gap_bytes_derived, qint64(50));

    debug_probe_core::metric_point_v1 merged = sample_point;
    debug_probe_core::merge_metric_point_v1(&merged, snapshot_point);
    QCOMPARE(merged.cache_accounted_ready_bytes, qint64(100));
    QCOMPARE(merged.widget_local_display_bytes_estimated, qint64(11));
    QCOMPARE(merged.process_memory_rss_bytes, qint64(150));
    QCOMPARE(merged.measured_accounted_gap_bytes_derived, qint64(50));
}

void debug_probe_core_tests::
    protocol_v1_message_builder_wraps_payload_and_identity() {
    const debug_probe_core::protocol_identity identity {
        .app_name = QStringLiteral("cppr"),
        .process_id = 101,
        .session_id = QStringLiteral("session-x"),
        .build_id = QStringLiteral("build-dev"),
        .protocol_version = QStringLiteral("debug_telemetry.v1"),
        .debug_flags = QStringList() << QStringLiteral("debug_build"),
        .instrumentation_mode = QStringLiteral("realistic"),
    };

    QJsonObject payload;
    payload.insert(
        QStringLiteral("payload_key"), QStringLiteral("payload_value")
    );

    const QJsonObject message = debug_probe_core::build_protocol_message_v1(
        QStringLiteral("sample_batch"), identity, 9876, payload
    );

    QCOMPARE(
        message.value(QStringLiteral("monotonic_timestamp_ms")).toInteger(),
        qint64(9876)
    );
    QCOMPARE(
        message.value(QStringLiteral("payload_key")).toString(),
        QStringLiteral("payload_value")
    );

    const QJsonObject protocol
        = message.value(QStringLiteral("protocol_v1")).toObject();
    QCOMPARE(
        protocol.value(QStringLiteral("message_family")).toString(),
        QStringLiteral("sample_batch")
    );
    const QJsonObject protocol_identity
        = protocol.value(QStringLiteral("identity")).toObject();
    QCOMPARE(
        protocol_identity.value(QStringLiteral("app")).toString(),
        QStringLiteral("cppr")
    );
    QCOMPARE(
        protocol_identity.value(QStringLiteral("pid")).toInteger(), qint64(101)
    );
}

void debug_probe_core_tests::telemetry_semantics_maps_expose_required_labels() {
    const QJsonObject snapshot_semantics
        = debug_probe_core::snapshot_telemetry_semantics();
    QCOMPARE(
        snapshot_semantics.value(QStringLiteral("cache_accounted_ready_bytes"))
            .toString(),
        QStringLiteral("accounted")
    );
    QCOMPARE(
        snapshot_semantics
            .value(QStringLiteral("widget_local_display_bytes_estimated"))
            .toString(),
        QStringLiteral("estimated")
    );
    QCOMPARE(
        snapshot_semantics.value(QStringLiteral("process_memory_rss_bytes"))
            .toString(),
        QStringLiteral("measured")
    );

    const QJsonObject process_semantics
        = debug_probe_core::process_memory_report_telemetry_semantics();
    QCOMPARE(
        process_semantics
            .value(QStringLiteral("collector_latest_process_rss_bytes"))
            .toString(),
        QStringLiteral("lightweight_sampled")
    );
    QCOMPARE(
        process_semantics.value(QStringLiteral("status_bytes")).toString(),
        QStringLiteral("measured_proc_status")
    );
    QCOMPARE(
        process_semantics.value(QStringLiteral("smaps_rollup_bytes"))
            .toString(),
        QStringLiteral("measured_proc_smaps_rollup_on_demand")
    );
}

void debug_probe_core_tests::snapshot_export_json_contains_expected_sections() {
    debug_probe_core::export_request_metadata metadata;
    metadata.collector_sequence = 77;
    metadata.cache_timeline_size = 1;
    metadata.event_timeline_size = 1;
    metadata.geometry_timeline_size = 1;
    metadata.resize_history_size = 1;
    metadata.latest_process_rss_bytes = 1234;
    metadata.process_memory_source = QStringLiteral("proc_status_vm_rss");
    metadata.protocol_app_name = QStringLiteral("cppr");
    metadata.protocol_process_id = 4321;
    metadata.protocol_session_id = QStringLiteral("session-1");
    metadata.protocol_build_id = QStringLiteral("build-dev");
    metadata.protocol_version = QStringLiteral("debug_telemetry.v1");
    metadata.protocol_debug_flags = QStringList()
        << QStringLiteral("debug_build");
    metadata.protocol_instrumentation_mode = QStringLiteral("realistic");

    debug_probe_core::cache_timeline_entry cache_entry;
    cache_entry.collector_sequence = 77;
    cache_entry.cache_snapshot.snapshot_sequence = 9;
    cache_entry.cache_snapshot.ready_entries = 3;
    cache_entry.cache_snapshot.ready_bytes = 2048;
    cache_entry.cache_snapshot.ready_images = 5;
    cache_entry.cache_snapshot.displayed_ready_entries = 2;
    cache_entry.cache_snapshot.cached_only_ready_entries = 1;
    cache_entry.cache_snapshot.displayed_ready_images = 2;
    cache_entry.cache_snapshot.cached_only_ready_images = 3;
    cache_entry.process_rss_bytes = 4096;

    debug_probe_core::event_timeline_entry event_entry;
    event_entry.collector_sequence = 77;
    event_entry.kind
        = debug_probe_core::event_timeline_entry::event_kind::manual_marker;
    event_entry.timestamp_ms = 1000;
    event_entry.label = QStringLiteral("marker_a");

    geometry_debug_snapshot geometry_entry;
    geometry_entry.timestamp_ms = 2000;
    geometry_entry.slot_count = 4;
    geometry_entry.visible_slot_count = 2;
    geometry_entry.window_size = QSize(1280, 720);
    geometry_entry.layout_size = QSize(1200, 640);
    geometry_entry.display_card_size = QSize(120, 180);

    debug_probe_core::resize_history_entry resize_entry;
    resize_entry.collector_sequence = 77;
    resize_entry.transition_start_timestamp_ms = 2100;
    resize_entry.transition_end_timestamp_ms = 2200;
    resize_entry.prewarm_completion_ms = 42;
    resize_entry.old_window_size = QSize(1000, 700);
    resize_entry.new_window_size = QSize(1280, 720);
    resize_entry.geometry_after_resize = geometry_entry;

    const QJsonObject json = debug_probe_core::build_snapshot_export_json(
        metadata,
        QVector<debug_probe_core::cache_timeline_entry> { cache_entry },
        QVector<debug_probe_core::event_timeline_entry> { event_entry },
        QVector<geometry_debug_snapshot> { geometry_entry },
        QVector<debug_probe_core::resize_history_entry> { resize_entry },
        debug_probe_core::debug_cadence_mode::realistic
    );

    QCOMPARE(
        json.value(QStringLiteral("collector_sequence")).toInteger(), qint64(77)
    );
    QCOMPARE(
        json.value(QStringLiteral("debug_cadence_mode")).toString(),
        QStringLiteral("realistic")
    );
    QCOMPARE(json.value(QStringLiteral("cache_timeline")).toArray().size(), 1);
    QCOMPARE(json.value(QStringLiteral("event_timeline")).toArray().size(), 1);
    QCOMPARE(
        json.value(QStringLiteral("geometry_timeline")).toArray().size(), 1
    );
    QCOMPARE(
        json.value(QStringLiteral("resize_history_recent")).toArray().size(), 1
    );
    QVERIFY(json.value(QStringLiteral("telemetry_semantics")).isObject());
    QVERIFY(json.value(QStringLiteral("protocol_v1")).isObject());

    const QJsonObject protocol
        = json.value(QStringLiteral("protocol_v1")).toObject();
    QCOMPARE(
        protocol.value(QStringLiteral("message_family")).toString(),
        QStringLiteral("snapshot")
    );
    QCOMPARE(
        protocol.value(QStringLiteral("version")).toString(),
        QStringLiteral("debug_telemetry.v1")
    );
    const QJsonObject identity
        = protocol.value(QStringLiteral("identity")).toObject();
    QCOMPARE(
        identity.value(QStringLiteral("app")).toString(), QStringLiteral("cppr")
    );
    QCOMPARE(identity.value(QStringLiteral("pid")).toInteger(), qint64(4321));
    QCOMPARE(
        identity.value(QStringLiteral("session")).toString(),
        QStringLiteral("session-1")
    );

    const QJsonObject event_json = json.value(QStringLiteral("event_timeline"))
                                       .toArray()
                                       .at(0)
                                       .toObject();
    QCOMPARE(
        event_json.value(QStringLiteral("kind")).toString(),
        QStringLiteral("manual_marker")
    );

    const QJsonObject resize_json
        = json.value(QStringLiteral("resize_history_recent"))
              .toArray()
              .at(0)
              .toObject();
    QVERIFY(
        resize_json.value(QStringLiteral("geometry_after_resize")).isObject()
    );

    const QString jsonl
        = debug_probe_core::resize_history_entry_to_jsonl_line(resize_entry);
    const QJsonDocument jsonl_doc = QJsonDocument::fromJson(jsonl.toUtf8());
    QVERIFY(!jsonl_doc.isNull());
    QCOMPARE(
        jsonl_doc.object()
            .value(QStringLiteral("transition_end_timestamp_ms"))
            .toInteger(),
        qint64(2200)
    );
}

void debug_probe_core_tests::
    process_memory_report_json_contains_expected_sections() {
    debug_probe_core::process_memory_report_inputs inputs;
    inputs.cadence_mode = debug_probe_core::debug_cadence_mode::instrumented;
    inputs.captured_at_utc_ms = 987654321;
    inputs.process_memory_sample_interval_ms = 1000;
    inputs.auto_process_report_rss_growth_threshold_bytes = 123;
    inputs.auto_process_report_cooldown_ms = 456;
    inputs.auto_process_report_baseline_rss_bytes = 789;
    inputs.auto_process_report_rss_growth_since_baseline_bytes = 42;
    inputs.auto_process_report_last_trigger_utc_ms = 22;
    inputs.auto_process_report_cooldown_remaining_ms = 11;
    inputs.auto_process_report_consecutive_growth_hits_required = 4;
    inputs.auto_process_report_consecutive_growth_hits_current = 2;
    inputs.latest_process_rss_bytes = 4096;
    inputs.latest_process_rss_source = QStringLiteral("proc_status_vm_rss");
    inputs.report_trigger_label = QStringLiteral("manual_on_demand_heavy_dump");
    inputs.status_vm_rss_bytes = 100;
    inputs.status_vm_hwm_bytes = 200;
    inputs.status_vm_size_bytes = 300;
    inputs.status_vm_swap_bytes = 400;
    inputs.status_bytes_available = true;
    inputs.smaps_rollup_bytes.insert(QStringLiteral("Rss"), 111);
    inputs.smaps_rollup_bytes_available = true;
    inputs.protocol_app_name = QStringLiteral("cppr");
    inputs.protocol_process_id = 123;
    inputs.protocol_session_id = QStringLiteral("session-xyz");
    inputs.protocol_build_id = QStringLiteral("build-dev");
    inputs.protocol_version = QStringLiteral("debug_telemetry.v1");
    inputs.protocol_debug_flags = QStringList()
        << QStringLiteral("debug_build");
    inputs.protocol_instrumentation_mode = QStringLiteral("instrumented");

    const QJsonObject json
        = debug_probe_core::build_process_memory_report_json(inputs);
    QCOMPARE(
        json.value(QStringLiteral("debug_cadence_mode")).toString(),
        QStringLiteral("instrumented")
    );
    QCOMPARE(
        json.value(QStringLiteral("collector_latest_process_rss_bytes"))
            .toInteger(),
        qint64(4096)
    );
    QCOMPARE(
        json.value(QStringLiteral("status_bytes"))
            .toObject()
            .value(QStringLiteral("VmRSS"))
            .toInteger(),
        qint64(100)
    );
    QCOMPARE(
        json.value(QStringLiteral("smaps_rollup_bytes"))
            .toObject()
            .value(QStringLiteral("Rss"))
            .toInteger(),
        qint64(111)
    );
    QCOMPARE(
        json.value(QStringLiteral("telemetry_semantics"))
            .toObject()
            .value(QStringLiteral("report_trigger"))
            .toString(),
        QStringLiteral("manual_on_demand_heavy_dump")
    );
    QVERIFY(json.value(QStringLiteral("protocol_v1")).isObject());
    const QJsonObject protocol
        = json.value(QStringLiteral("protocol_v1")).toObject();
    QCOMPARE(
        protocol.value(QStringLiteral("version")).toString(),
        QStringLiteral("debug_telemetry.v1")
    );
    QCOMPARE(
        protocol.value(QStringLiteral("identity"))
            .toObject()
            .value(QStringLiteral("pid"))
            .toInteger(),
        qint64(123)
    );
}
