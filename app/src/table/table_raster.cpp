#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "packing/layout/equal_rectangles.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_settings.hpp"
#include "table/card_widget.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include <QDateTime>
#include <QString>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

static QVector<QImage>
rasterize_all_card_faces(const QString& source_path, int bucket_px) {
    if (bucket_px <= 0) {
        return {};
    }

    return rasterize_card_faces_with_fallback(
        source_path, QSize(bucket_px, bucket_px)
    );
}

static raster_cache::family_key
family_for_entry(const raster_cache::entry_key& key) {
    return raster_cache::family_key {
        .name_space = key.name_space,
        .kind = key.kind,
        .source_id = key.source_id,
        .render_scope = key.render_scope,
    };
}

void table::schedule_card_preload() {
    if (slot_widgets.empty()) {
        if (preload_timer != nullptr) {
            preload_timer->stop();
        }
        return;
    }

    if (preload_timer == nullptr) {
        preload_timer = std::make_unique<time_interface>();
        preload_timer->set_single_shot(true);
        QObject::connect(
            preload_timer.get(), &time_interface::timeout, this,
            &table::on_preload_tick
        );
    }

    preload_timer->set_interval(rasterization_delay_ms());
    preload_timer->start();
}

void table::prepare_cards_for_start() {
    if (preload_timer != nullptr && preload_timer->is_active()) {
        preload_timer->stop();
    }
    update_shared_card_face_need(true);
    on_preload_tick();
}

bool table::is_rasterization_busy() const { return rasterization_busy; }

raster_cache* table::shared_raster_cache_service() {
    return &raster_cache_service;
}

const raster_cache* table::shared_raster_cache_service() const {
    return &raster_cache_service;
}

QString table::generation_render_scope(qint64 generation_id) {
    return QStringLiteral("all_faces#g%1").arg(generation_id);
}

qint64 table::generation_id_from_render_scope(const QString& render_scope) {
    const QString prefix = QStringLiteral("all_faces#g");
    if (!render_scope.startsWith(prefix)) {
        return 0;
    }

    bool ok = false;
    const qint64 generation_id
        = render_scope.mid(prefix.size()).toLongLong(&ok);
    if (!ok || generation_id <= 0) {
        return 0;
    }
    return generation_id;
}

raster_cache::entry_key table::entry_key_for_generation(
    const QString& source_id, int target_bucket_px, qint64 generation_id
) {
    return raster_cache::entry_key {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::card_sheet_faces,
        .source_id = source_id,
        .render_scope = generation_render_scope(generation_id),
        .target_bucket_px = target_bucket_px,
    };
}

void table::remember_shared_faces_key(const raster_cache::entry_key& key) {
    if (key.name_space != raster_cache::cache_namespace::main
        || key.kind != raster_cache::resource_kind::card_sheet_faces
        || generation_id_from_render_scope(key.render_scope) <= 0) {
        return;
    }
    retained_shared_faces_keys.insert(key);
}

void table::forget_shared_faces_key(const raster_cache::entry_key& key) {
    retained_shared_faces_keys.remove(key);
}

void table::enforce_shared_generation_bounds() {
    QSet<raster_cache::entry_key> keep_keys;
    if (active_shared_generation_id > 0
        && !active_card_sheet_source_id.isEmpty()
        && active_shared_bucket_px > 0) {
        keep_keys.insert(entry_key_for_generation(
            active_card_sheet_source_id, active_shared_bucket_px,
            active_shared_generation_id
        ));
    }
    if (warming_shared_generation_id > 0
        && !warming_card_sheet_source_id.isEmpty()
        && warming_shared_bucket_px > 0) {
        keep_keys.insert(entry_key_for_generation(
            warming_card_sheet_source_id, warming_shared_bucket_px,
            warming_shared_generation_id
        ));
    }
    if (displayed_shared_faces_key.has_value()) {
        keep_keys.insert(*displayed_shared_faces_key);
    }
    if (active_shared_faces_key.has_value()) {
        keep_keys.insert(*active_shared_faces_key);
    }
    if (warming_shared_faces_key.has_value()) {
        keep_keys.insert(*warming_shared_faces_key);
    }

    QVector<raster_cache::entry_key> stale_keys;
    stale_keys.reserve(retained_shared_faces_keys.size());
    for (const raster_cache::entry_key& key : retained_shared_faces_keys) {
        if (!keep_keys.contains(key)) {
            stale_keys.push_back(key);
        }
    }

    for (const raster_cache::entry_key& stale_key : stale_keys) {
        raster_cache_service.erase_result(stale_key);
        retained_shared_faces_keys.remove(stale_key);
    }
}

