// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/preview_carousel_tests.hpp"
#include "image_lifetime_fixture.hpp"

#include "card_helpers/card_sheet.hpp"
#include "image/card_preview_carousel.hpp"
#include "table/settings_template.hpp"
#include "table/settings_template_support.hpp"

#include <QtTest/QtTest>

#include <QScopeGuard>
#include <QSet>
#include <functional>
#include <memory>

static QPixmap test_card_pixmap(const QSize& size) {
    QPixmap pixmap(size);
    pixmap.fill(Qt::blue);
    return pixmap;
}

static QToolButton*
carousel_button(card_preview_carousel& carousel, Qt::ArrowType arrow) {
    const auto buttons = carousel.findChildren<QToolButton*>();
    for (auto* button : buttons) {
        if (button != nullptr && button->arrowType() == arrow) {
            return button;
        }
    }
    return nullptr;
}

static QPixmap provide_test_card_pixmap(
    QSet<int>* requested_indices, int index, const QSize& size
) {
    if (requested_indices != nullptr) {
        requested_indices->insert(index);
    }
    return test_card_pixmap(size);
}

void preview_carousel_tests::default_prefetch_loads_adjacent_cards() {
    card_preview_carousel carousel;
    carousel.resize(640, 200);
    carousel.set_visible_count(3);
    carousel.set_card_size(QSize(64, 96));

    QSet<int> requested_indices;
    carousel.set_card_provider(
        8, std::bind_front(provide_test_card_pixmap, &requested_indices)
    );

    QVERIFY(requested_indices.contains(0));
    QVERIFY(requested_indices.contains(1));
    QVERIFY(requested_indices.contains(2));
    QVERIFY(requested_indices.contains(7));
    QVERIFY(requested_indices.contains(3));
}

void preview_carousel_tests::disabled_prefetch_only_loads_visible_cards() {
    card_preview_carousel carousel;
    carousel.resize(640, 200);
    carousel.set_visible_count(3);
    carousel.set_prefetch_adjacent_cards(false);
    carousel.set_card_size(QSize(64, 96));

    QSet<int> requested_indices;
    carousel.set_card_provider(
        8, std::bind_front(provide_test_card_pixmap, &requested_indices)
    );

    QCOMPARE(requested_indices.size(), 3);
    QVERIFY(requested_indices.contains(0));
    QVERIFY(requested_indices.contains(1));
    QVERIFY(requested_indices.contains(2));

    QToolButton* next_button = carousel_button(carousel, Qt::RightArrow);
    QVERIFY(next_button != nullptr);
    QVERIFY(next_button->isEnabled());
    next_button->click();

    QVERIFY(requested_indices.contains(3));
    QCOMPARE(requested_indices.size(), 4);
}

void preview_carousel_tests::navigation_controls_expose_accessible_semantics() {
    card_preview_carousel carousel;

    QToolButton* previous_button = carousel_button(carousel, Qt::LeftArrow);
    QToolButton* next_button = carousel_button(carousel, Qt::RightArrow);
    QVERIFY(previous_button != nullptr);
    QVERIFY(next_button != nullptr);
    QCOMPARE(
        previous_button->accessibleName(), QStringLiteral("Previous cards")
    );
    QCOMPARE(next_button->accessibleName(), QStringLiteral("Next cards"));
    QVERIFY(!previous_button->accessibleDescription().isEmpty());
    QVERIFY(!next_button->accessibleDescription().isEmpty());
    QVERIFY(!previous_button->toolTip().isEmpty());
    QVERIFY(!next_button->toolTip().isEmpty());
}

void preview_carousel_tests::visible_cards_expose_domain_accessible_names() {
    card_preview_carousel carousel;
    carousel.resize(640, 200);
    carousel.set_visible_count(3);
    carousel.set_card_size(QSize(64, 96));
    carousel.set_card_accessible_name_provider([](int index) {
        return QStringLiteral("Semantic card %1, count value -1")
            .arg(index + 1);
    });
    carousel.set_card_provider(4, [](int, const QSize& size) {
        return test_card_pixmap(size);
    });

    const auto labels = carousel.findChildren<QLabel*>();
    QVERIFY(labels.size() >= 3);
    int semantic_label_count = 0;
    for (const QLabel* label : labels) {
        if (label != nullptr
            && label->accessibleName().startsWith(QStringLiteral("Semantic card"
            ))) {
            ++semantic_label_count;
            QVERIFY(label->accessibleName().contains(
                QStringLiteral("count value -1")
            ));
            QVERIFY(!label->accessibleDescription().isEmpty());
        }
    }
    QCOMPARE(semantic_label_count, 3);
}

