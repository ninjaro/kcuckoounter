#include "table/table_slot.hpp"
#include <shared_ui/qt/editor_panel.hpp>

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

#include <shared_ui/qt/action_group.hpp>

#include <QAction>
#include <QBoxLayout>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStackedLayout>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtGlobal>

void table_slot::setup_overlay() {
    overlay_widget = new BaseWidget(this);

    overlay_layout = new QBoxLayout(QBoxLayout::TopToBottom, overlay_widget);
    overlay_layout->setContentsMargins(2, 2, 2, 2);
    overlay_layout->setSpacing(2);

#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    // A packed slot must never inherit the controls' minimum size. If they
    // cannot fit, expose the same widgets in a transient desktop window.
    overlay_layout->setSizeConstraint(QLayout::SetNoConstraint);
    overlay_widget->installEventFilter(this);
    compact_controls_button = new QToolButton(this);
    compact_controls_button->setObjectName(
        QStringLiteral("compact_slot_controls")
    );
    compact_controls_button->setFocusPolicy(Qt::StrongFocus);
    compact_controls_button->hide();
    connect(
        compact_controls_button, &QToolButton::clicked, this,
        &table_slot::show_compact_controls
    );
#endif

    auto settings_frame = new QFrame(overlay_widget);
    settings_frame->setObjectName(QStringLiteral("settings_bar_frame"));
    shared_ui::qt::set_presentation_role(
        *settings_frame, QStringLiteral("panel")
    );
    auto settings_frame_layout = new QVBoxLayout(settings_frame);
    settings_frame_layout->setContentsMargins(2, 2, 2, 2);
    settings_frame_layout->setSpacing(0);

    auto settings_widget_internal = new slot_settings(settings_frame, true);
    settings_frame_layout->addWidget(settings_widget_internal);
    settings_bar_widget = settings_frame;
    settings_bar_widget->setSizePolicy(
        QSizePolicy::Minimum, QSizePolicy::Fixed
    );

    infinity_check_box = settings_widget_internal->infinity_check_box();
    deck_count_spin_box = settings_widget_internal->deck_count_spin_box();
    if (deck_count_spin_box != nullptr) {
        deck_count_minimum = deck_count_spin_box->minimum();
    }
    strategy_combo_box = settings_widget_internal->strategy_combo_box();
    show_card_indexing = settings_widget_internal->show_card_indexing();
    show_strategy_name = settings_widget_internal->show_strategy_name();
    training_check_box = settings_widget_internal->training_check_box();
    info_button = settings_widget_internal->info_button();

    swap_bar_widget = new QFrame(overlay_widget);
    swap_bar_widget->setObjectName(QStringLiteral("swap_bar_frame"));
    shared_ui::qt::set_presentation_role(
        *swap_bar_widget, QStringLiteral("transparent")
    );
    swap_bar_widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    swap_layout = new QBoxLayout(QBoxLayout::LeftToRight, swap_bar_widget);
    swap_layout->setContentsMargins(4, 4, 4, 4);
    swap_layout->setSpacing(2);

    quiz_bar_widget = new QFrame(overlay_widget);
    quiz_bar_widget->setObjectName(QStringLiteral("quiz_bar_frame"));
    shared_ui::qt::set_presentation_role(
        *quiz_bar_widget, QStringLiteral("transparent")
    );
    quiz_bar_widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    quiz_layout = new QStackedLayout(quiz_bar_widget);
    quiz_layout->setContentsMargins(4, 4, 4, 4);
    quiz_layout->setSpacing(2);

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
    copy_settings_action = command(
        "slot_copy", str_label("Copy"),
        str_label("Copy settings from this slot"), true
    );
    copy_all_action = command(
        "slot_copy_all", str_label("Copy all"),
        str_label("Copy settings to all slots"), false
    );
    details_action->setIcon(
        icon_loader::themed(
            { "document-properties", "view-list-details", "document-preview",
              "dialog-information" },
            QStyle::SP_FileDialogContentsView
        )
    );
    swap_action->setIcon(
        icon_loader::themed(
            { "view-refresh", "reload", "object-rotate-right" },
            QStyle::SP_BrowserReload
        )
    );
    const QIcon copy_icon = icon_loader::themed(
        { "edit-copy", "copy", "document-duplicate" },
        QStyle::SP_FileDialogNewFolder
    );
    copy_settings_action->setIcon(copy_icon);
    copy_all_action->setIcon(copy_icon);
    slot_actions = new shared_ui::qt::action_group(swap_bar_widget);
    slot_actions->setObjectName(QStringLiteral("slot_actions"));
    slot_actions->setAccessibleName(str_label("Slot actions"));
    // A crowded slot moves its whole overlay to the compact host instead.
    slot_actions->set_overflow(shared_ui::qt::action_overflow::host);
    slot_actions->set_icons_visible(true);
    // Native themes may supply the same glyph for Copy and Copy all. Keep the
    // scope explicit even in the otherwise icon-only rail.
    slot_actions->set_compact_text(copy_all_action, str_label("All"));
    slot_actions->set_actions(
        { details_action, swap_action, copy_settings_action, copy_all_action }
    );

    quiz_prompt_widget = new BaseWidget(quiz_bar_widget);
    auto quiz_prompt_layout = new QVBoxLayout(quiz_prompt_widget);
    quiz_prompt_layout->setContentsMargins(0, 0, 0, 0);
    quiz_prompt_layout->setSpacing(2);
    auto quiz_prompt_row_layout = new QHBoxLayout();
    quiz_prompt_row_layout->setContentsMargins(0, 0, 0, 0);
    quiz_prompt_row_layout->setSpacing(2);
    auto quiz_prompt_button_layout = new QHBoxLayout();
    quiz_prompt_button_layout->setContentsMargins(0, 0, 0, 0);
    quiz_prompt_button_layout->setSpacing(2);

    quiz_feedback_widget = new BaseWidget(quiz_bar_widget);
    auto quiz_feedback_layout = new QVBoxLayout(quiz_feedback_widget);
    quiz_feedback_layout->setContentsMargins(0, 0, 0, 0);
    quiz_feedback_layout->setSpacing(2);

    quiz_weight_label = new QLabel(str_label("Weight"), quiz_bar_widget);
    quiz_weight_label->setObjectName(QStringLiteral("quiz_weight_label"));
    shared_ui::qt::set_presentation_role(
        *quiz_weight_label, QStringLiteral("caption")
    );
    quiz_spin_box = new BaseSpinBox(quiz_bar_widget);
    quiz_weight_label->setBuddy(quiz_spin_box);
    quiz_spin_box->setAccessibleName(str_label("Card-count answer"));
    quiz_spin_box->setObjectName(QStringLiteral("quiz_spin_box"));
    shared_ui::qt::set_presentation_role(
        *quiz_spin_box, QStringLiteral("figure")
    );
    quiz_spin_box->setRange(-9999, 9999);
    quiz_spin_box->setValue(0);
    quiz_spin_box->setToolTip(
        str_label("Enter the total weight for the current cards")
    );
    android_ui::apply_spin_box_style(quiz_spin_box);
    quiz_answer_button = android_ui::create_button(quiz_bar_widget);
    quiz_answer_button->setObjectName(QStringLiteral("quiz_answer_button"));
    shared_ui::qt::set_presentation_role(
        *quiz_answer_button, QStringLiteral("primary")
    );
    quiz_answer_button->setText(str_label("Check"));
    quiz_answer_button->setToolTip(str_label("Check your answer"));
    quiz_skip_button = android_ui::create_button(quiz_bar_widget);
    quiz_skip_button->setObjectName(QStringLiteral("quiz_skip_button"));
    shared_ui::qt::set_presentation_role(
        *quiz_skip_button, QStringLiteral("secondary")
    );
    quiz_skip_button->setText(str_label("Skip"));
    quiz_skip_button->setToolTip(str_label("Skip this question"));
    quiz_feedback_label = new QLabel(quiz_bar_widget);
    quiz_feedback_label->setAccessibleName(str_label("Answer feedback"));
    quiz_feedback_label->setObjectName(QStringLiteral("quiz_feedback_label"));
    shared_ui::qt::set_presentation_role(
        *quiz_feedback_label, QStringLiteral("note")
    );
    quiz_feedback_label->setWordWrap(true);
    quiz_feedback_label->setVisible(false);
    quiz_continue_button = android_ui::create_button(quiz_bar_widget);
    quiz_continue_button->setObjectName(QStringLiteral("quiz_continue_button"));
    shared_ui::qt::set_presentation_role(
        *quiz_continue_button, QStringLiteral("primary")
    );
    quiz_continue_button->setText(str_label("Continue"));
    quiz_continue_button->setToolTip(str_label("Continue to the next card"));
    quiz_continue_button->setVisible(false);

    android_ui::apply_button_style(
        quiz_answer_button, android_button_profile::quiz_action
    );
    android_ui::apply_button_style(
        quiz_skip_button, android_button_profile::quiz_action
    );
    android_ui::apply_button_style(
        quiz_continue_button, android_button_profile::quiz_action
    );

    swap_layout->addWidget(slot_actions);
    swap_layout->addStretch();

    quiz_prompt_row_layout->addWidget(quiz_weight_label);
    quiz_prompt_row_layout->addWidget(quiz_spin_box);
    quiz_prompt_row_layout->addStretch();
    quiz_prompt_button_layout->addStretch();
    quiz_prompt_button_layout->addWidget(quiz_answer_button);
    quiz_prompt_button_layout->addWidget(quiz_skip_button);
    quiz_prompt_button_layout->addStretch();
    quiz_prompt_layout->addLayout(quiz_prompt_row_layout);
    setup_quiz_chips();
    if (quiz_chip_widget != nullptr) {
        quiz_prompt_layout->addWidget(quiz_chip_widget);
    }
    quiz_prompt_layout->addLayout(quiz_prompt_button_layout);

#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    quiz_feedback_heading
        = new QLabel(str_label("ACCUMULATED COUNT"), quiz_feedback_widget);
    quiz_feedback_heading->setObjectName(QStringLiteral("quiz_feedback_stamp"));
    shared_ui::qt::set_presentation_role(
        *quiz_feedback_heading, QStringLiteral("caption")
    );
    quiz_feedback_heading->setWordWrap(true);
    quiz_feedback_heading->setAlignment(Qt::AlignCenter);
    quiz_feedback_layout->addWidget(quiz_feedback_heading);
    quiz_feedback_band = new QLabel(quiz_feedback_widget);
    quiz_feedback_band->setObjectName(QStringLiteral("quiz_feedback_band"));
    shared_ui::qt::set_presentation_role(
        *quiz_feedback_band, QStringLiteral("negative-band")
    );
    quiz_feedback_band->setTextFormat(Qt::RichText);
    quiz_feedback_band->hide();
    quiz_feedback_layout->addWidget(quiz_feedback_band);
#endif
    quiz_feedback_layout->addWidget(quiz_feedback_label);
    quiz_feedback_layout->addWidget(quiz_continue_button, 0, Qt::AlignCenter);

    quiz_layout->addWidget(quiz_prompt_widget);
    quiz_layout->addWidget(quiz_feedback_widget);

    update_overlay_layout();

    update_overlay_palette();
    update_quiz_controls_visibility();

    overlay_widget->show();
    settings_bar_widget->show();
    swap_bar_widget->show();
    quiz_bar_widget->hide();
    overlay_widget->raise();

    QObject::connect(
        infinity_check_box, &BaseCheckBox::toggled, this,
        &table_slot::on_infinity_toggled
    );
    QObject::connect(
        swap_action, &QAction::triggered, this,
        &table_slot::on_swap_button_clicked
    );
    QObject::connect(
        details_action, &QAction::triggered, this,
        &table_slot::on_settings_button_clicked
    );
    if (info_button != nullptr) {
        QObject::connect(
            info_button, &BasePushButton::clicked, this,
            &table_slot::on_info_button_clicked
        );
    }
    QObject::connect(
        copy_settings_action, &QAction::triggered, this,
        &table_slot::on_copy_button_clicked
    );
    QObject::connect(
        copy_all_action, &QAction::triggered, this,
        &table_slot::on_copy_all_button_clicked
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
    QObject::connect(
        show_card_indexing, &BaseCheckBox::toggled, this,
        &table_slot::on_show_card_indexing_toggled
    );
    QObject::connect(
        show_strategy_name, &BaseCheckBox::toggled, this,
        &table_slot::on_show_strategy_name_toggled
    );
    QObject::connect(
        training_check_box, &BaseCheckBox::toggled, this,
        &table_slot::on_training_check_box_toggled
    );
    QObject::connect(
        strategy_combo_box, &BaseComboBox::currentTextChanged, this,
        &table_slot::on_strategy_name_changed
    );

    sync_card_display_settings();
    update_action_presentation();
    update_settings_button_state();
}

void table_slot::update_overlay_palette() {
    const auto context = shared_ui::qt::presentation_for(*this).context;
    const QColor base_color = context.felt_surface.isValid()
        ? context.felt_surface
        : theme_settings::base_color();
    const theme_palette_option& palette_option = theme_palette_registry::option(
        theme_palette_registry::id_from_color(base_color)
    );
    const QColor panel_color = palette_option.panel_color();
    const QColor accent_color = theme_settings::slot_border_color();
    const QColor input_color = palette_option.input_color();
    apply_palette_to_widget(
        settings_bar_widget, panel_color, accent_color, input_color
    );
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
    if (settings_editor_panel && theme_settings::felt_chrome(this)
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
    if (overlay_layout == nullptr || settings_bar_widget == nullptr
        || swap_bar_widget == nullptr || quiz_bar_widget == nullptr) {
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
        const bool docked = theme_settings::felt_chrome(this)
            && overlay_layout->direction() == QBoxLayout::TopToBottom;
        overlay_layout->addStretch(docked ? 3 : 1);
        overlay_layout->addWidget(quiz_bar_widget, 0, Qt::AlignCenter);
        if (gameplay_owner
            && gameplay_owner->phase() == gameplay::session_phase::paused)
            overlay_layout->addWidget(swap_bar_widget, 0, Qt::AlignCenter);
        overlay_layout->addStretch(1);
    } else {
        overlay_layout->addWidget(settings_bar_widget, 0, Qt::AlignCenter);
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
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
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
            if (quiz_spin_box != nullptr
                && (watched == quiz_spin_box
                    || watched == quiz_spin_box->findChild<QLineEdit*>())) {
                if (!key->isAutoRepeat()) {
                    on_quiz_answer_button_clicked();
                }
                return true; // Do not also activate a dialog default button.
            }
            auto* button = qobject_cast<QPushButton*>(watched);
            if (button != nullptr && quiz_chip_widget != nullptr
                && quiz_chip_widget->isAncestorOf(button)) {
                if (!key->isAutoRepeat()) {
                    button->click();
                }
                return true;
            }
        }
    }
    if (watched == overlay_widget && event->type() == QEvent::LayoutRequest) {
        update_compact_controls();
    }
    return BaseWidget::eventFilter(watched, event);
}
