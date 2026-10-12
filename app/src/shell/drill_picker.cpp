#include "shell/drill_picker.hpp"

#include "arch/str_label.hpp"
#include "settings/strategy_data.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <utility>

drill_picker::drill_picker(
    QSettings& storage_value, saved_drill snapshot, drill_picker_policy rules,
    QWidget* parent
)
    : QDialog(parent)
    , storage(storage_value)
    , current(std::move(snapshot))
    , policy(rules) {
    legacy = training_drill_service(storage).load(&legacy_error);
    target = gameplay_drill_service(storage, policy.limits).load(&target_error);
    setObjectName(QStringLiteral("saved_drills_dialog"));
    setWindowTitle(str_label("Pick a drill"));
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        str_label(
            "Save current configuration in its own format. Legacy drills "
            "launch "
            "the legacy trainer; Gameplay drills load editable setup before "
            "Start. "
            "Appearance and progress are not saved. Close to keep the current "
            "session."
        ),
        this
    );
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    list = new QListWidget(this);
    list->setObjectName(QStringLiteral("saved_drills_list"));
    list->setAccessibleName(str_label("Saved drills"));
    layout->addWidget(list, 1);
    details = new QPlainTextEdit(this);
    details->setObjectName(QStringLiteral("saved_drill_details"));
    details->setAccessibleName(str_label("Drill settings"));
    details->setReadOnly(true);
    layout->addWidget(details, 1);
    name = new QLineEdit(this);
    name->setObjectName(QStringLiteral("saved_drill_name"));
    name->setMaxLength(gameplay_drill_service::maximum_name_length);
    auto* label = new QLabel(str_label("Drill name"), this);
    label->setBuddy(name);
    layout->addWidget(label);
    layout->addWidget(name);
    auto* edits = new QDialogButtonBox(this);
    save = edits->addButton(
        str_label("Save current"), QDialogButtonBox::ActionRole
    );
    rename
        = edits->addButton(str_label("Rename"), QDialogButtonBox::ActionRole);
    remove
        = edits->addButton(str_label("Delete"), QDialogButtonBox::ActionRole);
    auto* conversion = new QDialogButtonBox(this);
    convert = conversion->addButton(
        str_label("Convert to gameplay…"), QDialogButtonBox::ActionRole
    );
    save->setObjectName(QStringLiteral("save_current_drill"));
    rename->setObjectName(QStringLiteral("rename_drill"));
    remove->setObjectName(QStringLiteral("delete_drill"));
    convert->setObjectName(QStringLiteral("convert_drill"));
    for (auto* button : { save, rename, remove, convert })
        button->setAutoDefault(false);
    layout->addWidget(edits);
    layout->addWidget(conversion);
    message = new QLabel(this);
    message->setObjectName(QStringLiteral("saved_drill_message"));
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    launch = buttons->addButton(
        str_label("Launch drill"), QDialogButtonBox::ActionRole
    );
    launch->setObjectName(QStringLiteral("launch_drill"));
    launch->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Close)->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(
        list, &QListWidget::currentRowChanged, this,
        &drill_picker::selection_changed
    );
    connect(save, &QPushButton::clicked, this, &drill_picker::save_current);
    connect(
        rename, &QPushButton::clicked, this, &drill_picker::rename_selected
    );
    connect(
        remove, &QPushButton::clicked, this, &drill_picker::delete_selected
    );
    connect(
        convert, &QPushButton::clicked, this, &drill_picker::convert_selected
    );
    connect(
        launch, &QPushButton::clicked, this, &drill_picker::choose_selected
    );
    show_message();
    refresh(
        legacy && !legacy->isEmpty()       ? row_key { false, 0 }
            : target && !target->isEmpty() ? row_key { true, 0 }
                                           : row_key {}
    );
    resize(460, 570);
}

const std::optional<saved_drill>& drill_picker::chosen() const {
    return selection;
}

void drill_picker::invalidate() {
    invalidated = true;
    selection.reset();
    setEnabled(false);
    for (auto* dialog : findChildren<QDialog*>())
        dialog->reject();
    reject();
}

