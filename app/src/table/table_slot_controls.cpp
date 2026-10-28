#include "table/table_slot.hpp"
#include <shared_ui/qt/adaptive_surface.hpp>
#include <shared_ui/qt/answer_panel.hpp>
#include <shared_ui/qt/editor_panel.hpp>
#include <shared_ui/qt/presentation.hpp>

#include "arch/asset_locator.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "table/card_widget.hpp"
#include "table/gameplay_setup.hpp"
#include "table/settings_template.hpp"
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
    details_action->setText(str_label("Details"));
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
        });
    }
    quiz_bar_widget->set_steps(steps);
#endif
}

void table_slot::update_quiz_presentation() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    const bool felt = shared_ui::qt::presentation_for(*this).value
        == shared_ui::qt::material::felt;
    quiz_weight_label->setText(
        felt ? str_label("YOUR COUNT") : str_label("Count")
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
        quiz_bar_widget->setVisible(quiz_prompt_active);
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
        return;
    const bool visible = !settings_editor_panel
        && (details_action->isEnabled() || quiz_prompt_active);
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
            ? quiz_continue_button
            : static_cast<QWidget*>(gameplay_input);
    controls_surface->set_preferred_focus(preferred);
    controls_surface->place_in(rect(), visible, visible);
}

void table_slot::update_settings_button_state() {
    details_action->setChecked(settings_editor_panel != nullptr);
}

void table_slot::resizeEvent(QResizeEvent* event) {
    BaseWidget::resizeEvent(event);
    quiz_bar_widget->set_available_area(size());
    card_widget_internal->setGeometry(rect());
    update_overlay_margins();
    place_settings_editor();
    update_compact_controls();
}

void table_slot::update_action_button_state() {
    swap_action->setChecked(swap_selected());
}
