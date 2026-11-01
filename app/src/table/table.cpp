#include "table/table.hpp"
#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "packing/layout/equal_rectangles.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "table/card_widget.hpp"
#include "table/table_slot.hpp"
#include <shared_ui/qt/presentation.hpp>

#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QString>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

table::table(BaseWidget* parent)
    : BaseWidget(parent)
    , slot_widgets()
    , scene_motion(new shared_ui::qt::scene_transition(*this))
    , swap_source_slot(nullptr)
    , card_orientation(card_orientation_mode::automatic)
    , rasterizing_slots()
    , rasterization_busy(false)
    , card_images(this) {
    auto presentation = shared_ui::qt::presentation_for(*this);
    if (!presentation.context.felt_surface.isValid()) {
        presentation.context.felt_surface
            = theme_palette_registry::option(
                  load_table_appearance_preferences().palette
            )
                  .base_color();
        shared_ui::qt::apply_presentation(*this, presentation);
    }
    connect(
        &current_card_sheet_metadata(), &card_sheet_metadata::geometry_changed,
        this,
        [this] {
            update_layout();
            schedule_card_preload();
        },
        Qt::QueuedConnection
    );
    setMinimumHeight(88);
    gameplay_timer.setInterval(50);
    gameplay_timer.setTimerType(Qt::PreciseTimer);
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    scene_motion->set_animation_enabled(false);
#endif
    connect(
        scene_motion, &shared_ui::qt::scene_transition::frame_changed, this,
        &table::project_gameplay_layout
    );
    connect(
        scene_motion, &shared_ui::qt::scene_transition::finished, this,
        [this] {
            if (gameplay_owner)
                update_card_image_demands();
        }
    );
    connect(
        &gameplay_timer, &QTimer::timeout, this, &table::on_gameplay_clock_tick
    );
    connect(
        &card_images, &card_image_consumer::images_changed, this,
        &table::on_card_images_changed, Qt::QueuedConnection
    );
}

table::~table() {
    const QSignalBlocker teardown_notifications(this);
    // Withdraw only this table; other consumers retain shared jobs and images.
    card_images.clear();
    stop_gameplay_clock();
    scene_motion->clear();
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
    slot->set_presented_image_mode(true);
    connect(
        slot->card_widget_internal, &card_widget::image_demand_changed, this,
        &table::schedule_card_preload
    );
    connect(slot.get(), &table_slot::swap_clicked, this, &table::on_slot_swap);
    connect(
        slot.get(), &table_slot::rasterization_busy_changed, this,
        &table::on_slot_rasterization_busy_changed
    );
    connect(
        slot.get(), &table_slot::gameplay_configuration_changed, this,
        [this] {
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
    connect(
        slot.get(), &table_slot::touch_tapped, this,
        [this](table_slot* tapped) {
            if (const auto deck = tapped->gameplay_deck_id())
                emit gameplay_deck_tapped(static_cast<int>(deck->value));
        }
    );
    slot->set_touch_presentation(touch_presentation);
    return slot;
}

void table::destroy_slot_widgets() {
    scene_motion->clear();
    clear_swap_selection();
    for (auto* slot : slot_widgets) {
        update_rasterization_state(slot, false);
        delete slot;
    }
    slot_widgets.clear();
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
    if (!gameplay_layout
        || !gameplay_layout->swap_decks(
            first, second, scene_motion->progress()
        ))
        return false;
    clear_swap_selection();
    scene_motion->transition(layout_tracks(), true);
    refresh_gameplay_session();
    emit gameplay_layout_changed(); // mapping-dependent projections follow Swap
    return true;
}

void table::clear_gameplay_session() {
    if (gameplay_start_pending || !gameplay_owner)
        return;
    stop_gameplay_clock();
    scene_motion->cancel();
    gameplay_runtime_attached = false;
    gameplay_pick_elapsed_ms = 0;
    gameplay_runtime_failure.clear();
    gameplay_presented_batch.reset();
    emit gameplay_session_about_to_change();
    destroy_slot_widgets();
    gameplay_completion = {};
    gameplay_layout.reset();
    gameplay_owner.reset();
    ++gameplay_scene_revision;
    clear_presented_card_images();
    emit gameplay_session_changed();
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

void table::set_card_orientation(card_orientation_mode orientation) {
    if (card_orientation == orientation) {
        return;
    }

    card_orientation = orientation;
    update_layout();
    schedule_card_preload();
    update_card_image_demands();
}

void table::paintEvent(QPaintEvent* event) {
    BaseWidget::paintEvent(event);

    QPainter painter(this);
    const auto color
        = shared_ui::qt::presentation_for(*this).context.felt_surface;
    painter.fillRect(
        rect(),
        color.isValid() ? color
                        : theme_palette_registry::option(
                              load_table_appearance_preferences().palette
                          )
                              .base_color()
    );
}

bool table::event(QEvent* event) {
    const bool screen_metrics_changed = event != nullptr
        && (event->type() == QEvent::DevicePixelRatioChange
            || event->type() == QEvent::ScreenChangeInternal);
    const bool accepted = BaseWidget::event(event);

    if (screen_metrics_changed) {
        // Let Qt finish updating the window/screen association before reading
        // devicePixelRatioF(). The context-bound callback is discarded if the
        // table is destroyed while the event is being handled.
        QTimer::singleShot(0, this, [this]() {
            update_layout();
            schedule_card_preload();
            update_card_image_demands();
        });
    }

    return accepted;
}

void table::resizeEvent(QResizeEvent* event) {
    BaseWidget::resizeEvent(event);
    update_layout();
    schedule_card_preload();
    update_card_image_demands();
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
    const bool source_changed = desired_card_source != next_source_id;

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
        update_card_image_demands(true, true);
        return;
    }

    update_card_image_demands();
}

void table::refresh_material() {
    for (table_slot* slot_widget : slot_widgets) {
        slot_widget->update_overlay_palette(); // also restyles quiz controls
        slot_widget->update_action_presentation();
        slot_widget->update_overlay_layout();
        slot_widget->card_widget_internal->update();
    }
}

void table::set_touch_presentation(bool enabled) {
    if (touch_presentation == enabled)
        return;
    touch_presentation = enabled;
    for (auto* slot : slot_widgets)
        slot->set_touch_presentation(enabled);
}

void table::set_touch_ordinal(
    gameplay::deck_id id, const QString& ordinal, bool done
) {
    if (id.value < slot_widgets.size())
        slot_widgets[id.value]->set_touch_ordinal(ordinal, done);
}

void table::set_touch_dealing(gameplay::deck_id id, bool dealing) {
    if (id.value < slot_widgets.size())
        slot_widgets[id.value]->set_touch_dealing(dealing);
}

bool table::choose_swap_deck(gameplay::deck_id id) {
    if (!gameplay_owner || id.value >= slot_widgets.size())
        return false;
    on_slot_swap(slot_widgets[id.value]);
    return true;
}

void table::cancel_swap_choice() { clear_swap_selection(); }

void table::on_slot_swap(table_slot* slot) {
    if (slot == nullptr) {
        return;
    }
    if (gameplay_owner
        && (gameplay_owner->phase() != gameplay::session_phase::paused
            || std::ranges::find(slot_widgets, slot) == slot_widgets.end()))
        return;

    if (swap_source_slot == nullptr) {
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
}

void table::clear_swap_selection() {
    if (swap_source_slot == nullptr) {
        return;
    }
    swap_source_slot->set_swap_selected(false);
    swap_source_slot = nullptr;
}
