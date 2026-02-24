#include "include/raster_cache_tests.hpp"

#include "image/raster_cache.hpp"

#include <QtTest/QtTest>

namespace {

raster_cache::entry_key make_entry(int bucket) {
    return raster_cache::entry_key {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = QStringLiteral("assets/cuckoo.svg"),
        .render_scope = QStringLiteral("full"),
        .target_bucket_px = bucket,
    };
}

raster_cache::entry_key make_entry_in_namespace(
    raster_cache::cache_namespace name_space, int bucket,
    QString source = QStringLiteral("assets/cuckoo.svg")
) {
    return raster_cache::entry_key {
        .name_space = name_space,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = source,
        .render_scope = QStringLiteral("full"),
        .target_bucket_px = bucket,
    };
}

raster_cache::request make_request(int bucket) {
    return raster_cache::request {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = QStringLiteral("assets/cuckoo.svg"),
        .render_scope = QStringLiteral("full"),
        .need_short_px = bucket,
        .target_bucket_px = bucket,
        .high_priority = false,
        .interactive = true,
        .preview = false,
    };
}

raster_cache::family_key make_family() {
    return raster_cache::family_key {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = QStringLiteral("assets/cuckoo.svg"),
        .render_scope = QStringLiteral("full"),
    };
}

} // namespace

