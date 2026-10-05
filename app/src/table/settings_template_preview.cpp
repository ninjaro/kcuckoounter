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
    prune_preview_queue();
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

    for (const QString& element_id :
         std::as_const(warming_preview_element_ids)) {
        if (!preview_key_ready(
                warming_preview_source, warming_preview_bucket,
                warming_preview_generation, element_id
            )) {
            return false;
        }
    }

    const QString previous_source_id = active_preview_source;
    const int previous_bucket_px = active_preview_bucket;
    const qint64 previous_generation_id = active_preview_generation;

    active_preview_source = warming_preview_source;
    active_preview_bucket = warming_preview_bucket;
    active_preview_generation = warming_preview_generation;
    active_preview_element_ids = warming_preview_element_ids;

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
    if (pending_preview_keys.contains(key)) {
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
    if (preview_watcher.isRunning() || active_render_key.has_value()) {
        return;
    }

    raster_cache::entry_key key;
    bool has_key = false;
    while (!pending_preview_queue.isEmpty()) {
        const raster_cache::entry_key candidate
            = pending_preview_queue.dequeue();
        pending_preview_keys.remove(candidate);
        if (!preview_key_relevant(candidate)) {
            continue;
        }
        key = candidate;
        has_key = true;
        break;
    }
    if (!has_key) {
        return;
    }

    const QString render_scope = key.render_scope;
    if (!render_scope.startsWith(QStringLiteral("subset:"))) {
        const raster_cache::family_key family {
            .name_space = key.name_space,
            .kind = key.kind,
            .source_id = key.source_id,
            .render_scope = key.render_scope,
        };
        auto& service = settings_template_support::preview_cache();
        const raster_cache::finish_outcome finish
            = service.finish_active_request(family, key);
        if (finish.next_entry_to_start.has_value()) {
            enqueue_preview(finish.next_entry_to_start.value());
        }
        return;
    }

    const settings_template_support::parsed_scope parsed
        = settings_template_support::parse_preview_scope(render_scope);
    if (!parsed.valid) {
        const raster_cache::family_key family {
            .name_space = key.name_space,
            .kind = key.kind,
            .source_id = key.source_id,
            .render_scope = key.render_scope,
        };
        auto& service = settings_template_support::preview_cache();
        const raster_cache::finish_outcome finish
            = service.finish_active_request(family, key);
        if (finish.next_entry_to_start.has_value()) {
            enqueue_preview(finish.next_entry_to_start.value());
        }
        return;
    }

    active_render_key = key;
    const QString source_id = key.source_id;
    const QString element_id = parsed.element_id;
    const int target_bucket_px = key.target_bucket_px;
    preview_watcher.setFuture(
        QtConcurrent::run(
            &settings_template_widget::render_preview_face, source_id,
            element_id, target_bucket_px
        )
    );
}

void settings_template_widget::preview_render_finished() {
    if (!active_render_key.has_value()) {
        return;
    }

    const raster_cache::entry_key key = *active_render_key;
    active_render_key.reset();
    const settings_template_support::parsed_scope parsed
        = settings_template_support::parse_preview_scope(key.render_scope);

    auto& service = settings_template_support::preview_cache();
    const QImage image = preview_watcher.result();
    const bool key_is_expected = parsed.valid && preview_key_relevant(key);
    if (!image.isNull() && key_is_expected) {
        const raster_cache::result ready {
            .key = key,
            .raster_size = QSize(key.target_bucket_px, key.target_bucket_px),
            .generation = static_cast<int>(parsed.generation_id),
            .timestamp_ms = QDateTime::currentMSecsSinceEpoch(),
            .use_count = 0,
            .single_image = {},
            .face_images = { image },
        };
        service.insert_or_update_result(ready);
    } else if (!key_is_expected) {
        service.erase_result(key);
    }

    const raster_cache::family_key family {
        .name_space = key.name_space,
        .kind = key.kind,
        .source_id = key.source_id,
        .render_scope = key.render_scope,
    };
    const raster_cache::finish_outcome finish
        = service.finish_active_request(family, key);
    if (finish.next_entry_to_start.has_value()) {
        enqueue_preview(finish.next_entry_to_start.value());
    }

    if (!pending_preview_queue.isEmpty()) {
        preview_render_scheduled = true;
        QTimer::singleShot(
            0, this, &settings_template_widget::render_next_preview
        );
    }
}

QImage settings_template_widget::render_preview_face(
    const QString& source_id, const QString& element_id, int target_bucket_px
) {
    if (target_bucket_px <= 0) {
        return {};
    }

    const QSize raster_size(target_bucket_px, target_bucket_px);
    return rasterize_card_face_with_fallback(
        source_id, element_id, raster_size
    );
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
    prune_preview_queue();
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
    const bool suit_is_relevant
        = suit_index == theme_suit || suit_index == weights_suit;
    if (!suit_is_relevant) {
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

void settings_template_widget::prune_preview_queue() {
    if (pending_preview_queue.isEmpty()) {
        return;
    }

    QQueue<raster_cache::entry_key> filtered;
    QSet<raster_cache::entry_key> filtered_set;
    while (!pending_preview_queue.isEmpty()) {
        const raster_cache::entry_key key = pending_preview_queue.dequeue();
        if (!preview_key_relevant(key) || filtered_set.contains(key)) {
            continue;
        }
        filtered.enqueue(key);
        filtered_set.insert(key);
    }

    pending_preview_queue = filtered;
    pending_preview_keys = filtered_set;
}
