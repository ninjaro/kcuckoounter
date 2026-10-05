#include "shell/main_window.hpp"

#include "card_helpers/card_sheet.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"

#include "arch/android_ui.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/session_checkpoint.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "settings/training_progress.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalBlocker>
#include <QStringList>

training_drill main_window::capture_training_drill() const {
    training_drill drill;
    drill.quiz_type = quiz_type->currentIndex();
    drill.wait_for_answers = wait_for_answers->isChecked();
    drill.allow_skipping = allow_skipping->isChecked();
    drill.dealing_mode = dealing_mode->currentIndex();
    drill.pickup_interval_ms = speed_slider->value();
    drill.slot_settings = table_widget->capture_drill_settings();
    return drill;
}

bool main_window::launch_training_drill(const training_drill& drill) {
    if (table_widget->active_gameplay_session())
        return false;
    if (!table_widget->configure_drill(drill))
        return false;
    {
        const QSignalBlocker count_blocker(table_slots_count);
        const QSignalBlocker quiz_blocker(quiz_type);
        const QSignalBlocker wait_blocker(wait_for_answers);
        const QSignalBlocker skip_blocker(allow_skipping);
        const QSignalBlocker dealing_blocker(dealing_mode);
        table_slots_count->setValue(
            static_cast<int>(drill.slot_settings.size())
        );
        quiz_type->setCurrentIndex(drill.quiz_type);
        wait_for_answers->setChecked(drill.wait_for_answers);
        wait_for_answers->setEnabled(drill.quiz_type == 0);
        allow_skipping->setChecked(drill.allow_skipping);
        dealing_mode->setCurrentIndex(drill.dealing_mode);
    }
    speed_slider->setValue(drill.pickup_interval_ms);
    on_continue_button_clicked();
    persist_setup_preferences();
    on_start_pause_triggered();
    return true;
}

