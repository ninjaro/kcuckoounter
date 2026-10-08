#include "shell/drill_picker.hpp"

#include "arch/str_label.hpp"
#include "settings/strategy_data.hpp"
#include "table/gameplay_setup.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

void drill_picker::convert_selected() {
    const auto row = selected_row();
    if (!row || row->gameplay || !target || !catalogue_current(false))
        return;
    const auto source = legacy->at(row->index);
    // Defaults are a visible proposal, not an inferred equivalence to v1.
    const auto draft = draft_legacy_drill(source, {}, policy.limits);
    if (!draft)
        return;
    auto preview = create_drill_session(
        *draft, strategy_repository(),
        { draft->decks.size(), policy.limits.technical_cap }
    );
    if (!preview)
        return;

    const QPointer<drill_picker> guard(this);
    QPointer<QDialog> review = new QDialog(this);
    review->setObjectName(QStringLiteral("drill_conversion_dialog"));
    review->setWindowTitle(str_label("Review gameplay conversion"));
    auto* layout = new QVBoxLayout(review);
    auto* scroll = new QScrollArea(review);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    auto* fields = new QVBoxLayout(content);
    auto* notice = new QLabel(
        str_label(
            "This is not a lossless conversion. Legacy quiz mode and Pause for "
            "answers "
            "do not define Gameplay question source, scope or global pause. "
            "Review "
            "failure policy, lives and every proposed setting below. The "
            "legacy drill "
            "will be retained. A saved copy loads fresh Setup; it does not "
            "start a game."
        ),
        content
    );
    notice->setObjectName(QStringLiteral("drill_conversion_notice"));
    notice->setWordWrap(true);
    fields->addWidget(notice);
    auto* title = new QLineEdit(source.name, content);
    title->setObjectName(QStringLiteral("converted_drill_name"));
    title->setMaxLength(gameplay_drill_service::maximum_name_length);
    auto* label = new QLabel(str_label("Gameplay copy name"), content);
    label->setBuddy(title);
    fields->addWidget(label);
    fields->addWidget(title);
    auto* error = new QLabel(content);
    error->setObjectName(QStringLiteral("drill_conversion_error"));
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    fields->addWidget(error);
    auto* summary = new QPlainTextEdit(summarize(*draft), content);
    summary->setReadOnly(true);
    summary->setAccessibleName(
        str_label("Copied deck settings and initial proposal")
    );
    fields->addWidget(summary);
    fields->addWidget(new gameplay::setup_widget(*preview, content));
    scroll->setWidget(content);
    layout->addWidget(scroll);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, review);
    auto* save_copy = buttons->addButton(
        str_label("Save gameplay copy"), QDialogButtonBox::ActionRole
    );
    save_copy->setObjectName(QStringLiteral("save_converted_drill"));
    save_copy->setAutoDefault(false);
    connect(buttons, &QDialogButtonBox::rejected, review, &QDialog::reject);
    connect(
        save_copy, &QPushButton::clicked, review,
        [guard, review, &preview, title, error] {
            if (!guard || !review || guard->invalidated)
                return;
            for (auto* spin : review->findChildren<QSpinBox*>()) {
                spin->interpretText();
                if (!guard || !review || guard->invalidated)
                    return;
            }
            if (!capture_gameplay_drill(
                    *preview, title->text().trimmed(), guard->policy.limits
                )) {
                error->setText(str_label(
                    "Choose a valid name and review the settings before saving."
                ));
                return;
            }
            error->clear();
            const auto answer = QMessageBox::question(
                review, str_label("Save gameplay copy"),
                str_label(
                    "Save these reviewed Gameplay settings as a separate "
                    "drill? "
                    "They are not equivalent to the legacy trainer. The v1 "
                    "drill "
                    "is kept."
                ),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No
            );
            if (guard && review && !guard->invalidated
                && answer == QMessageBox::Yes)
                review->accept();
        }
    );
    layout->addWidget(buttons);
    review->resize(480, 650);
    const auto result = review->exec();
    std::optional<gameplay_drill> converted;
    if (guard && review && !invalidated && result == QDialog::Accepted)
        converted = capture_gameplay_drill(
            *preview, title->text().trimmed(), policy.limits
        );
    delete review.data(); // Borrowed setup fields must die before preview.
    if (!guard || invalidated)
        return;
    if (result == QDialog::Accepted && !converted) {
        show_message(str_label(
            "The reviewed copy has invalid settings or an invalid name. "
            "Nothing was saved."
        ));
        return;
    }
    if (!converted || !catalogue_current(false) || !target)
        return;
    auto updated = *target;
    updated.append(*converted);
    persist_target(updated, static_cast<int>(updated.size()) - 1);
}
