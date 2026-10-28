#include "table/table_slot.hpp"
#include <shared_ui/qt/adaptive_surface.hpp>
#include <shared_ui/qt/answer_panel.hpp>
#include <shared_ui/qt/editor_panel.hpp>
#include <shared_ui/qt/icons.hpp>
#include <shared_ui/qt/presentation.hpp>

#include "arch/asset_locator.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "table/card_widget.hpp"
#include "table/gameplay_setup.hpp"
#include "table/settings_template.hpp"

#include <shared_ui/qt/action_group.hpp>

#include <QAction>
#include <QBoxLayout>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtGlobal>

void table_slot::setup_overlay() {
    overlay_widget = new BaseWidget(this);

    overlay_layout = new QBoxLayout(QBoxLayout::TopToBottom, overlay_widget);
    overlay_layout->setContentsMargins(2, 2, 2, 2);
    overlay_layout->setSpacing(2);

    // A packed slot must never inherit the controls' minimum size. If they
    // cannot fit, expose the same widgets in a transient desktop window.
    overlay_layout->setSizeConstraint(QLayout::SetNoConstraint);
    controls_surface
        = new shared_ui::qt::adaptive_surface(*this, *overlay_widget);
    controls_surface->set_open_permission([this] {
        return !gameplay_owner
            || gameplay_owner->phase() == gameplay::session_phase::setup
            || gameplay_owner->phase() == gameplay::session_phase::paused
            || quiz_prompt_active;
    });
    controls_surface->set_window_title(str_label("Card controls"));
    compact_controls_button = controls_surface->trigger();
    compact_controls_button->setObjectName(
        QStringLiteral("compact_slot_controls")
    );
    connect(
        controls_surface, &shared_ui::qt::adaptive_surface::opened, this,
        [](QDialog* window) {
            window->setObjectName(QStringLiteral("slot_controls_dialog"));
        }
    );

    swap_bar_widget = new QFrame(overlay_widget);
    swap_bar_widget->setObjectName(QStringLiteral("swap_bar_frame"));
    shared_ui::qt::set_presentation_role(
        *swap_bar_widget, QStringLiteral("transparent")
    );
    swap_bar_widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    swap_layout = new QBoxLayout(QBoxLayout::LeftToRight, swap_bar_widget);
    swap_layout->setContentsMargins(4, 4, 4, 4);
    swap_layout->setSpacing(2);

    quiz_bar_widget = new shared_ui::qt::answer_panel(overlay_widget);
    quiz_bar_widget->setObjectName(QStringLiteral("quiz_bar_frame"));
    quiz_chip_widget = quiz_bar_widget->step_area();
    quiz_chip_widget->setObjectName(QStringLiteral("quiz_chip_stepper"));
    quiz_bar_widget->feedback_band()->setObjectName(
        QStringLiteral("quiz_feedback_band")
    );

    // The slot's own commands; Appearance's action group draws them in the
    // chosen Classic, Icon rail or Labelled pills form.
    const auto command = [this](
                             const char* name, const QString& text,
                             const QString& tip, const bool checkable
                         ) {
        auto* action = new QAction(text, this);
        action->setObjectName(QString::fromLatin1(name));
        action->setToolTip(tip);
        action->setCheckable(checkable);
        return action;
    };
    details_action = command(
        "slot_details", str_label("Details"),
        str_label("Show card details and settings"), true
    );
    swap_action = command(
        "slot_swap", str_label("Swap"),
        str_label("Swap this slot with another"), true
    );

    details_action->setIcon(
        shared_ui::qt::themed_icon(
            { "document-properties", "view-list-details", "document-preview",
              "dialog-information" },
            QStyle::SP_FileDialogContentsView
        )
    );
    swap_action->setIcon(
        shared_ui::qt::themed_icon(
            { "view-refresh", "reload", "object-rotate-right" },
            QStyle::SP_BrowserReload
        )
    );
    slot_actions = new shared_ui::qt::action_group(swap_bar_widget);
    slot_actions->setObjectName(QStringLiteral("slot_actions"));
    slot_actions->setAccessibleName(str_label("Slot actions"));
    // A crowded slot moves its whole overlay to the compact host instead.
    slot_actions->set_overflow(shared_ui::qt::action_overflow::host);
    slot_actions->set_icons_visible(true);
    slot_actions->set_actions({ details_action, swap_action });

    quiz_weight_label = new QLabel(str_label("Count"), quiz_bar_widget);
    quiz_weight_label->setObjectName(QStringLiteral("quiz_weight_label"));
    quiz_answer_button = new QPushButton(quiz_bar_widget);
    quiz_answer_button->setObjectName(QStringLiteral("quiz_answer_button"));
    shared_ui::qt::set_presentation_role(
        *quiz_answer_button, QStringLiteral("primary")
    );
    quiz_answer_button->setText(str_label("Check"));
    quiz_answer_button->setToolTip(str_label("Check your answer"));
    quiz_skip_button = new QPushButton(quiz_bar_widget);
    quiz_skip_button->setObjectName(QStringLiteral("quiz_skip_button"));
    shared_ui::qt::set_presentation_role(
        *quiz_skip_button, QStringLiteral("secondary")
    );
    quiz_skip_button->setText(str_label("Skip"));
    quiz_skip_button->setToolTip(str_label("Skip this question"));
    quiz_feedback_label = new QLabel(quiz_bar_widget);
    quiz_feedback_label->setAccessibleName(str_label("Answer feedback"));
    quiz_feedback_label->setObjectName(QStringLiteral("quiz_feedback_label"));
    quiz_feedback_label->setVisible(false);
    quiz_continue_button = new QPushButton(quiz_bar_widget);
    quiz_continue_button->setObjectName(QStringLiteral("quiz_continue_button"));
    shared_ui::qt::set_presentation_role(
        *quiz_continue_button, QStringLiteral("primary")
    );
    quiz_continue_button->setText(str_label("Continue"));
    quiz_continue_button->setToolTip(str_label("Continue to the next card"));
    quiz_continue_button->setVisible(false);
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    // Return belongs to the validated answer input, including in its mobile
    // dialog. Reparenting must not add a second implicit default submission.
    for (auto* button :
         { quiz_answer_button, quiz_skip_button, quiz_continue_button })
        button->setAutoDefault(false);
#endif

    swap_layout->addWidget(slot_actions);
    swap_layout->addStretch();

#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    quiz_feedback_heading
        = new QLabel(str_label("ACCUMULATED COUNT"), quiz_bar_widget);
    quiz_feedback_heading->setObjectName(QStringLiteral("quiz_feedback_stamp"));
#endif
    quiz_bar_widget->set_widgets(
        { quiz_weight_label,
          nullptr,
          { quiz_answer_button, quiz_skip_button },
          quiz_feedback_heading,
          quiz_feedback_label,
          quiz_continue_button }
    );
    setup_quiz_chips();

    update_overlay_layout();

    update_overlay_palette();
    update_quiz_controls_visibility();

    overlay_widget->show();
    swap_bar_widget->show();
    quiz_bar_widget->hide();
    overlay_widget->raise();

    QObject::connect(
        swap_action, &QAction::triggered, this,
        &table_slot::on_swap_button_clicked
    );
    QObject::connect(
        details_action, &QAction::triggered, this,
        &table_slot::on_settings_button_clicked
    );

    QObject::connect(
        quiz_answer_button, &BasePushButton::clicked, this,
        &table_slot::on_quiz_answer_button_clicked
    );
    QObject::connect(
        quiz_skip_button, &BasePushButton::clicked, this,
        &table_slot::on_quiz_skip_button_clicked
    );
    QObject::connect(
        quiz_continue_button, &BasePushButton::clicked, this,
        &table_slot::on_quiz_continue_button_clicked
    );

    update_action_presentation();
    update_settings_button_state();
}