void preview_carousel_tests::settings_previews_share_one_queued_render_job() {
    const QString original_source = card_sheet_source_path();
    const auto restore_source = qScopeGuard([&original_source] {
        set_card_sheet_source_path(original_source);
    });
    set_card_sheet_source_path(
        QStringLiteral(":/kcuckoounter/sets/cards/cards_4.svg")
    );
    settings_template_widget editor(settings_tab_kind::appearance);
    editor.prune_preview_queue();
    const auto requests = editor.pending_preview_queue;
    QVERIFY(requests.size() >= 2);
    for (const auto& key : requests) {
        QCOMPARE(key.source_id, requests.front().source_id);
        QCOMPARE(key.target_bucket_px, requests.front().target_bucket_px);
        QVERIFY(editor.preview_key_relevant(key));
    }

    editor.render_next_preview();
    editor.preview_watcher.future().waitForFinished();
    editor.preview_render_finished();
    for (const auto& key : requests) {
        const auto result
            = settings_template_support::preview_cache().get_if_ready(key);
        QVERIFY2(result.has_value(), qPrintable(key.render_scope));
        QVERIFY(!result->face_images.front().isNull());
        const auto parsed
            = settings_template_support::parse_preview_scope(key.render_scope);
        QCOMPARE(
            result->face_images.front(),
            rasterize_card_face_with_fallback(
                key.source_id, parsed.element_id,
                QSize(key.target_bucket_px, key.target_bucket_px)
            )
        );
    }
}

// Keep producer completion under test control, without running GUI callbacks
// inside a worker or relying on scheduling delays. Cache admission and
// publication still use the actual settings widget and its real watcher
// connections.
void preview_carousel_tests::hold_preview_batch(
    settings_template_widget& editor, QPromise<QImage>& promise
) {
    editor.prune_preview_queue();
    editor.active_render_keys = editor.pending_preview_queue;
    editor.pending_preview_queue.clear();
    editor.pending_preview_keys.clear();
    promise.start();
    editor.preview_watcher.setFuture(promise.future());
}

static raster_cache::family_key
preview_family(const raster_cache::entry_key& key) {
    return {
        .name_space = key.name_space,
        .kind = key.kind,
        .source_id = key.source_id,
        .render_scope = key.render_scope,
    };
}

void preview_carousel_tests::
    settings_preview_results_publish_before_batch_finishes() {
    settings_template_widget editor(settings_tab_kind::appearance);
    QPromise<QImage> promise;
    hold_preview_batch(editor, promise);
    const auto requests = editor.active_render_keys;
    QVERIFY(requests.size() >= 3);
    auto& cache = settings_template_support::preview_cache();
    const QImage face = test_card_pixmap(QSize(32, 32)).toImage();
    int first_publications = 0;
    QObject::connect(
        &cache, &raster_cache::result_updated, &editor,
        [&first_publications, &requests](const auto& key) {
            if (key == requests.front()) {
                ++first_publications;
            }
        }
    );

    QVERIFY(promise.addResult(face));
    QTRY_VERIFY(cache.get_if_ready(requests.front()).has_value());
    QCOMPARE(first_publications, 1);
    QVERIFY(!editor.preview_watcher.isFinished());
    QVERIFY(!cache.get_if_ready(requests.at(1)).has_value());
    // A failed face retains its result index rather than shifting later faces.
    QVERIFY(promise.addResult(QImage()));
    QVERIFY(promise.addResult(face));
    promise.finish();
    QTRY_VERIFY(editor.active_render_keys.isEmpty());
    QCOMPARE(first_publications, 1);
    QVERIFY(!cache.get_if_ready(requests.at(1)).has_value());
    const auto third = cache.get_if_ready(requests.at(2));
    QVERIFY(third.has_value());
    QCOMPARE(third->face_images.front(), face);
    for (const auto& key : requests) {
        QVERIFY(!cache.is_in_flight(preview_family(key)));
    }
}