void raster_cache_tests::stores_and_reads_ready_result() {
    raster_cache service;
    QSignalSpy spy(&service, &raster_cache::result_updated);

    raster_cache::result stored {
        .key = make_entry(224),
        .raster_size = QSize(224, 224),
        .generation = 3,
        .timestamp_ms = 12345,
        .use_count = 1,
        .single_image = QImage(224, 224, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };

    service.insert_or_update_result(stored);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(service.ready_entry_count(), 1);

    const std::optional<raster_cache::result> result
        = service.get_if_ready(stored.key);
    QVERIFY(result.has_value());
    QCOMPARE(result->raster_size, QSize(224, 224));
    QCOMPARE(result->generation, 3);
}

void raster_cache_tests::pending_entry_is_latest_per_family() {
    raster_cache service;
    const raster_cache::family_key family = make_family();

    service.set_pending_latest(family, make_entry(160));
    service.set_pending_latest(family, make_entry(256));

    const std::optional<raster_cache::entry_key> pending
        = service.take_pending_latest(family);

    QVERIFY(pending.has_value());
    QCOMPARE(pending->target_bucket_px, 256);

    const std::optional<raster_cache::entry_key> nothing_pending
        = service.take_pending_latest(family);
    QVERIFY(!nothing_pending.has_value());
}

void raster_cache_tests::in_flight_state_tracks_family_lifecycle() {
    raster_cache service;
    const raster_cache::family_key family = make_family();

    QVERIFY(!service.is_in_flight(family));

    service.mark_in_flight(family, make_entry(192));
    QVERIFY(service.is_in_flight(family));
    QCOMPARE(service.in_flight_count(), 1);

    service.clear_in_flight(family);
    QVERIFY(!service.is_in_flight(family));
    QCOMPARE(service.in_flight_count(), 0);
}

void raster_cache_tests::submit_request_starts_async_for_cache_miss() {
    raster_cache service;

    const raster_cache::submit_outcome first
        = service.submit_request(make_request(160));

    QCOMPARE(first.state, raster_cache::request_state::start_async);
    QCOMPARE(first.key.target_bucket_px, 160);
    QVERIFY(!first.ready_result.has_value());
    QVERIFY(service.is_in_flight(make_family()));
}

void raster_cache_tests::submit_request_hits_cache_when_ready() {
    raster_cache service;

    raster_cache::result stored {
        .key = make_entry(224),
        .raster_size = QSize(224, 224),
        .generation = 4,
        .timestamp_ms = 200,
        .use_count = 0,
        .single_image = QImage(224, 224, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };
    service.insert_or_update_result(stored);

    const raster_cache::submit_outcome outcome
        = service.submit_request(make_request(224));

    QCOMPARE(outcome.state, raster_cache::request_state::cache_hit);
    QVERIFY(outcome.ready_result.has_value());
    QCOMPARE(outcome.ready_result->generation, 4);
}

void raster_cache_tests::submit_request_coalesces_latest_pending_target() {
    raster_cache service;
    const raster_cache::family_key family = make_family();

    const raster_cache::submit_outcome first
        = service.submit_request(make_request(160));
    QCOMPARE(first.state, raster_cache::request_state::start_async);

    const raster_cache::submit_outcome same_target
        = service.submit_request(make_request(160));
    QCOMPARE(same_target.state, raster_cache::request_state::already_in_flight);

    const raster_cache::submit_outcome newer
        = service.submit_request(make_request(320));
    QCOMPARE(newer.state, raster_cache::request_state::pending_coalesced);

    const std::optional<raster_cache::entry_key> pending
        = service.take_pending_latest(family);
    QVERIFY(pending.has_value());
    QCOMPARE(pending->target_bucket_px, 320);
}

void raster_cache_tests::finish_active_request_starts_latest_pending_entry() {
    raster_cache service;
    const raster_cache::family_key family = make_family();

    QCOMPARE(
        service.submit_request(make_request(160)).state,
        raster_cache::request_state::start_async
    );
    QCOMPARE(
        service.submit_request(make_request(288)).state,
        raster_cache::request_state::pending_coalesced
    );

    const raster_cache::finish_outcome finish
        = service.finish_active_request(family, make_entry(160));

    QVERIFY(finish.accepted_completion);
    QVERIFY(finish.next_entry_to_start.has_value());
    QCOMPARE(finish.next_entry_to_start->target_bucket_px, 288);
    QVERIFY(service.is_in_flight(family));
}

void raster_cache_tests::finish_active_request_rejects_stale_completion() {
    raster_cache service;
    const raster_cache::family_key family = make_family();

    QCOMPARE(
        service.submit_request(make_request(192)).state,
        raster_cache::request_state::start_async
    );

    const raster_cache::finish_outcome finish
        = service.finish_active_request(family, make_entry(224));

    QVERIFY(!finish.accepted_completion);
    QVERIFY(!finish.next_entry_to_start.has_value());
    QVERIFY(service.is_in_flight(family));
}

void raster_cache_tests::namespaces_keep_separate_ready_entries() {
    raster_cache service;

    const raster_cache::result main_result {
        .key
        = make_entry_in_namespace(raster_cache::cache_namespace::main, 160),
        .raster_size = QSize(160, 160),
        .generation = 1,
        .timestamp_ms = 10,
        .use_count = 0,
        .single_image = QImage(160, 160, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };
    const raster_cache::result settings_result {
        .key
        = make_entry_in_namespace(raster_cache::cache_namespace::settings, 160),
        .raster_size = QSize(160, 160),
        .generation = 1,
        .timestamp_ms = 11,
        .use_count = 0,
        .single_image = QImage(160, 160, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };

    service.insert_or_update_result(main_result);
    service.insert_or_update_result(settings_result);

    QCOMPARE(service.ready_entry_count(), 2);
    QCOMPARE(service.ready_entry_count(raster_cache::cache_namespace::main), 1);
    QCOMPARE(
        service.ready_entry_count(raster_cache::cache_namespace::settings), 1
    );
}

void raster_cache_tests::settings_namespace_evicts_oldest_entries() {
    raster_cache service;

    for (int i = 0; i < 4; ++i) {
        const raster_cache::result entry {
            .key = make_entry_in_namespace(
                raster_cache::cache_namespace::settings, 128 + i,
                QStringLiteral("settings/%1.svg").arg(i)
            ),
            .raster_size = QSize(128 + i, 128 + i),
            .generation = i,
            .timestamp_ms = i,
            .use_count = 0,
            .single_image
            = QImage(128 + i, 128 + i, QImage::Format_ARGB32_Premultiplied),
            .face_images = {},
        };
        service.insert_or_update_result(entry);
    }

    QCOMPARE(
        service.ready_entry_count(raster_cache::cache_namespace::settings), 3
    );
    QVERIFY(!service
                 .get_if_ready(make_entry_in_namespace(
                     raster_cache::cache_namespace::settings, 128,
                     QStringLiteral("settings/0.svg")
                 ))
                 .has_value());
    QVERIFY(service
                .get_if_ready(make_entry_in_namespace(
                    raster_cache::cache_namespace::settings, 131,
                    QStringLiteral("settings/3.svg")
                ))
                .has_value());
}

void raster_cache_tests::settings_namespace_limit_can_be_tuned_at_runtime() {
    raster_cache service;
    service.set_namespace_entry_limit(
        raster_cache::cache_namespace::settings, 5
    );

    for (int i = 0; i < 5; ++i) {
        const raster_cache::result entry {
            .key = make_entry_in_namespace(
                raster_cache::cache_namespace::settings, 140 + i,
                QStringLiteral("settings/tuned_%1.svg").arg(i)
            ),
            .raster_size = QSize(140 + i, 140 + i),
            .generation = i,
            .timestamp_ms = i,
            .use_count = 0,
            .single_image
            = QImage(140 + i, 140 + i, QImage::Format_ARGB32_Premultiplied),
            .face_images = {},
        };
        service.insert_or_update_result(entry);
    }

    QCOMPARE(
        service.ready_entry_count(raster_cache::cache_namespace::settings), 5
    );
    QVERIFY(service
                .get_if_ready(make_entry_in_namespace(
                    raster_cache::cache_namespace::settings, 140,
                    QStringLiteral("settings/tuned_0.svg")
                ))
                .has_value());
}

void raster_cache_tests::settings_lookup_can_fallback_to_main_namespace() {
    raster_cache service;

    const raster_cache::result main_result {
        .key = make_entry_in_namespace(
            raster_cache::cache_namespace::main, 224,
            QStringLiteral("shared/theme.svg")
        ),
        .raster_size = QSize(224, 224),
        .generation = 7,
        .timestamp_ms = 77,
        .use_count = 0,
        .single_image = QImage(224, 224, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };
    service.insert_or_update_result(main_result);

    const std::optional<raster_cache::result> fallback
        = service.get_if_ready_with_namespace_fallback(make_entry_in_namespace(
            raster_cache::cache_namespace::settings, 224,
            QStringLiteral("shared/theme.svg")
        ));

    QVERIFY(fallback.has_value());
    QCOMPARE(fallback->key.name_space, raster_cache::cache_namespace::main);
    QCOMPARE(fallback->generation, 7);
}

void raster_cache_tests::settings_lookup_prefers_settings_when_both_ready() {
    raster_cache service;

    const raster_cache::result main_result {
        .key = make_entry_in_namespace(
            raster_cache::cache_namespace::main, 192,
            QStringLiteral("shared/theme.svg")
        ),
        .raster_size = QSize(192, 192),
        .generation = 2,
        .timestamp_ms = 20,
        .use_count = 0,
        .single_image = QImage(192, 192, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };
    const raster_cache::result settings_result {
        .key = make_entry_in_namespace(
            raster_cache::cache_namespace::settings, 192,
            QStringLiteral("shared/theme.svg")
        ),
        .raster_size = QSize(192, 192),
        .generation = 9,
        .timestamp_ms = 21,
        .use_count = 0,
        .single_image = QImage(192, 192, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };

    service.insert_or_update_result(main_result);
    service.insert_or_update_result(settings_result);

    const std::optional<raster_cache::result> picked
        = service.get_if_ready_with_namespace_fallback(make_entry_in_namespace(
            raster_cache::cache_namespace::settings, 192,
            QStringLiteral("shared/theme.svg")
        ));

    QVERIFY(picked.has_value());
    QCOMPARE(picked->key.name_space, raster_cache::cache_namespace::settings);
    QCOMPARE(picked->generation, 9);
}

void raster_cache_tests::
    subset_render_scope_is_normalized_for_dedup_and_cache_hits() {
    raster_cache service;

    raster_cache::request first_req {
        .name_space = raster_cache::cache_namespace::settings,
        .kind = raster_cache::resource_kind::card_sheet_faces,
        .source_id = QStringLiteral("shared/theme.svg"),
        .render_scope = QStringLiteral("subset: face_03, face_01, face_02"),
        .need_short_px = 160,
        .target_bucket_px = 160,
        .high_priority = false,
        .interactive = true,
        .preview = true,
    };

    const raster_cache::submit_outcome first
        = service.submit_request(first_req);
    QCOMPARE(first.state, raster_cache::request_state::start_async);

    raster_cache::request reordered_req = first_req;
    reordered_req.render_scope
        = QStringLiteral("subset:face_02, face_01, face_03, face_01");

    const raster_cache::submit_outcome reordered
        = service.submit_request(reordered_req);
    QCOMPARE(reordered.state, raster_cache::request_state::already_in_flight);

    raster_cache::result stored {
        .key = first.key,
        .raster_size = QSize(160, 160),
        .generation = 5,
        .timestamp_ms = 100,
        .use_count = 0,
        .single_image = QImage(),
        .face_images
        = { QImage(160, 160, QImage::Format_ARGB32_Premultiplied) },
    };
    service.insert_or_update_result(stored);

    const raster_cache::family_key family {
        .name_space = raster_cache::cache_namespace::settings,
        .kind = raster_cache::resource_kind::card_sheet_faces,
        .source_id = QStringLiteral("shared/theme.svg"),
        .render_scope = QStringLiteral("subset:face_01,face_02,face_03"),
    };
    const raster_cache::finish_outcome finish
        = service.finish_active_request(family, first.key);
    QVERIFY(finish.accepted_completion);

    const raster_cache::submit_outcome hit
        = service.submit_request(reordered_req);
    QCOMPARE(hit.state, raster_cache::request_state::cache_hit);
    QVERIFY(hit.ready_result.has_value());
    QCOMPARE(hit.ready_result->generation, 5);
}

void raster_cache_tests::debug_snapshot_tracks_stock_and_delta_counters() {
    raster_cache service;
    service.set_namespace_entry_limit(
        raster_cache::cache_namespace::settings, 1
    );

    const raster_cache::result first {
        .key = make_entry_in_namespace(
            raster_cache::cache_namespace::settings, 140,
            QStringLiteral("settings/a.svg")
        ),
        .raster_size = QSize(140, 140),
        .generation = 1,
        .timestamp_ms = 10,
        .use_count = 0,
        .single_image = QImage(140, 140, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };
    const raster_cache::result second {
        .key = make_entry_in_namespace(
            raster_cache::cache_namespace::settings, 141,
            QStringLiteral("settings/b.svg")
        ),
        .raster_size = QSize(141, 141),
        .generation = 1,
        .timestamp_ms = 11,
        .use_count = 0,
        .single_image = QImage(141, 141, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };

    service.insert_or_update_result(first);
    service.insert_or_update_result(second);

    const raster_cache::debug_snapshot snapshot = service.get_debug_snapshot();

    QCOMPARE(snapshot.ready_entries, 1);
    QCOMPARE(snapshot.ready_images, 1);
    QCOMPARE(snapshot.high_water_ready_entries, 1);
    QCOMPARE(snapshot.high_water_ready_images, 1);
    QCOMPARE(snapshot.lifetime_deltas.entries_added, 2);
    QCOMPARE(snapshot.lifetime_deltas.entries_removed, 1);
    QCOMPARE(snapshot.lifetime_deltas.images_added, 2);
    QCOMPARE(snapshot.lifetime_deltas.images_removed, 1);
    QVERIFY(snapshot.ready_bytes > 0);
    QVERIFY(snapshot.high_water_ready_bytes >= snapshot.ready_bytes);
    QVERIFY(
        snapshot.lifetime_deltas.bytes_added
        > snapshot.lifetime_deltas.bytes_removed
    );
}

void raster_cache_tests::debug_snapshot_signal_is_emitted_on_cache_updates() {
    raster_cache service;
    QSignalSpy spy(&service, &raster_cache::debug_snapshot_updated);

    const raster_cache::result entry {
        .key = make_entry(200),
        .raster_size = QSize(200, 200),
        .generation = 1,
        .timestamp_ms = 0,
        .use_count = 0,
        .single_image = QImage(200, 200, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };

    service.insert_or_update_result(entry);

    QCOMPARE(spy.count(), 1);
}

void raster_cache_tests::
    debug_snapshot_tracks_raster_and_coalesced_wait_timings() {
    raster_cache service;

    const raster_cache::request first_req {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::card_sheet_faces,
        .source_id = QStringLiteral("timing/theme.svg"),
        .render_scope = QStringLiteral("all_faces"),
        .need_short_px = 128,
        .target_bucket_px = 128,
        .high_priority = false,
        .interactive = true,
        .preview = false,
    };

    raster_cache::request pending_req = first_req;
    pending_req.target_bucket_px = 144;

    const raster_cache::submit_outcome first
        = service.submit_request(first_req);
    QCOMPARE(first.state, raster_cache::request_state::start_async);

    const raster_cache::submit_outcome pending
        = service.submit_request(pending_req);
    QCOMPARE(pending.state, raster_cache::request_state::pending_coalesced);

    const raster_cache::family_key family {
        .name_space = first.key.name_space,
        .kind = first.key.kind,
        .source_id = first.key.source_id,
        .render_scope = first.key.render_scope,
    };

    const raster_cache::finish_outcome first_finish
        = service.finish_active_request(family, first.key);
    QVERIFY(first_finish.accepted_completion);
    QVERIFY(first_finish.next_entry_to_start.has_value());

    const raster_cache::finish_outcome second_finish
        = service.finish_active_request(
            family, *first_finish.next_entry_to_start
        );
    QVERIFY(second_finish.accepted_completion);

    const raster_cache::debug_snapshot snapshot = service.get_debug_snapshot();

    QVERIFY(snapshot.raster_timing_samples >= 2);
    QVERIFY(snapshot.coalesced_wait_samples >= 1);
    QVERIFY(snapshot.raster_timing_avg_ms >= 0);
    QVERIFY(snapshot.coalesced_wait_avg_ms >= 0);
}

void raster_cache_tests::debug_snapshot_tracks_deadline_readiness_counters() {
    raster_cache service;

    const raster_cache::request req {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = QStringLiteral("deadline/theme.svg"),
        .render_scope = QStringLiteral("full"),
        .need_short_px = 160,
        .target_bucket_px = 160,
        .high_priority = true,
        .interactive = true,
        .preview = false,
    };

    const raster_cache::submit_outcome submitted = service.submit_request(req);
    QCOMPARE(submitted.state, raster_cache::request_state::start_async);

    const raster_cache::family_key family {
        .name_space = submitted.key.name_space,
        .kind = submitted.key.kind,
        .source_id = submitted.key.source_id,
        .render_scope = submitted.key.render_scope,
    };

    const raster_cache::finish_outcome finished
        = service.finish_active_request(family, submitted.key);
    QVERIFY(finished.accepted_completion);

    const raster_cache::debug_snapshot snapshot = service.get_debug_snapshot();

    QVERIFY(snapshot.deadline_readiness_samples >= 1);
    QCOMPARE(
        snapshot.deadline_readiness_samples,
        snapshot.deadline_ready_early + snapshot.deadline_ready_on_time
            + snapshot.deadline_ready_late
    );
}

void raster_cache_tests::
    debug_snapshot_includes_size_buckets_and_largest_entries() {
    raster_cache service;

    auto make_result = [](int bucket, int height_scale) {
        return raster_cache::result {
            .key = make_entry(bucket),
            .raster_size = QSize(bucket, bucket * height_scale),
            .generation = 1,
            .timestamp_ms = 0,
            .use_count = 0,
            .single_image = QImage(
                bucket, bucket * height_scale,
                QImage::Format_ARGB32_Premultiplied
            ),
            .face_images = {},
        };
    };

    service.insert_or_update_result(make_result(96, 1));
    service.insert_or_update_result(make_result(96, 2));
    service.insert_or_update_result(make_result(256, 2));
    service.insert_or_update_result(make_result(160, 1));

    const raster_cache::debug_snapshot snapshot = service.get_debug_snapshot();

    QCOMPARE(snapshot.unique_size_buckets, 3);
    QCOMPARE(snapshot.size_buckets.size(), 3);
    QCOMPARE(snapshot.size_buckets.at(0).target_bucket_px, 96);
    QCOMPARE(snapshot.size_buckets.at(0).entry_count, 2);
    QCOMPARE(snapshot.size_buckets.at(1).target_bucket_px, 160);
    QCOMPARE(snapshot.size_buckets.at(2).target_bucket_px, 256);

    QCOMPARE(snapshot.largest_entries.size(), 3);
    QVERIFY(
        snapshot.largest_entries.at(0).estimated_bytes
        >= snapshot.largest_entries.at(1).estimated_bytes
    );
    QVERIFY(
        snapshot.largest_entries.at(1).estimated_bytes
        >= snapshot.largest_entries.at(2).estimated_bytes
    );
    QCOMPARE(snapshot.largest_entries.at(0).target_bucket_px, 256);
}

void raster_cache_tests::
    debug_snapshot_includes_displayed_split_and_task_toplists() {
    raster_cache service;

    service.insert_or_update_result(
        raster_cache::result {
            .key = make_entry(128),
            .raster_size = QSize(128, 128),
            .generation = 1,
            .timestamp_ms = 0,
            .use_count = 0,
            .single_image
            = QImage(128, 128, QImage::Format_ARGB32_Premultiplied),
            .face_images = {},
        }
    );

    service.insert_or_update_result(
        raster_cache::result {
            .key = make_entry(192),
            .raster_size = QSize(192, 192),
            .generation = 1,
            .timestamp_ms = 0,
            .use_count = 0,
            .single_image
            = QImage(192, 192, QImage::Format_ARGB32_Premultiplied),
            .face_images = {},
        }
    );

    service.note_entry_displayed(make_entry(128));

    raster_cache::request requested_twice {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = QStringLiteral("top/theme.svg"),
        .render_scope = QStringLiteral("full"),
        .need_short_px = 128,
        .target_bucket_px = 128,
        .high_priority = false,
        .interactive = true,
        .preview = false,
    };

    const raster_cache::submit_outcome first
        = service.submit_request(requested_twice);
    QCOMPARE(first.state, raster_cache::request_state::start_async);

    const raster_cache::family_key first_family {
        .name_space = first.key.name_space,
        .kind = first.key.kind,
        .source_id = first.key.source_id,
        .render_scope = first.key.render_scope,
    };
    QVERIFY(service.finish_active_request(first_family, first.key)
                .accepted_completion);

    const raster_cache::submit_outcome second
        = service.submit_request(requested_twice);
    QCOMPARE(second.state, raster_cache::request_state::cache_hit);

    raster_cache::request requested_once = requested_twice;
    requested_once.target_bucket_px = 224;

    const raster_cache::submit_outcome third
        = service.submit_request(requested_once);
    QCOMPARE(third.state, raster_cache::request_state::start_async);

    const raster_cache::family_key second_family {
        .name_space = third.key.name_space,
        .kind = third.key.kind,
        .source_id = third.key.source_id,
        .render_scope = third.key.render_scope,
    };
    QVERIFY(service.finish_active_request(second_family, third.key)
                .accepted_completion);

    const raster_cache::debug_snapshot snapshot = service.get_debug_snapshot();

    QCOMPARE(snapshot.displayed_ready_entries, 1);
    QCOMPARE(snapshot.cached_only_ready_entries, 1);
    QCOMPARE(snapshot.displayed_entry_window_ms, 2000);
    QCOMPARE(snapshot.displayed_entry_coverage_percent, 50);
    QCOMPARE(
        snapshot.displayed_ready_entries + snapshot.cached_only_ready_entries,
        snapshot.ready_entries
    );

    QVERIFY(!snapshot.top_requested_entries.isEmpty());
    QCOMPARE(snapshot.top_requested_entries.at(0).request_count, 2);
    QCOMPARE(snapshot.top_requested_entries.at(0).target_bucket_px, 128);

    QVERIFY(!snapshot.top_expensive_tasks.isEmpty());
    QCOMPARE(
        snapshot.top_expensive_tasks.at(0).stage,
        raster_cache::debug_snapshot::timing_stage::raster_lifecycle
    );
    QVERIFY(snapshot.top_expensive_tasks.at(0).completed_samples >= 1);
    QVERIFY(snapshot.top_expensive_tasks.at(0).max_elapsed_ms >= 0);
}

void raster_cache_tests::interval_deltas_can_be_taken_and_reset() {
    raster_cache service;

    const raster_cache::result entry {
        .key = make_entry(188),
        .raster_size = QSize(188, 188),
        .generation = 1,
        .timestamp_ms = 0,
        .use_count = 0,
        .single_image = QImage(188, 188, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };

    service.insert_or_update_result(entry);

    const raster_cache::debug_delta_counters first
        = service.take_interval_deltas();
    QCOMPARE(first.entries_added, 1);
    QCOMPARE(first.entries_removed, 0);
    QCOMPARE(first.images_added, 1);
    QCOMPARE(first.images_removed, 0);
    QVERIFY(first.bytes_added > 0);

    const raster_cache::debug_delta_counters second
        = service.take_interval_deltas();
    QCOMPARE(second.entries_added, 0);
    QCOMPARE(second.entries_removed, 0);
    QCOMPARE(second.images_added, 0);
    QCOMPARE(second.images_removed, 0);
    QCOMPARE(second.bytes_added, 0);
    QCOMPARE(second.bytes_removed, 0);

    const raster_cache::debug_snapshot snapshot = service.get_debug_snapshot();
    QVERIFY(snapshot.snapshot_sequence >= 1);
}
