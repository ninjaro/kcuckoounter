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

#include <QLabel>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QString>
#include <QVBoxLayout>
#include <QtGlobal>

bool table_slot::bind_gameplay_deck(
    table& host, gameplay::session& owner, gameplay::deck_id id
) {
    if (gameplay_owner || !card_widget_internal->bind_gameplay_deck(owner, id))
        return false;
    gameplay_owner = &owner;
    gameplay_id = id;
    gameplay_host = &host;
    setup_gameplay_input();
    settings_bar_widget->setEnabled(false);
    quiz_bar_widget->setEnabled(false);
    copy_button->setEnabled(false);
    copy_all_button->setEnabled(false);
    refresh_gameplay_deck();
    return true;
}

void table_slot::refresh_gameplay_deck() {
    if (!gameplay_owner)
        return;
    if (gameplay_editor_fields
        && !gameplay_editor_fields->pending_changes_current())
        finish_settings_editor(false);
    if (gameplay_editor_fields)
        gameplay_editor_fields->refresh();
    const auto* deck = gameplay_owner->deck(gameplay_id);
    const auto slug = QString::fromStdString(deck->configuration.strategy_slug);
    const auto& catalog = strategy_repository();
    const auto strategy = std::ranges::find_if(
        catalog.strategies,
        [&slug](const auto& candidate) { return candidate.slug == slug; }
    );
    card_widget_internal->set_strategy_name(
        strategy == catalog.strategies.end() ? slug : strategy->name
    );
    card_widget_internal->refresh_gameplay_deck();
    refresh_gameplay_quiz();
    update_compact_controls();
}

void table_slot::setup_gameplay_input() {
    gameplay_input = new QLineEdit(quiz_prompt_widget);
    gameplay_input->setObjectName(QStringLiteral("gameplay_quiz_input"));
    gameplay_input->setAccessibleName(str_label("Accumulated-count answer"));
    gameplay_input->setToolTip(
        str_label("Enter a signed 64-bit accumulated count")
    );
    gameplay_input->setInputMethodHints(Qt::ImhFormattedNumbersOnly);
    gameplay_input->setMaxLength(20);
    gameplay_input->setMinimumWidth(
        gameplay_input->fontMetrics().horizontalAdvance(
            QStringLiteral("-9223372036854775808")
        )
        + 20
    );
    gameplay_input->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[+-]?[0-9]{1,19}")), gameplay_input
    ));
    delete quiz_prompt_widget->layout()->replaceWidget(
        quiz_spin_box, gameplay_input
    );
    quiz_spin_box->hide();
    quiz_spin_box->setEnabled(false);
    quiz_weight_label->setText(str_label("Count"));
    quiz_weight_label->setBuddy(gameplay_input);
    gameplay_input->installEventFilter(this);
    quiz_answer_button->installEventFilter(this);
    quiz_skip_button->installEventFilter(this);
    if (compact_controls_button)
        compact_controls_button->installEventFilter(this);
    auto* layout = qobject_cast<QVBoxLayout*>(quiz_prompt_widget->layout());
    gameplay_input_error = new QLabel(quiz_prompt_widget);
    gameplay_input_error->setObjectName(
        QStringLiteral("gameplay_quiz_validation")
    );
    gameplay_input_error->setWordWrap(true);
    gameplay_input_error->setText(
        str_label("Enter a whole count in the signed 64-bit range.")
    );
    layout->insertWidget(1, gameplay_input_error);
    gameplay_correction_label = new QLabel(quiz_prompt_widget);
    gameplay_correction_label->setObjectName(
        QStringLiteral("gameplay_previous_correction")
    );
    gameplay_correction_label->setWordWrap(true);
    gameplay_correction_label->setTextFormat(Qt::PlainText);
    layout->insertWidget(2, gameplay_correction_label);
    quiz_feedback_label->setTextFormat(Qt::PlainText);
    QWidget* previous = gameplay_input;
    if (quiz_chip_widget) {
        for (auto* button : quiz_chip_widget->findChildren<QPushButton*>()) {
            QWidget::setTabOrder(previous, button);
            previous = button;
        }
    }
    QWidget::setTabOrder(previous, quiz_answer_button);
    connect(
        gameplay_input, &QLineEdit::textEdited, this,
        &table_slot::on_gameplay_input_edited
    );
}

