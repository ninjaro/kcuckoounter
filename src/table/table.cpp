#include "table/table.hpp"
#include "arch/str_label.hpp"
#include "card_helpers/card_packer.hpp"
#include "card_helpers/card_sheet.hpp"
#include "settings/theme_settings.hpp"
#include "table/table_slot.hpp"

#include <QColor>
#include <QDateTime>
#include <QFuture>
#include <QGridLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QRect>
#include <QResizeEvent>
#include <QString>
#include <QtConcurrent>

#include <algorithm>
#include <limits>
#include <memory>

namespace {

QVector<QImage>
rasterize_all_card_faces(const QString& source_path, int bucket_px) {
    if (bucket_px <= 0) {
        return {};
    }

    return rasterize_required_card_faces_with_fallback(
        source_path, QSize(bucket_px, bucket_px)
    );
}

raster_cache::family_key family_for_entry(
    const raster_cache::entry_key& key
) {
    return raster_cache::family_key {
        .name_space = key.name_space,
        .kind = key.kind,
        .source_id = key.source_id,
        .render_scope = key.render_scope,
    };
}

} // namespace

table::table(BaseWidget* parent)
    : BaseWidget(parent)
    , slot_widgets()
    , swap_source_slot(nullptr)
    , copy_source_slot(nullptr)
    , pick_interval_ms(300)
    , pick_elapsed_ms(0)
    , quiz_running(false)
    , quiz_paused(false)
    , allow_skipping(true)
    , current_mode(dealing_mode::sequential)
    , next_slot_index(0)
    , rasterizing_slots()
    , rasterization_busy(false)
    , random_gen()
    , preload_timer(nullptr)
    , main_faces_runner(this)
    , raster_cache_service(this)
    , shared_faces_watcher(this)
    , active_shared_faces_key(std::nullopt)
    , displayed_shared_faces_key(std::nullopt)
    , warming_shared_faces_key(std::nullopt)
    , active_shared_faces_fallback_usage()
    , shared_faces_refresh_queued(false)
    , active_card_sheet_source_id(card_sheet_source_path())
    , active_shared_bucket_px(0)
    , active_shared_generation_id(0)
    , warming_card_sheet_source_id()
    , warming_shared_bucket_px(0)
    , warming_shared_generation_id(0)
    , next_shared_generation_id(1) {
    setMinimumHeight(88);
    QObject::connect(
        &main_faces_runner, &rasterization_runner::rasterization_requested,
        this, &table::on_shared_rasterization_requested
    );
    QObject::connect(
        &raster_cache_service, &raster_cache::result_updated, this,
        &table::on_shared_cache_result_updated
    );
    QObject::connect(
        &shared_faces_watcher, &QFutureWatcher<QVector<QImage>>::finished, this,
        &table::on_shared_rasterization_finished
    );
}

table::~table() = default;

void table::set_slot_count(int count) {
    if (count < 0) {
        count = 0;
    }

    int current_count = static_cast<int>(slot_widgets.size());
    if (count == current_count) {
        return;
    }

    clear_swap_selection();
    clear_copy_selection();

    if (count < current_count) {
        if (count == 0) {
            quiz_running = false;
            pick_elapsed_ms = 0;
        }
        for (int index = count; index < current_count; ++index) {
            table_slot* slot_widget
                = slot_widgets[static_cast<std::size_t>(index)];
            if (slot_widget != nullptr) {
                update_rasterization_state(slot_widget, false);
                delete slot_widget;
            }
        }
        slot_widgets.resize(static_cast<std::size_t>(count));
    } else {
        slot_widgets.reserve(static_cast<std::size_t>(count));
        for (int index = current_count; index < count; ++index) {
            auto slot_widget = new table_slot(this);
            slot_widget->set_allow_skipping(allow_skipping);
            slot_widget->set_shared_card_faces_mode(true);
            QObject::connect(
                slot_widget, &table_slot::swap_clicked, this,
                &table::on_slot_swap
            );
            QObject::connect(
                slot_widget, &table_slot::copy_clicked, this,
                &table::on_slot_copy
            );
            QObject::connect(
                slot_widget, &table_slot::copy_all_clicked, this,
                &table::on_slot_copy_all
            );
            QObject::connect(
                slot_widget, &table_slot::rasterization_busy_changed, this,
                [this, slot_widget](bool busy) {
                    update_rasterization_state(slot_widget, busy);
                }
            );
            QObject::connect(
                slot_widget, &table_slot::dialog_opened, this,
                &table::dialog_opened
            );
            QObject::connect(
                slot_widget, &table_slot::score_adjusted, this,
                [this](int correct_delta, int total_delta) {
                    emit score_adjusted(correct_delta, total_delta);
                }
            );
            slot_widgets.push_back(slot_widget);
        }
    }

    if (next_slot_index >= count) {
        next_slot_index = 0;
    }

    if (count > 0) {
        card_packer_instance = std::make_unique<card_packer>(count);
    } else {
        card_packer_instance.reset();
    }

    update_layout();
    schedule_card_preload();
    update_shared_card_face_need();
    emit_geometry_debug_snapshot();
}