void table_slot::update_overlay_palette() {
    const auto context = shared_ui::qt::presentation_for(*this).context;
    const QColor base_color = context.felt_surface.isValid()
        ? context.felt_surface
        : theme_palette_registry::option(
              load_table_appearance_preferences().palette
          )
              .base_color();
    const theme_palette_option& palette_option = theme_palette_registry::option(
        theme_palette_registry::id_from_color(base_color)
    );
    const QColor panel_color = palette_option.panel_color();
    const QColor accent_color
        = shared_ui::qt::tokens_for(
              shared_ui::qt::material::felt,
              shared_ui::qt::presentation_for(*this).profile, palette()
        )
              .accent;
    const QColor input_color = palette_option.input_color();
    apply_palette_to_widget(
        swap_bar_widget, panel_color, accent_color, input_color
    );
    apply_palette_to_widget(
        quiz_bar_widget, panel_color, accent_color, input_color
    );
    apply_palette_to_widget(
        settings_editor_panel, panel_color, accent_color, input_color
    );
    // D-SET-1: under the felt chrome the settings card is warm paper placed
    // on the table rather than another dark panel.
    if (settings_editor_panel
        && shared_ui::qt::presentation_for(*this).value
            == shared_ui::qt::material::felt
        && settings_style == slot_settings_style::card) {
        shared_ui::qt::set_presentation_role(
            *settings_editor_panel, QStringLiteral("paper-panel")
        );
        shared_ui::qt::apply_presentation(
            *settings_editor_panel, shared_ui::qt::presentation_for(*this),
            shared_ui::qt::presentation_surface::paper
        );
    }
    update_quiz_presentation();
}

