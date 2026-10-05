#include "table/table_slot.hpp"

#include "arch/android_ui.hpp"
#include "arch/asset_locator.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/card_widget.hpp"
#include "table/gameplay_setup.hpp"
#include "table/infinity_spinbox.hpp"
#include "table/settings_template.hpp"
#include "table/slot_settings.hpp"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QToolButton>
#include <QToolTip>
#include <QtGlobal>

#include <algorithm>

table_slot::table_slot(BaseWidget* parent)
    : BaseWidget(parent)
    , current_phase(slot_phase::paused)
    , card_widget_internal(new card_widget(this))
    , overlay_widget(nullptr)
    , settings_bar_widget(nullptr)
    , swap_bar_widget(nullptr)
    , overlay_layout(nullptr)
    , swap_layout(nullptr)
    , infinity_check_box(nullptr)
    , deck_count_spin_box(nullptr)
    , strategy_combo_box(nullptr)
    , info_button(nullptr)
    , show_card_indexing(nullptr)
    , show_strategy_name(nullptr)
    , training_check_box(nullptr)
    , swap_button(nullptr)
    , settings_button(nullptr)
    , copy_button(nullptr)
    , copy_all_button(nullptr)
    , quiz_bar_widget(nullptr)
    , quiz_layout(nullptr)
    , quiz_prompt_widget(nullptr)
    , quiz_feedback_widget(nullptr)
    , quiz_weight_label(nullptr)
    , quiz_spin_box(nullptr)
    , quiz_answer_button(nullptr)
    , quiz_skip_button(nullptr)
    , quiz_feedback_label(nullptr)
    , quiz_continue_button(nullptr)
    , settings_overlay_visible(true)
    , is_rotated(false)
    , use_dialog_for_settings(false)
    , deck_count_minimum(1)
    , quiz_prompt_active(false)
    , quiz_feedback_active(false)
    , quiz_continue_visible(false)
    , allow_skipping_flag(true)
    , last_quiz_input_value(0) {
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

void table_slot::set_allow_skipping(bool allow) {
    if (gameplay_owner)
        return;
    if (allow_skipping_flag == allow) {
        return;
    }
    allow_skipping_flag = allow;
    update_quiz_controls_visibility();
}

void table_slot::start_quiz(int quiz_type_index) {
    if (gameplay_owner)
        return;
    finish_settings_editor(false);
    int decks_count = 1;
    if (deck_count_spin_box != nullptr) {
        decks_count = deck_count_spin_box->value();
    }
    if (decks_count <= 0) {
        decks_count = 1;
    }

    const bool is_infinity = is_infinity_enabled();
    if (card_widget_internal != nullptr) {
        card_widget_internal->start_quiz(
            quiz_type_index, decks_count, is_infinity
        );
    }

    quiz_prompt_active = false;
    quiz_feedback_active = false;
    quiz_continue_visible = false;
    last_quiz_input_value = 0;
    update_overlay_layout();
    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->hide();
    }
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_hide_cards(false);
        card_widget_internal->set_table_marking_source(
            bundled_asset_path(QStringLiteral("cuckoo.svg"))
        );
    }

    sync_card_display_settings();
    set_paused(false);
}

void table_slot::clear_quiz() {
    if (gameplay_owner)
        return;
    finish_settings_editor(false);
    if (card_widget_internal != nullptr) {
        card_widget_internal->clear_quiz();
        card_widget_internal->set_hide_cards(false);
        card_widget_internal->set_table_marking_source(
            bundled_asset_path(QStringLiteral("cuckoo.svg"))
        );
    }
    quiz_prompt_active = false;
    quiz_feedback_active = false;
    quiz_continue_visible = false;
    last_quiz_input_value = 0;
    update_overlay_layout();
    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->hide();
    }
    set_paused(true);
}