void table::retire_warming_generation() {
    if (warming_shared_faces_key.has_value()) {
        const raster_cache::family_key family
            = family_for_entry(*warming_shared_faces_key);
        raster_cache_service.take_pending_latest(family);
        raster_cache_service.clear_in_flight(family);
        raster_cache_service.erase_result(*warming_shared_faces_key);
        forget_shared_faces_key(*warming_shared_faces_key);
        warming_shared_faces_key.reset();
    }

    warming_card_sheet_source_id.clear();
    warming_shared_bucket_px = 0;
    warming_shared_generation_id = 0;
}

void table::begin_warming_generation(
    const QString& source_id, int target_bucket_px
) {
    if (source_id.isEmpty() || target_bucket_px <= 0) {
        return;
    }

    if (warming_shared_generation_id > 0
        && warming_card_sheet_source_id == source_id
        && warming_shared_bucket_px == target_bucket_px) {
        return;
    }

    const qint64 superseded_generation_id = warming_shared_generation_id;
    retire_warming_generation();
    if (active_shared_faces_key.has_value()
        && generation_id_from_render_scope(
               active_shared_faces_key->render_scope
           ) == superseded_generation_id) {
        shared_faces_refresh_queued = true;
    }

    warming_card_sheet_source_id = source_id;
    warming_shared_bucket_px = target_bucket_px;
    warming_shared_generation_id = next_shared_generation_id++;
    warming_shared_faces_key = entry_key_for_generation(
        warming_card_sheet_source_id, warming_shared_bucket_px,
        warming_shared_generation_id
    );
    remember_shared_faces_key(*warming_shared_faces_key);
    enforce_shared_generation_bounds();
}

bool table::start_shared_raster_for_key(const raster_cache::entry_key& key) {
    if (key.target_bucket_px <= 0 || key.source_id.isEmpty()
        || generation_id_from_render_scope(key.render_scope) <= 0) {
        return false;
    }
    remember_shared_faces_key(key);

    const raster_cache::request req {
        .name_space = key.name_space,
        .kind = key.kind,
        .source_id = key.source_id,
        .render_scope = key.render_scope,
        .target_bucket_px = key.target_bucket_px,
    };

    const raster_cache::submit_outcome outcome
        = raster_cache_service.submit_request(req);
    if (outcome.state == raster_cache::request_state::cache_hit) {
        apply_shared_faces_entry(outcome.key);
    }

    if (outcome.state == raster_cache::request_state::start_async) {
        const raster_cache::family_key family = family_for_entry(outcome.key);
        if (shared_faces_watcher.isRunning()) {
            raster_cache_service.clear_in_flight(family);
            raster_cache_service.set_pending_latest(family, outcome.key);
            shared_faces_refresh_queued = true;
            return true;
        }

        active_shared_faces_key = outcome.key;
        const QString source_id = outcome.key.source_id;
        const int target_bucket_px = outcome.key.target_bucket_px;
        shared_faces_watcher.setFuture(
            QtConcurrent::run(
                rasterize_all_card_faces, source_id, target_bucket_px
            )
        );
        refresh_rasterization_busy_state();
    }

    enforce_shared_generation_bounds();
    return true;
}

void table::cutover_to_ready_generation(const raster_cache::entry_key& key) {
    const std::optional<raster_cache::result> ready
        = raster_cache_service.get_if_ready(key);
    const qsizetype required_faces_count = required_card_ids_with_back().size();
    if (!ready.has_value()
        || ready->face_images.size() < required_faces_count) {
        return;
    }

    if (displayed_shared_faces_key.has_value()
        && !(*displayed_shared_faces_key == key)) {
        raster_cache_service.erase_result(*displayed_shared_faces_key);
        forget_shared_faces_key(*displayed_shared_faces_key);
    }

    displayed_shared_faces_key = key;
    remember_shared_faces_key(key);

    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->set_shared_card_faces(
                ready->face_images, ready->raster_size
            );
        }
    }

    const qint64 cutover_generation_id
        = generation_id_from_render_scope(key.render_scope);
    if (cutover_generation_id > 0) {
        active_shared_generation_id = cutover_generation_id;
    }
    active_card_sheet_source_id = key.source_id;
    active_shared_bucket_px = key.target_bucket_px;
    main_faces_runner.set_cached_short_px(key.target_bucket_px);

    if (warming_shared_generation_id == cutover_generation_id) {
        warming_shared_faces_key.reset();
        warming_card_sheet_source_id.clear();
        warming_shared_bucket_px = 0;
        warming_shared_generation_id = 0;
    }

    enforce_shared_generation_bounds();
}

