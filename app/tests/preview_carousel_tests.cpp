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

#include <QPointer>
#include <QScopeGuard>
#include <QSet>
#include <functional>
#include <memory>
#include <utility>

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

static QVector<QRgb> carousel_colors(const card_preview_carousel& carousel) {
    QVector<QRgb> colors;
    for (const auto* label : carousel.findChildren<QLabel*>()) {
        const QImage image = label->pixmap().toImage();
        colors.append(image.isNull() ? QRgb { 0 } : image.pixel(0, 0));
    }
    return colors;
}

static QRgb card_color(int index) { return qRgb(index + 1, 0, 0); }

void preview_carousel_tests::geometry_requests_use_settled_dimensions_data() {
    QTest::addColumn<QString>("transition");
    QTest::addColumn<bool>("prefetch");
    for (const char* transition :
         { "grow", "shrink", "range", "minimum-width", "spacing", "resize",
           "pending", "aspect", "unchanged", "no-size" }) {
        for (const bool prefetch : { false, true }) {
            const QByteArray name = QByteArray(transition)
                + (prefetch ? "-adjacent" : "-visible");
            QTest::newRow(name.constData())
                << QString::fromLatin1(transition) << prefetch;
        }
    }
}

void preview_carousel_tests::geometry_requests_use_settled_dimensions() {
    QFETCH(QString, transition);
    QFETCH(bool, prefetch);

    card_preview_carousel carousel;
    carousel.setFixedSize(1000, 400);
    carousel.set_visible_count(
        transition == QStringLiteral("shrink")
                || transition == QStringLiteral("pending")
            ? 5
            : 3
    );
    carousel.set_prefetch_adjacent_cards(prefetch);
    if (transition != QStringLiteral("no-size"))
        carousel.set_card_size(QSize(64, 96));
    if (transition == QStringLiteral("minimum-width")
        || transition == QStringLiteral("spacing")
        || transition == QStringLiteral("resize"))
        carousel.set_visible_range(3, 5);
    carousel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&carousel));

    QVector<int> requests;
    QVector<QSize> requested_sizes;
    carousel.set_card_provider(
        13,
        [&requests, &requested_sizes,
         &transition](int index, const QSize& size) {
            requests.append(index);
            requested_sizes.append(size);
            if (transition == QStringLiteral("pending"))
                return QPixmap();
            QPixmap pixmap(size.isValid() ? size : QSize(1, 1));
            pixmap.fill(card_color(index));
            return pixmap;
        }
    );
    const auto initial_labels = carousel.findChildren<QLabel*>();
    QVERIFY(!initial_labels.isEmpty());
    const QSize initial_size = initial_labels.front()->size();
    requests.clear();
    requested_sizes.clear();

    if (transition == QStringLiteral("grow")
        || transition == QStringLiteral("no-size"))
        carousel.set_visible_count(4);
    else if (transition == QStringLiteral("shrink")
             || transition == QStringLiteral("pending"))
        carousel.set_visible_count(3);
    else if (transition == QStringLiteral("range"))
        carousel.set_visible_range(4, 5);
    else if (transition == QStringLiteral("minimum-width"))
        carousel.set_minimum_card_width(300);
    else if (transition == QStringLiteral("spacing"))
        carousel.set_card_spacing(240);
    else if (transition == QStringLiteral("resize"))
        carousel.setFixedSize(320, 400);
    else if (transition == QStringLiteral("aspect"))
        carousel.set_card_size(QSize(96, 64));
    else
        carousel.set_visible_count(3);

    const auto labels = carousel.findChildren<QLabel*>();
    QVERIFY(!labels.isEmpty());
    const int expected_count = transition == QStringLiteral("grow")
            || transition == QStringLiteral("no-size")
        ? 4
        : transition == QStringLiteral("range") ? 5
                                                : 3;
    QCOMPARE(labels.size(), expected_count);
    if (transition == QStringLiteral("unchanged")) {
        QVERIFY(requests.isEmpty());
        QCOMPARE(labels.front()->size(), initial_size);
    } else {
        QCOMPARE(requests, carousel.requested_card_indices());
        for (const QSize& size : requested_sizes) {
            if (transition == QStringLiteral("no-size"))
                QVERIFY(!size.isValid());
            else
                QCOMPARE(size, labels.front()->size());
        }
    }
    for (int index = 0; index < labels.size(); ++index) {
        if (transition == QStringLiteral("pending")) {
            QVERIFY(labels[index]->pixmap().isNull());
            continue;
        }
        QCOMPARE(
            labels[index]->pixmap().toImage().pixel(0, 0), card_color(index)
        );
        if (transition != QStringLiteral("no-size"))
            QCOMPARE(labels[index]->pixmap().size(), labels[index]->size());
    }
    requests.clear();
    requested_sizes.clear();
    if (transition != QStringLiteral("no-size")) {
        carousel.refresh_cards();
        QCOMPARE(requests, carousel.requested_card_indices());
        for (const QSize& size : requested_sizes)
            QCOMPARE(size, labels.front()->size());
    }
}

