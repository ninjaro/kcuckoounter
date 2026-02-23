#include "widget/table.hpp"
#include "card_helpers/card_packer.hpp"
#include "card_helpers/card_sheet.hpp"
#include "helpers/str_label.hpp"
#include "helpers/theme_settings.hpp"
#include "widget/table_slot.hpp"

#include <QColor>
#include <QGridLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QString>

#include <algorithm>
#include <limits>
#include <memory>

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
    , raster_cache_service(this) {
    setMinimumHeight(88);
    QObject::connect(
        &main_faces_runner, &rasterization_runner::rasterization_requested,
        this, &table::on_shared_rasterization_requested
    );
    QObject::connect(
        &raster_cache_service, &svg_raster_cache_service::result_updated, this,
        &table::on_shared_cache_result_updated
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
    BaseWidget::resizeEvent(event);
    update_layout();
    schedule_card_preload();
    update_shared_card_face_need();
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
    update();
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->apply_theme();
        }
    }
    update_shared_card_face_need();
}

bool table::is_rasterization_busy() const { return rasterization_busy; }

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

    const svg_raster_cache_service::request req {
        .name_space = svg_raster_cache_service::cache_namespace::main,
        .kind = svg_raster_cache_service::resource_kind::card_sheet_faces,
        .source_id = str_label("assets/cards.svg"),
        .render_scope = str_label("all_faces"),
        .need_short_px = std::max(1, compute_max_card_face_need_short_px()),
        .target_bucket_px = target_cache_px,
        .high_priority = false,
        .interactive = true,
        .preview = false,
    };

    const svg_raster_cache_service::submit_outcome outcome
        = raster_cache_service.submit_request(req);
    if (outcome.state == svg_raster_cache_service::request_state::cache_hit) {
        apply_shared_card_faces_from_entry(outcome.key);
    }

    if (outcome.state == svg_raster_cache_service::request_state::start_async
        || outcome.state
            == svg_raster_cache_service::request_state::cache_hit) {
        main_faces_runner.set_cached_short_px(target_cache_px);
    }
}

void table::on_shared_cache_result_updated(
    const svg_raster_cache_service::entry_key& key
) {
    apply_shared_card_faces_from_entry(key);
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

void table::apply_shared_card_faces_from_entry(
    const svg_raster_cache_service::entry_key& key
) {
    if (key.name_space != svg_raster_cache_service::cache_namespace::main
        || key.kind != svg_raster_cache_service::resource_kind::card_sheet_faces
        || key.source_id != str_label("assets/cards.svg")
        || key.render_scope != str_label("all_faces")) {
        return;
    }

    const std::optional<svg_raster_cache_service::result> ready
        = raster_cache_service.get_if_ready(key);
    if (!ready.has_value() || ready->face_images.isEmpty()) {
        return;
    }

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

    const bool is_busy = !rasterizing_slots.isEmpty();
    if (rasterization_busy == is_busy) {
        return;
    }

    rasterization_busy = is_busy;
    emit rasterization_busy_changed(is_busy);
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