void table::on_preload_tick() {
    const QSize current_size = size();
    if (current_size.isEmpty()) {
        return;
    }

    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget == nullptr || slot_widget->has_shared_card_faces()) {
            continue;
        }

        slot_widget->prepare_card_faces();
    }
}

void table::on_slot_rasterization_busy_changed(bool busy) {
    auto* slot_widget = qobject_cast<table_slot*>(sender());
    if (slot_widget == nullptr) {
        return;
    }

    update_rasterization_state(slot_widget, busy);
}

void table::on_shared_rasterization_requested(int target_cache_px) {
    if (target_cache_px <= 0) {
        return;
    }

    const QString desired_source_id = card_sheet_source_path();
    const bool has_active_generation = active_shared_generation_id > 0;
    const bool active_generation_matches = has_active_generation
        && active_card_sheet_source_id == desired_source_id
        && active_shared_bucket_px == target_cache_px;

    const bool warming_generation_mismatch = warming_shared_generation_id > 0
        && (warming_card_sheet_source_id != desired_source_id
            || warming_shared_bucket_px != target_cache_px);
    if (!has_active_generation || !active_generation_matches
        || warming_generation_mismatch) {
        begin_warming_generation(desired_source_id, target_cache_px);
    }

    qint64 generation_id_to_request = warming_shared_generation_id;
    if (generation_id_to_request <= 0 && has_active_generation) {
        generation_id_to_request = active_shared_generation_id;
    }
    if (generation_id_to_request <= 0) {
        active_card_sheet_source_id = desired_source_id;
        active_shared_bucket_px = target_cache_px;
        active_shared_generation_id = next_shared_generation_id++;
        generation_id_to_request = active_shared_generation_id;
    }

    const raster_cache::entry_key request_key = entry_key_for_generation(
        desired_source_id, target_cache_px, generation_id_to_request
    );
    if (warming_shared_generation_id == generation_id_to_request) {
        warming_shared_faces_key = request_key;
    }
    start_shared_raster_for_key(request_key);
}

void table::on_shared_cache_result_updated(const raster_cache::entry_key& key) {
    apply_shared_faces_entry(key);
}

void table::on_shared_rasterization_finished() {
    if (!active_shared_faces_key.has_value()) {
        return;
    }

    const raster_cache::entry_key key = *active_shared_faces_key;
    active_shared_faces_key.reset();
    const qint64 completed_generation_id
        = generation_id_from_render_scope(key.render_scope);
    const bool generation_is_still_expected = completed_generation_id > 0
        && (completed_generation_id == active_shared_generation_id
            || completed_generation_id == warming_shared_generation_id);

    const QVector<QImage> face_images = shared_faces_watcher.result();
    if (generation_is_still_expected && !face_images.isEmpty()) {
        const raster_cache::result ready {
            .key = key,
            .raster_size = QSize(key.target_bucket_px, key.target_bucket_px),
            .generation = static_cast<int>(completed_generation_id),
            .timestamp_ms = QDateTime::currentMSecsSinceEpoch(),
            .use_count = 0,
            .single_image = {},
            .face_images = face_images,
        };
        raster_cache_service.insert_or_update_result(ready);
        remember_shared_faces_key(key);
    }

    const raster_cache::family_key family = family_for_entry(key);
    const raster_cache::finish_outcome finish
        = raster_cache_service.finish_active_request(family, key);
    if (!generation_is_still_expected) {
        // Finish first so a coalesced request can be promoted, then retire the
        // stale result from the bounded cache.
        raster_cache_service.erase_result(key);
        forget_shared_faces_key(key);
    }
    if (finish.next_entry_to_start.has_value()) {
        if (warming_shared_generation_id > 0
            && generation_id_from_render_scope(
                   finish.next_entry_to_start->render_scope
               ) != warming_shared_generation_id) {
            const raster_cache::entry_key restarted_key
                = entry_key_for_generation(
                    warming_card_sheet_source_id, warming_shared_bucket_px,
                    warming_shared_generation_id
                );
            start_shared_raster_for_key(restarted_key);
        } else {
            start_shared_raster_for_key(*finish.next_entry_to_start);
        }
    }

    if (shared_faces_refresh_queued && !shared_faces_watcher.isRunning()) {
        shared_faces_refresh_queued = false;
        update_shared_card_face_need(true);
    }

    enforce_shared_generation_bounds();
    refresh_rasterization_busy_state();
}

