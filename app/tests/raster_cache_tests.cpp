// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/raster_cache_tests.hpp"

#include "image/raster_cache.hpp"

#include <QtTest/QtTest>
#include <utility>

static raster_cache::entry_key make_entry(int bucket) {
    return raster_cache::entry_key {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = QStringLiteral("assets/cuckoo.svg"),
        .render_scope = QStringLiteral("full"),
        .target_bucket_px = bucket,
    };
}

static raster_cache::entry_key make_entry_in_namespace(
    raster_cache::cache_namespace name_space, int bucket,
    QString source = QStringLiteral("assets/cuckoo.svg")
) {
    return raster_cache::entry_key {
        .name_space = name_space,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = std::move(source),
        .render_scope = QStringLiteral("full"),
        .target_bucket_px = bucket,
    };
}

static raster_cache::request make_request(int bucket) {
    return raster_cache::request {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = QStringLiteral("assets/cuckoo.svg"),
        .render_scope = QStringLiteral("full"),
        .target_bucket_px = bucket,
    };
}

static raster_cache::family_key make_family() {
    return raster_cache::family_key {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::single_svg,
        .source_id = QStringLiteral("assets/cuckoo.svg"),
        .render_scope = QStringLiteral("full"),
    };
}

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
        = service.get_ready_with_namespace_fallback(make_entry_in_namespace(
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
        = service.get_ready_with_namespace_fallback(make_entry_in_namespace(
            raster_cache::cache_namespace::settings, 192,
            QStringLiteral("shared/theme.svg")
        ));

    QVERIFY(picked.has_value());
    QCOMPARE(picked->key.name_space, raster_cache::cache_namespace::settings);
    QCOMPARE(picked->generation, 9);
}

void raster_cache_tests::render_scope_normalizes_for_cache_hits() {
    raster_cache service;

    raster_cache::request first_req {
        .name_space = raster_cache::cache_namespace::settings,
        .kind = raster_cache::resource_kind::card_sheet_faces,
        .source_id = QStringLiteral("shared/theme.svg"),
        .render_scope = QStringLiteral("subset: face_03, face_01, face_02"),
        .target_bucket_px = 160,
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
