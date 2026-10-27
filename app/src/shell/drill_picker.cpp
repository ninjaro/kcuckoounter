#include "shell/drill_picker.hpp"

#include "arch/str_label.hpp"
#include "settings/strategy_data.hpp"
#include <shared_ui/qt/editor_panel.hpp>
#include <shared_ui/qt/presentation.hpp>

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
    QSettings& storage_value, std::optional<gameplay_drill> snapshot,
    drill_picker_policy rules, QWidget* parent
)
    : QDialog(parent)
    , storage(storage_value)
    , current(std::move(snapshot))
    , policy(rules) {
    target = gameplay_drill_service(storage, policy.limits).load(&target_error);
    setObjectName(QStringLiteral("saved_drills_dialog"));
    setWindowTitle(str_label("Pick a drill"));
    auto* outer = new QVBoxLayout(this);
    auto* panel = new shared_ui::qt::editor_panel(this);
    panel->set_scrollable(true);
    outer->addWidget(panel);
    auto* content = new QWidget(panel);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    panel->set_content(content);
    auto* explanation = new QLabel(
        str_label(
            "Save a named gameplay setup or load it before Start. Appearance "
            "and progress are not saved. Close to keep the current session."
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
    save->setObjectName(QStringLiteral("save_current_drill"));
    rename->setObjectName(QStringLiteral("rename_drill"));
    remove->setObjectName(QStringLiteral("delete_drill"));
    for (auto* button : { save, rename, remove })
        button->setAutoDefault(false);
    layout->addWidget(edits);
    message = new QLabel(this);
    message->setObjectName(QStringLiteral("saved_drill_message"));
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto* buttons = panel->buttons();
    buttons->setStandardButtons(QDialogButtonBox::Close);
    launch = buttons->addButton(
        str_label("Launch drill"), QDialogButtonBox::ActionRole
    );
    launch->setObjectName(QStringLiteral("launch_drill"));
    launch->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Close)->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
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
        launch, &QPushButton::clicked, this, &drill_picker::choose_selected
    );
    show_message();
    refresh(target && !target->isEmpty() ? 0 : -1);
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    auto presentation
        = shared_ui::qt::presentation_for(parent ? *parent : *this);
    presentation.profile = shared_ui::qt::platform_profile::android;
    shared_ui::qt::apply_presentation(*this, presentation);
#endif
    resize(460, 570);
}

const std::optional<gameplay_drill>& drill_picker::chosen() const {
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

std::optional<int> drill_picker::selected_row() const {
    const auto* item = list->currentItem();
    if (invalidated || !item || !target)
        return std::nullopt;
    const int index = item->data(Qt::UserRole).toInt();
    return index >= 0 && index < target->size() ? std::make_optional(index)
                                                : std::nullopt;
}

void drill_picker::refresh(int selected) {
    list->clear();
    if (target)
        for (int index = 0; index < target->size(); ++index) {
            auto* item = new QListWidgetItem(target->at(index).name, list);
            item->setData(Qt::UserRole, index);
            if (selected == index)
                list->setCurrentItem(item);
        }
    save->setEnabled(policy.save_allowed && !invalidated && current && target);
    selection_changed();
}

void drill_picker::selection_changed() {
    const auto row = selected_row();
    rename->setEnabled(row.has_value());
    remove->setEnabled(row.has_value());
    launch->setEnabled(false);
    if (!row) {
        details->setPlainText(
            !current
                ? str_label("Create a gameplay setup before saving a drill.")
                : !policy.save_allowed
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
    const bool supported = supports_drill(
        target->at(*row), strategy_repository(), policy.limits
    );
    const bool allowed = policy.load_gameplay;
    launch->setText(str_label("Load gameplay setup"));
    launch->setEnabled(supported && allowed);
    name->setText(target->at(*row).name);
    QString text = summarize(target->at(*row));
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
    if (!policy.save_allowed || !current)
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
    if (!target_error.isEmpty())
        lines << str_label("Gameplay drills") + QStringLiteral(": ")
                + target_error;
    if (!extra.isEmpty())
        lines << extra;
    message->setText(lines.join(QLatin1Char('\n')));
}