void table::start_quiz(int quiz_type_index, bool wait_for_answers) {
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->set_allow_skipping(allow_skipping);
            slot_widget->start_quiz(quiz_type_index);
        }
    }

    next_slot_index = 0;
    quiz_running = true;
    quiz_paused = wait_for_answers;
    if (quiz_paused) {
        for (table_slot* slot_widget : slot_widgets) {
            if (slot_widget != nullptr) {
                slot_widget->set_paused(true);
            }
        }
    }
    pick_elapsed_ms = 0;
    update_shared_card_face_need();
}

void table::clear_quiz() {
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->clear_quiz();
        }
    }

    quiz_running = false;
    quiz_paused = false;
    pick_elapsed_ms = 0;
}

void table::set_paused(bool paused) {
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->set_paused(paused);
        }
    }

    quiz_paused = paused;
}

void table::set_pick_interval(int interval_ms) {
    if (interval_ms < 1) {
        interval_ms = 1;
    }
    pick_interval_ms = interval_ms;
    if (preload_timer != nullptr && preload_timer->is_active()) {
        preload_timer->set_interval(rasterization_delay_ms());
        preload_timer->start();
    }
}

void table::set_dealing_mode(int mode_index) {
    switch (mode_index) {
    case 0:
        current_mode = dealing_mode::sequential;
        break;
    case 1:
        current_mode = dealing_mode::random;
        break;
    default:
        current_mode = dealing_mode::simultaneous;
        break;
    }
}

void table::set_allow_skipping(bool allow) {
    allow_skipping = allow;
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->set_allow_skipping(allow_skipping);
        }
    }
}

void table::paintEvent(QPaintEvent* event) {
    BaseWidget::paintEvent(event);

    QPainter painter(this);
    painter.fillRect(rect(), theme_settings::table_color());
}

void table::resizeEvent(QResizeEvent* event) {
    const QSize old_window_size
        = event != nullptr ? event->oldSize() : QSize();
    const int old_active_bucket_px = active_shared_bucket_px;
    const int old_warming_bucket_px = warming_shared_bucket_px;

    BaseWidget::resizeEvent(event);
    update_layout();
    schedule_card_preload();
    update_shared_card_face_need();
    emit_geometry_debug_snapshot();

    if (!old_window_size.isValid() || old_window_size == size()) {
        return;
    }

    const resize_transition_debug_event transition {
        .timestamp_ms = QDateTime::currentMSecsSinceEpoch(),
        .old_window_size = old_window_size,
        .new_window_size = size(),
        .old_active_bucket_px = old_active_bucket_px,
        .new_active_bucket_px = active_shared_bucket_px,
        .old_warming_bucket_px = old_warming_bucket_px,
        .new_warming_bucket_px = warming_shared_bucket_px,
        .geometry_after_resize = build_geometry_debug_snapshot(
            raster_cache_service.get_debug_snapshot()
        ),
    };
    emit debug_resize_transition_recorded(transition);
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

void table::apply_theme() {
    const QString next_source_id = card_sheet_source_path();
    const bool source_changed = active_card_sheet_source_id != next_source_id;

    update();
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->apply_theme();
        }
    }

    if (source_changed) {
        main_faces_runner.set_cached_short_px(1);
        update_shared_card_face_need(true);
        return;
    }

    update_shared_card_face_need();
}

