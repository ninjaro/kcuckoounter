#ifndef KCUCKOOUNTER_SHELL_DRILL_PICKER_HPP
#define KCUCKOOUNTER_SHELL_DRILL_PICKER_HPP

#include "settings/gameplay_drill.hpp"
#include "settings/preferences.hpp"

#include <QDialog>
#include <variant>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

using saved_drill = std::variant<training_drill, gameplay_drill>;

struct drill_picker_policy {
    gameplay::slot_limits limits;
    bool save_allowed = true;
    bool launch_legacy = true;
    bool load_gameplay = true;
    bool confirm_replacement = false;
};

/// One native catalogue picker; snapshots only, no borrowed live session.
/// The shell installs the selected engine/setup after exec returns.
class drill_picker : public QDialog {
    Q_OBJECT

public:
    drill_picker(
        QSettings& storage, saved_drill current, drill_picker_policy policy,
        QWidget* parent = nullptr
    );
    [[nodiscard]] const std::optional<saved_drill>& chosen() const;
    void invalidate();

private:
    struct row_key {
        bool gameplay = false;
        int index = -1;
    };

    QSettings& storage;
    saved_drill current;
    drill_picker_policy policy;
    std::optional<QVector<training_drill>> legacy;
    std::optional<QVector<gameplay_drill>> target;
    QString legacy_error;
    QString target_error;
    std::optional<saved_drill> selection;
    bool invalidated = false;
    QListWidget* list;
    QPlainTextEdit* details;
    QLineEdit* name;
    QLabel* message;
    QPushButton* save;
    QPushButton* rename;
    QPushButton* remove;
    QPushButton* convert;
    QPushButton* launch;

    [[nodiscard]] std::optional<row_key> selected_row() const;
    void refresh(row_key selected);
    void selection_changed();
    void show_message(const QString& extra = {});
    bool catalogue_current(bool gameplay);
    bool persist_legacy(const QVector<training_drill>& updated, int selected);
    bool persist_target(const QVector<gameplay_drill>& updated, int selected);
    void save_current();
    void rename_selected();
    void delete_selected();
    void choose_selected();
    void convert_selected();
    [[nodiscard]] QString summarize(const training_drill& drill) const;
    [[nodiscard]] QString summarize(const gameplay_drill& drill) const;
};

#endif // KCUCKOOUNTER_SHELL_DRILL_PICKER_HPP