void main_window::on_saved_drills_triggered() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    return;
#else
    if (table_widget->active_gameplay_session())
        return;
    const bool resume_on_cancel = quiz_started && !quiz_paused;
    pause_for_dialog();
    const auto current = capture_training_drill();
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    training_drill_service service(settings);
    QString error;
    auto stored = service.load(&error);

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("saved_drills_dialog"));
    dialog.setWindowTitle(str_label("Pick a drill"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel(
        str_label(
            "Save the current table's gameplay settings, then launch fresh "
            "shoes later. "
            "Appearance and progress are not saved in a drill. Close to keep "
            "the current session."
        ),
        &dialog
    );
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("saved_drills_list"));
    list->setAccessibleName(str_label("Saved drills"));
    layout->addWidget(list, 1);
    auto* details = new QPlainTextEdit(&dialog);
    details->setObjectName(QStringLiteral("saved_drill_details"));
    details->setAccessibleName(str_label("Drill settings"));
    details->setReadOnly(true);
    layout->addWidget(details, 1);
    auto* name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("saved_drill_name"));
    name->setMaxLength(training_drill_service::maximum_name_length);
    auto* name_label = new QLabel(str_label("Drill name"), &dialog);
    name_label->setBuddy(name);
    layout->addWidget(name_label);
    layout->addWidget(name);
    auto* edits = new QDialogButtonBox(&dialog);
    auto* save = edits->addButton(
        str_label("Save current"), QDialogButtonBox::ActionRole
    );
    auto* rename
        = edits->addButton(str_label("Rename"), QDialogButtonBox::ActionRole);
    auto* remove
        = edits->addButton(str_label("Delete"), QDialogButtonBox::ActionRole);
    save->setObjectName(QStringLiteral("save_current_drill"));
    rename->setObjectName(QStringLiteral("rename_drill"));
    remove->setObjectName(QStringLiteral("delete_drill"));
    for (auto* button : { save, rename, remove })
        button->setAutoDefault(false);
    layout->addWidget(edits);
    auto* message = new QLabel(error, &dialog);
    message->setObjectName(QStringLiteral("saved_drill_message"));
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* launch = buttons->addButton(
        str_label("Launch drill"), QDialogButtonBox::ActionRole
    );
    launch->setObjectName(QStringLiteral("launch_drill"));
    launch->setAutoDefault(false); // typing a name must not replace a session
    buttons->button(QDialogButtonBox::Close)->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    const auto selection_changed = [&] {
        const int index = list->currentRow();
        const bool selected = stored && index >= 0 && index < stored->size();
        rename->setEnabled(selected);
        remove->setEnabled(selected);
        launch->setEnabled(
            selected
            && is_drill_configuration_supported(
                stored->at(index), strategy_repository()
            )
        );
        if (!selected) {
            if (!stored)
                details->setPlainText(error);
            else if (stored->isEmpty())
                details->setPlainText(str_label(
                    "No saved drills. Configure the table with New game and "
                    "slot Details, then save it here."
                ));
            else
                details->setPlainText(
                    str_label("Select a saved drill to review its settings.")
                );
            return;
        }
        const auto& drill = stored->at(index);
        name->setText(drill.name);
        QStringList lines;
        lines << str_label("Pickup interval (ms)") + QStringLiteral(": ")
                + QString::number(drill.pickup_interval_ms)
              << str_label("Quiz mode") + QStringLiteral(": ")
                + quiz_type->itemText(drill.quiz_type)
              << str_label("Pause for answers") + QStringLiteral(": ")
                + (drill.wait_for_answers ? str_label("Yes") : str_label("No"))
              << str_label("Allow skipping") + QStringLiteral(": ")
                + (drill.allow_skipping ? str_label("Yes") : str_label("No"))
              << str_label("Dealing mode") + QStringLiteral(": ")
                + dealing_mode->itemText(drill.dealing_mode);
        int number = 0;
        for (const auto& slot : drill.slot_settings) {
            QString strategy_name = slot.strategy_slug;
            for (const auto& strategy : strategy_repository().strategies)
                if (strategy.slug == slot.strategy_slug)
                    strategy_name = strategy.name;
            lines << QString()
                  << str_label("Slot") + QStringLiteral(" ")
                    + QString::number(++number)
                  << strategy_name
                  << str_label("Deck count") + QStringLiteral(": ")
                    + QString::number(slot.deck_count)
                  << (slot.infinity_enabled ? str_label("Infinite shoe")
                                            : str_label("Finite shoe"))
                  << (slot.training_mode ? str_label("Training mode")
                                         : str_label("Scored mode"));
        }
        if (!launch->isEnabled())
            lines.prepend(str_label(
                "Cannot launch: a saved strategy is unavailable. No "
                "replacement strategy will be chosen."
            ));
        details->setPlainText(lines.join(QLatin1Char('\n')));
    };
    connect(list, &QListWidget::currentRowChanged, &dialog, selection_changed);
    const auto refresh = [&](int selected) {
        list->clear();
        save->setEnabled(stored.has_value());
        if (stored)
            for (const auto& drill : *stored)
                list->addItem(drill.name);
        list->setCurrentRow(selected);
        selection_changed();
    };
    const auto persist
        = [&](const QVector<training_drill>& updated, int selected) {
              // Avoid overwriting changes made by another settings reader while
              // this modal picker was open. This is not a multi-process
              // transaction API.
              const auto latest = service.load(&error);
              if (!latest || latest != stored) {
                  stored = latest;
                  message->setText(
                      latest ? str_label(
                                   "Saved drills changed. Review the refreshed "
                                   "list and try again."
                               )
                             : error
                  );
                  refresh(-1);
                  return;
              }
              if (!service.save(updated, &error)) {
                  message->setText(error);
                  return;
              }
              stored = updated;
              message->clear();
              refresh(selected);
          };
    connect(save, &QPushButton::clicked, &dialog, [&] {
        if (!stored)
            return;
        auto drill = current;
        drill.name = name->text().trimmed();
        auto updated = *stored;
        updated.append(drill);
        persist(updated, static_cast<int>(updated.size()) - 1);
    });
    connect(rename, &QPushButton::clicked, &dialog, [&] {
        if (!stored || list->currentRow() < 0)
            return;
        auto updated = *stored;
        updated[list->currentRow()].name = name->text().trimmed();
        persist(updated, list->currentRow());
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        if (!stored || list->currentRow() < 0)
            return;
        if (QMessageBox::question(
                &dialog, str_label("Delete drill"),
                str_label(
                    "Delete the selected saved drill? This does not change the "
                    "current session."
                ),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No
            )
            != QMessageBox::Yes)
            return;
        auto updated = *stored;
        updated.removeAt(list->currentRow());
        persist(updated, updated.isEmpty() ? -1 : 0);
    });
    bool launched = false;
    connect(launch, &QPushButton::clicked, &dialog, [&] {
        if (!stored || list->currentRow() < 0)
            return;
        const auto drill = stored->at(list->currentRow());
        if (quiz_started
            && QMessageBox::question(
                   &dialog, str_label("Launch drill"),
                   str_label(
                       "Discard the current session and start fresh shoes? "
                       "This session will not be recorded as a result."
                   ),
                   QMessageBox::Yes | QMessageBox::No, QMessageBox::No
               ) != QMessageBox::Yes)
            return;
        if (!launch_training_drill(drill)) {
            message->setText(str_label(
                "This drill can no longer be launched. The current session has "
                "not been changed."
            ));
            return;
        }
        launched = true;
        dialog.accept();
    });
    refresh(stored && !stored->isEmpty() ? 0 : -1);
    dialog.resize(460, 570);
    dialog.exec();
    if (!launched && resume_on_cancel && quiz_started && quiz_paused)
        on_start_pause_triggered();
#endif
}
