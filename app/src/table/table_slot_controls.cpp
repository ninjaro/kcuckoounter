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
#include <QToolButton>

#include <QApplication>
#include <QBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QResizeEvent>
#include <QStackedLayout>
#include <QString>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <utility>

void table_slot::update_action_presentation() {
    if (settings_button == nullptr || copy_all_button == nullptr) {
        return;
    }
    const bool running_with_deck = current_phase == slot_phase::running
        && card_widget_internal->has_cards();
    const QString copy_label = current_copy_action == copy_action::cancel
        ? str_label("Cancel")
        : current_copy_action == copy_action::apply ? str_label("Set")
                                                    : str_label("Copy");
    const std::array<std::pair<BasePushButton*, QString>, 4> actions {
        { { settings_button,
            running_with_deck ? str_label("Info") : str_label("Details") },
          { swap_button, str_label("Swap") },
          { copy_button, copy_label },
          { copy_all_button, str_label("Copy all") } }
    };
    const bool felt = theme_settings::felt_chrome();
    const felt_material material = theme_settings::felt();
    copy_button->setToolTip(
        current_copy_action == copy_action::cancel
            ? str_label("Cancel copying settings")
            : current_copy_action == copy_action::apply
            ? str_label("Apply the selected slot's settings to this slot")
            : str_label("Copy settings from this slot")
    );
    for (const auto& [button, label] : actions) {
        button->setAccessibleName(label);
        // Native themes may supply the same glyph for Copy and Copy all.
        // Keep the scope explicit even in the otherwise icon-only rail.
        button->setText(
            action_style == slot_action_style::rail
                ? (button == copy_all_button ? str_label("All") : QString())
                : label
        );
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
        // Keep the native icon so leaving the felt chrome restores it.
        if (!button->property("native_icon").isValid())
            button->setProperty("native_icon", button->icon());
        const auto native_icon = button->property("native_icon").value<QIcon>();
        if (felt) {
            button->setIcon(
                action_style == slot_action_style::pills
                    ? QIcon()
                    : theme_settings::tinted_icon(native_icon, material.text)
            );
        } else {
            button->setIcon(native_icon);
        }
        button->setStyleSheet(
            action_style == slot_action_style::rail
                ? (felt ? theme_settings::felt_style(QStringLiteral(
                              "QPushButton { min-width: 26px; min-height: "
                              "26px; padding: 4px; border-radius: 6px; } "
                              "QPushButton:checked { background: $gold$; "
                              "border-color: $gold$; }"
                          ))
                        : QStringLiteral(
                              "QPushButton { min-width: 24px; min-height: "
                              "24px; padding: 4px; } QPushButton:focus { "
                              "border-style: dashed; }"
                          ))
                : action_style == slot_action_style::pills
                ? (felt ? theme_settings::felt_style(
                              // D-BAR-2: the leading action is a paper pill,
                              // the rest are translucent outlined pills.
                              button == settings_button
                                  ? QStringLiteral(
                                        "QPushButton { border-radius: 15px; "
                                        "padding: 7px 13px; background: "
                                        "$paper$; color: #12261a; border: "
                                        "1px solid $paper$; font-size: 13px; "
                                        "font-weight: 600; } "
                                        "QPushButton:checked { background: "
                                        "$gold$; border-color: $gold$; } "
                                        "QPushButton:focus { border: 1px "
                                        "dashed $deep$; }"
                                    )
                                  : QStringLiteral(
                                        "QPushButton { border-radius: 15px; "
                                        "padding: 7px 13px; background: "
                                        "rgba(243, 239, 228, 0.12); color: "
                                        "$paper$; border: 1px solid "
                                        "rgba(243, 239, 228, 0.35); "
                                        "font-size: 13px; } "
                                        "QPushButton:hover { border-color: "
                                        "$paper$; } "
                                        "QPushButton:checked { background: "
                                        "$gold$; color: $deep$; border-color: "
                                        "$gold$; } "
                                        "QPushButton:focus { border: 1px "
                                        "dashed $gold_light$; }"
                                    )
                          )
                        : QStringLiteral(
                              "QPushButton { border-radius: 12px; padding: 6px "
                              "12px; } QPushButton:focus { border-style: "
                              "dashed; }"
                          ))
                : QString()
        );
#endif
    }
}

