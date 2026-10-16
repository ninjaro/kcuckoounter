#include "table/settings_template.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "image/card_preview_carousel.hpp"
#include "settings/preferences.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/table.hpp"

#include <QDateTime>
#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <utility>

#include "table/settings_template_support.hpp"

#include <QListWidget>

void settings_template_widget::update_theme_carousel(int suit_index) {
    if (theme_carousel == nullptr) {
        return;
    }
    theme_suit = suit_index;
    active_preview_element_ids.clear();
    warming_preview_element_ids.clear();
    clear_preview_entries(displayed_theme_entries);
    const QSize card_size = settings_template_support::preview_card_size();
    theme_carousel->set_card_size(card_size);
    theme_carousel->set_card_accessible_name_provider(
        [suit_index](int card_index) {
            return card_label_from_index(suit_index * 13 + card_index);
        }
    );
    theme_carousel->set_card_provider(
        13, std::bind_front(&settings_template_widget::active_theme_card, this)
    );
    prune_preview_queue();
}

QPixmap
settings_template_widget::active_theme_card(int card_index, const QSize& size) {
    return theme_card(card_index, theme_suit, size);
}

raster_cache::entry_key settings_template_widget::preview_key(
    const QString& source_id, int target_bucket_px, qint64 generation_id,
    const QString& element_id
) const {
    return raster_cache::entry_key {
        .name_space = raster_cache::cache_namespace::settings,
        .kind = raster_cache::resource_kind::card_sheet_faces,
        .source_id = source_id,
        .render_scope = settings_template_support::generation_scope(
            element_id, preview_id, generation_id
        ),
        .target_bucket_px = target_bucket_px,
    };
}

bool settings_template_widget::preview_key_ready(
    const QString& source_id, int target_bucket_px, qint64 generation_id,
    const QString& element_id
) const {
    if (source_id.isEmpty() || target_bucket_px <= 0 || generation_id <= 0
        || element_id.isEmpty()) {
        return false;
    }

    const raster_cache::entry_key key
        = preview_key(source_id, target_bucket_px, generation_id, element_id);
    const std::optional<raster_cache::result> ready
        = settings_template_support::preview_cache().get_if_ready(key);
    return ready.has_value() && !ready->face_images.isEmpty()
        && !ready->face_images[0].isNull();
}

void settings_template_widget::retire_preview_generation(
    const QString& source_id, int target_bucket_px, qint64 generation_id
) {
    if (source_id.isEmpty() || target_bucket_px <= 0 || generation_id <= 0) {
        return;
    }

    auto& service = settings_template_support::preview_cache();
    for (const QString& element_id : card_element_ids()) {
        service.erase_result(
            preview_key(source_id, target_bucket_px, generation_id, element_id)
        );
    }
}

void settings_template_widget::warm_preview_generation(
    const QString& source_id, int target_bucket_px
) {
    if (source_id.isEmpty() || target_bucket_px <= 0) {
        return;
    }

    if (warming_preview_generation > 0 && warming_preview_source == source_id
        && warming_preview_bucket == target_bucket_px) {
        return;
    }

    retire_preview_generation(
        warming_preview_source, warming_preview_bucket,
        warming_preview_generation
    );

    warming_preview_source = source_id;
    warming_preview_bucket = target_bucket_px;
    warming_preview_generation = next_preview_generation++;
    warming_preview_element_ids.clear();
    prune_preview_queue();
}

void settings_template_widget::ensure_preview_generation(
    const QString& source_id, int target_bucket_px
) {
    if (source_id.isEmpty() || target_bucket_px <= 0) {
        return;
    }

    if (active_preview_generation <= 0) {
        active_preview_source = source_id;
        active_preview_bucket = target_bucket_px;
        active_preview_generation = next_preview_generation++;
        active_preview_element_ids.clear();
        return;
    }

    const bool active_matches = active_preview_source == source_id
        && active_preview_bucket == target_bucket_px;
    if (active_matches) {
        if (warming_preview_generation > 0) {
            retire_preview_generation(
                warming_preview_source, warming_preview_bucket,
                warming_preview_generation
            );
            warming_preview_source.clear();
            warming_preview_bucket = 0;
            warming_preview_generation = 0;
            warming_preview_element_ids.clear();
            prune_preview_queue();
        }
        return;
    }

    warm_preview_generation(source_id, target_bucket_px);
}

