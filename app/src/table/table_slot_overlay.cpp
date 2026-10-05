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
#include <QToolTip>
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
    swap_bar_widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    swap_layout = new QBoxLayout(QBoxLayout::LeftToRight, swap_bar_widget);
    swap_layout->setContentsMargins(4, 4, 4, 4);
    swap_layout->setSpacing(2);

    quiz_bar_widget = new QFrame(overlay_widget);
    quiz_bar_widget->setObjectName(QStringLiteral("quiz_bar_frame"));
    quiz_bar_widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    quiz_layout = new QStackedLayout(quiz_bar_widget);
    quiz_layout->setContentsMargins(4, 4, 4, 4);
    quiz_layout->setSpacing(2);

    settings_button = android_ui::create_button(swap_bar_widget);
    settings_button->setText(str_label("Details"));
    settings_button->setCheckable(true);
    swap_button = android_ui::create_button(swap_bar_widget);
    swap_button->setText(str_label("Swap"));
    swap_button->setCheckable(true);
    copy_button = android_ui::create_button(swap_bar_widget);
    copy_button->setText(str_label("Copy"));
    copy_button->setCheckable(true);
    copy_all_button = android_ui::create_button(swap_bar_widget);
    copy_all_button->setText(str_label("Copy all"));
    settings_button->setObjectName(QStringLiteral("slot_details_button"));
    swap_button->setObjectName(QStringLiteral("slot_swap_button"));
    copy_button->setObjectName(QStringLiteral("slot_copy_button"));
    copy_all_button->setObjectName(QStringLiteral("slot_copy_all_button"));
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    for (auto* button :
         { settings_button, swap_button, copy_button, copy_all_button }) {
        button->installEventFilter(this);
    }
#endif

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
    quiz_spin_box = new BaseSpinBox(quiz_bar_widget);
    quiz_weight_label->setBuddy(quiz_spin_box);
    quiz_spin_box->setAccessibleName(str_label("Card-count answer"));
    quiz_spin_box->setObjectName(QStringLiteral("quiz_spin_box"));
    quiz_spin_box->setRange(-9999, 9999);
    quiz_spin_box->setValue(0);
    quiz_spin_box->setToolTip(
        str_label("Enter the total weight for the current cards")
    );
    android_ui::apply_spin_box_style(quiz_spin_box);
    quiz_answer_button = android_ui::create_button(quiz_bar_widget);
    quiz_answer_button->setObjectName(QStringLiteral("quiz_answer_button"));
    quiz_answer_button->setText(str_label("Check"));
    quiz_answer_button->setToolTip(str_label("Check your answer"));
    quiz_skip_button = android_ui::create_button(quiz_bar_widget);
    quiz_skip_button->setObjectName(QStringLiteral("quiz_skip_button"));
    quiz_skip_button->setText(str_label("Skip"));
    quiz_skip_button->setToolTip(str_label("Skip this question"));
    quiz_feedback_label = new QLabel(quiz_bar_widget);
    quiz_feedback_label->setAccessibleName(str_label("Answer feedback"));
    quiz_feedback_label->setObjectName(QStringLiteral("quiz_feedback_label"));
    quiz_feedback_label->setWordWrap(true);
    quiz_feedback_label->setVisible(false);
    quiz_continue_button = android_ui::create_button(quiz_bar_widget);
    quiz_continue_button->setObjectName(QStringLiteral("quiz_continue_button"));
    quiz_continue_button->setText(str_label("Continue"));
    quiz_continue_button->setToolTip(str_label("Continue to the next card"));
    quiz_continue_button->setVisible(false);

    settings_button->setIcon(
        icon_loader::themed(
            { "document-properties", "view-list-details", "document-preview",
              "dialog-information" },
            QStyle::SP_FileDialogContentsView
        )
    );
    swap_button->setIcon(
        icon_loader::themed(
            { "view-refresh", "reload", "object-rotate-right" },
            QStyle::SP_BrowserReload
        )
    );
    copy_button->setIcon(
        icon_loader::themed(
            { "edit-copy", "copy", "document-duplicate" },
            QStyle::SP_FileDialogNewFolder
        )
    );
    copy_all_button->setIcon(
        icon_loader::themed(
            { "edit-copy", "copy", "document-duplicate" },
            QStyle::SP_FileDialogNewFolder
        )
    );
    settings_button->setToolTip(str_label("Show card details and settings"));
    swap_button->setToolTip(str_label("Swap this slot with another"));
    copy_button->setToolTip(str_label("Copy settings from this slot"));
    copy_all_button->setToolTip(str_label("Copy settings to all slots"));
    android_ui::apply_button_style(
        settings_button, android_button_profile::compact_action
    );
    android_ui::apply_button_style(
        swap_button, android_button_profile::compact_action
    );
    android_ui::apply_button_style(
        copy_button, android_button_profile::compact_action
    );
    android_ui::apply_button_style(
        copy_all_button, android_button_profile::compact_action
    );
    android_ui::apply_button_style(
        quiz_answer_button, android_button_profile::quiz_action
    );
    android_ui::apply_button_style(
        quiz_skip_button, android_button_profile::quiz_action
    );
    android_ui::apply_button_style(
        quiz_continue_button, android_button_profile::quiz_action
    );

    swap_layout->addWidget(settings_button);
    swap_layout->addWidget(swap_button);
    swap_layout->addWidget(copy_button);
    swap_layout->addWidget(copy_all_button);
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
    quiz_feedback_heading->setWordWrap(true);
    quiz_feedback_heading->setAlignment(Qt::AlignCenter);
    quiz_feedback_layout->addWidget(quiz_feedback_heading);
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
        swap_button, &BasePushButton::clicked, this,
        &table_slot::on_swap_button_clicked
    );
    QObject::connect(
        settings_button, &BasePushButton::clicked, this,
        &table_slot::on_settings_button_clicked
    );
    if (info_button != nullptr) {
        QObject::connect(
            info_button, &BasePushButton::clicked, this,
            &table_slot::on_info_button_clicked
        );
    }
    QObject::connect(
        copy_button, &BasePushButton::clicked, this,
        &table_slot::on_copy_button_clicked
    );
    QObject::connect(
        copy_all_button, &BasePushButton::clicked, this,
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
    const QColor base_color = theme_settings::base_color();
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
    update_quiz_presentation();
}

