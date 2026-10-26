#include "table/table_slot.hpp"
#include <shared_ui/qt/adaptive_surface.hpp>
#include <shared_ui/qt/answer_panel.hpp>
#include <shared_ui/qt/editor_panel.hpp>
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
#include "table/infinity_spinbox.hpp"
#include "table/settings_template.hpp"
#include "table/slot_settings.hpp"
#include <QToolButton>

#include <shared_ui/qt/action_group.hpp>

#include <QAction>
#include <QBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QResizeEvent>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>

void table_slot::update_action_presentation() {
    if (slot_actions == nullptr) {
        return;
    }
    const bool running_with_deck = current_phase == slot_phase::running
        && card_widget_internal->has_cards();
    details_action->setText(
        running_with_deck ? str_label("Info") : str_label("Details")
    );
    copy_settings_action->setText(
        current_copy_action == copy_action::cancel      ? str_label("Cancel")
            : current_copy_action == copy_action::apply ? str_label("Set")
                                                        : str_label("Copy")
    );
    copy_settings_action->setToolTip(
        current_copy_action == copy_action::cancel
            ? str_label("Cancel copying settings")
            : current_copy_action == copy_action::apply
            ? str_label("Apply the selected slot's settings to this slot")
            : str_label("Copy settings from this slot")
    );
    // Slot action style → Appearance form: Classic labelled buttons, the
    // Icon rail as a compact column, Labelled pills (D-BAR-2: Details is the
    // paper pill). Native chrome has no pill form; it keeps labelled buttons.
    slot_actions->set_variant(
        action_style == slot_action_style::rail
            ? shared_ui::qt::action_variant::compact
            : action_style == slot_action_style::pills
            ? shared_ui::qt::action_variant::pills
            : shared_ui::qt::action_variant::labelled
    );
    slot_actions->set_primary(
        action_style == slot_action_style::pills ? details_action : nullptr
    );
    const bool felt = shared_ui::qt::presentation_for(*this).value
        == shared_ui::qt::material::felt;
    slot_actions->set_presentation(
        felt ? shared_ui::qt::material::felt : shared_ui::qt::material::system,
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
        shared_ui::qt::platform_profile::android,
#elif defined(KC_KDE)
        shared_ui::qt::platform_profile::desktop_kde,
#else
        shared_ui::qt::platform_profile::desktop_qt,
#endif
        // Felt follows the selected table colour, as the rest of the chrome.
        shared_ui::qt::presentation_for(*this).context
    );
}

bool table_slot::focus_details_action() {
    auto* button = slot_actions != nullptr
        ? slot_actions->button_for(details_action)
        : nullptr;
    if (button == nullptr || !button->isVisible())
        return false;
    button->setFocus(Qt::OtherFocusReason);
    return true;
}

void table_slot::setup_quiz_chips() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    quiz_spin_box->installEventFilter(this);
    quiz_spin_box->findChild<QLineEdit*>()->installEventFilter(this);
    QList<shared_ui::qt::answer_step> steps;
    for (const int step : { -2, -1, 1, 2 }) {
#ifdef KC_KDE
        const QString text = step > 0 ? i18n("+%1", step) : i18n("−%1", -step);
        const QString description = step > 0
            ? i18n("Add %1 to answer", step)
            : i18n("Subtract %1 from answer", -step);
#else
        const QString text = step > 0 ? str_label("+%1").arg(step)
                                      : str_label("−%1").arg(-step);
        const QString description = step > 0
            ? str_label("Add %1 to answer").arg(step)
            : str_label("Subtract %1 from answer").arg(-step);
#endif
        auto* button = new QPushButton(text, quiz_chip_widget);
        button->setObjectName(QStringLiteral("quiz_chip_%1").arg(step));
        button->setAccessibleName(description);
        button->setToolTip(button->accessibleName());
        button->setProperty("chip_step", step);
        button->installEventFilter(this);
        steps.append({ button, step, std::abs(step) == 2 });
        connect(button, &QPushButton::clicked, this, [this, step] {
            if (!quiz_prompt_active || quiz_feedback_active) {
                return;
            }
            if (gameplay_owner) {
                const auto value = gameplay_input_value();
                if (!gameplay_input->isEnabled() || !value
                    || (step > 0
                        && *value
                            > std::numeric_limits<std::int64_t>::max() - step)
                    || (step < 0
                        && *value
                            < std::numeric_limits<std::int64_t>::min() - step))
                    return;
                gameplay_input->setText(QString::number(*value + step));
                on_gameplay_input_edited();
                return;
            }
            quiz_spin_box->interpretText();
            quiz_spin_box->setValue(quiz_spin_box->value() + step);
        });
        connect(
            quiz_spin_box, &QSpinBox::valueChanged, button,
            [this, button, step](int value) {
                button->setEnabled(
                    step > 0 ? value < quiz_spin_box->maximum()
                             : value > quiz_spin_box->minimum()
                );
            }
        );
    }
    quiz_bar_widget->set_steps(steps);
#endif
}

void table_slot::update_quiz_presentation() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    const bool felt = shared_ui::qt::presentation_for(*this).value
        == shared_ui::qt::material::felt;
    quiz_weight_label->setText(
        felt                 ? str_label("YOUR COUNT")
            : gameplay_owner ? str_label("Count")
                             : str_label("Weight")
    );
    quiz_bar_widget->set_available_area(size());
    quiz_bar_widget->set_variants(
        answer_style == quiz_answer_style::chips
            ? shared_ui::qt::answer_variant::chips
            : shared_ui::qt::answer_variant::numeric,
        feedback_style == quiz_feedback_style::stamp
            ? shared_ui::qt::answer_feedback_variant::stamp
            : shared_ui::qt::answer_feedback_variant::classic
    );
    update_compact_controls();