bool settings_template_widget::cutover_preview_generation() {
    if (warming_preview_generation <= 0
        || warming_preview_element_ids.isEmpty()) {
        return false;
    }

    QSet<QString> needed_elements;
    for (const QString& element_id :
         std::as_const(warming_preview_element_ids)) {
        if (!preview_key_relevant(preview_key(
                warming_preview_source, warming_preview_bucket,
                warming_preview_generation, element_id
            ))) {
            continue;
        }
        needed_elements.insert(element_id);
        if (!preview_key_ready(
                warming_preview_source, warming_preview_bucket,
                warming_preview_generation, element_id
            )) {
            return false;
        }
    }
    if (needed_elements.isEmpty())
        return false;

    const QString previous_source_id = active_preview_source;
    const int previous_bucket_px = active_preview_bucket;
    const qint64 previous_generation_id = active_preview_generation;

    active_preview_source = warming_preview_source;
    active_preview_bucket = warming_preview_bucket;
    active_preview_generation = warming_preview_generation;
    active_preview_element_ids = std::move(needed_elements);

    warming_preview_source.clear();
    warming_preview_bucket = 0;
    warming_preview_generation = 0;
    warming_preview_element_ids.clear();

    clear_preview_entries(displayed_theme_entries);
    clear_preview_entries(displayed_weights_entries);

    retire_preview_generation(
        previous_source_id, previous_bucket_px, previous_generation_id
    );
    prune_preview_queue();

    if (theme_carousel != nullptr) {
        theme_preview_needs_refresh = true;
    }
    if (weights_carousel != nullptr) {
        weights_preview_needs_refresh = true;
    }
    if (!preview_refresh_scheduled
        && (theme_preview_needs_refresh || weights_preview_needs_refresh)) {
        preview_refresh_scheduled = true;
        QTimer::singleShot(
            0, this, &settings_template_widget::flush_preview_refresh
        );
    }
    return true;
}

