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
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>

#include <algorithm>

void main_window::close_gameplay_review_dialog() {
    delete gameplay_review_dialog.data();
    gameplay_review_dialog = nullptr;
    gameplay_review_rows = nullptr;
}

void main_window::on_gameplay_review_triggered() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    return;
#else
    if (!table_widget->has_gameplay_corrections())
        return;
    if (!gameplay_review_dialog) {
        auto* dialog = new QDialog(this);
        gameplay_review_dialog = dialog;
        dialog->setObjectName(QStringLiteral("gameplay_review_dialog"));
        dialog->setWindowTitle(str_label("Review corrections"));
        dialog->setModal(false);
        auto* layout = new QVBoxLayout(dialog);
        auto* notice = new QLabel(
            str_label(
                "Latest retained correction for each deck, not a complete "
                "batch or "
                "history. New wrong/Skip replaces it; correct or Dismiss "
                "clears it. "
                "Reviewing and closing do not pause play. Use Pause to freeze "
                "play."
            ),
            dialog
        );
        notice->setWordWrap(true);
        layout->addWidget(notice);
        auto* empty = new QLabel(str_label("No retained corrections."), dialog);
        empty->setObjectName(QStringLiteral("gameplay_review_empty"));
        layout->addWidget(empty);
        gameplay_review_rows = new QTableWidget(dialog);
        gameplay_review_rows->setObjectName(
            QStringLiteral("gameplay_review_rows")
        );
        gameplay_review_rows->setAccessibleName(
            str_label("Retained per-deck corrections")
        );
        gameplay_review_rows->setColumnCount(7);
        gameplay_review_rows->setHorizontalHeaderLabels(
            { str_label("Deck"), str_label("Current slot"), str_label("Mode"),
              str_label("Result"), str_label("Entered count"),
              str_label("Expected count"), str_label("Quiz batch") }
        );
        gameplay_review_rows->setEditTriggers(
            QAbstractItemView::NoEditTriggers
        );
        gameplay_review_rows->setSelectionBehavior(
            QAbstractItemView::SelectRows
        );
        gameplay_review_rows->setSelectionMode(
            QAbstractItemView::SingleSelection
        );
        gameplay_review_rows->setSortingEnabled(false);
        gameplay_review_rows->verticalHeader()->hide();
        gameplay_review_rows->horizontalHeader()->setSectionResizeMode(
            QHeaderView::ResizeToContents
        );
        layout->addWidget(gameplay_review_rows);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog->resize(760, 420);
    }
    gameplay_review_dialog->show();
    refresh_gameplay_review();
    gameplay_review_rows->setFocus(Qt::OtherFocusReason);
    gameplay_review_dialog->raise();
    gameplay_review_dialog->activateWindow();
#endif
}

void main_window::refresh_gameplay_review() {
    if (gameplay_review_action)
        gameplay_review_action->setEnabled(
            table_widget->has_gameplay_corrections()
        );
    if (!gameplay_review_dialog || !gameplay_review_dialog->isVisible())
        return; // no hidden-view rebuild and no polling/tick subscription
    const auto* owner = table_widget->active_gameplay_session();
    if (!owner)
        return;
    const auto corrections = table_widget->gameplay_corrections();
    auto* rows = gameplay_review_rows;
    const int selected_column = std::max(0, rows->currentColumn());
    const auto selected = rows->currentRow() >= 0
        ? rows->item(rows->currentRow(), 0)->data(Qt::UserRole)
        : QVariant();
    const QSignalBlocker blocker(rows);
    rows->setRowCount(static_cast<int>(corrections.size()));
    int selected_row = -1;
    for (std::size_t index = 0; index < corrections.size(); ++index) {
        const auto& answer = corrections[index];
        const auto slot = owner->slot_for(answer.owner);
        const auto* deck = owner->deck(answer.owner);
        const auto result = answer.outcome == gameplay::quiz_outcome::skipped
            ? str_label("Skipped")
            : answer.timed_out ? str_label("Wrong (timeout)")
                               : str_label("Wrong");
        const QStringList text {
            QString::number(static_cast<qulonglong>(answer.owner.value + 1)),
            slot ? QString::number(static_cast<qulonglong>(slot->value + 1))
                 : QStringLiteral("—"),
            deck->configuration.training ? str_label("Training")
                                         : str_label("Challenge"),
            result,
            QString::number(static_cast<qlonglong>(answer.submitted_count)),
            QString::number(static_cast<qlonglong>(answer.expected_count)),
            QString::number(static_cast<qulonglong>(answer.batch_id))
        };
        const int row = static_cast<int>(index);
        for (int column = 0; column < text.size(); ++column) {
            auto* item = rows->item(row, column);
            if (!item) {
                item = new QTableWidgetItem;
                rows->setItem(row, column, item);
            }
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setText(text[column]);
        }
        const auto identity
            = QVariant::fromValue(static_cast<qulonglong>(answer.owner.value));
        rows->item(row, 0)->setData(Qt::UserRole, identity);
        if (selected.isValid() && selected == identity)
            selected_row = row;
    }
    if (selected_row >= 0)
        rows->setCurrentCell(selected_row, selected_column);
    else {
        rows->setCurrentItem(nullptr);
        rows->clearSelection();
    }
    gameplay_review_dialog
        ->findChild<QLabel*>(QStringLiteral("gameplay_review_empty"))
        ->setVisible(corrections.empty());
}