void table_slot::setup_quiz_chips() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    quiz_spin_box->installEventFilter(this);
    quiz_spin_box->findChild<QLineEdit*>()->installEventFilter(this);
    quiz_chip_widget = new BaseWidget(quiz_prompt_widget);
    quiz_chip_widget->setObjectName(QStringLiteral("quiz_chip_stepper"));
    auto* row = new QHBoxLayout(quiz_chip_widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    row->addStretch();
    QWidget* previous = quiz_spin_box;
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
        button->setAutoDefault(false);
        button->setAccessibleName(description);
        button->setToolTip(button->accessibleName());
        button->setProperty("chip_step", step);
        button->installEventFilter(this);
        row->addWidget(button);
        QWidget::setTabOrder(previous, button);
        previous = button;
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
    QWidget::setTabOrder(previous, quiz_answer_button);
    QWidget::setTabOrder(quiz_answer_button, quiz_skip_button);
    QWidget::setTabOrder(quiz_skip_button, quiz_continue_button);
    row->addStretch();
#endif
}

void table_slot::update_quiz_presentation() {
    if (quiz_chip_widget == nullptr || quiz_feedback_heading == nullptr) {
        return;
    }
    const bool chips = answer_style == quiz_answer_style::chips;
    const auto* focused = QApplication::focusWidget();
    const bool chip_had_focus
        = focused != nullptr && quiz_chip_widget->isAncestorOf(focused);
    const bool controls_had_focus
        = focused != nullptr && overlay_widget->isAncestorOf(focused);
    quiz_chip_widget->setVisible(chips);
    const bool felt = theme_settings::felt_chrome();
    // D-COUNT-1 keeps a plain figure; arrow keys still nudge the value.
    quiz_spin_box->setButtonSymbols(
        chips || felt ? QAbstractSpinBox::NoButtons
                      : QAbstractSpinBox::UpDownArrows
    );
    // D-COUNT-1 sets the figure beside its buttons; the chip stepper keeps
    // the stacked composition so its chips stay centred under the figure.
    const bool felt_typed = felt && !chips;
    if (auto* prompt
        = qobject_cast<QBoxLayout*>(quiz_prompt_widget->layout())) {
        prompt->setDirection(
            felt_typed ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom
        );
        prompt->setSpacing(felt_typed ? 20 : (felt ? 12 : 2));
        if (auto* row
            = qobject_cast<QBoxLayout*>(prompt->itemAt(0)->layout())) {
            row->setDirection(
                felt ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight
            );
            row->setSpacing(felt ? 8 : 2);
            const Qt::Alignment figure
                = felt_typed ? Qt::AlignLeft : Qt::AlignHCenter;
            row->setAlignment(
                quiz_weight_label, felt ? figure : Qt::Alignment()
            );
            row->setAlignment(quiz_spin_box, felt ? figure : Qt::Alignment());
            if (gameplay_input != nullptr)
                row->setAlignment(
                    gameplay_input, felt ? figure : Qt::Alignment()
                );
        }
        if (auto* buttons = qobject_cast<QBoxLayout*>(
                prompt->itemAt(prompt->count() - 1)->layout()
            )) {
            buttons->setSpacing(felt ? 8 : 2);
            buttons->setContentsMargins(0, 0, 0, felt_typed ? 12 : 0);
            prompt->setAlignment(
                buttons, felt_typed ? Qt::AlignBottom : Qt::Alignment()
            );
        }
    }
    // D-COUNT-2 sets the figure in a gold ring between the chips; every other
    // composition keeps it in the prompt row under its label.
    if (auto* prompt
        = qobject_cast<QBoxLayout*>(quiz_prompt_widget->layout())) {
        auto* row = qobject_cast<QBoxLayout*>(prompt->itemAt(0)->layout());
        auto* chip_row = qobject_cast<QBoxLayout*>(quiz_chip_widget->layout());
        const bool ring = felt && chips;
        for (QWidget* figure : { static_cast<QWidget*>(quiz_spin_box),
                                 static_cast<QWidget*>(gameplay_input) }) {
            if (figure == nullptr || row == nullptr || chip_row == nullptr)
                continue;
            if (ring && chip_row->indexOf(figure) < 0) {
                row->removeWidget(figure);
                // stretch, -2, -1 | figure | +1, +2, stretch
                chip_row->insertWidget(3, figure);
            } else if (!ring && chip_row->indexOf(figure) >= 0) {
                chip_row->removeWidget(figure);
                row->addWidget(figure);
            }
        }
        if (quiz_spin_box != nullptr)
            quiz_spin_box->setAlignment(
                ring ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter
            );
        if (gameplay_input != nullptr)
            gameplay_input->setAlignment(
                ring ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter
            );
        if (chip_row != nullptr)
            chip_row->setSpacing(ring ? 8 : 6);
        // The ring is the label; collapse rather than hide so prompt
        // visibility stays owned by update_quiz_controls_visibility().
        if (quiz_weight_label != nullptr)
            quiz_weight_label->setMaximumHeight(ring ? 0 : QWIDGETSIZE_MAX);
    }
    // QStackedLayout ignores margins; pad the pages themselves.
    for (auto* page : { quiz_prompt_widget, quiz_feedback_widget })
        if (page->layout() != nullptr)
            page->layout()->setContentsMargins(
                felt ? QMargins(20, 14, 20, 14) : QMargins()
            );
    update_quiz_figure_size();
    if (!chips && chip_had_focus && quiz_prompt_active
        && !quiz_feedback_active) {
        if (gameplay_owner)
            focus_gameplay_input();
        else
            quiz_spin_box->setFocus(Qt::OtherFocusReason);
    }
    for (auto* chip : quiz_chip_widget->findChildren<QPushButton*>()) {
        const int step = chip->property("chip_step").toInt();
        // D-COUNT-2: casino chips, red below zero and teal above it.
        // Kit chip colours: deeper for the outer steps, lighter edges.
        const char* chip_colour = step == -2 ? "#b4503f"
            : step == -1                     ? "#8d4234"
            : step == 1                      ? "#2a7a66"
                                             : "#2f8f78";
        chip->setStyleSheet(
            felt ? theme_settings::felt_style(
                       QStringLiteral(
                           "QPushButton { border-radius: 22px; min-width: "
                           "38px; max-width: 38px; min-height: 38px; "
                           "max-height: 38px; padding: 0px; font-family: "
                           "\"$mono$\"; font-size: 14px; color: #ffffff; "
                           "background: %1; border: 3px dashed "
                           "rgba(243, 239, 228, %2); } "
                           "QPushButton:hover { border-style: solid; } "
                           "QPushButton:focus { border: 3px solid $gold$; } "
                           "QPushButton:disabled { background: transparent; "
                           "color: $muted_dim$; border-color: $line$; }"
                       )
                           .arg(QLatin1String(chip_colour))
                           .arg(std::abs(step) == 2 ? 0.55 : 0.45)
                   )
                 : QStringLiteral(
                       "QPushButton { border-radius: 18px; min-width: 36px; "
                       "min-height: 36px; padding: 0px; font-weight: 600; }"
                   )
        );
    }
    if (quiz_weight_label != nullptr) {
        quiz_weight_label->setText(
            felt                 ? str_label("YOUR COUNT")
                : gameplay_owner ? str_label("Count")
                                 : str_label("Weight")
        );
    }
    const bool stamp = feedback_style == quiz_feedback_style::stamp;
    quiz_feedback_heading->setVisible(stamp);
    quiz_feedback_heading->setStyleSheet(
        felt ? QString() : QStringLiteral("font-weight: 700;")
    );
    // Felt: the stamp band carries both figures; the sentence stays as a
    // quiet, readable restatement under it.
    quiz_feedback_label->setStyleSheet(
        stamp ? (felt ? QString()
                      : QStringLiteral(
                            "QLabel { font-weight: 600; border: 2px solid %1; "
                            "border-radius: 4px; padding: 8px; }"
                        )
                            .arg(theme_settings::slot_border_color().name()))
              : QString()
    );
    // Only presentation hints change. Invalidate them before checking whether
    // the controls still fit inline; neither table packing nor card demand
    // changes.
    quiz_prompt_widget->updateGeometry();
    quiz_feedback_widget->updateGeometry();
    quiz_bar_widget->updateGeometry();
    update_compact_controls();
    if (controls_had_focus && !controls_dialog
        && compact_controls_button != nullptr
        && compact_controls_button->isVisible() && quiz_prompt_active) {
        compact_controls_button->setFocus(Qt::OtherFocusReason);
    }
}