int table::rasterization_delay_ms() const {
    const int interval = gameplay_owner ? gameplay_owner->pick_interval_ms()
                                        : pick_interval_ms;
    const int computed = std::max(400, interval * 2);
    return std::min(600, computed);
}

int table::max_card_need_short_px() const {
    int max_need = 0;
    for (const table_slot* slot_widget : slot_widgets) {
        if (slot_widget == nullptr || !slot_widget->isVisible()) {
            continue;
        }
        max_need = std::max(max_need, slot_widget->card_face_need_short_px());
    }

    if (max_need <= 0) {
        return 0;
    }
    return std::max(
        1, static_cast<int>(std::ceil(max_need * devicePixelRatioF()))
    );
}

void table::update_shared_card_face_need(bool immediate, bool force) {
    // Ordinary resize demand uses final geometry, never each animation frame.
    // A theme-source change may still force its existing independent cutover.
    if (gameplay_owner && gameplay_layout_progress < 1.0 && !force)
        return;
    const int need_short_px = max_card_need_short_px();
    if (need_short_px <= 0) {
        main_faces_runner.cancel_pending();
        return;
    }

    rasterization_runner::evaluation evaluation;
    if (immediate) {
        evaluation
            = main_faces_runner.request_immediately(need_short_px, force);
    } else {
        evaluation = main_faces_runner.on_need_changed(
            need_short_px,
            static_cast<double>(
                gameplay_owner ? gameplay_owner->pick_interval_ms()
                               : pick_interval_ms
            ) / 1000.0,
            std::numeric_limits<double>::quiet_NaN(), false, false, false
        );
    }
    if (!evaluation.rasterization_required && warming_shared_generation_id > 0
        && active_shared_generation_id > 0
        && active_card_sheet_source_id == card_sheet_source_path()
        && rasterization_runner::accepted_window_for_cached_size(
               active_shared_bucket_px
        )
               .contains(evaluation.required_short_px)) {
        retire_warming_generation();
        enforce_shared_generation_bounds();
    }
}

void table::clear_shared_card_faces() {
    if (displayed_shared_faces_key.has_value()) {
        displayed_shared_faces_key.reset();
    }

    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->clear_shared_card_faces();
        }
    }

    enforce_shared_generation_bounds();
}

void table::apply_shared_faces_entry(const raster_cache::entry_key& key) {
    if (key.name_space != raster_cache::cache_namespace::main
        || key.kind != raster_cache::resource_kind::card_sheet_faces) {
        return;
    }

    const qint64 generation_id
        = generation_id_from_render_scope(key.render_scope);
    if (generation_id <= 0) {
        return;
    }

    const bool is_warming_generation
        = generation_id == warming_shared_generation_id;
    const bool is_active_generation
        = generation_id == active_shared_generation_id;
    if (!is_warming_generation && !is_active_generation) {
        return;
    }

    if (is_warming_generation) {
        cutover_to_ready_generation(key);
        return;
    }

    const std::optional<raster_cache::result> ready
        = raster_cache_service.get_if_ready(key);
    const qsizetype required_faces_count = required_card_ids_with_back().size();
    if (!ready.has_value()
        || ready->face_images.size() < required_faces_count) {
        return;
    }
    displayed_shared_faces_key = key;
    remember_shared_faces_key(key);
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->set_shared_card_faces(
                ready->face_images, ready->raster_size
            );
        }
    }
    enforce_shared_generation_bounds();
}

void table::update_rasterization_state(table_slot* slot, bool busy) {
    if (slot == nullptr) {
        return;
    }

    if (busy) {
        rasterizing_slots.insert(slot);
    } else {
        rasterizing_slots.remove(slot);
    }

    refresh_rasterization_busy_state();
}

void table::refresh_rasterization_busy_state() {
    const bool is_busy
        = !rasterizing_slots.isEmpty() || shared_faces_watcher.isRunning();
    if (rasterization_busy == is_busy) {
        return;
    }

    rasterization_busy = is_busy;
    emit rasterization_busy_changed(is_busy);
}