std::optional<QImage> settings_template_widget::preview_face(
    int card_index, int suit_index, const QSize& size,
    QSet<raster_cache::entry_key>& tracked_keys
) {
    if (!size.isValid()) {
        return std::nullopt;
    }

    const QString element_id
        = settings_template_support::preview_scope(card_index, suit_index);
    if (element_id.isEmpty()) {
        return std::nullopt;
    }

    const int short_px = std::max(1, std::min(size.width(), size.height()));
    ensure_preview_generation(selected_theme_source_id(), short_px);
    const bool use_warming_generation = warming_preview_generation > 0;
    if (use_warming_generation) {
        warming_preview_element_ids.insert(element_id);
        active_preview_element_ids.insert(element_id);
    } else {
        active_preview_element_ids.insert(element_id);
    }
    cutover_preview_generation();
    cancel_obsolete_preview_batch();

    const bool use_warming_after_cutover = warming_preview_generation > 0;
    const QString request_source_id = use_warming_after_cutover
        ? warming_preview_source
        : active_preview_source;
    const int request_bucket_px = use_warming_after_cutover
        ? warming_preview_bucket
        : active_preview_bucket;
    const qint64 request_generation_id = use_warming_after_cutover
        ? warming_preview_generation
        : active_preview_generation;
    if (request_source_id.isEmpty() || request_bucket_px <= 0
        || request_generation_id <= 0) {
        return std::nullopt;
    }

    auto& service = settings_template_support::preview_cache();
    std::optional<QImage> active_fallback;
    if (preview_key_ready(
            active_preview_source, active_preview_bucket,
            active_preview_generation, element_id
        )) {
        const raster_cache::entry_key key = preview_key(
            active_preview_source, active_preview_bucket,
            active_preview_generation, element_id
        );
        const std::optional<raster_cache::result> ready
            = service.get_if_ready(key);
        if (ready.has_value() && !ready->face_images.isEmpty()
            && !ready->face_images[0].isNull()) {
            active_fallback = ready->face_images[0];
            track_preview_entry(key, tracked_keys);
            if (!use_warming_after_cutover) {
                return active_fallback;
            }
        }
    }

    const raster_cache::entry_key request_key = preview_key(
        request_source_id, request_bucket_px, request_generation_id, element_id
    );
    const raster_cache::request req {
        .name_space = request_key.name_space,
        .kind = request_key.kind,
        .source_id = request_key.source_id,
        .render_scope = request_key.render_scope,
        .target_bucket_px = request_key.target_bucket_px,
    };

    const raster_cache::submit_outcome outcome = service.submit_request(req);
    const bool request_has_ready_image = outcome.ready_result.has_value()
        && !outcome.ready_result->face_images.isEmpty()
        && !outcome.ready_result->face_images[0].isNull();
    if (request_has_ready_image) {
        if (request_generation_id == active_preview_generation) {
            track_preview_entry(outcome.key, tracked_keys);
            return outcome.ready_result->face_images[0];
        }
        if (cutover_preview_generation()
            && preview_key_ready(
                active_preview_source, active_preview_bucket,
                active_preview_generation, element_id
            )) {
            const raster_cache::entry_key key = preview_key(
                active_preview_source, active_preview_bucket,
                active_preview_generation, element_id
            );
            const std::optional<raster_cache::result> ready
                = service.get_if_ready(key);
            if (ready.has_value() && !ready->face_images.isEmpty()
                && !ready->face_images[0].isNull()) {
                track_preview_entry(key, tracked_keys);
                return ready->face_images[0];
            }
        }
        if (active_fallback.has_value()) {
            return active_fallback;
        }
    }

    if (!request_has_ready_image) {
        enqueue_preview(outcome.key);
    }
    const bool did_cutover = cutover_preview_generation();
    if (did_cutover
        && preview_key_ready(
            active_preview_source, active_preview_bucket,
            active_preview_generation, element_id
        )) {
        const raster_cache::entry_key key = preview_key(
            active_preview_source, active_preview_bucket,
            active_preview_generation, element_id
        );
        const std::optional<raster_cache::result> ready
            = service.get_if_ready(key);
        if (ready.has_value() && !ready->face_images.isEmpty()
            && !ready->face_images[0].isNull()) {
            track_preview_entry(key, tracked_keys);
            return ready->face_images[0];
        }
    }
    if (active_fallback.has_value()) {
        return active_fallback;
    }
    return std::nullopt;
}

QPixmap settings_template_widget::theme_card(
    int card_index, int suit_index, const QSize& size
) {
    const std::optional<QImage> face
        = preview_face(card_index, suit_index, size, displayed_theme_entries);
    if (!face.has_value()) {
        return {};
    }
    return QPixmap::fromImage(*face);
}

void settings_template_widget::enqueue_preview(
    const raster_cache::entry_key& key
) {
    if (key.render_scope.isEmpty()) {
        return;
    }
    if (!preview_key_relevant(key)) {
        return;
    }
    if (pending_preview_keys.contains(key)
        || active_render_keys.contains(key)) {
        return;
    }
    pending_preview_keys.insert(key);
    pending_preview_queue.enqueue(key);
    if (preview_render_scheduled) {
        return;
    }
    preview_render_scheduled = true;
    QTimer::singleShot(0, this, &settings_template_widget::render_next_preview);
}