void preview_carousel_tests::settings_preview_cutover_rejects_late_results() {
    settings_template_widget editor(settings_tab_kind::appearance);
    QPromise<QImage> promise;
    hold_preview_batch(editor, promise);
    const auto requests = editor.active_render_keys;
    QVERIFY(requests.size() >= 3);
    const QImage face = test_card_pixmap(QSize(32, 32)).toImage();
    auto& cache = settings_template_support::preview_cache();
    QVERIFY(promise.addResult(face));
    QTRY_VERIFY(cache.get_if_ready(requests.front()).has_value());
    // Complete the remaining faces before cutover, withholding GUI delivery.
    for (qsizetype index = 1; index < requests.size(); ++index) {
        QVERIFY(promise.addResult(face));
    }
    editor.warm_preview_generation(
        editor.active_preview_source, editor.active_preview_bucket + 1
    );
    editor.warming_preview_element_ids = editor.active_preview_element_ids;
    const auto warming_elements = editor.warming_preview_element_ids;
    for (const auto& id : warming_elements) {
        cache.insert_or_update_result({
            .key = editor.preview_key(
                editor.warming_preview_source, editor.warming_preview_bucket,
                editor.warming_preview_generation, id
            ),
            .raster_size = QSize(32, 32),
            .generation = static_cast<int>(editor.warming_preview_generation),
            .timestamp_ms = 0,
            .use_count = 0,
            .single_image = {},
            .face_images = { face },
        });
    }
    QVERIFY(editor.warming_preview_generation == 0); // cache signal cutover
    QVERIFY(editor.preview_watcher.isCanceled());
    promise.finish();
    QTRY_VERIFY(editor.active_render_keys.isEmpty());
    for (const auto& key : requests) {
        QVERIFY(!cache.get_if_ready(key).has_value());
        QVERIFY(!cache.is_in_flight(preview_family(key)));
    }
    // The accepted generation is usable after stale completion and retirement.
    const auto current = editor.preview_face(
        0, 0, QSize(editor.active_preview_bucket, editor.active_preview_bucket),
        editor.displayed_theme_entries
    );
    QVERIFY(current.has_value());
    QCOMPARE(*current, face);
}

void preview_carousel_tests::settings_preview_pruning_releases_requests() {
    auto& cache = settings_template_support::preview_cache();
    settings_template_widget editor(settings_tab_kind::appearance);
    const auto requests = editor.pending_preview_queue;
    QVERIFY(!requests.isEmpty());
    for (const auto& key : requests) {
        QVERIFY(cache.is_in_flight(preview_family(key)));
    }
    editor.active_preview_element_ids.clear();
    editor.prune_preview_queue();
    QVERIFY(editor.pending_preview_queue.isEmpty());
    for (const auto& key : requests) {
        QVERIFY(!cache.is_in_flight(preview_family(key)));
    }
    // An abandoned request must be admissible again, rather than stuck
    // in-flight.
    const auto& key = requests.front();
    const auto outcome = cache.submit_request({
        .name_space = key.name_space,
        .kind = key.kind,
        .source_id = key.source_id,
        .render_scope = key.render_scope,
        .target_bucket_px = key.target_bucket_px,
    });
    QCOMPARE(outcome.state, raster_cache::request_state::start_async);
    editor.finish_preview_request(key);
    QVERIFY(!cache.is_in_flight(preview_family(key)));
}

void preview_carousel_tests::settings_preview_destruction_cancels_batch() {
    auto editor = std::make_unique<settings_template_widget>(
        settings_tab_kind::appearance
    );
    QPromise<QImage> promise;
    hold_preview_batch(*editor, promise);
    const auto requests = editor->active_render_keys;
    QVERIFY(!requests.isEmpty());
    auto future = promise.future();
    editor.reset();
    QVERIFY(future.isCanceled());
    QVERIFY(!promise.addResult(test_card_pixmap(QSize(32, 32)).toImage()));
    promise.finish();
    future.waitForFinished();
    auto& cache = settings_template_support::preview_cache();
    for (const auto& key : requests) {
        QVERIFY(!cache.is_in_flight(preview_family(key)));
        QVERIFY(!cache.get_if_ready(key).has_value());
    }
}

void preview_carousel_tests::
    settings_preview_cancellation_recovers_returning_demand() {
    settings_template_widget editor(settings_tab_kind::appearance);
    QPromise<QImage> promise;
    hold_preview_batch(editor, promise);
    const auto desired = editor.active_preview_element_ids;
    const auto requests = editor.active_render_keys;
    QVERIFY(!requests.isEmpty());
    // No remaining member is useful: stop, without waiting on the GUI thread.
    editor.active_preview_element_ids.clear();
    editor.prune_preview_queue();
    QVERIFY(promise.isCanceled());
    // Demand returns while the canceled worker's completion is still queued.
    editor.active_preview_element_ids = desired;
    promise.finish();
    editor.preview_render_finished();
    QCOMPARE(editor.pending_preview_queue.size(), requests.size());
    editor.render_next_preview();
    editor.preview_watcher.future().waitForFinished();
    editor.preview_render_finished();
    auto& cache = settings_template_support::preview_cache();
    for (const auto& key : requests) {
        const auto ready = cache.get_if_ready(key);
        QVERIFY(ready.has_value());
        QVERIFY(!ready->face_images.front().isNull());
        QVERIFY(!cache.is_in_flight(preview_family(key)));
    }
}

void preview_carousel_tests::settings_preview_worker_outlives_widget() {
    auto editor = std::make_unique<settings_template_widget>(
        settings_tab_kind::appearance
    );
    const auto requests = editor->pending_preview_queue;
    QVERIFY(!requests.isEmpty());
    editor->render_next_preview();
    auto future = editor->preview_watcher.future();
    editor.reset();
    future.waitForFinished();
    QVERIFY(future.isCanceled());
    auto& cache = settings_template_support::preview_cache();
    for (const auto& key : requests) {
        QVERIFY(!cache.is_in_flight(preview_family(key)));
        QVERIFY(!cache.get_if_ready(key).has_value());
    }
}