void table_slot::apply_palette_to_widget(
    BaseWidget* widget, const QColor& panel_color, const QColor& accent_color,
    const QColor& input_color
) {
    if (widget == nullptr) {
        return;
    }

    auto value = widget->parentWidget() != nullptr
        ? shared_ui::qt::presentation_for(*widget->parentWidget())
        : shared_ui::qt::presentation {};
    value.context.overlay_surface = panel_color;
    value.context.overlay_text = accent_color;
    value.context.overlay_input = input_color;
    shared_ui::qt::apply_presentation(
        *widget, value, shared_ui::qt::presentation_surface::overlay
    );
    widget->setAutoFillBackground(
        value.value == shared_ui::qt::material::system
    );
}

void table_slot::update_overlay_layout() {
    if (overlay_layout == nullptr || swap_bar_widget == nullptr
        || quiz_bar_widget == nullptr) {
        return;
    }

    while (overlay_layout->count() > 0) {
        delete overlay_layout->takeAt(0);
    }

    const bool rail = action_style == slot_action_style::rail;
    const bool horizontal_composition
        = !quiz_prompt_active && action_style != slot_action_style::classic
        ? rail
        : is_rotated;
    overlay_layout->setDirection(
        horizontal_composition ? QBoxLayout::LeftToRight
                               : QBoxLayout::TopToBottom
    );

    if (quiz_prompt_active) {
        // Felt docks the prompt below the raised quiz marking.
        const bool docked = shared_ui::qt::presentation_for(*this).value
                == shared_ui::qt::material::felt
            && overlay_layout->direction() == QBoxLayout::TopToBottom;
        overlay_layout->addStretch(docked ? 3 : 1);
        overlay_layout->addWidget(quiz_bar_widget, 0, Qt::AlignCenter);
        if (gameplay_owner
            && gameplay_owner->phase() == gameplay::session_phase::paused)
            overlay_layout->addWidget(swap_bar_widget, 0, Qt::AlignCenter);
        overlay_layout->addStretch(1);
    } else {
        overlay_layout->addStretch();
        overlay_layout->addWidget(swap_bar_widget, 0, Qt::AlignCenter);
        overlay_layout->addWidget(quiz_bar_widget, 0, Qt::AlignCenter);
    }

    if (swap_layout != nullptr) {
        const bool column = rail
            || (action_style == slot_action_style::classic && is_rotated);
        swap_layout->setDirection(
            column ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight
        );
        slot_actions->set_orientation(column ? Qt::Vertical : Qt::Horizontal);
    }
    update_compact_controls();
}

bool table_slot::eventFilter(QObject* watched, QEvent* event) {
    if (watched == settings_editor_panel
        && event->type() == QEvent::LayoutRequest) {
        place_settings_editor();
    }
    if (auto* key = dynamic_cast<QKeyEvent*>(event);
        key && event->type() == QEvent::KeyPress) {
        if (gameplay_prompt
            && (watched == gameplay_input || watched == quiz_answer_button
                || watched == quiz_skip_button
                || watched == compact_controls_button
                || (quiz_chip_widget
                    && quiz_chip_widget->isAncestorOf(
                        qobject_cast<QWidget*>(watched)
                    )))) {
            if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
                return gameplay_host->focus_gameplay_quiz(
                    gameplay_id,
                    key->key() == Qt::Key_Backtab
                        || key->modifiers().testFlag(Qt::ShiftModifier)
                );
            }
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (watched == gameplay_input && gameplay_owner) {
                if (!key->isAutoRepeat())
                    on_quiz_answer_button_clicked();
                return true;
            }
        }
    }
    return BaseWidget::eventFilter(watched, event);
}