#endif
}

void table_slot::update_compact_controls() {
    if (gameplay_owner) {
        settings_bar_widget->hide();
        quiz_bar_widget->setVisible(quiz_prompt_active);
        copy_settings_action->setVisible(false);
        copy_all_action->setVisible(false);
        const auto phase = gameplay_owner->phase();
        details_action->setEnabled(
            phase == gameplay::session_phase::setup
            || phase == gameplay::session_phase::paused
        );
        swap_action->setEnabled(phase == gameplay::session_phase::paused);
        swap_bar_widget->setVisible(details_action->isEnabled());
        update_settings_button_state();
    }
    if (!controls_surface)
        return; // The Android branch retains its existing inline controls.
    const bool visible = !settings_editor_panel
        && (gameplay_owner ? details_action->isEnabled() || quiz_prompt_active
                           : current_phase == slot_phase::paused
                    || !card_widget_internal->has_cards()
                    || quiz_prompt_active);
    controls_surface->set_description(
        quiz_prompt_active ? QStringLiteral("?") : QStringLiteral("…"),
        quiz_prompt_active
            ? (quiz_feedback_active
                   ? str_label("Show answer feedback")
                   : str_label("Answer accumulated-count question"))
            : str_label("Show slot actions")
    );
    QWidget* preferred = slot_actions->button_for(details_action);
    if (quiz_prompt_active)
        preferred = quiz_feedback_active
            ? (quiz_continue_visible ? quiz_continue_button : nullptr)
            : (gameplay_owner ? static_cast<QWidget*>(gameplay_input)
                              : quiz_spin_box);
    controls_surface->set_preferred_focus(preferred);
    controls_surface->place_in(rect(), visible, visible);
}

void table_slot::update_settings_button_state(bool dialog_open) {
    if (details_action == nullptr) {
        return;
    }
    if (gameplay_owner) {
        details_action->setChecked(settings_editor_panel != nullptr);
        return;
    }
    if (settings_editor_panel
        || settings_style != slot_settings_style::classic) {
        details_action->setChecked(settings_editor_panel != nullptr);
        return;
    }
    if (use_dialog_for_settings) {
        details_action->setChecked(dialog_open);
        return;
    }
    details_action->setChecked(settings_overlay_visible);
}

void table_slot::update_lockable_settings() {
    const bool has_deck
        = card_widget_internal != nullptr && card_widget_internal->has_cards();
    const bool paused = current_phase == slot_phase::paused;
    const bool lock_infinity = has_deck && paused
        && infinity_check_box != nullptr && infinity_check_box->isChecked();
    const bool lock_training = has_deck && paused
        && training_check_box != nullptr && training_check_box->isChecked();
    if (infinity_check_box != nullptr) {
        infinity_check_box->setEnabled(!lock_infinity);
    }
    if (training_check_box != nullptr) {
        training_check_box->setEnabled(!lock_training);
    }
}

void table_slot::resizeEvent(QResizeEvent* event) {
    BaseWidget::resizeEvent(event);
    if (quiz_bar_widget != nullptr)
        quiz_bar_widget->set_available_area(size());

    if (card_widget_internal != nullptr) {
        card_widget_internal->setGeometry(rect());
    }
    update_overlay_margins();
    if (gameplay_owner) {
        if (!controls_surface)
            overlay_widget->setGeometry(rect());
        place_settings_editor();
        update_compact_controls();
        return;
    }

    if (overlay_widget != nullptr && !controls_surface) {
        overlay_widget->setGeometry(rect());
    }
    place_settings_editor();

    if (settings_bar_widget == nullptr) {
        return;
    }

    const QSize settings_size_needed = slot_settings::minimum_settings_size();
    int width_needed = settings_size_needed.width();
    int height_needed = settings_size_needed.height();
    if (swap_bar_widget != nullptr) {
        height_needed += swap_bar_widget->sizeHint().height();
    }

    int available_width = width();
    int available_height = height();

    if (is_rotated && settings_bar_widget != nullptr) {
        const int max_settings_width
            = std::max(1, static_cast<int>(available_width * 0.33));
        settings_bar_widget->setMaximumWidth(max_settings_width);
    } else if (settings_bar_widget != nullptr) {
        settings_bar_widget->setMaximumWidth(QWIDGETSIZE_MAX);
    }

    bool new_use_dialog_for_settings
        = available_width < width_needed || available_height < height_needed;

    if (new_use_dialog_for_settings == use_dialog_for_settings) {
        update_compact_controls();
        return;
    }

    use_dialog_for_settings = new_use_dialog_for_settings;

    if (use_dialog_for_settings) {
        settings_overlay_visible = false;
        settings_bar_widget->hide();
    } else {
        if (overlay_widget != nullptr && overlay_widget->isVisible()) {
            settings_bar_widget->setVisible(
                settings_overlay_visible
                && settings_style == slot_settings_style::classic
            );
        }
    }
    update_settings_button_state();
    update_compact_controls();
}

void table_slot::update_action_button_state() {
    if (swap_action == nullptr || copy_settings_action == nullptr) {
        return;
    }

    if (!swap_selected()) {
        swap_action->setChecked(false);
        copy_settings_action->setChecked(false);
        return;
    }

    const bool is_copy_mode = current_copy_action == copy_action::cancel;
    swap_action->setChecked(!is_copy_mode);
    copy_settings_action->setChecked(is_copy_mode);
}