bool table::is_rasterization_busy() const { return rasterization_busy; }

raster_cache* table::shared_raster_cache_service() {
    return &raster_cache_service;
}

const raster_cache* table::shared_raster_cache_service() const {
    return &raster_cache_service;
}

geometry_debug_snapshot table::current_geometry_debug_snapshot() const {
    return build_geometry_debug_snapshot(raster_cache_service.get_debug_snapshot());
}

QString table::generation_render_scope(qint64 generation_id) {
    return str_label("all_faces#g%1").arg(generation_id);
}

qint64 table::generation_id_from_render_scope(const QString& render_scope) {
    const QString prefix = str_label("all_faces#g");
    if (!render_scope.startsWith(prefix)) {
        return 0;
    }

    bool ok = false;
    const qint64 generation_id = render_scope.mid(prefix.size()).toLongLong(&ok);
    if (!ok || generation_id <= 0) {
        return 0;
    }
    return generation_id;
}

raster_cache::entry_key table::entry_key_for_generation(
    const QString& source_id, int target_bucket_px, qint64 generation_id
) const {
    return raster_cache::entry_key {
        .name_space = raster_cache::cache_namespace::main,
        .kind = raster_cache::resource_kind::card_sheet_faces,
        .source_id = source_id,
        .render_scope = generation_render_scope(generation_id),
        .target_bucket_px = target_bucket_px,
    };
}

void table::retire_warming_generation() {
    if (warming_shared_faces_key.has_value()) {
        raster_cache_service.erase_result(*warming_shared_faces_key);
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
        && generation_id_from_render_scope(active_shared_faces_key->render_scope)
            == superseded_generation_id) {
        shared_faces_refresh_queued = true;
    }

    warming_card_sheet_source_id = source_id;
    warming_shared_bucket_px = target_bucket_px;
    warming_shared_generation_id = next_shared_generation_id++;
    warming_shared_faces_key = entry_key_for_generation(
        warming_card_sheet_source_id, warming_shared_bucket_px,
        warming_shared_generation_id
    );
}

bool table::start_shared_raster_for_key(const raster_cache::entry_key& key) {
    if (key.target_bucket_px <= 0 || key.source_id.isEmpty()
        || generation_id_from_render_scope(key.render_scope) <= 0) {
        return false;
    }

    const raster_cache::request req {
        .name_space = key.name_space,
        .kind = key.kind,
        .source_id = key.source_id,
        .render_scope = key.render_scope,
        .need_short_px = std::max(1, compute_max_card_face_need_short_px()),
        .target_bucket_px = key.target_bucket_px,
        .high_priority = false,
        .interactive = true,
        .preview = false,
    };

    const raster_cache::submit_outcome outcome
        = raster_cache_service.submit_request(req);
    if (outcome.state == raster_cache::request_state::cache_hit) {
        apply_shared_card_faces_from_entry(outcome.key);
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
        const card_sheet_fallback_resolution fallback_resolution
            = resolve_required_card_face_sources(outcome.key.source_id);
        active_shared_faces_fallback_usage = {
            .active_theme_keys = fallback_resolution.active_theme_keys,
            .default_theme_keys = fallback_resolution.default_theme_keys,
            .placeholder_keys = fallback_resolution.placeholder_keys,
        };
        const QString source_id = outcome.key.source_id;
        const int target_bucket_px = outcome.key.target_bucket_px;
        shared_faces_watcher.setFuture(
            QtConcurrent::run([source_id, target_bucket_px]() {
                return rasterize_all_card_faces(source_id, target_bucket_px);
            })
        );
        refresh_rasterization_busy_state();
    }

    if (outcome.state == raster_cache::request_state::start_async
        || outcome.state == raster_cache::request_state::cache_hit) {
        main_faces_runner.set_cached_short_px(key.target_bucket_px);
    }

    return true;
}

