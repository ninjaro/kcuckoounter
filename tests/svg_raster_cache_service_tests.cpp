#include "include/svg_raster_cache_service_tests.hpp"

#include "helpers/svg_raster_cache_service.hpp"

#include <QtTest/QtTest>

namespace {

svg_raster_cache_service::entry_key make_entry(int bucket) {
    return svg_raster_cache_service::entry_key {
        .name_space = svg_raster_cache_service::cache_namespace::main,
        .kind = svg_raster_cache_service::resource_kind::single_svg,
        .source_id = QStringLiteral("assets/cuckoo.svg"),
        .render_scope = QStringLiteral("full"),
        .target_bucket_px = bucket,
    };
}

svg_raster_cache_service::entry_key make_entry_in_namespace(
    svg_raster_cache_service::cache_namespace name_space, int bucket,
    QString source = QStringLiteral("assets/cuckoo.svg")
) {
    return svg_raster_cache_service::entry_key {
        .name_space = name_space,
        .kind = svg_raster_cache_service::resource_kind::single_svg,
        .source_id = source,
        .render_scope = QStringLiteral("full"),
        .target_bucket_px = bucket,
    };
}

svg_raster_cache_service::request make_request(int bucket) {
    return svg_raster_cache_service::request {
        .name_space = svg_raster_cache_service::cache_namespace::main,
        .kind = svg_raster_cache_service::resource_kind::single_svg,
        .source_id = QStringLiteral("assets/cuckoo.svg"),
        .render_scope = QStringLiteral("full"),
        .need_short_px = bucket,
        .target_bucket_px = bucket,
        .high_priority = false,
        .interactive = true,
        .preview = false,
    };
}

svg_raster_cache_service::family_key make_family() {
    return svg_raster_cache_service::family_key {
        .name_space = svg_raster_cache_service::cache_namespace::main,
        .kind = svg_raster_cache_service::resource_kind::single_svg,
        .source_id = QStringLiteral("assets/cuckoo.svg"),
        .render_scope = QStringLiteral("full"),
    };
}

} // namespace

