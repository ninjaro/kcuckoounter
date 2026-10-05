#include "table/table.hpp"
#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "packing/layout/equal_rectangles.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_settings.hpp"
#include "table/card_widget.hpp"
#include "table/table_slot.hpp"

#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QString>
#include <QStyle>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

table::table(BaseWidget* parent)
    : BaseWidget(parent)
    , slot_widgets()
    , swap_source_slot(nullptr)
    , copy_source_slot(nullptr)
    , card_orientation(card_orientation_mode::automatic)
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
    , shared_faces_refresh_queued(false)
    , active_card_sheet_source_id(card_sheet_source_path())
    , active_shared_bucket_px(0)
    , active_shared_generation_id(0)
    , warming_card_sheet_source_id()
    , warming_shared_bucket_px(0)
    , warming_shared_generation_id(0)
    , next_shared_generation_id(1)
    , retained_shared_faces_keys() {
    setMinimumHeight(88);
    gameplay_timer.setInterval(50);
    gameplay_timer.setTimerType(Qt::PreciseTimer);
    gameplay_layout_animation.setStartValue(0.0);
    gameplay_layout_animation.setEndValue(1.0);
    gameplay_layout_animation.setEasingCurve(QEasingCurve::InOutCubic);
    connect(
        &gameplay_layout_animation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) {
            if (!gameplay_layout)
                return;
            gameplay_layout_progress = value.toDouble();
            project_gameplay_layout();
        }
    );
    connect(
        &gameplay_layout_animation, &QVariantAnimation::finished, this,
        &table::finish_gameplay_layout_transition
    );
    connect(
        &gameplay_timer, &QTimer::timeout, this, &table::on_gameplay_clock_tick
    );
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

table::~table() {
    stop_gameplay_clock();
    gameplay_layout_animation.stop();
    // QObject deletes children after member destruction. Bound views must go
    // first; do not emit host notifications from a partially destroyed table.
    if (gameplay_owner)
        destroy_slot_widgets();
}

std::unique_ptr<table_slot> table::create_slot_widget() {
    auto slot = std::make_unique<table_slot>(this);
    slot->set_frame_style(frame_style);
    slot->set_action_style(action_style);
    slot->set_settings_style(settings_style);
    slot->set_quiz_presentation(answer_style, feedback_style);
    slot->set_allow_skipping(allow_skipping);
    slot->set_shared_card_faces_mode(true);
    connect(slot.get(), &table_slot::swap_clicked, this, &table::on_slot_swap);
    connect(slot.get(), &table_slot::copy_clicked, this, &table::on_slot_copy);
    connect(
        slot.get(), &table_slot::copy_all_clicked, this,
        &table::on_slot_copy_all
    );
    connect(
        slot.get(), &table_slot::rasterization_busy_changed, this,
        &table::on_slot_rasterization_busy_changed
    );
    connect(
        slot.get(), &table_slot::dialog_opened, this, &table::dialog_opened
    );
    connect(
        slot.get(), &table_slot::score_adjusted, this, &table::score_adjusted
    );
    connect(
        slot.get(), &table_slot::gameplay_configuration_changed, this, [this] {
            refresh_gameplay_session();
            emit gameplay_preparation_required();
        }
    );
    connect(
        slot.get(), &table_slot::gameplay_show_count_changed, this,
        &table::refresh_gameplay_session
    );
    connect(
        slot.get(), &table_slot::gameplay_correction_dismissed, this,
        &table::gameplay_corrections_changed
    );
    return slot;
}

void table::destroy_slot_widgets() {
    retire_gameplay_content_motion();
    delete gameplay_motion_layer;
    gameplay_motion_layer = nullptr;
    clear_swap_selection();
    clear_copy_selection();
    for (auto* slot : slot_widgets) {
        update_rasterization_state(slot, false);
        delete slot;
    }
    slot_widgets.clear();
}