void settings_template_widget::render_next_preview() {
    preview_render_scheduled = false;
    if (preview_watcher.isRunning() || !active_render_keys.isEmpty()) {
        return;
    }

    prune_preview_queue();
    if (pending_preview_queue.isEmpty()) {
        return;
    }

    // Capture only the queued contiguous requests for this source, size and
    // generation. Later arrivals belong to another job, not an open-ended
    // batch.
    const auto first = pending_preview_queue.head();
    const auto first_scope
        = settings_template_support::parse_preview_scope(first.render_scope);
    QStringList element_ids;
    while (!pending_preview_queue.isEmpty()) {
        const auto& key = pending_preview_queue.head();
        const auto parsed
            = settings_template_support::parse_preview_scope(key.render_scope);
        if (key.source_id != first.source_id
            || key.target_bucket_px != first.target_bucket_px
            || parsed.generation_id != first_scope.generation_id) {
            break;
        }
        element_ids.append(parsed.element_id);
        active_render_keys.append(key);
        pending_preview_keys.remove(key);
        pending_preview_queue.dequeue();
    }
    published_preview_results.clear();
    preview_watcher.setFuture(QtConcurrent::run(
        &settings_template_widget::render_preview_faces, first.source_id,
        element_ids, first.target_bucket_px
    ));
}

void settings_template_widget::preview_result_ready(int result_index) {
    if (result_index < 0 || result_index >= active_render_keys.size()
        || published_preview_results.contains(result_index)
        || !preview_watcher.future().isResultReadyAt(result_index)) {
        return;
    }
    // Cache signals may update generations synchronously. Mark before
    // publishing.
    published_preview_results.insert(result_index);
    const auto key = active_render_keys.at(result_index);
    const auto parsed
        = settings_template_support::parse_preview_scope(key.render_scope);
    auto& service = settings_template_support::preview_cache();
    if (!preview_key_relevant(key)) {
        service.erase_result(key);
        return;
    }

    const QImage image = preview_watcher.resultAt(result_index);
    if (!image.isNull()) {
        service.insert_or_update_result(raster_cache::result {
            .key = key,
            .raster_size = QSize(key.target_bucket_px, key.target_bucket_px),
            .generation = static_cast<int>(parsed.generation_id),
            .timestamp_ms = QDateTime::currentMSecsSinceEpoch(),
            .use_count = 0,
            .single_image = {},
            .face_images = { image },
        });
    }
}

void settings_template_widget::finish_preview_request(
    const raster_cache::entry_key& key
) {
    const raster_cache::family_key family {
        .name_space = key.name_space,
        .kind = key.kind,
        .source_id = key.source_id,
        .render_scope = key.render_scope,
    };
    auto& service = settings_template_support::preview_cache();
    auto next = service.finish_active_request(family, key).next_entry_to_start;
    while (next.has_value()) {
        if (preview_key_relevant(*next)) {
            enqueue_preview(*next);
            return;
        }
        next = service.finish_active_request(family, *next).next_entry_to_start;
    }
}

void settings_template_widget::preview_render_finished() {
    if (active_render_keys.isEmpty()) {
        return;
    }
    // Drain results even when the worker finished before GUI notifications ran.
    // Individual notifications and this drain must never publish a face twice.
    for (int index = 0; index < preview_watcher.future().resultCount();
         ++index) {
        preview_result_ready(index);
    }
    const bool canceled = preview_watcher.isCanceled();
    const auto completed_keys = std::exchange(active_render_keys, {});
    published_preview_results.clear();
    // Draining precedes release; independent future/cache/display owners remain
    // valid. Never takeResult(), which would consume other observers' results.
    preview_watcher.setFuture(QFuture<QImage>());
    for (const auto& key : completed_keys) {
        finish_preview_request(key);
        // Demand can return after cancellation but before completion delivery.
        // Re-admit only current missing faces; ordinary failures are not
        // retried.
        if (canceled && preview_key_relevant(key)
            && !settings_template_support::preview_cache().get_if_ready(key)) {
            settings_template_support::preview_cache().submit_request({
                .name_space = key.name_space,
                .kind = key.kind,
                .source_id = key.source_id,
                .render_scope = key.render_scope,
                .target_bucket_px = key.target_bucket_px,
            });
            enqueue_preview(key);
        }
    }

    if (!pending_preview_queue.isEmpty() && !preview_render_scheduled) {
        preview_render_scheduled = true;
        QTimer::singleShot(
            0, this, &settings_template_widget::render_next_preview
        );
    }
}