static QVector<QRgb> expected_carousel_colors(int first, int total) {
    QVector<QRgb> colors;
    for (int offset = 0; offset < 3; ++offset)
        colors.append(card_color((first + offset) % total));
    return colors;
}

void preview_carousel_tests::provider_window_releases_departed_cards_data() {
    QTest::addColumn<bool>("prefetch");
    QTest::addColumn<int>("total");
    QTest::newRow("visible-only") << false << 13;
    QTest::newRow("visible-and-adjacent") << true << 13;
    QTest::newRow("short-visible-only") << false << 2;
    QTest::newRow("short-visible-and-adjacent") << true << 2;
}

void preview_carousel_tests::provider_window_releases_departed_cards() {
    QFETCH(bool, prefetch);
    QFETCH(int, total);
    card_preview_carousel carousel;
    carousel.resize(640, 200);
    carousel.set_visible_count(3);
    carousel.set_prefetch_adjacent_cards(prefetch);
    carousel.set_card_size(QSize(64, 96));
    QVector<int> requests;
    carousel.set_card_provider(
        total,
        [&requests](int index, const QSize& size) {
            requests.append(index);
            QPixmap image(size);
            image.fill(QColor::fromRgb(card_color(index)));
            return image;
        }
    );
    QCOMPARE(
        requests,
        total == 2     ? QVector<int>({ 0, 1 })
            : prefetch ? QVector<int>({ 0, 1, 2, 12, 3 })
                       : QVector<int>({ 0, 1, 2 })
    );
    QCOMPARE(carousel_colors(carousel), expected_carousel_colors(0, total));
    auto* next = carousel_button(carousel, Qt::RightArrow);
    auto* previous = carousel_button(carousel, Qt::LeftArrow);
    QVERIFY(next != nullptr);
    QVERIFY(previous != nullptr);

    // Moving one position reuses the overlap and loads only the entering face.
    // Two circuits in both directions exercise eviction, return and wraparound.
    for (int step = 1; step <= 2 * total; ++step) {
        requests.clear();
        next->click();
        const int first = step % total;
        QCOMPARE(
            requests,
            total == 2 ? QVector<int>()
                       : QVector<int>({ (first + (prefetch ? 3 : 2)) % total })
        );
        QCOMPARE(
            carousel_colors(carousel), expected_carousel_colors(first, total)
        );
    }
    for (int step = 1; step <= 2 * total; ++step) {
        requests.clear();
        previous->click();
        const int first = (total - step % total) % total;
        QCOMPARE(
            requests,
            total == 2
                ? QVector<int>()
                : QVector<int>({ (first + total - (prefetch ? 1 : 0)) % total })
        );
        QCOMPARE(
            carousel_colors(carousel), expected_carousel_colors(first, total)
        );
    }
}