std::optional<drill_picker::row_key> drill_picker::selected_row() const {
    const auto* item = list->currentItem();
    if (invalidated || !item)
        return std::nullopt;
    const row_key result { item->data(Qt::UserRole).toBool(),
                           item->data(Qt::UserRole + 1).toInt() };
    const auto size = result.gameplay ? (target ? target->size() : 0)
                                      : (legacy ? legacy->size() : 0);
    return result.index >= 0 && result.index < size ? std::make_optional(result)
                                                    : std::nullopt;
}

void drill_picker::refresh(row_key selected) {
    list->clear();
    const auto append = [&](const QString& text, bool gameplay, int index) {
        auto* item = new QListWidgetItem(text, list);
        item->setData(Qt::UserRole, gameplay);
        item->setData(Qt::UserRole + 1, index);
        item->setToolTip(
            gameplay ? str_label("Gameplay drill (v2)")
                     : str_label("Legacy drill (v1)")
        );
        if (selected.gameplay == gameplay && selected.index == index)
            list->setCurrentItem(item);
    };
    if (legacy)
        for (int index = 0; index < legacy->size(); ++index)
            append(legacy->at(index).name, false, index);
    if (target)
        for (int index = 0; index < target->size(); ++index)
            append(
                target->at(index).name + str_label(" [Gameplay]"), true, index
            );
    save->setEnabled(
        policy.save_allowed && !invalidated
        && (std::holds_alternative<gameplay_drill>(current)
                ? target.has_value()
                : legacy.has_value())
    );
    selection_changed();
}

void drill_picker::selection_changed() {
    const auto row = selected_row();
    rename->setEnabled(row.has_value());
    remove->setEnabled(row.has_value());
    convert->setEnabled(false);
    launch->setEnabled(false);
    if (!row) {
        details->setPlainText(
            !policy.save_allowed
                ? str_label(
                      "Apply or close open deck Details before saving current "
                      "configuration."
                  )
                : str_label(
                      "Select a saved drill, or configure the table and save "
                      "its current settings here."
                  )
        );
        return;
    }
    const bool supported = row->gameplay
        ? supports_drill(
              target->at(row->index), strategy_repository(), policy.limits
          )
        : is_drill_configuration_supported(
              legacy->at(row->index), strategy_repository()
          );
    const bool allowed
        = row->gameplay ? policy.load_gameplay : policy.launch_legacy;
    launch->setText(
        row->gameplay ? str_label("Load gameplay setup")
                      : str_label("Launch drill")
    );
    launch->setEnabled(supported && allowed);
    if (!row->gameplay) {
        const auto draft
            = draft_legacy_drill(legacy->at(row->index), {}, policy.limits);
        convert->setEnabled(
            target.has_value() && draft
            && supports_drill(*draft, strategy_repository(), policy.limits)
        );
    }
    QString text;
    if (row->gameplay) {
        name->setText(target->at(row->index).name);
        text = summarize(target->at(row->index));
    } else {
        name->setText(legacy->at(row->index).name);
        text = summarize(legacy->at(row->index));
    }
    if (!supported)
        text.prepend(
            str_label(
                "Cannot launch: a saved strategy is unavailable or "
                "unsupported. No replacement strategy will be chosen."
            )
            + QLatin1Char('\n')
        );
    else if (!allowed)
        text.prepend(
            str_label(
                "Finish the current session before loading another drill."
            )
            + QLatin1Char('\n')
        );
    if (!policy.save_allowed)
        text.prepend(
            str_label(
                "Apply or close open deck Details before saving current "
                "configuration."
            )
            + QLatin1Char('\n')
        );
    details->setPlainText(text);
}

void drill_picker::show_message(const QString& extra) {
    QStringList lines;
    if (!legacy_error.isEmpty())
        lines << str_label("Legacy drills") + QStringLiteral(": ")
                + legacy_error;
    if (!target_error.isEmpty())
        lines << str_label("Gameplay drills") + QStringLiteral(": ")
                + target_error;
    if (!extra.isEmpty())
        lines << extra;
    message->setText(lines.join(QLatin1Char('\n')));
}
