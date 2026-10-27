#include "table/table_slot.hpp"
#include <shared_ui/qt/adaptive_surface.hpp>
#include <shared_ui/qt/answer_panel.hpp>
#include <shared_ui/qt/presentation.hpp>

#include "arch/android_ui.hpp"
#include "arch/asset_locator.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "table/card_widget.hpp"
#include "table/gameplay_setup.hpp"
#include "table/settings_template.hpp"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QToolButton>
#include <QToolTip>
#include <QtGlobal>

#include <algorithm>

table_slot::table_slot(BaseWidget* parent)
    : BaseWidget(parent)
    , card_widget_internal(new card_widget(this))
    , overlay_widget(nullptr)
    , swap_bar_widget(nullptr)
    , overlay_layout(nullptr)
    , swap_layout(nullptr)
    , quiz_bar_widget(nullptr)
    , quiz_weight_label(nullptr)
    , quiz_answer_button(nullptr)
    , quiz_skip_button(nullptr)
    , quiz_feedback_label(nullptr)
    , quiz_continue_button(nullptr)
    , is_rotated(false)
    , quiz_prompt_active(false)
    , quiz_feedback_active(false) {
    auto presentation = shared_ui::qt::presentation_for(*this);
    if (!presentation.context.felt_surface.isValid()) {
        presentation.context.felt_surface
            = theme_palette_registry::option(
                  load_table_appearance_preferences().palette
            )
                  .base_color();
        shared_ui::qt::apply_presentation(*this, presentation);
    }
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    card_widget_internal->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Expanding
    );
    QObject::connect(
        card_widget_internal, &card_widget::rasterization_busy_changed, this,
        &table_slot::rasterization_busy_changed
    );
    setup_overlay();
    card_widget_internal->show();
}

table_slot::~table_slot() = default;

bool table_slot::has_cards() const { return card_widget_internal->has_cards(); }

std::optional<gameplay::deck_id> table_slot::gameplay_deck_id() const {
    return gameplay_owner ? std::optional { gameplay_id } : std::nullopt;
}

void table_slot::set_swap_selected(bool selected) {
    if (card_widget_internal == nullptr) {
        return;
    }
    card_widget_internal->set_swap_selected(selected);
    update_action_button_state();
}

bool table_slot::swap_selected() const {
    return card_widget_internal != nullptr
        && card_widget_internal->swap_selected();
}

void table_slot::set_rotated(bool rotated) {
    if (is_rotated == rotated) {
        return;
    }

    is_rotated = rotated;
    update_overlay_layout();
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_slot_rotated(rotated);
    }
    updateGeometry();
    update();
}

void table_slot::set_gameplay_layout_rotation(std::optional<qreal> degrees) {
    if (gameplay_owner && card_widget_internal)
        card_widget_internal->set_gameplay_layout_rotation(degrees);
}

void table_slot::trigger_highlight(int duration_ms) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->trigger_highlight(duration_ms);
    }
}

void table_slot::tick_highlight(int delta_ms) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->tick_highlight(delta_ms);
    }
}

void table_slot::prepare_card_faces() {
    if (card_widget_internal != nullptr) {
        card_widget_internal->prepare_card_faces();
    }
}

int table_slot::card_face_need_short_px() const {
    if (card_widget_internal == nullptr) {
        return 0;
    }

    return card_widget_internal->card_face_target_short_px();
}

void table_slot::set_presented_card_images(
    const QHash<QString, QImage>& images
) {
    if (card_widget_internal == nullptr) {
        return;
    }

    card_widget_internal->set_presented_card_images(images);
}

void table_slot::clear_presented_card_images() {
    if (card_widget_internal == nullptr) {
        return;
    }

    card_widget_internal->clear_presented_card_images();
}

bool table_slot::has_presented_card_images() const {
    if (card_widget_internal == nullptr) {
        return false;
    }

    return card_widget_internal->has_presented_card_images();
}

void table_slot::set_presented_image_mode(bool enabled) {
    if (card_widget_internal == nullptr) {
        return;
    }

    card_widget_internal->set_presented_image_mode(enabled);
}

void table_slot::set_frame_style(slot_frame_style style) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_frame_style(style);
    }
    update_overlay_margins();
}

void table_slot::update_overlay_margins() {
    if (overlay_layout == nullptr || card_widget_internal == nullptr)
        return;
    // D-BAR-2: actions sit on the felt, clear of the piping's bottom legends.
    overlay_layout->setContentsMargins(
        2, 2, 2, std::max(2, card_widget_internal->bottom_legend_clearance())
    );
}

void table_slot::set_quiz_presentation(
    quiz_answer_style answer, quiz_feedback_style feedback
) {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    if (answer_style == answer && feedback_style == feedback) {
        return;
    }
    answer_style = answer;
    feedback_style = feedback;
    update_quiz_presentation();
#else
    Q_UNUSED(answer);
    Q_UNUSED(feedback);
#endif
}

void table_slot::set_action_style(slot_action_style style) {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    if (action_style == style) {
        return;
    }
    action_style = style;
    QToolTip::hideText();
    update_action_presentation();
    update_overlay_layout();
#else
    Q_UNUSED(style);
#endif
}

void table_slot::set_settings_style(slot_settings_style style) {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    if (settings_style == style)
        return;
    settings_style = style;
    update_compact_controls();
    place_settings_editor();
#else
    Q_UNUSED(style);
#endif
}

void table_slot::apply_theme() {
    if (card_widget_internal != nullptr) {
        card_widget_internal->sync_card_sheet_source();
        card_widget_internal->update();
    }
    update_overlay_palette();
    // Felt's actions follow the table colour too.
    update_action_presentation();
    update();
}

void table_slot::on_swap_button_clicked() {
    if (!gameplay_owner
        || gameplay_owner->phase() == gameplay::session_phase::paused)
        emit swap_clicked(this);
}

bool table_slot::is_quiz_prompt_active() const {
    return gameplay_owner ? gameplay_owner->deck(gameplay_id)->quiz.has_value()
                          : quiz_prompt_active;
}