void table::cutover_to_ready_generation(const raster_cache::entry_key& key) {
    const std::optional<raster_cache::result> ready
        = raster_cache_service.get_if_ready(key);
    const qsizetype required_faces_count
        = required_card_element_ids_with_back().size();
    if (!ready.has_value() || ready->face_images.size() < required_faces_count) {
        return;
    }

    if (displayed_shared_faces_key.has_value()
        && !(*displayed_shared_faces_key == key)) {
        raster_cache_service.note_entry_no_longer_displayed(
            *displayed_shared_faces_key,
            raster_cache::debug_consumer_scope::table_slots
        );
        raster_cache_service.erase_result(*displayed_shared_faces_key);
    }

    raster_cache_service.note_entry_displayed(
        key, raster_cache::debug_consumer_scope::table_slots
    );
    displayed_shared_faces_key = key;

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

    if (warming_shared_generation_id == cutover_generation_id) {
        warming_shared_faces_key.reset();
        warming_card_sheet_source_id.clear();
        warming_shared_bucket_px = 0;
        warming_shared_generation_id = 0;
    }

    emit_geometry_debug_snapshot();
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

void table::on_shared_rasterization_requested(int target_cache_px) {
    if (target_cache_px <= 0) {
        return;
    }

    const QString desired_source_id = card_sheet_source_path();
    const bool has_active_generation = active_shared_generation_id > 0;
    const bool active_generation_matches = has_active_generation
        && active_card_sheet_source_id == desired_source_id
        && active_shared_bucket_px == target_cache_px;

    if (!has_active_generation || !active_generation_matches) {
        begin_warming_generation(desired_source_id, target_cache_px);
    } else if (
        warming_shared_generation_id > 0
        && (warming_card_sheet_source_id != desired_source_id
            || warming_shared_bucket_px != target_cache_px)) {
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
    emit_geometry_debug_snapshot();
}

void table::on_shared_cache_result_updated(const raster_cache::entry_key& key) {
    apply_shared_card_faces_from_entry(key);
    emit_geometry_debug_snapshot();
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
            .fallback_usage = active_shared_faces_fallback_usage,
        };
        raster_cache_service.insert_or_update_result(ready);
    } else if (!generation_is_still_expected) {
        raster_cache_service.erase_result(key);
    }

    const raster_cache::family_key family = family_for_entry(key);
    const raster_cache::finish_outcome finish
        = raster_cache_service.finish_active_request(family, key);
    if (finish.next_entry_to_start.has_value()) {
        if (warming_shared_generation_id > 0
            && generation_id_from_render_scope(
                   finish.next_entry_to_start->render_scope
               )
                != warming_shared_generation_id) {
            const raster_cache::entry_key restarted_key = entry_key_for_generation(
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

    refresh_rasterization_busy_state();
    emit_geometry_debug_snapshot();
}

int table::rasterization_delay_ms() const {
    const int computed = std::max(400, pick_interval_ms * 2);
    return std::min(600, computed);
}

int table::compute_max_card_face_need_short_px() const {
    int max_need = 0;
    for (const table_slot* slot_widget : slot_widgets) {
        if (slot_widget == nullptr || !slot_widget->isVisible()) {
            continue;
        }
        max_need = std::max(max_need, slot_widget->card_face_need_short_px());
    }

    return max_need;
}

void table::update_shared_card_face_need(bool immediate) {
    const int need_short_px = compute_max_card_face_need_short_px();
    if (need_short_px <= 0) {
        main_faces_runner.cancel_pending();
        return;
    }

    if (immediate) {
        main_faces_runner.cancel_pending();
        on_shared_rasterization_requested(need_short_px);
        return;
    }

    main_faces_runner.on_need_changed(
        need_short_px, static_cast<double>(pick_interval_ms) / 1000.0,
        std::numeric_limits<double>::quiet_NaN(), false, false, false
    );
}

void table::clear_shared_card_faces() {
    if (displayed_shared_faces_key.has_value()) {
        raster_cache_service.note_entry_no_longer_displayed(
            *displayed_shared_faces_key,
            raster_cache::debug_consumer_scope::table_slots
        );
        displayed_shared_faces_key.reset();
    }

    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->clear_shared_card_faces();
        }
    }

    emit_geometry_debug_snapshot();
}

void table::apply_shared_card_faces_from_entry(
    const raster_cache::entry_key& key
) {
    if (key.name_space != raster_cache::cache_namespace::main
        || key.kind != raster_cache::resource_kind::card_sheet_faces) {
        return;
    }

    const qint64 generation_id = generation_id_from_render_scope(key.render_scope);
    if (generation_id <= 0) {
        return;
    }

    const bool is_warming_generation
        = generation_id == warming_shared_generation_id;
    const bool is_active_generation = generation_id == active_shared_generation_id;
    if (!is_warming_generation && !is_active_generation) {
        return;
    }

    if (is_warming_generation) {
        cutover_to_ready_generation(key);
        return;
    }

    const std::optional<raster_cache::result> ready
        = raster_cache_service.get_if_ready(key);
    const qsizetype required_faces_count
        = required_card_element_ids_with_back().size();
    if (!ready.has_value() || ready->face_images.size() < required_faces_count) {
        return;
    }
    raster_cache_service.note_entry_displayed(
        key, raster_cache::debug_consumer_scope::table_slots
    );
    displayed_shared_faces_key = key;
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->set_shared_card_faces(
                ready->face_images, ready->raster_size
            );
        }
    }
}