void settings_template_widget::render_preview_faces(
    QPromise<QImage>& promise, const QString& source_id,
    const QStringList& element_ids, int target_bucket_px
) {
    if (target_bucket_px <= 0 || element_ids.isEmpty()
        || promise.isCanceled()) {
        return;
    }

    card_sheet_render_context context(source_id);
    const QSize raster_size(target_bucket_px, target_bucket_px);
    for (const auto& element_id : element_ids) {
        if (promise.isCanceled()) {
            return;
        }
        // Null images retain their index, so later faces cannot be
        // misattributed.
        if (!promise.addResult(context.rasterize_face(element_id, raster_size)
            )) {
            return;
        }
    }
}

void settings_template_widget::preview_cache_updated(
    const raster_cache::entry_key& key
) {
    if (key.name_space != raster_cache::cache_namespace::settings
        || key.kind != raster_cache::resource_kind::card_sheet_faces) {
        return;
    }
    const settings_template_support::parsed_scope parsed
        = settings_template_support::parse_preview_scope(key.render_scope);
    if (!parsed.valid || !preview_key_relevant(key)) {
        return;
    }

    if (parsed.generation_id == warming_preview_generation) {
        cutover_preview_generation();
        return;
    }
    if (parsed.generation_id != active_preview_generation) {
        return;
    }

    const QStringList& ids = card_element_ids();
    const int card_index = static_cast<int>(ids.indexOf(parsed.element_id));
    if (card_index < 0) {
        return;
    }
    const int suit_index = card_index / 13;
    if (suit_index == theme_suit && theme_carousel != nullptr) {
        theme_preview_needs_refresh = true;
    }
    if (suit_index == weights_suit && weights_carousel != nullptr) {
        weights_preview_needs_refresh = true;
    }

    if (!preview_refresh_scheduled
        && (theme_preview_needs_refresh || weights_preview_needs_refresh)) {
        preview_refresh_scheduled = true;
        QTimer::singleShot(
            0, this, &settings_template_widget::flush_preview_refresh
        );
    }
}

void settings_template_widget::flush_preview_refresh() {
    preview_refresh_scheduled = false;

    if (theme_preview_needs_refresh && theme_carousel != nullptr) {
        theme_carousel->refresh_cards();
    }
    if (weights_preview_needs_refresh && weights_carousel != nullptr) {
        weights_carousel->refresh_cards();
    }

    theme_preview_needs_refresh = false;
    weights_preview_needs_refresh = false;
}

void settings_template_widget::clear_displayed_theme_entries() {
    clear_preview_entries(displayed_theme_entries);
    clear_preview_entries(displayed_weights_entries);
}

void settings_template_widget::mark_preview_refresh_pending(
    bool theme_preview, bool weights_preview
) {
    if (theme_preview) {
        theme_preview_needs_refresh = true;
    }
    if (weights_preview) {
        weights_preview_needs_refresh = true;
    }

    if (!preview_refresh_scheduled
        && (theme_preview_needs_refresh || weights_preview_needs_refresh)) {
        preview_refresh_scheduled = true;
        QTimer::singleShot(
            0, this, &settings_template_widget::flush_preview_refresh
        );
    }
}

void settings_template_widget::update_weights_carousel(int suit_index) {
    if (weights_carousel == nullptr) {
        return;
    }
    weights_suit = suit_index;
    active_preview_element_ids.clear();
    warming_preview_element_ids.clear();
    clear_preview_entries(displayed_weights_entries);
    int strategy_index = strategy_list_widget != nullptr
        ? strategy_list_widget->currentRow()
        : -1;
    if (strategy_index < 0 || strategy_index >= strategies.size()) {
        return;
    }
    const QSize card_size = settings_template_support::preview_card_size();
    weights_carousel->set_card_size(card_size);
    weights_carousel->set_card_accessible_name_provider([this, suit_index](
                                                            int card_index
                                                        ) {
        QString card_name = card_label_from_index(suit_index * 13 + card_index);
        const int selected_strategy = strategy_list_widget != nullptr
            ? strategy_list_widget->currentRow()
            : -1;
        if (selected_strategy < 0 || selected_strategy >= strategies.size()
            || card_index < 0
            || card_index >= strategies.at(selected_strategy).weights.size()) {
            return card_name;
        }
        return QStringLiteral("%1, count value %2")
            .arg(
                card_name,
                QString::number(
                    strategies.at(selected_strategy).weights.at(card_index)
                )
            );
    });
    weights_carousel->set_card_provider(
        13,
        std::bind_front(&settings_template_widget::active_weighted_card, this)
    );
    prune_preview_queue();
}