bool table::install_gameplay_session(std::unique_ptr<gameplay::session> owner) {
    if (!owner || owner->size() == 0
        || owner->size() > gameplay::desktop_slot_technical_cap
        || (!gameplay_owner
            && (quiz_running
                || std::ranges::any_of(slot_widgets, [](const auto* slot) {
                       return slot->has_cards();
                   }))))
        return false;
    const auto packed = pack_gameplay_slots(owner->size());
    if (!packed)
        return false;
    auto paths = std::make_unique<gameplay::layout_transition>(*owner);
    if (!paths->repack(*packed))
        return false;
    std::vector<rotation_motion> rotations;
    rotations.reserve(owner->size());
    for (std::size_t index = 0; index < owner->size(); ++index) {
        const double angle
            = paths->deck_motion({ index })->to.rotated ? 0.0 : 90.0;
        rotations.push_back({ angle, angle });
    }
    std::vector<std::unique_ptr<table_slot>> staged;
    staged.reserve(owner->size());
    std::vector<table_slot*> next_slots;
    next_slots.reserve(owner->size());
    for (std::size_t index = 0; index < owner->size(); ++index) {
        auto slot = create_slot_widget();
        slot->hide();
        if (!slot->bind_gameplay_deck(*this, *owner, { index }))
            return false;
        next_slots.push_back(slot.get());
        staged.push_back(std::move(slot));
    }

    stop_gameplay_clock();
    gameplay_layout_animation.stop();
    gameplay_runtime_attached = false;
    gameplay_pick_elapsed_ms = 0;
    gameplay_runtime_failure.clear();
    gameplay_presented_batch.reset();
    emit gameplay_session_about_to_change();
    destroy_slot_widgets();
    gameplay_layout.reset();
    gameplay_owner = std::move(owner);
    ++gameplay_scene_revision;
    gameplay_layout = std::move(paths);
    gameplay_layout_progress = 1.0;
    gameplay_rotation_paths = std::move(rotations);
    slot_widgets
        = std::move(next_slots); // already allocated before replacement
    for (auto& slot : staged)
        (void)slot.release(); // QObject/table now owns each view
    quiz_running = false;
    quiz_paused = false;
    pick_elapsed_ms = 0;
    next_slot_index = 0;
    project_gameplay_layout();
    if (displayed_shared_faces_key)
        apply_shared_faces_entry(*displayed_shared_faces_key);
    schedule_card_preload();
    update_shared_card_face_need();
    emit gameplay_session_changed();
    emit gameplay_layout_changed();
    return true;
}

gameplay::session* table::active_gameplay_session() {
    return gameplay_owner.get();
}

const gameplay::session* table::active_gameplay_session() const {
    return gameplay_owner.get();
}

void table::refresh_gameplay_session() {
    if (!gameplay_owner)
        return;
    if (gameplay_owner->phase() != gameplay::session_phase::running)
        stop_gameplay_clock();
    if (gameplay_owner->phase() != gameplay::session_phase::paused)
        clear_swap_selection();
    for (auto* slot : slot_widgets)
        slot->refresh_gameplay_deck();
}

bool table::swap_gameplay_decks(
    gameplay::deck_id first, gameplay::deck_id second
) {
    auto rotations = gameplay_rotation_paths;
    for (auto& rotation : rotations)
        rotation.from
            = std::lerp(rotation.from, rotation.to, gameplay_layout_progress);
    if (!gameplay_layout
        || !gameplay_layout->swap_decks(
            first, second, gameplay_layout_progress
        ))
        return false;
    clear_swap_selection();
    for (std::size_t index = 0; index < rotations.size(); ++index)
        rotations[index].to
            = gameplay_layout->deck_motion({ index })->to.rotated ? 0.0 : 90.0;
    gameplay_rotation_paths = std::move(rotations);
    gameplay_content_motion = true;
    restart_gameplay_layout_transition();
    refresh_gameplay_session();
    emit gameplay_layout_changed(); // mapping-dependent projections follow Swap
    return true;
}

void table::clear_gameplay_session() {
    if (!gameplay_owner)
        return;
    stop_gameplay_clock();
    gameplay_layout_animation.stop();
    gameplay_runtime_attached = false;
    gameplay_pick_elapsed_ms = 0;
    gameplay_runtime_failure.clear();
    gameplay_presented_batch.reset();
    emit gameplay_session_about_to_change();
    destroy_slot_widgets();
    gameplay_layout.reset();
    gameplay_rotation_paths.clear();
    gameplay_layout_progress = 1.0;
    gameplay_owner.reset();
    ++gameplay_scene_revision;
    main_faces_runner.cancel_pending();
    if (preload_timer)
        preload_timer->stop();
    clear_shared_card_faces();
    emit gameplay_session_changed();
}

void table::set_slot_count(int count) {
    if (gameplay_owner)
        return;
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
            slot_widgets.push_back(create_slot_widget().release());
        }
    }

    if (next_slot_index >= count) {
        next_slot_index = 0;
    }

    update_layout();
    schedule_card_preload();
    update_shared_card_face_need();
}

void table::start_quiz(int quiz_type_index, bool wait_for_answers) {
    if (gameplay_owner)
        return;
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
    if (gameplay_owner)
        return;
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
    if (gameplay_owner)
        return;
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->set_paused(paused);
        }
    }

    quiz_paused = paused;
}