void table_slot::apply_palette_to_widget(
    BaseWidget* widget, const QColor& panel_color, const QColor& accent_color,
    const QColor& input_color
) {
    if (widget == nullptr) {
        return;
    }

    const QString accent_hex = accent_color.name();
    const QString input_hex = input_color.name();
    const QString panel_hex = panel_color.name(QColor::HexArgb);

    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, panel_color);
    palette.setColor(QPalette::WindowText, accent_color);
    palette.setColor(QPalette::ButtonText, accent_color);
    palette.setColor(QPalette::Text, accent_color);
    palette.setColor(QPalette::Button, panel_color);
    palette.setColor(QPalette::Base, input_color);
    widget->setStyleSheet(
        QString(
            "QLabel { color: %1; background-color: transparent; }"
            "QCheckBox, QComboBox, QSpinBox, QLineEdit#gameplay_quiz_input { "
            "color: %1; }"
            "QComboBox, QSpinBox, QLineEdit#gameplay_quiz_input { "
            "background-color: %2; }"
            "QPushButton, QToolButton {"
            " background-color: %3;"
            " color: %1;"
            " border: 1px solid %1;"
            " border-radius: 4px;"
            " padding: 2px 6px;"
            "}"
            "QPushButton:checked, QToolButton:checked {"
            " background-color: %1;"
            " color: %3;"
            "}"
            "QFrame#settings_bar_frame, QFrame#swap_bar_frame,"
            " QFrame#quiz_bar_frame, QFrame#slot_settings_editor {"
            " background-color: %3;"
            " border: 1px solid %1;"
            " border-radius: 6px;"
            "}"
            "QFrame { background-color: %3; }"
        )
            .arg(accent_hex, input_hex, panel_hex)
    );
    widget->setPalette(palette);
    widget->setAutoFillBackground(true);
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
        overlay_layout->addStretch();
        overlay_layout->addWidget(quiz_bar_widget, 0, Qt::AlignCenter);
        if (gameplay_owner
            && gameplay_owner->phase() == gameplay::session_phase::paused)
            overlay_layout->addWidget(swap_bar_widget, 0, Qt::AlignCenter);
        overlay_layout->addStretch();
    } else {
        overlay_layout->addWidget(settings_bar_widget, 0, Qt::AlignCenter);
        overlay_layout->addStretch();
        overlay_layout->addWidget(swap_bar_widget, 0, Qt::AlignCenter);
        overlay_layout->addWidget(quiz_bar_widget, 0, Qt::AlignCenter);
    }

    if (swap_layout != nullptr) {
        swap_layout->setDirection(
            rail || (action_style == slot_action_style::classic && is_rotated)
                ? QBoxLayout::TopToBottom
                : QBoxLayout::LeftToRight
        );
    }
    update_compact_controls();
}

bool table_slot::eventFilter(QObject* watched, QEvent* event) {
    if (watched == settings_editor_panel
        && event->type() == QEvent::LayoutRequest) {
        place_settings_editor();
    }
    if (action_style == slot_action_style::rail) {
        auto* button = qobject_cast<BasePushButton*>(watched);
        if (button != nullptr && button->parentWidget() == swap_bar_widget) {
            // A keyboard focus label must not widen the rail and change fit.
            if (event->type() == QEvent::FocusIn) {
                QToolTip::showText(
                    button->mapToGlobal(QPoint(button->width(), 0)),
                    button->toolTip(), button
                );
            } else if (
                event->type() == QEvent::FocusOut
                || event->type() == QEvent::Hide
            ) {
                QToolTip::hideText();
            }
        }
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