void preview_carousel_tests::changing_prefetch_preserves_visible_cards() {
    card_preview_carousel carousel;
    carousel.set_visible_count(3);
    carousel.set_card_size(QSize(64, 96));
    QVector<int> requests;
    carousel.set_card_provider(13, [&requests](int index, const QSize& size) {
        requests.append(index);
        return test_card_pixmap(size);
    });
    QCOMPARE(requests, QVector<int>({ 0, 1, 2, 12, 3 }));
    requests.clear();
    carousel.set_prefetch_adjacent_cards(false);
    QVERIFY(requests.isEmpty());
    auto* next = carousel_button(carousel, Qt::RightArrow);
    auto* previous = carousel_button(carousel, Qt::LeftArrow);
    QVERIFY(next != nullptr);
    QVERIFY(previous != nullptr);
    next->click();
    QCOMPARE(requests, QVector<int>({ 3 }));
    requests.clear();
    carousel.set_prefetch_adjacent_cards(true);
    QVERIFY(requests.isEmpty());
    next->click();
    QCOMPARE(requests, QVector<int>({ 4, 5 }));
    requests.clear();
    carousel.set_prefetch_adjacent_cards(false);
    QVERIFY(requests.isEmpty());
    previous->click();
    QCOMPARE(requests, QVector<int>({ 1 }));
}