QPixmap settings_template_widget::active_weighted_card(
    int card_index, const QSize& size
) {
    return weighted_card(card_index, weights_suit, size);
}

QPixmap settings_template_widget::weighted_card(
    int card_index, int suit_index, const QSize& size
) {
    int strategy_index = strategy_list_widget != nullptr
        ? strategy_list_widget->currentRow()
        : -1;
    if (strategy_index < 0 || strategy_index >= strategies.size()) {
        return {};
    }

    const std::optional<QImage> base_face
        = preview_face(card_index, suit_index, size, displayed_weights_entries);
    if (!base_face.has_value()) {
        return {};
    }
    return settings_template_support::build_weighted_card_preview(
        *base_face, card_index, suit_index, size,
        strategies[strategy_index].weights
    );
}

void settings_template_widget::track_preview_entry(
    const raster_cache::entry_key& key,
    QSet<raster_cache::entry_key>& tracked_keys
) {
    tracked_keys.insert(key);
}

void settings_template_widget::clear_preview_entries(
    QSet<raster_cache::entry_key>& tracked_keys
) {
    tracked_keys.clear();
}

bool settings_template_widget::preview_key_relevant(
    const raster_cache::entry_key& key
) const {
    if (key.name_space != raster_cache::cache_namespace::settings
        || key.kind != raster_cache::resource_kind::card_sheet_faces) {
        return false;
    }

    const settings_template_support::parsed_scope parsed
        = settings_template_support::parse_preview_scope(key.render_scope);
    if (!parsed.valid || parsed.instance_id != preview_id) {
        return false;
    }

    const QStringList& ids = card_element_ids();
    const int card_index = static_cast<int>(ids.indexOf(parsed.element_id));
    if (card_index < 0) {
        return false;
    }

    const int suit_index = card_index / 13;
    const int rank_index = card_index % 13;
    const bool theme_requests_face = theme_carousel != nullptr
        && suit_index == theme_suit
        && theme_carousel->requested_card_indices().contains(rank_index);
    const bool weights_request_face = weights_carousel != nullptr
        && suit_index == weights_suit
        && weights_carousel->requested_card_indices().contains(rank_index);
    if (!theme_requests_face && !weights_request_face) {
        return false;
    }

    if (parsed.generation_id == active_preview_generation) {
        return key.source_id == active_preview_source
            && key.target_bucket_px == active_preview_bucket
            && active_preview_element_ids.contains(parsed.element_id);
    }
    if (parsed.generation_id == warming_preview_generation) {
        return key.source_id == warming_preview_source
            && key.target_bucket_px == warming_preview_bucket
            && warming_preview_element_ids.contains(parsed.element_id);
    }

    return false;
}

void settings_template_widget::cancel_obsolete_preview_batch() {
    if (!active_render_keys.isEmpty()
        && std::none_of(
            active_render_keys.cbegin(), active_render_keys.cend(),
            [this](const auto& key) { return preview_key_relevant(key); }
        )) {
        preview_watcher.cancel();
    }
}

void settings_template_widget::prune_preview_queue() {
    const auto queued = std::exchange(pending_preview_queue, {});
    pending_preview_keys.clear();
    for (const auto& key : queued) {
        if (preview_key_relevant(key)) {
            enqueue_preview(key);
        } else {
            finish_preview_request(key);
        }
    }
    cancel_obsolete_preview_batch();
}
