#include "shell/drill_picker.hpp"

#include "arch/str_label.hpp"
#include "settings/strategy_data.hpp"

#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>

bool drill_picker::catalogue_current(bool gameplay) {
    if (invalidated)
        return false;
    bool same = false;
    if (gameplay) {
        auto latest = gameplay_drill_service(storage, policy.limits)
                          .load(&target_error);
        same = latest && latest == target;
        target = std::move(latest);
    } else {
        auto latest = training_drill_service(storage).load(&legacy_error);
        same = latest && latest == legacy;
        legacy = std::move(latest);
    }
    if (!same) {
        refresh({});
        show_message(str_label(
            "Saved drills changed. Review the refreshed list and try again."
        ));
    }
    return same;
}

bool drill_picker::persist_legacy(
    const QVector<training_drill>& updated, int selected
) {
    if (!catalogue_current(false))
        return false;
    if (!training_drill_service(storage).save(updated, &legacy_error)) {
        show_message();
        return false;
    }
    legacy = updated;
    refresh({ false, selected });
    show_message();
    return true;
}

bool drill_picker::persist_target(
    const QVector<gameplay_drill>& updated, int selected
) {
    if (!catalogue_current(true))
        return false;
    if (!gameplay_drill_service(storage, policy.limits)
             .save(updated, &target_error)) {
        show_message();
        return false;
    }
    target = updated;
    refresh({ true, selected });
    show_message();
    return true;
}

void drill_picker::save_current() {
    if (invalidated || !policy.save_allowed)
        return;
    const auto title = name->text().trimmed();
    if (const auto* snapshot = std::get_if<gameplay_drill>(&current)) {
        if (!target)
            return;
        auto updated = *target;
        auto drill = *snapshot;
        drill.name = title;
        updated.append(drill);
        persist_target(updated, static_cast<int>(updated.size()) - 1);
    } else {
        if (!legacy)
            return;
        auto updated = *legacy;
        auto drill = std::get<training_drill>(current);
        drill.name = title;
        updated.append(drill);
        persist_legacy(updated, static_cast<int>(updated.size()) - 1);
    }
}

void drill_picker::rename_selected() {
    const auto row = selected_row();
    if (!row)
        return;
    if (row->gameplay) {
        auto updated = *target;
        updated[row->index].name = name->text().trimmed();
        persist_target(updated, row->index);
    } else {
        auto updated = *legacy;
        updated[row->index].name = name->text().trimmed();
        persist_legacy(updated, row->index);
    }
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
        || !catalogue_current(row->gameplay))
        return;
    if (row->gameplay) {
        auto updated = *target;
        updated.removeAt(row->index);
        persist_target(updated, updated.isEmpty() ? -1 : 0);
    } else {
        auto updated = *legacy;
        updated.removeAt(row->index);
        persist_legacy(updated, updated.isEmpty() ? -1 : 0);
    }
}

void drill_picker::choose_selected() {
    const auto row = selected_row();
    if (!row || !(row->gameplay ? policy.load_gameplay : policy.launch_legacy)
        || !catalogue_current(row->gameplay))
        return;
    const saved_drill drill = row->gameplay
        ? saved_drill(target->at(row->index))
        : saved_drill(legacy->at(row->index));
    const bool supported = row->gameplay
        ? supports_drill(
              std::get<gameplay_drill>(drill), strategy_repository(),
              policy.limits
          )
        : is_drill_configuration_supported(
              std::get<training_drill>(drill), strategy_repository()
          );
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
            str_label(
                "Discard the current session and load this drill? No "
                "result will be recorded."
            ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No
        );
        if (!guard || invalidated || answer != QMessageBox::Yes
            || !catalogue_current(row->gameplay))
            return;
    }
    selection = drill;
    accept();
}
