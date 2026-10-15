#include "shell/drill_picker.hpp"

#include "arch/str_label.hpp"
#include "settings/strategy_data.hpp"

#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>

bool drill_picker::catalogue_current() {
    if (invalidated)
        return false;
    auto latest
        = gameplay_drill_service(storage, policy.limits).load(&target_error);
    const bool same = latest && latest == target;
    target = std::move(latest);
    if (!same) {
        refresh(-1);
        show_message(str_label(
            "Saved drills changed. Review the refreshed list and try again."
        ));
    }
    return same;
}

bool drill_picker::persist_target(
    const QVector<gameplay_drill>& updated, int selected
) {
    if (!catalogue_current())
        return false;
    if (!gameplay_drill_service(storage, policy.limits)
             .save(updated, &target_error)) {
        show_message();
        return false;
    }
    target = updated;
    refresh(selected);
    show_message();
    return true;
}

void drill_picker::save_current() {
    if (invalidated || !policy.save_allowed || !current || !target)
        return;
    auto updated = *target;
    auto drill = *current;
    drill.name = name->text().trimmed();
    updated.append(drill);
    persist_target(updated, static_cast<int>(updated.size()) - 1);
}

void drill_picker::rename_selected() {
    const auto row = selected_row();
    if (!row)
        return;
    auto updated = *target;
    updated[*row].name = name->text().trimmed();
    persist_target(updated, *row);
}

void drill_picker::delete_selected() {
    const auto row = selected_row();
    if (!row)
        return;
    const QPointer<drill_picker> guard(this);
    const auto answer = QMessageBox::question(
        this, str_label("Delete drill"),
        str_label(
            "Delete the selected saved drill? This does not change the current "
            "session."
        ),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No
    );
    if (!guard || invalidated || answer != QMessageBox::Yes
        || !catalogue_current())
        return;
    auto updated = *target;
    updated.removeAt(*row);
    persist_target(updated, updated.isEmpty() ? -1 : 0);
}

void drill_picker::choose_selected() {
    const auto row = selected_row();
    if (!row || !policy.load_gameplay || !catalogue_current())
        return;
    const auto drill = target->at(*row);
    const bool supported
        = supports_drill(drill, strategy_repository(), policy.limits);
    if (!supported) {
        show_message(str_label(
            "This drill cannot be loaded. The current session is unchanged."
        ));
        return;
    }
    if (policy.confirm_replacement) {
        const QPointer<drill_picker> guard(this);
        const auto answer = QMessageBox::question(
            this, str_label("Load drill"),
            str_label("Discard the current session and load this drill? No "
                      "result will be recorded."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No
        );
        if (!guard || invalidated || answer != QMessageBox::Yes
            || !catalogue_current())
            return;
    }
    selection = drill;
    accept();
}