void svg_raster_cache_service_tests::stores_and_reads_ready_result() {
    svg_raster_cache_service service;
    QSignalSpy spy(&service, &svg_raster_cache_service::result_updated);

    svg_raster_cache_service::result stored {
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

    const std::optional<svg_raster_cache_service::result> result
        = service.get_if_ready(stored.key);
    QVERIFY(result.has_value());
    QCOMPARE(result->raster_size, QSize(224, 224));
    QCOMPARE(result->generation, 3);
}

void svg_raster_cache_service_tests::pending_entry_is_latest_per_family() {
    svg_raster_cache_service service;
    const svg_raster_cache_service::family_key family = make_family();

    service.set_pending_latest(family, make_entry(160));
    service.set_pending_latest(family, make_entry(256));

    const std::optional<svg_raster_cache_service::entry_key> pending
        = service.take_pending_latest(family);

    QVERIFY(pending.has_value());
    QCOMPARE(pending->target_bucket_px, 256);

    const std::optional<svg_raster_cache_service::entry_key> nothing_pending
        = service.take_pending_latest(family);
    QVERIFY(!nothing_pending.has_value());
}

void svg_raster_cache_service_tests::in_flight_state_tracks_family_lifecycle() {
    svg_raster_cache_service service;
    const svg_raster_cache_service::family_key family = make_family();

    QVERIFY(!service.is_in_flight(family));

    service.mark_in_flight(family, make_entry(192));
    QVERIFY(service.is_in_flight(family));
    QCOMPARE(service.in_flight_count(), 1);

    service.clear_in_flight(family);
    QVERIFY(!service.is_in_flight(family));
    QCOMPARE(service.in_flight_count(), 0);
}

void svg_raster_cache_service_tests::
    submit_request_starts_async_for_cache_miss() {
    svg_raster_cache_service service;

    const svg_raster_cache_service::submit_outcome first
        = service.submit_request(make_request(160));

    QCOMPARE(first.state, svg_raster_cache_service::request_state::start_async);
    QCOMPARE(first.key.target_bucket_px, 160);
    QVERIFY(!first.ready_result.has_value());
    QVERIFY(service.is_in_flight(make_family()));
}

void svg_raster_cache_service_tests::submit_request_hits_cache_when_ready() {
    svg_raster_cache_service service;

    svg_raster_cache_service::result stored {
        .key = make_entry(224),
        .raster_size = QSize(224, 224),
        .generation = 4,
        .timestamp_ms = 200,
        .use_count = 2,
        .single_image = QImage(224, 224, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };
    service.insert_or_update_result(stored);

    const svg_raster_cache_service::submit_outcome outcome
        = service.submit_request(make_request(224));

    QCOMPARE(outcome.state, svg_raster_cache_service::request_state::cache_hit);
    QVERIFY(outcome.ready_result.has_value());
    QCOMPARE(outcome.ready_result->generation, 4);
}

void svg_raster_cache_service_tests::
    submit_request_coalesces_latest_pending_target() {
    svg_raster_cache_service service;
    const svg_raster_cache_service::family_key family = make_family();

    const svg_raster_cache_service::submit_outcome first
        = service.submit_request(make_request(160));
    QCOMPARE(first.state, svg_raster_cache_service::request_state::start_async);

    const svg_raster_cache_service::submit_outcome same_target
        = service.submit_request(make_request(160));
    QCOMPARE(
        same_target.state,
        svg_raster_cache_service::request_state::already_in_flight
    );

    const svg_raster_cache_service::submit_outcome newer
        = service.submit_request(make_request(320));
    QCOMPARE(
        newer.state, svg_raster_cache_service::request_state::pending_coalesced
    );

    const std::optional<svg_raster_cache_service::entry_key> pending
        = service.take_pending_latest(family);
    QVERIFY(pending.has_value());
    QCOMPARE(pending->target_bucket_px, 320);
}

void svg_raster_cache_service_tests::
    finish_active_request_starts_latest_pending_entry() {
    svg_raster_cache_service service;
    const svg_raster_cache_service::family_key family = make_family();

    QCOMPARE(
        service.submit_request(make_request(160)).state,
        svg_raster_cache_service::request_state::start_async
    );
    QCOMPARE(
        service.submit_request(make_request(288)).state,
        svg_raster_cache_service::request_state::pending_coalesced
    );

    const svg_raster_cache_service::finish_outcome finish
        = service.finish_active_request(family, make_entry(160));

    QVERIFY(finish.accepted_completion);
    QVERIFY(finish.next_entry_to_start.has_value());
    QCOMPARE(finish.next_entry_to_start->target_bucket_px, 288);
    QVERIFY(service.is_in_flight(family));
}

void svg_raster_cache_service_tests::
    finish_active_request_rejects_stale_completion() {
    svg_raster_cache_service service;
    const svg_raster_cache_service::family_key family = make_family();

    QCOMPARE(
        service.submit_request(make_request(192)).state,
        svg_raster_cache_service::request_state::start_async
    );

    const svg_raster_cache_service::finish_outcome finish
        = service.finish_active_request(family, make_entry(224));

    QVERIFY(!finish.accepted_completion);
    QVERIFY(!finish.next_entry_to_start.has_value());
    QVERIFY(service.is_in_flight(family));
}

void svg_raster_cache_service_tests::namespaces_keep_separate_ready_entries() {
    svg_raster_cache_service service;

    const svg_raster_cache_service::result main_result {
        .key = make_entry_in_namespace(
            svg_raster_cache_service::cache_namespace::main, 160
        ),
        .raster_size = QSize(160, 160),
        .generation = 1,
        .timestamp_ms = 10,
        .use_count = 0,
        .single_image = QImage(160, 160, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };
    const svg_raster_cache_service::result settings_result {
        .key = make_entry_in_namespace(
            svg_raster_cache_service::cache_namespace::settings, 160
        ),
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
    QCOMPARE(
        service.ready_entry_count(
            svg_raster_cache_service::cache_namespace::main
        ),
        1
    );
    QCOMPARE(
        service.ready_entry_count(
            svg_raster_cache_service::cache_namespace::settings
        ),
        1
    );
}

void svg_raster_cache_service_tests::
    settings_namespace_evicts_oldest_entries() {
    svg_raster_cache_service service;

    for (int i = 0; i < 4; ++i) {
        const svg_raster_cache_service::result entry {
            .key = make_entry_in_namespace(
                svg_raster_cache_service::cache_namespace::settings, 128 + i,
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
        service.ready_entry_count(
            svg_raster_cache_service::cache_namespace::settings
        ),
        3
    );
    QVERIFY(!service
                 .get_if_ready(make_entry_in_namespace(
                     svg_raster_cache_service::cache_namespace::settings, 128,
                     QStringLiteral("settings/0.svg")
                 ))
                 .has_value());
    QVERIFY(service
                .get_if_ready(make_entry_in_namespace(
                    svg_raster_cache_service::cache_namespace::settings, 131,
                    QStringLiteral("settings/3.svg")
                ))
                .has_value());
}

void svg_raster_cache_service_tests::
    settings_lookup_can_fallback_to_main_namespace() {
    svg_raster_cache_service service;

    const svg_raster_cache_service::result main_result {
        .key = make_entry_in_namespace(
            svg_raster_cache_service::cache_namespace::main, 224,
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

    const std::optional<svg_raster_cache_service::result> fallback
        = service.get_if_ready_with_namespace_fallback(make_entry_in_namespace(
            svg_raster_cache_service::cache_namespace::settings, 224,
            QStringLiteral("shared/theme.svg")
        ));

    QVERIFY(fallback.has_value());
    QCOMPARE(
        fallback->key.name_space,
        svg_raster_cache_service::cache_namespace::main
    );
    QCOMPARE(fallback->generation, 7);
}

void svg_raster_cache_service_tests::
    settings_lookup_prefers_settings_when_both_ready() {
    svg_raster_cache_service service;

    const svg_raster_cache_service::result main_result {
        .key = make_entry_in_namespace(
            svg_raster_cache_service::cache_namespace::main, 192,
            QStringLiteral("shared/theme.svg")
        ),
        .raster_size = QSize(192, 192),
        .generation = 2,
        .timestamp_ms = 20,
        .use_count = 0,
        .single_image = QImage(192, 192, QImage::Format_ARGB32_Premultiplied),
        .face_images = {},
    };
    const svg_raster_cache_service::result settings_result {
        .key = make_entry_in_namespace(
            svg_raster_cache_service::cache_namespace::settings, 192,
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

    const std::optional<svg_raster_cache_service::result> picked
        = service.get_if_ready_with_namespace_fallback(make_entry_in_namespace(
            svg_raster_cache_service::cache_namespace::settings, 192,
            QStringLiteral("shared/theme.svg")
        ));

    QVERIFY(picked.has_value());
    QCOMPARE(
        picked->key.name_space,
        svg_raster_cache_service::cache_namespace::settings
    );
    QCOMPARE(picked->generation, 9);
}

void svg_raster_cache_service_tests::
    subset_render_scope_is_normalized_for_dedup_and_cache_hits() {
    svg_raster_cache_service service;

    svg_raster_cache_service::request first_req {
        .name_space = svg_raster_cache_service::cache_namespace::settings,
        .kind = svg_raster_cache_service::resource_kind::card_sheet_faces,
        .source_id = QStringLiteral("shared/theme.svg"),
        .render_scope = QStringLiteral("subset: face_03, face_01, face_02"),
        .need_short_px = 160,
        .target_bucket_px = 160,
        .high_priority = false,
        .interactive = true,
        .preview = true,
    };

    const svg_raster_cache_service::submit_outcome first
        = service.submit_request(first_req);
    QCOMPARE(first.state, svg_raster_cache_service::request_state::start_async);

    svg_raster_cache_service::request reordered_req = first_req;
    reordered_req.render_scope
        = QStringLiteral("subset:face_02, face_01, face_03, face_01");

    const svg_raster_cache_service::submit_outcome reordered
        = service.submit_request(reordered_req);
    QCOMPARE(
        reordered.state,
        svg_raster_cache_service::request_state::already_in_flight
    );

    svg_raster_cache_service::result stored {
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

    const svg_raster_cache_service::family_key family {
        .name_space = svg_raster_cache_service::cache_namespace::settings,
        .kind = svg_raster_cache_service::resource_kind::card_sheet_faces,
        .source_id = QStringLiteral("shared/theme.svg"),
        .render_scope = QStringLiteral("subset:face_01,face_02,face_03"),
    };
    const svg_raster_cache_service::finish_outcome finish
        = service.finish_active_request(family, first.key);
    QVERIFY(finish.accepted_completion);

    const svg_raster_cache_service::submit_outcome hit
        = service.submit_request(reordered_req);
    QCOMPARE(hit.state, svg_raster_cache_service::request_state::cache_hit);
    QVERIFY(hit.ready_result.has_value());
    QCOMPARE(hit.ready_result->generation, 5);
}