std::optional<std::int64_t> table_slot::gameplay_input_value() const {
    if (!gameplay_input || !gameplay_input->hasAcceptableInput())
        return std::nullopt;
    bool valid = false;
    const auto value = gameplay_input->text().toLongLong(&valid);
    return valid ? std::optional<std::int64_t> { value } : std::nullopt;
}

void table_slot::refresh_gameplay_quiz() {
    const auto* deck = gameplay_owner->deck(gameplay_id);
    const auto next_prompt
        = gameplay_host->active_gameplay_session() == gameplay_owner
            && gameplay_owner->phase() != gameplay::session_phase::finished
        ? gameplay_host->gameplay_quiz_prompt(gameplay_id)
        : std::nullopt;
    const auto value = deck->quiz ? deck->quiz->input : deck->latest_input;
    // Preserve partially typed signs, caret/selection and valid spelling on
    // no-op clock/theme refresh; external edits/new questions reproject.
    if (gameplay_prompt != next_prompt || gameplay_projected_input != value) {
        gameplay_input->setText(QString::number(value));
        gameplay_projected_input = value;
    }
    gameplay_prompt = next_prompt;
    quiz_prompt_active
        = gameplay_prompt.has_value() || gameplay_feedback.has_value();
    quiz_feedback_active = gameplay_feedback.has_value() && !gameplay_prompt;
    quiz_continue_button->setText(str_label("Dismiss"));
    quiz_continue_button->setToolTip(
        str_label("Dismiss correction; dealing is not held by feedback")
    );
    update_quiz_controls_visibility();
    update_overlay_layout();
}

void table_slot::on_gameplay_input_edited() {
    if (!gameplay_prompt || !gameplay_input->isEnabled())
        return;
    const auto value = gameplay_input_value();
    if (!value) {
        update_quiz_controls_visibility();
        return; // temporary invalid text never changes last valid input
    }
    const auto prompt = *gameplay_prompt;
    gameplay_projected_input = *value;
    const QPointer<table_slot> guard(this);
    const bool accepted = gameplay_host->edit_gameplay_quiz(prompt, *value);
    if (!guard)
        return;
    if (!accepted) {
        gameplay_projected_input.reset();
        refresh_gameplay_quiz();
    }
}

void table_slot::show_gameplay_answer(const gameplay::quiz_answer& answer) {
    if (answer.owner != gameplay_id)
        return;
    if (answer.outcome == gameplay::quiz_outcome::correct) {
        gameplay_feedback.reset();
    } else {
        gameplay_feedback = answer; // one small correction, never event history
#ifdef KC_KDE
        const auto text = i18n(
            "You entered %1. Expected accumulated count: %2.",
            static_cast<qlonglong>(answer.submitted_count),
            static_cast<qlonglong>(answer.expected_count)
        );
#else
        const auto text
            = str_label("You entered %1. Expected accumulated count: %2.")
                  .arg(static_cast<qlonglong>(answer.submitted_count))
                  .arg(static_cast<qlonglong>(answer.expected_count));
#endif
        quiz_feedback_label->setText(text);
        gameplay_correction_label->setText(text);
    }
    refresh_gameplay_quiz();
}

void table_slot::focus_gameplay_input() {
    if (!gameplay_prompt || !gameplay_input->isEnabled())
        return;
    if (compact_controls_button->isVisible() && !controls_dialog)
        compact_controls_button->setFocus(Qt::OtherFocusReason);
    else
        gameplay_input->setFocus(Qt::OtherFocusReason);
}