void table::update_layout() {
    if (!card_packer_instance)
        return;

    const size_t slot_count = slot_widgets.size();
    if (slot_count == 0) {
        return;
    }

    const int field_width = width();
    const int field_height = height();
    if (field_width <= 0 || field_height <= 0) {
        return;
    }

    auto [scale, cards] = card_packer_instance->pack(field_width, field_height);

    const auto [base_card_height, base_card_width] = card_sheet_ratio();

    const int horizontal_width = static_cast<int>(base_card_height * scale);
    const int horizontal_height = static_cast<int>(base_card_width * scale);

    const size_t mapped_count = std::min(slot_count, cards.size());

    for (size_t i = 0; i < mapped_count; ++i) {
        const placed_card& card = cards[i];

        int card_width = card.rotated ? horizontal_height : horizontal_width;
        int card_height = card.rotated ? horizontal_width : horizontal_height;

        table_slot* slot = slot_widgets[i];
        slot->set_rotated(card.rotated);
        slot->setGeometry(
            static_cast<int>(card.x), static_cast<int>(card.y), card_width,
            card_height
        );
        slot->show();
    }

    for (size_t i = mapped_count; i < slot_count; ++i) {
        slot_widgets[i]->hide();
    }
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

geometry_debug_snapshot table::build_geometry_debug_snapshot(
    const raster_cache::debug_snapshot& cache_snapshot
) const {
    int visible_slot_count = 0;
    QRect layout_bounds;
    int max_display_card_width = 0;
    int max_display_card_height = 0;

    for (const table_slot* slot_widget : slot_widgets) {
        if (slot_widget == nullptr || !slot_widget->isVisible()) {
            continue;
        }

        ++visible_slot_count;
        const QRect slot_rect = slot_widget->geometry();
        layout_bounds = layout_bounds.isNull() ? slot_rect
                                               : layout_bounds.united(slot_rect);
        max_display_card_width = std::max(max_display_card_width, slot_rect.width());
        max_display_card_height
            = std::max(max_display_card_height, slot_rect.height());
    }

    QSize cache_raster_size;
    if (displayed_shared_faces_key.has_value()) {
        const std::optional<raster_cache::result> displayed_ready
            = raster_cache_service.get_if_ready(*displayed_shared_faces_key);
        if (displayed_ready.has_value()) {
            cache_raster_size = displayed_ready->raster_size;
        }
    }
    if (cache_raster_size.isEmpty() && active_shared_bucket_px > 0) {
        cache_raster_size = QSize(active_shared_bucket_px, active_shared_bucket_px);
    }

    QSize preloaded_raster_size;
    if (warming_shared_faces_key.has_value()) {
        const std::optional<raster_cache::result> preloaded_ready
            = raster_cache_service.get_if_ready(*warming_shared_faces_key);
        if (preloaded_ready.has_value()) {
            preloaded_raster_size = preloaded_ready->raster_size;
        }
    }
    if (preloaded_raster_size.isEmpty() && warming_shared_bucket_px > 0) {
        preloaded_raster_size
            = QSize(warming_shared_bucket_px, warming_shared_bucket_px);
    }

    return geometry_debug_snapshot {
        .timestamp_ms = QDateTime::currentMSecsSinceEpoch(),
        .slot_count = static_cast<int>(slot_widgets.size()),
        .visible_slot_count = visible_slot_count,
        .window_size = size(),
        .layout_size = layout_bounds.isNull() ? QSize() : layout_bounds.size(),
        .display_card_size
        = QSize(max_display_card_width, max_display_card_height),
        .display_card_need_short_px = compute_max_card_face_need_short_px(),
        .active_bucket_px = active_shared_bucket_px,
        .warming_bucket_px = warming_shared_bucket_px,
        .cache_raster_size = cache_raster_size,
        .preloaded_raster_size = preloaded_raster_size,
        .coverage_percent = cache_snapshot.displayed_entry_coverage_percent,
        .coverage_window_ms
        = static_cast<qint64>(cache_snapshot.displayed_entry_window_ms),
        .unique_size_buckets = cache_snapshot.unique_size_buckets,
        .prewarm_in_flight = warming_shared_generation_id > 0,
        .active_generation_id = active_shared_generation_id,
        .warming_generation_id = warming_shared_generation_id,
    };
}

void table::emit_geometry_debug_snapshot() {
    emit debug_geometry_snapshot_updated(
        build_geometry_debug_snapshot(raster_cache_service.get_debug_snapshot())
    );
}

void table::on_slot_swap(table_slot* slot) {
    if (slot == nullptr) {
        return;
    }

    if (swap_source_slot == nullptr) {
        clear_copy_selection();
        swap_source_slot = slot;
        swap_source_slot->set_swap_selected(true);
        return;
    }

    if (swap_source_slot == slot) {
        swap_source_slot->set_swap_selected(false);
        swap_source_slot = nullptr;
        return;
    }

    auto it_first
        = std::find(slot_widgets.begin(), slot_widgets.end(), swap_source_slot);
    auto it_second = std::find(slot_widgets.begin(), slot_widgets.end(), slot);

    if (it_first == slot_widgets.end() || it_second == slot_widgets.end()) {
        swap_source_slot->set_swap_selected(false);
        swap_source_slot = nullptr;
        return;
    }

    std::iter_swap(it_first, it_second);

    swap_source_slot->set_swap_selected(false);
    slot->set_swap_selected(false);
    swap_source_slot = nullptr;

    update_layout();
}

void table::on_slot_copy(table_slot* slot) {
    if (slot == nullptr) {
        return;
    }

    if (copy_source_slot == nullptr) {
        clear_swap_selection();
        copy_source_slot = slot;
        copy_source_slot->set_swap_selected(true);
        update_copy_button_labels(copy_source_slot);
        return;
    }

    if (copy_source_slot == slot) {
        clear_copy_selection();
        return;
    }

    slot->apply_settings_from(*copy_source_slot);
    clear_copy_selection();
}

void table::on_slot_copy_all(table_slot* slot) {
    if (slot == nullptr) {
        return;
    }

    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr && slot_widget != slot) {
            slot_widget->apply_settings_from(*slot);
        }
    }
}