int table::pick_interval() const {
    return gameplay_owner ? gameplay_owner->pick_interval_ms()
                          : pick_interval_ms;
}

card_orientation_mode table::current_card_orientation() const {
    return card_orientation;
}

bool table::has_open_gameplay_settings() const {
    return gameplay_owner
        && std::ranges::any_of(slot_widgets, [](const auto* slot) {
               return slot->gameplay_editor_fields != nullptr;
           });
}

bool table::has_usable_gameplay_layout() const {
    return gameplay_owner
        && pack_gameplay_slots(gameplay_owner->size()).has_value();
}

bool table::has_gameplay_corrections() const {
    return gameplay_owner
        && std::ranges::any_of(slot_widgets, [](const auto* slot) {
               return slot->gameplay_feedback.has_value();
           });
}

std::vector<gameplay::quiz_answer> table::gameplay_corrections() const {
    std::vector<gameplay::quiz_answer> corrections;
    if (!gameplay_owner)
        return corrections;
    for (const auto* slot : slot_widgets)
        if (slot->gameplay_feedback)
            corrections.push_back(*slot->gameplay_feedback);
    return corrections;
}

void table::set_pick_interval(int interval_ms) {
    if (gameplay_owner)
        return;
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
    if (gameplay_owner)
        return;
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
    if (gameplay_owner)
        return;
    allow_skipping = allow;
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->set_allow_skipping(allow_skipping);
        }
    }
}

void table::set_card_orientation(card_orientation_mode orientation) {
    if (card_orientation == orientation) {
        return;
    }

    card_orientation = orientation;
    update_layout();
    schedule_card_preload();
    update_shared_card_face_need();
}

QVector<drill_slot_preferences> table::capture_drill_settings() const {
    if (gameplay_owner)
        return {};
    QVector<drill_slot_preferences> result;
    result.reserve(static_cast<qsizetype>(slot_widgets.size()));
    for (const auto* slot : slot_widgets)
        result.append(slot->capture_drill_settings());
    return result;
}

bool table::configure_drill(const training_drill& drill) {
    if (gameplay_owner
        || !is_drill_configuration_supported(drill, strategy_repository()))
        return false;
    clear_quiz();
    clear_swap_selection();
    clear_copy_selection();
    set_slot_count(static_cast<int>(drill.slot_settings.size()));
    for (qsizetype index = 0; index < drill.slot_settings.size(); ++index)
        slot_widgets[static_cast<std::size_t>(index)]->apply_drill_settings(
            drill.slot_settings[index]
        );
    set_pick_interval(drill.pickup_interval_ms);
    set_dealing_mode(drill.dealing_mode);
    set_allow_skipping(drill.allow_skipping);
    return true;
}

table_session_state table::capture_session_state() const {
    if (gameplay_owner)
        return {}; // no target data through legacy v1 codecs
    table_session_state state;
    state.slot_states.reserve(static_cast<qsizetype>(slot_widgets.size()));
    for (const table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            state.slot_states.append(slot_widget->capture_session_state());
        }
    }
    state.pick_elapsed_ms = pick_elapsed_ms;
    state.quiz_running = quiz_running;
    state.quiz_paused = quiz_paused;
    state.allow_skipping = allow_skipping;
    switch (current_mode) {
    case dealing_mode::sequential:
        state.dealing_mode = 0;
        break;
    case dealing_mode::random:
        state.dealing_mode = 1;
        break;
    case dealing_mode::simultaneous:
        state.dealing_mode = 2;
        break;
    }
    state.next_slot_index = next_slot_index;
    return state;
}

bool table::restore_session_state(const table_session_state& state) {
    if (gameplay_owner || !state.quiz_running || state.slot_states.isEmpty()
        || state.slot_states.size() > 16 || state.dealing_mode < 0
        || state.dealing_mode > 2 || state.next_slot_index < 0
        || state.next_slot_index >= state.slot_states.size()
        || state.pick_elapsed_ms < 0) {
        return false;
    }
    if (std::ranges::any_of(
            state.slot_states, [](const table_slot_session_state& slot_state) {
                return !table_slot::is_session_state_valid(slot_state);
            }
        )) {
        return false;
    }

    set_slot_count(static_cast<int>(state.slot_states.size()));
    if (slot_widgets.size() != static_cast<size_t>(state.slot_states.size())) {
        return false;
    }
    for (qsizetype index = 0; index < state.slot_states.size(); ++index) {
        table_slot* slot_widget = slot_widgets[static_cast<std::size_t>(index)];
        if (slot_widget == nullptr
            || !slot_widget->restore_session_state(
                state.slot_states.at(index)
            )) {
            clear_quiz();
            return false;
        }
    }

    set_dealing_mode(state.dealing_mode);
    allow_skipping = state.allow_skipping;
    next_slot_index = state.next_slot_index;
    pick_elapsed_ms = std::min<qint64>(
        state.pick_elapsed_ms, std::max(0, pick_interval_ms - 1)
    );
    quiz_running = true;
    quiz_paused = true;
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget != nullptr) {
            slot_widget->set_allow_skipping(allow_skipping);
            slot_widget->set_paused(true);
        }
    }
    schedule_card_preload();
    update_shared_card_face_need();
    return true;
}