void table_slot::update_quiz_figure_size() {
    if (quiz_spin_box == nullptr)
        return;
    QString sheet;
    if (theme_settings::felt_chrome()
        && answer_style == quiz_answer_style::chips) {
        // D-COUNT-2's 88px gold ring with a 6px halo, scaled for small slots.
        const int ring = std::clamp(
            static_cast<int>(std::min(width(), height()) * 0.26), 56, 88
        );
        sheet = QStringLiteral(
                    "background: #04240f; color: #d4af37; font-size: %1px; "
                    "border: 2px solid #d4af37; border-radius: %2px; "
                    "min-width: %3px; max-width: %3px; min-height: %3px; "
                    "max-height: %3px; padding: 0px;"
        )
                    .arg(ring * 34 / 88)
                    .arg(ring / 2)
                    .arg(ring - 4);
    } else if (theme_settings::felt_chrome()) {
        // 54px in the kit's 290px slot; scaled down for crowded tables.
        const int size = std::clamp(
            static_cast<int>(std::min(width(), height()) * 0.15), 22, 54
        );
        // The kit's figure is a short ruled field, not a full-width input.
        sheet = QStringLiteral("font-size: %1px; min-width: %2px; "
                               "max-width: %3px;")
                    .arg(size)
                    .arg(size * 5 / 2)
                    .arg(size * 3);
    }
    for (QWidget* figure : { static_cast<QWidget*>(quiz_spin_box),
                             static_cast<QWidget*>(gameplay_input) })
        if (figure != nullptr && figure->styleSheet() != sheet)
            figure->setStyleSheet(sheet);
}