void table::on_pick_timeout() {
    if (!quiz_running) {
        return;
    }

    const int slot_count = static_cast<int>(slot_widgets.size());
    if (slot_count <= 0) {
        return;
    }

    if (current_mode == dealing_mode::simultaneous) {
        for (table_slot* slot_widget : slot_widgets) {
            if (slot_widget != nullptr && !slot_widget->is_deck_exhausted()
                && !slot_widget->is_quiz_prompt_active()) {
                slot_widget->advance_card();
                slot_widget->trigger_highlight(pick_interval_ms);
            }
        }
        if (all_slots_exhausted()) {
            handle_game_over();
        }
        return;
    }

    int slot_index = 0;
    if (current_mode == dealing_mode::random) {
        std::vector<int> eligible_slots;
        eligible_slots.reserve(static_cast<size_t>(slot_count));
        bool has_available_slots = false;
        for (int index = 0; index < slot_count; ++index) {
            table_slot* slot_widget
                = slot_widgets[static_cast<std::size_t>(index)];
            if (slot_widget != nullptr && !slot_widget->is_deck_exhausted()) {
                has_available_slots = true;
            }
            if (slot_widget != nullptr && !slot_widget->is_deck_exhausted()
                && !slot_widget->is_quiz_prompt_active()) {
                eligible_slots.push_back(index);
            }
        }
        if (eligible_slots.empty()) {
            if (!has_available_slots) {
                handle_game_over();
            }
            return;
        }
        const int pick_index = random_gen.uniform_int(
            0, static_cast<int>(eligible_slots.size()) - 1
        );
        slot_index = eligible_slots[static_cast<std::size_t>(pick_index)];
    } else {
        int attempts = 0;
        slot_index = next_slot_index;
        while (attempts < slot_count) {
            table_slot* slot_widget
                = slot_widgets[static_cast<std::size_t>(slot_index)];
            if (slot_widget != nullptr && !slot_widget->is_deck_exhausted()
                && !slot_widget->is_quiz_prompt_active()) {
                break;
            }
            slot_index = (slot_index + 1) % slot_count;
            ++attempts;
        }
        if (attempts >= slot_count) {
            if (all_slots_exhausted()) {
                handle_game_over();
            }
            return;
        }
        next_slot_index = (slot_index + 1) % slot_count;
    }

    if (slot_index < 0 || slot_index >= slot_count) {
        return;
    }

    table_slot* slot_widget
        = slot_widgets[static_cast<std::size_t>(slot_index)];
    if (slot_widget == nullptr) {
        return;
    }
    slot_widget->advance_card();
    slot_widget->trigger_highlight(pick_interval_ms);

    if (all_slots_exhausted()) {
        handle_game_over();
    }
}