void table::paintEvent(QPaintEvent* event) {
    BaseWidget::paintEvent(event);

    QPainter painter(this);
    painter.fillRect(rect(), theme_settings::table_color());
}

bool table::event(QEvent* event) {
    const bool screen_metrics_changed = event != nullptr
        && (event->type() == QEvent::DevicePixelRatioChange
            || event->type() == QEvent::ScreenChangeInternal);
    const bool accepted = BaseWidget::event(event);

    if (gameplay_owner && event != nullptr
        && (event->type() == QEvent::Hide
            || (event->type() == QEvent::StyleChange
                && style()->styleHint(
                       QStyle::SH_Widget_Animation_Duration, nullptr, this
                   ) <= 0)))
        finish_gameplay_layout_transition();

    if (screen_metrics_changed) {
        // Let Qt finish updating the window/screen association before reading
        // devicePixelRatioF(). The context-bound callback is discarded if the
        // table is destroyed while the event is being handled.
        QTimer::singleShot(0, this, [this]() {
            update_layout();
            schedule_card_preload();
            update_shared_card_face_need();
        });
    }

    return accepted;
}

void table::resizeEvent(QResizeEvent* event) {
    BaseWidget::resizeEvent(event);
    update_layout();
    schedule_card_preload();
    update_shared_card_face_need();
}

void table::set_frame_style(slot_frame_style style) {
    frame_style = style;
    for (table_slot* slot : slot_widgets) {
        slot->set_frame_style(style);
    }
}

void table::set_quiz_presentation(
    quiz_answer_style answer, quiz_feedback_style feedback
) {
    answer_style = answer;
    feedback_style = feedback;
    for (table_slot* slot : slot_widgets) {
        slot->set_quiz_presentation(answer, feedback);
    }
}

void table::set_action_style(slot_action_style style) {
    action_style = style;
    for (auto* slot : slot_widgets) {
        slot->set_action_style(style);
    }
}

void table::set_settings_style(slot_settings_style style) {
    settings_style = style;
    for (auto* slot : slot_widgets)
        slot->set_settings_style(style);
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
    // The shared packer samples the active SVG ratio for every layout. A theme
    // may change that ratio, so geometry must be refreshed before calculating
    // the next raster-cache demand.
    update_layout();

    if (source_changed) {
        update_shared_card_face_need(true, true);
        return;
    }

    update_shared_card_face_need();
}

void table::on_slot_swap(table_slot* slot) {
    if (slot == nullptr) {
        return;
    }
    if (gameplay_owner
        && (gameplay_owner->phase() != gameplay::session_phase::paused
            || std::ranges::find(slot_widgets, slot) == slot_widgets.end()))
        return;

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

    if (gameplay_owner) {
        const auto first = *swap_source_slot->gameplay_deck_id();
        const auto second = *slot->gameplay_deck_id();
        (void)swap_gameplay_decks(first, second);
        clear_swap_selection();
        return;
    }
    std::iter_swap(it_first, it_second);

    swap_source_slot->set_swap_selected(false);
    slot->set_swap_selected(false);
    swap_source_slot = nullptr;

    update_layout();
}

void table::on_slot_copy(table_slot* slot) {
    if (gameplay_owner)
        return;
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
    if (gameplay_owner)
        return;
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
    if (gameplay_owner || !quiz_running) {
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
    if (gameplay_owner || !quiz_running || quiz_paused) {
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
    for (table_slot* slot_widget : slot_widgets) {
        if (slot_widget == nullptr) {
            continue;
        }
        slot_widget->set_copy_action(
            selected_slot == nullptr ? table_slot::copy_action::copy
                                     : (slot_widget == selected_slot
                                            ? table_slot::copy_action::cancel
                                            : table_slot::copy_action::apply)
        );
    }
}

bool table::all_slots_exhausted() const {
    if (slot_widgets.empty()) {
        return false;
    }

    return std::ranges::all_of(slot_widgets, [](const table_slot* slot_widget) {
        return slot_widget == nullptr || slot_widget->is_deck_exhausted();
    });
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