void preview_carousel_tests::supplied_cards_survive_window_navigation() {
    card_preview_carousel carousel;
    carousel.set_visible_count(3);
    carousel.set_prefetch_adjacent_cards(false);
    QVector<QPixmap> cards;
    for (int index = 0; index < 13; ++index) {
        QPixmap image(16, 24);
        image.fill(QColor::fromRgb(card_color(index)));
        cards.append(image);
    }
    carousel.set_cards(cards);
    auto* next = carousel_button(carousel, Qt::RightArrow);
    QVERIFY(next != nullptr);
    for (int step = 0; step < 26; ++step) {
        QCOMPARE(
            carousel_colors(carousel), expected_carousel_colors(step % 13, 13)
        );
        next->click();
    }
    carousel.set_prefetch_adjacent_cards(true);
    carousel.refresh_cards();
    QCOMPARE(carousel_colors(carousel), expected_carousel_colors(0, 13));
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
    settings_preview_navigation_cancels_obsolete_batch_data() {
    QTest::addColumn<bool>("weighted");
    QTest::addColumn<bool>("cached_destination");
    QTest::newRow("theme-missing") << false << false;
    QTest::newRow("weights-missing") << true << false;
    QTest::newRow("theme-cached") << false << true;
    QTest::newRow("weights-cached") << true << true;
}

void preview_carousel_tests::settings_preview_navigation_cancels_obsolete_batch(
) {
    QFETCH(bool, weighted);
    QFETCH(bool, cached_destination);
    settings_template_widget editor(
        weighted ? settings_tab_kind::strategies : settings_tab_kind::appearance
    );
    QPromise<QImage> promise;
    hold_preview_batch(editor, promise);
    const auto old_requests = editor.active_render_keys;
    auto* carousel = weighted ? editor.weights_carousel : editor.theme_carousel;
    QVERIFY(carousel != nullptr);
    const int suit = weighted ? editor.weights_suit : editor.theme_suit;
    auto& cache = settings_template_support::preview_cache();
    if (cached_destination) {
        const QImage face = test_card_pixmap(QSize(32, 32)).toImage();
        // All later positions hit the lower cache. Cancellation must not rely
        // on a new missing-face enqueue or dispatch timer.
        for (int rank = 3; rank < 13; ++rank) {
            cache.insert_or_update_result({
                .key = editor.preview_key(
                    editor.active_preview_source, editor.active_preview_bucket,
                    editor.active_preview_generation,
                    settings_template_support::preview_scope(rank, suit)
                ),
                .raster_size = face.size(),
                .generation
                = static_cast<int>(editor.active_preview_generation),
                .timestamp_ms = 0,
                .use_count = 0,
                .single_image = {},
                .face_images = { face },
            });
        }
    }
    auto* next = carousel_button(*carousel, Qt::RightArrow);
    QVERIFY(next != nullptr);
    for (int step = 0; step < 10; ++step)
        next->click();
    const bool canceled = promise.isCanceled();
    promise.finish();
    editor.preview_render_finished();
    QVERIFY(canceled);
    for (const auto& key : old_requests) {
        QVERIFY(!cache.is_in_flight(preview_family(key)));
        QVERIFY(!cache.get_if_ready(key).has_value());
    }
    if (cached_destination) {
        QVERIFY(editor.pending_preview_queue.isEmpty());
    } else {
        editor.render_next_preview();
        QCOMPARE(editor.active_render_keys.size(), 3);
        editor.preview_watcher.future().waitForFinished();
        editor.preview_render_finished();
    }
    for (int rank = 10; rank < 13; ++rank) {
        const auto key = editor.preview_key(
            editor.active_preview_source, editor.active_preview_bucket,
            editor.active_preview_generation,
            settings_template_support::preview_scope(rank, suit)
        );
        const auto ready = cache.get_if_ready(key);
        QVERIFY(ready.has_value());
        QVERIFY(!ready->face_images.front().isNull());
    }
}

void preview_carousel_tests::settings_preview_navigation_keeps_useful_batch() {
    settings_template_widget editor(settings_tab_kind::appearance);
    QPromise<QImage> promise;
    hold_preview_batch(editor, promise);
    const auto requests = editor.active_render_keys;
    auto* next = carousel_button(*editor.theme_carousel, Qt::RightArrow);
    QVERIFY(next != nullptr);
    next->click(); // Two original faces remain in the logical window.
    QVERIFY(!promise.isCanceled());
    const QImage face = test_card_pixmap(QSize(32, 32)).toImage();
    for (qsizetype index = 0; index < requests.size(); ++index)
        QVERIFY(promise.addResult(face));
    promise.finish();
    editor.preview_render_finished();
    auto& cache = settings_template_support::preview_cache();
    QVERIFY(!cache.get_if_ready(requests.front()).has_value());
    for (qsizetype index = 1; index < requests.size(); ++index) {
        const auto ready = cache.get_if_ready(requests.at(index));
        QVERIFY(ready.has_value());
        QCOMPARE(ready->face_images.front(), face);
    }
    editor.render_next_preview();
    QCOMPARE(editor.active_render_keys.size(), 1);
    editor.preview_watcher.future().waitForFinished();
    editor.preview_render_finished();
}

void preview_carousel_tests::
    settings_preview_navigation_recovers_canceled_demand() {
    settings_template_widget editor(settings_tab_kind::appearance);
    QPromise<QImage> promise;
    hold_preview_batch(editor, promise);
    const auto original = editor.active_render_keys;
    auto* next = carousel_button(*editor.theme_carousel, Qt::RightArrow);
    auto* previous = carousel_button(*editor.theme_carousel, Qt::LeftArrow);
    QVERIFY(next != nullptr);
    QVERIFY(previous != nullptr);
    for (int step = 0; step < 3; ++step)
        next->click();
    const bool canceled = promise.isCanceled();
    for (int step = 0; step < 3; ++step)
        previous->click();
    const bool result_accepted
        = promise.addResult(test_card_pixmap(QSize(32, 32)).toImage());
    promise.finish();
    editor.preview_render_finished();
    QVERIFY(canceled);
    QVERIFY(!result_accepted);
    editor.render_next_preview();
    QCOMPARE(editor.active_render_keys, original);
    editor.preview_watcher.future().waitForFinished();
    editor.preview_render_finished();
    auto& cache = settings_template_support::preview_cache();
    for (const auto& key : original) {
        QVERIFY(!cache.is_in_flight(preview_family(key)));
        const auto ready = cache.get_if_ready(key);
        QVERIFY(ready.has_value());
        QVERIFY(!ready->face_images.front().isNull());
    }
}

void preview_carousel_tests::settings_preview_cancellation_is_per_editor() {
    settings_template_widget first(settings_tab_kind::appearance);
    settings_template_widget second(settings_tab_kind::strategies);
    QPromise<QImage> first_promise;
    QPromise<QImage> second_promise;
    hold_preview_batch(first, first_promise);
    hold_preview_batch(second, second_promise);
    const auto second_requests = second.active_render_keys;
    auto* next = carousel_button(*first.theme_carousel, Qt::RightArrow);
    QVERIFY(next != nullptr);
    for (int step = 0; step < 3; ++step)
        next->click();
    const bool first_canceled = first_promise.isCanceled();
    const bool second_canceled = second_promise.isCanceled();
    const QImage face = test_card_pixmap(QSize(32, 32)).toImage();
    for (qsizetype index = 0; index < second_requests.size(); ++index)
        QVERIFY(second_promise.addResult(face));
    first_promise.finish();
    second_promise.finish();
    first.preview_render_finished();
    second.preview_render_finished();
    QVERIFY(first_canceled);
    QVERIFY(!second_canceled);
    auto& cache = settings_template_support::preview_cache();
    for (const auto& key : second_requests) {
        QVERIFY(!cache.is_in_flight(preview_family(key)));
        const auto ready = cache.get_if_ready(key);
        QVERIFY(ready.has_value());
        QCOMPARE(ready->face_images.front(), face);
    }
}

static QSet<QString> preview_window_ids(int first, int suit, bool prefetch) {
    QSet<QString> ids;
    for (int offset = prefetch ? -1 : 0; offset < (prefetch ? 4 : 3); ++offset)
        ids.insert(settings_template_support::preview_scope(
            (first + offset + 13) % 13, suit
        ));
    return ids;
}

void preview_carousel_tests::settings_preview_dispatch_uses_current_window_data(
) {
    QTest::addColumn<bool>("weighted");
    QTest::addColumn<bool>("prefetch");
    QTest::newRow("theme-visible") << false << false;
    QTest::newRow("theme-adjacent") << false << true;
    QTest::newRow("weights-visible") << true << false;
    QTest::newRow("weights-adjacent") << true << true;
}

void preview_carousel_tests::settings_preview_dispatch_uses_current_window() {
    QFETCH(bool, weighted);
    QFETCH(bool, prefetch);
    settings_template_widget editor(
        weighted ? settings_tab_kind::strategies : settings_tab_kind::appearance
    );
    auto* carousel = weighted ? editor.weights_carousel : editor.theme_carousel;
    QVERIFY(carousel != nullptr);
    carousel->set_visible_count(3);
    carousel->set_prefetch_adjacent_cards(prefetch);
    auto* next = carousel_button(*carousel, Qt::RightArrow);
    auto* previous = carousel_button(*carousel, Qt::LeftArrow);
    QVERIFY(next != nullptr);
    QVERIFY(previous != nullptr);
    // Withhold dispatch while the user moves. This controls ordering without
    // sleeps, an artificial renderer or a different queue implementation.
    for (int step = 0; step < 10; ++step)
        next->click();
    const auto queued = editor.pending_preview_queue;
    const int suit = weighted ? editor.weights_suit : editor.theme_suit;
    const auto wanted = preview_window_ids(10, suit, prefetch);
    editor.render_next_preview();
    QSet<QString> dispatched;
    for (const auto& key : std::as_const(editor.active_render_keys))
        dispatched.insert(
            settings_template_support::parse_preview_scope(key.render_scope)
                .element_id
        );
    editor.preview_watcher.future().waitForFinished();
    editor.preview_render_finished();
    QCOMPARE(dispatched, wanted);
    auto& cache = settings_template_support::preview_cache();
    for (const auto& key : queued) {
        QVERIFY(!cache.is_in_flight(preview_family(key)));
        const bool needed = wanted.contains(
            settings_template_support::parse_preview_scope(key.render_scope)
                .element_id
        );
        const auto ready = cache.get_if_ready(key);
        QCOMPARE(ready.has_value(), needed);
        if (ready.has_value())
            QVERIFY(!ready->face_images.front().isNull());
    }
    // Returning demand is admitted again; overlapping ready faces are reused.
    previous->click();
    editor.render_next_preview();
    QCOMPARE(editor.active_render_keys.size(), 1);
    const auto returning = editor.active_render_keys.front();
    QCOMPARE(
        settings_template_support::parse_preview_scope(returning.render_scope)
            .element_id,
        settings_template_support::preview_scope(prefetch ? 8 : 9, suit)
    );
    editor.preview_watcher.future().waitForFinished();
    editor.preview_render_finished();
    const auto ready = cache.get_if_ready(returning);
    QVERIFY(ready.has_value());
    QVERIFY(!ready->face_images.front().isNull());
    QVERIFY(!cache.is_in_flight(preview_family(returning)));
}

void preview_carousel_tests::settings_preview_cutover_ignores_departed_demand(
) {
    settings_template_widget editor(settings_tab_kind::appearance);
    auto* carousel = editor.theme_carousel;
    QVERIFY(carousel != nullptr);
    carousel->set_visible_count(3);
    editor.render_next_preview();
    editor.preview_watcher.future().waitForFinished();
    editor.preview_render_finished();
    editor.flush_preview_refresh();
    const auto old_requests = editor.displayed_theme_entries;
    QCOMPARE(old_requests.size(), 3);

    carousel->set_minimum_card_width(128);
    QVERIFY(editor.warming_preview_generation > 0);
    const qint64 generation = editor.warming_preview_generation;
    const QString source = editor.warming_preview_source;
    const int bucket = editor.warming_preview_bucket;
    auto* next = carousel_button(*carousel, Qt::RightArrow);
    QVERIFY(next != nullptr);
    next->click();
    const auto departed = editor.preview_key(
        source, bucket, generation,
        settings_template_support::preview_scope(0, editor.theme_suit)
    );
    QVERIFY(editor.warming_preview_element_ids.contains(
        settings_template_support::preview_scope(0, editor.theme_suit)
    ));
    auto& cache = settings_template_support::preview_cache();
    QImage face(bucket, bucket, QImage::Format_ARGB32_Premultiplied);
    face.fill(Qt::red);
    for (int rank = 1; rank <= 3; ++rank) {
        const auto key = editor.preview_key(
            source, bucket, generation,
            settings_template_support::preview_scope(rank, editor.theme_suit)
        );
        cache.insert_or_update_result({
            .key = key,
            .raster_size = face.size(),
            .generation = static_cast<int>(generation),
            .timestamp_ms = 0,
            .use_count = 0,
            .single_image = {},
            .face_images = { face },
        });
        if (rank < 3)
            QCOMPARE(editor.warming_preview_generation, generation);
    }
    // Rank zero never completes. Only the current window gates cutover.
    QCOMPARE(editor.warming_preview_generation, qint64 { 0 });
    QCOMPARE(editor.active_preview_generation, generation);
    QVERIFY(!cache.get_if_ready(departed).has_value());
    for (const auto& key : old_requests)
        QVERIFY(!cache.get_if_ready(key).has_value());
    const QPixmap current
        = editor.theme_card(1, editor.theme_suit, QSize(bucket, bucket));
    QVERIFY(!current.isNull());
    QCOMPARE(current.toImage().pixelColor(0, 0), QColor(Qt::red));
}

void preview_carousel_tests::settings_preview_navigation_rejects_late_results(
) {
    settings_template_widget editor(settings_tab_kind::appearance);
    QPromise<QImage> promise;
    hold_preview_batch(editor, promise);
    const auto old_requests = editor.active_render_keys;
    const QImage face = test_card_pixmap(QSize(32, 32)).toImage();
    for (qsizetype index = 0; index < old_requests.size(); ++index)
        QVERIFY(promise.addResult(face));
    auto* next = carousel_button(*editor.theme_carousel, Qt::RightArrow);
    QVERIFY(next != nullptr);
    for (int step = 0; step < 10; ++step)
        next->click();
    // Deliver completed results after navigation, before the next dispatch.
    promise.finish();
    editor.preview_render_finished();
    auto& cache = settings_template_support::preview_cache();
    for (const auto& key : old_requests) {
        QVERIFY(!cache.get_if_ready(key).has_value());
        QVERIFY(!cache.is_in_flight(preview_family(key)));
    }
    editor.render_next_preview();
    const auto current_requests = editor.active_render_keys;
    QCOMPARE(current_requests.size(), 3);
    editor.preview_watcher.future().waitForFinished();
    editor.preview_render_finished();
    for (const auto& key : current_requests) {
        const auto ready = cache.get_if_ready(key);
        QVERIFY(ready.has_value());
        QVERIFY(!ready->face_images.front().isNull());
        QVERIFY(!cache.is_in_flight(preview_family(key)));
    }
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

void preview_carousel_tests::publication_can_retire_preview_owner_data() {
    QTest::addColumn<bool>("weighted");
    QTest::addColumn<bool>("queued");
    QTest::addColumn<int>("position");
    for (const bool weighted : { false, true }) {
        for (const bool queued : { false, true }) {
            for (int position = 0; position < 3; ++position) {
                const QByteArray name
                    = QByteArray(weighted ? "weights" : "theme")
                    + (queued ? "-queued-" : "-drain-")
                    + QByteArray::number(position);
                QTest::newRow(name.constData())
                    << weighted << queued << position;
            }
        }
    }
}

void preview_carousel_tests::publication_can_retire_preview_owner() {
    QFETCH(bool, weighted);
    QFETCH(bool, queued);
    QFETCH(int, position);
    const auto live = std::make_shared<image_lifetime_test::accounting>();
    auto editor = std::make_unique<settings_template_widget>(
        weighted ? settings_tab_kind::strategies : settings_tab_kind::appearance
    );
    const QPointer<settings_template_widget> lifetime(editor.get());
    {
        QPromise<QImage> producer;
        hold_preview_batch(*editor, producer);
        for (qsizetype index = 0; index < editor->active_render_keys.size();
             ++index) {
            QVERIFY(
                producer.addResult(image_lifetime_test::tracked_image(live, 32))
            );
        }
        producer.finish();
    }
    const auto keys = editor->active_render_keys;
    QVERIFY(keys.size() >= 3);
    const qsizetype retire_at = position == 0 ? 0
        : position == 1                       ? keys.size() / 2
                                              : keys.size() - 1;
    QFuture<QImage> independent_future = editor->preview_watcher.future();

    settings_template_widget survivor(settings_tab_kind::appearance);
    QPromise<QImage> survivor_producer;
    hold_preview_batch(survivor, survivor_producer);
    const auto survivor_keys = survivor.active_render_keys;
    auto& cache = settings_template_support::preview_cache();
    QVector<raster_cache::entry_key> publications;
    std::optional<raster_cache::result> independent_pixels;
    int destructions = 0;
    QObject::connect(editor.get(), &QObject::destroyed, this, [&destructions] {
        ++destructions;
    });
    const auto observer = QObject::connect(
        &cache, &raster_cache::result_updated, this,
        [&](const auto& key) {
            if (!keys.contains(key))
                return;
            publications.append(key);
            if (key == keys.at(retire_at)) {
                independent_pixels = cache.get_if_ready(key);
                editor.reset();
            }
        }
    );
    const auto disconnect
        = qScopeGuard([&observer] { QObject::disconnect(observer); });

    if (queued) {
        QTRY_VERIFY_WITH_TIMEOUT(lifetime.isNull(), 4000);
    } else {
        editor->preview_render_finished();
    }
    QVERIFY(lifetime.isNull());
    QCOMPARE(destructions, 1);
    QCOMPARE(publications.size(), retire_at + 1);
    QVERIFY(independent_pixels.has_value());
    QCOMPARE(
        independent_pixels->face_images.front().pixelColor(0, 0),
        QColor(Qt::red)
    );
    for (const auto& key : keys) {
        QVERIFY(!cache.is_in_flight(preview_family(key)));
        QVERIFY(!cache.get_if_ready(key).has_value());
    }
    // Canceled future copies still own their stored buffers; never read their
    // results after cancellation. Independently handed-off pixels remain valid.
    QVERIFY(independent_future.isCanceled());
    QCOMPARE(live->live_images.load(), static_cast<int>(keys.size()));
    independent_future = QFuture<QImage>();
    QCOMPARE(live->live_images.load(), 1);
    independent_pixels.reset();
    QCOMPARE(live->live_images.load(), 0);
    QCOMPARE(live->live_bytes.load(), qsizetype { 0 });

    QVERIFY(!survivor_producer.isCanceled());
    for (const auto& key : survivor_keys)
        QVERIFY(cache.is_in_flight(preview_family(key)));
    const QImage face = test_card_pixmap(QSize(32, 32)).toImage();
    for (qsizetype index = 0; index < survivor_keys.size(); ++index)
        QVERIFY(survivor_producer.addResult(face));
    survivor_producer.finish();
    survivor.preview_render_finished();
    for (const auto& key : survivor_keys) {
        const auto ready = cache.get_if_ready(key);
        QVERIFY(ready.has_value());
        QCOMPARE(ready->face_images.front(), face);
        QVERIFY(!cache.is_in_flight(preview_family(key)));
    }
    QCoreApplication::processEvents();
    QCOMPARE(destructions, 1);
    QCOMPARE(publications.size(), retire_at + 1);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