void table_slot::set_paused(bool paused) {
    if (gameplay_owner)
        return;
    if (!paused)
        finish_settings_editor(false);
    const slot_phase new_phase
        = paused ? slot_phase::paused : slot_phase::running;
    current_phase = new_phase;

    const bool has_deck
        = card_widget_internal != nullptr && card_widget_internal->has_cards();
    const bool show_overlay = paused || !has_deck || quiz_prompt_active;

    if (overlay_widget != nullptr) {
        overlay_widget->setVisible(show_overlay);
    }

    if (!has_deck) {
        settings_overlay_visible = true;
    }

    if (settings_bar_widget != nullptr) {
        const bool can_show_settings_inline = show_overlay
            && settings_overlay_visible && !use_dialog_for_settings
            && !quiz_prompt_active
            && settings_style == slot_settings_style::classic;
        settings_bar_widget->setVisible(can_show_settings_inline);
    }

    if (swap_bar_widget != nullptr) {
        swap_bar_widget->setVisible(show_overlay && !quiz_prompt_active);
    }

    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->setVisible(show_overlay && quiz_prompt_active);
    }

    if (swap_button != nullptr) {
        swap_button->setEnabled(show_overlay);
    }

    update_action_presentation();
    update_settings_button_state();

    if (card_widget_internal != nullptr) {
        card_widget_internal->set_running(current_phase == slot_phase::running);
    }
    if (deck_count_spin_box != nullptr) {
        if (paused) {
            if (has_deck) {
                const int current_value = deck_count_spin_box->value();
                deck_count_spin_box->setMinimum(
                    std::max(deck_count_minimum, current_value)
                );
            } else {
                deck_count_spin_box->setMinimum(deck_count_minimum);
            }
        } else {
            deck_count_spin_box->setMinimum(deck_count_minimum);
        }
    }
    update_lockable_settings();
    update_compact_controls();
    update();
}

void table_slot::advance_card() {
    if (gameplay_owner || current_phase != slot_phase::running
        || quiz_prompt_active) {
        return;
    }

    if (card_widget_internal != nullptr) {
        card_widget_internal->advance_card();
        const int current_position = card_widget_internal->current_position();
        if (current_position >= 0 && (current_position + 1) % 30 == 0) {
            show_quiz_prompt();
        }
    }
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

void table_slot::set_shared_card_faces(
    const QVector<QImage>& face_images, const QSize& raster_size
) {
    if (card_widget_internal == nullptr) {
        return;
    }

    card_widget_internal->set_shared_card_faces(face_images, raster_size);
}

void table_slot::clear_shared_card_faces() {
    if (card_widget_internal == nullptr) {
        return;
    }

    card_widget_internal->clear_shared_card_faces();
}

bool table_slot::has_shared_card_faces() const {
    if (card_widget_internal == nullptr) {
        return false;
    }

    return card_widget_internal->has_shared_card_faces();
}

void table_slot::set_shared_card_faces_mode(bool enabled) {
    if (card_widget_internal == nullptr) {
        return;
    }

    card_widget_internal->set_shared_card_faces_mode(enabled);
}

void table_slot::set_frame_style(slot_frame_style style) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_frame_style(style);
    }
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
    const auto* focused = QApplication::focusWidget();
    const bool had_focus
        = focused != nullptr && swap_bar_widget->isAncestorOf(focused);
    action_style = style;
    QToolTip::hideText();
    update_action_presentation();
    update_overlay_layout();
    if (had_focus && !controls_dialog && compact_controls_button->isVisible()) {
        compact_controls_button->setFocus(Qt::OtherFocusReason);
    }
#else
    Q_UNUSED(style);
#endif
}

void table_slot::set_settings_style(slot_settings_style style) {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    if (settings_style == style)
        return;
    settings_style = style;
    set_paused(current_phase == slot_phase::paused);
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
    update();
}

void table_slot::on_swap_button_clicked() {
    if (!gameplay_owner
        || gameplay_owner->phase() == gameplay::session_phase::paused)
        emit swap_clicked(this);
}

void table_slot::on_copy_button_clicked() {
    if (!gameplay_owner)
        emit copy_clicked(this);
}

void table_slot::on_copy_all_button_clicked() {
    if (!gameplay_owner)
        emit copy_all_clicked(this);
}

void table_slot::set_copy_action(copy_action action) {
    current_copy_action = action;
    update_action_presentation();
    update_action_button_state();
}

bool table_slot::is_deck_exhausted() const {
    return card_widget_internal != nullptr
        && card_widget_internal->is_deck_exhausted();
}

bool table_slot::is_quiz_prompt_active() const {
    return gameplay_owner ? gameplay_owner->deck(gameplay_id)->quiz.has_value()
                          : quiz_prompt_active;
}