void preview_carousel_tests::
    settings_preview_batches_keep_generation_boundaries() {
    settings_template_widget editor(settings_tab_kind::appearance);
    const auto first_requests = editor.pending_preview_queue;
    QVERIFY(!first_requests.isEmpty());
    editor.warm_preview_generation(
        editor.active_preview_source, editor.active_preview_bucket + 1
    );
    editor.warming_preview_element_ids = editor.active_preview_element_ids;
    auto& cache = settings_template_support::preview_cache();
    for (const auto& key : first_requests) {
        const auto parsed
            = settings_template_support::parse_preview_scope(key.render_scope);
        const auto next = editor.preview_key(
            editor.warming_preview_source, editor.warming_preview_bucket,
            editor.warming_preview_generation, parsed.element_id
        );
        cache.submit_request({
            .name_space = next.name_space,
            .kind = next.kind,
            .source_id = next.source_id,
            .render_scope = next.render_scope,
            .target_bucket_px = next.target_bucket_px,
        });
        editor.enqueue_preview(next);
    }
    const auto second_requests
        = editor.pending_preview_queue.mid(first_requests.size());
    QCOMPARE(second_requests.size(), first_requests.size());
    editor.render_next_preview();
    QCOMPARE(editor.active_render_keys, first_requests);
    QCOMPARE(editor.pending_preview_queue, second_requests);
    // A refresh during the batch must not enqueue its members again.
    for (const auto& key : first_requests) {
        editor.enqueue_preview(key);
    }
    QCOMPARE(editor.pending_preview_queue, second_requests);
    editor.preview_watcher.future().waitForFinished();
    editor.preview_render_finished();
    editor.render_next_preview();
    QCOMPARE(editor.active_render_keys, second_requests);
    editor.preview_watcher.future().waitForFinished();
    editor.preview_render_finished();
    QVERIFY(editor.warming_preview_generation == 0);
    for (const auto& key : first_requests) {
        QVERIFY(!cache.get_if_ready(key).has_value());
        QVERIFY(!cache.is_in_flight(preview_family(key)));
    }
    for (const auto& key : second_requests) {
        QVERIFY(cache.get_if_ready(key).has_value());
        QVERIFY(!cache.is_in_flight(preview_family(key)));
    }
}

void preview_carousel_tests::completed_preview_result_releases_future_data() {
    QTest::addColumn<int>("mode");
    QTest::newRow("accepted-then-evicted") << 0;
    QTest::newRow("obsolete-demand") << 1;
    QTest::newRow("independent-future-owner") << 2;
}

void preview_carousel_tests::completed_preview_result_releases_future() {
    QFETCH(int, mode);
    const auto totals = std::make_shared<image_lifetime_test::accounting>();
    settings_template_widget editor(settings_tab_kind::appearance);
    {
        QPromise<QImage> producer;
        hold_preview_batch(editor, producer);
        for (qsizetype index = 0; index < editor.active_render_keys.size();
             ++index)
            QVERIFY(producer.addResult(
                image_lifetime_test::tracked_image(totals, 32)
            ));
        producer.finish();
    }
    const auto keys = editor.active_render_keys;
    QVERIFY(!keys.isEmpty());
    QFuture<QImage> independent_owner;
    if (mode == 2)
        independent_owner = editor.preview_watcher.future();
    if (mode == 1) {
        editor.active_preview_element_ids.clear();
        editor.prune_preview_queue();
    }
    editor.preview_render_finished();
    auto& cache = settings_template_support::preview_cache();
    for (const auto& key : keys) {
        const auto ready = cache.get_if_ready(key);
        QCOMPARE(ready.has_value(), mode != 1);
        if (ready.has_value())
            QCOMPARE(
                ready->face_images.front().pixelColor(0, 0), QColor(Qt::red)
            );
        QVERIFY(!cache.is_in_flight(preview_family(key)));
    }
    for (const auto& key : keys)
        cache.erase_result(key);
    if (mode == 2) {
        QVERIFY(independent_owner.isValid());
        QCOMPARE(
            independent_owner.resultCount(), static_cast<int>(keys.size())
        );
        QCOMPARE(
            independent_owner.resultAt(0).pixelColor(0, 0), QColor(Qt::red)
        );
        QCOMPARE(totals->live_images.load(), static_cast<int>(keys.size()));
        independent_owner = QFuture<QImage>();
    }
    QCOMPARE(totals->live_bytes.load(), qsizetype { 0 });
    QCOMPARE(totals->live_images.load(), 0);
    editor.preview_render_finished();
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
