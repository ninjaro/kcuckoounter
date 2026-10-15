#ifndef KCUCKOOUNTER_SHELL_DRILL_PICKER_HPP
#define KCUCKOOUNTER_SHELL_DRILL_PICKER_HPP

#include "settings/gameplay_drill.hpp"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

struct drill_picker_policy {
    gameplay::slot_limits limits {};
    bool save_allowed = true;
    bool load_gameplay = true;
    bool confirm_replacement = false;
};

/// Current named gameplay setups only; no live session is borrowed or
/// converted.
class drill_picker : public QDialog {
    Q_OBJECT

public:
    drill_picker(
        QSettings& storage_value, std::optional<gameplay_drill> snapshot,
        drill_picker_policy rules, QWidget* parent = nullptr
    );
    [[nodiscard]] const std::optional<gameplay_drill>& chosen() const;
    void invalidate();

private:
    QSettings& storage;
    std::optional<gameplay_drill> current;
    drill_picker_policy policy;
    std::optional<QVector<gameplay_drill>> target;
    QString target_error;
    std::optional<gameplay_drill> selection;
    bool invalidated = false;
    QListWidget* list;
    QPlainTextEdit* details;
    QLineEdit* name;
    QLabel* message;
    QPushButton* save;
    QPushButton* rename;
    QPushButton* remove;
    QPushButton* launch;

    [[nodiscard]] std::optional<int> selected_row() const;
    void refresh(int selected);
    void selection_changed();
    void show_message(const QString& extra = {});
    bool catalogue_current();
    bool persist_target(const QVector<gameplay_drill>& updated, int selected);
    void save_current();
    void rename_selected();
    void delete_selected();
    void choose_selected();
    [[nodiscard]] static QString summarize(const gameplay_drill& drill);
};

#endif // KCUCKOOUNTER_SHELL_DRILL_PICKER_HPP