void table_slot::update_compact_controls() {
    if (gameplay_owner) {
        settings_bar_widget->hide();
        quiz_bar_widget->setVisible(quiz_prompt_active);
        copy_button->hide();
        copy_all_button->hide();
        const auto phase = gameplay_owner->phase();
        settings_button->setEnabled(
            phase == gameplay::session_phase::setup
            || phase == gameplay::session_phase::paused
        );
        swap_button->setEnabled(phase == gameplay::session_phase::paused);
        swap_bar_widget->setVisible(settings_button->isEnabled());
        update_settings_button_state();
    }
    if (compact_controls_button == nullptr || overlay_layout == nullptr
        || quiz_bar_widget == nullptr || swap_bar_widget == nullptr) {
        return;
    }
    if (settings_editor_panel) {
        compact_controls_button->hide();
        overlay_widget->hide();
        return;
    }
    const bool controls_visible = gameplay_owner
        ? settings_button->isEnabled() || quiz_prompt_active
        : current_phase == slot_phase::paused
            || !card_widget_internal->has_cards() || quiz_prompt_active;
    if (quiz_prompt_active) {
        // A stacked page's minimum hint allows wrapped text to be clipped.
        // Reserve its natural height-for-width before either centering it or
        // deciding to host it. Derive this from layout hints, never the old
        // explicit minimum, so shorter feedback/styles can shrink again.
        QSize quiz_size = quiz_bar_widget->sizeHint().expandedTo(
            quiz_bar_widget->minimumSizeHint()
        );
        quiz_size.setHeight(std::max(
            quiz_size.height(),
            quiz_bar_widget->heightForWidth(quiz_size.width())
        ));
        quiz_bar_widget->setMinimumSize(quiz_size);
    }
    // Read the active surfaces, not the outer layout's QWidgetItem cache:
    // that cache can still describe a hidden bar while restoring a question.
    const auto minimum = [](const QWidget* widget) {
        return widget->minimumSizeHint().expandedTo(widget->minimumSize());
    };
    QSize required
        = minimum(quiz_prompt_active ? quiz_bar_widget : swap_bar_widget);
    if (gameplay_owner && quiz_prompt_active
        && gameplay_owner->phase() == gameplay::session_phase::paused) {
        const auto actions = minimum(swap_bar_widget);
        required = overlay_layout->direction() == QBoxLayout::LeftToRight
            ? QSize(
                  required.width() + actions.width()
                      + overlay_layout->spacing(),
                  std::max(required.height(), actions.height())
              )
            : QSize(
                  std::max(required.width(), actions.width()),
                  required.height() + actions.height()
                      + overlay_layout->spacing()
              );
    }
    if (!gameplay_owner && !quiz_prompt_active && settings_overlay_visible
        && !use_dialog_for_settings
        && settings_style == slot_settings_style::classic) {
        const QSize settings = minimum(settings_bar_widget);
        required = overlay_layout->direction() == QBoxLayout::LeftToRight
            ? QSize(
                  required.width() + settings.width()
                      + overlay_layout->spacing(),
                  std::max(required.height(), settings.height())
              )
            : QSize(
                  std::max(required.width(), settings.width()),
                  required.height() + settings.height()
                      + overlay_layout->spacing()
              );
    }
    const auto margins = overlay_layout->contentsMargins();
    required += QSize(
        margins.left() + margins.right(), margins.top() + margins.bottom()
    );
    const bool compact
        = required.width() > width() || required.height() > height();
    compact_controls_button->setText(
        quiz_prompt_active ? QStringLiteral("?") : QStringLiteral("…")
    );
    const QString description = quiz_prompt_active
        ? (quiz_feedback_active
               ? str_label("Show answer feedback")
               : str_label("Answer accumulated-count question"))
        : str_label("Show slot actions");
    compact_controls_button->setAccessibleName(description);
    compact_controls_button->setToolTip(description);
    const QSize button_size
        = compact_controls_button->sizeHint().boundedTo(size());
    compact_controls_button->resize(button_size);
    compact_controls_button->move(
        std::max(0, (width() - button_size.width()) / 2),
        std::max(0, height() - button_size.height() - 2)
    );
    compact_controls_button->setVisible(
        controls_visible && (compact || controls_dialog)
    );
    compact_controls_button->raise();
    if (controls_dialog) {
        if (!controls_visible) {
            controls_dialog->close();
        }
        return;
    }
    overlay_widget->setVisible(controls_visible && !compact);
}