void table::on_clock_tick(qint64 elapsed_ms, qint64 delta_ms) {
    Q_UNUSED(elapsed_ms);
    if (!quiz_running || quiz_paused) {
        return;
    }

    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->tick_highlight(static_cast<int>(delta_ms));
        }
    }

    pick_elapsed_ms += delta_ms;
    while (pick_elapsed_ms >= pick_interval_ms) {
        pick_elapsed_ms -= pick_interval_ms;
        on_pick_timeout();
    }
}

void table::clear_swap_selection() {
    if (swap_source_slot == nullptr) {
        return;
    }
    swap_source_slot->set_swap_selected(false);
    swap_source_slot = nullptr;
}

void table::clear_copy_selection() {
    if (copy_source_slot == nullptr) {
        return;
    }
    copy_source_slot->set_swap_selected(false);
    copy_source_slot = nullptr;
    update_copy_button_labels();
}

void table::update_copy_button_labels(table_slot* selected_slot) {
    const auto copy_label = str_label("Copy");
    const auto set_label = str_label("Set");
    const auto cancel_label = str_label("Cancel");
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget == nullptr) {
            continue;
        }
        slot_widget->set_copy_button_text(
            selected_slot == nullptr
                ? copy_label
                : (slot_widget == selected_slot ? cancel_label : set_label)
        );
    }
}

bool table::all_slots_exhausted() const {
    if (slot_widgets.empty()) {
        return false;
    }

    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr && !slot_widget->is_deck_exhausted()) {
            return false;
        }
    }
    return true;
}

void table::handle_game_over() {
    if (!quiz_running) {
        return;
    }
    quiz_running = false;
    quiz_paused = false;
    pick_elapsed_ms = 0;
    emit game_over();
}