void table_slot::show_compact_controls() {
    if (gameplay_owner
        && gameplay_owner->phase() != gameplay::session_phase::setup
        && gameplay_owner->phase() != gameplay::session_phase::paused
        && !quiz_prompt_active)
        return;
    if (controls_dialog) {
        controls_dialog->raise();
        controls_dialog->activateWindow();
        return;
    }
    auto* dialog = new QDialog(this);
    controls_dialog = dialog;
    dialog->setObjectName(QStringLiteral("slot_controls_dialog"));
    dialog->setWindowTitle(str_label("Card controls"));
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(overlay_widget);
    overlay_widget->show();
    auto* close_buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(
        close_buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject
    );
    layout->addWidget(close_buttons);
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        dialog->layout()->removeWidget(overlay_widget);
        overlay_widget->setParent(this);
        overlay_widget->setGeometry(rect());
        controls_dialog = nullptr;
        dialog->deleteLater();
        update_compact_controls();
        if (compact_controls_button->isVisible()) {
            compact_controls_button->setFocus(Qt::OtherFocusReason);
        }
    });
    dialog->show();
    if (quiz_prompt_active && !quiz_feedback_active) {
        if (gameplay_owner && gameplay_input->isEnabled())
            gameplay_input->setFocus(Qt::OtherFocusReason);
        else if (!gameplay_owner)
            quiz_spin_box->setFocus(Qt::OtherFocusReason);
    } else if (quiz_prompt_active && quiz_continue_visible) {
        quiz_continue_button->setFocus(Qt::OtherFocusReason);
    } else if (quiz_prompt_active) {
        close_buttons->button(QDialogButtonBox::Close)
            ->setFocus(Qt::OtherFocusReason);
    } else {
        settings_button->setFocus(Qt::OtherFocusReason);
    }
}

void table_slot::update_settings_button_state(bool dialog_open) {
    if (settings_button == nullptr) {
        return;
    }
    if (gameplay_owner) {
        settings_button->setChecked(settings_editor_panel != nullptr);
        return;
    }
    if (settings_editor_panel
        || settings_style != slot_settings_style::classic) {
        settings_button->setChecked(settings_editor_panel != nullptr);
        return;
    }
    if (use_dialog_for_settings) {
        settings_button->setChecked(dialog_open);
        return;
    }
    settings_button->setChecked(settings_overlay_visible);
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
    update_quiz_figure_size();

    if (card_widget_internal != nullptr) {
        card_widget_internal->setGeometry(rect());
    }
    update_overlay_margins();
    if (gameplay_owner) {
        if (!controls_dialog)
            overlay_widget->setGeometry(rect());
        place_settings_editor();
        update_compact_controls();
        return;
    }

    if (overlay_widget != nullptr && !controls_dialog) {
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
    if (swap_button == nullptr || copy_button == nullptr) {
        return;
    }

    if (!swap_selected()) {
        swap_button->setChecked(false);
        copy_button->setChecked(false);
        return;
    }

    const bool is_copy_mode = current_copy_action == copy_action::cancel;
    swap_button->setChecked(!is_copy_mode);
    copy_button->setChecked(is_copy_mode);
}
