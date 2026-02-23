#ifndef KCUCKOOUNTER_WIDGET_SETTINGS_TEMPLATE_HPP
#define KCUCKOOUNTER_WIDGET_SETTINGS_TEMPLATE_HPP

#include "helpers/strategy_data.hpp"
#include "helpers/widget_helpers.hpp"

#include <QObject>

class settings_shared_state : public QObject {
    Q_OBJECT

public:
    explicit settings_shared_state(QObject* parent = nullptr);

    void set_default_suit(int index);
    int default_suit() const;

    void set_table_color_index(int index);
    int table_color_index() const;

signals:
    void default_suit_changed(int index);
    void table_color_index_changed(int index);

private:
    int default_suit_value;
    int table_color_index_value;
};

class QLabel;
class QListWidget;
class QButtonGroup;
class card_preview_carousel;
class QTableWidget;
class table;

enum class settings_tab_kind { appearance, strategies };

class settings_template_widget : public BaseWidget {
    Q_OBJECT

public:
    explicit settings_template_widget(
        settings_tab_kind tab_kind, BaseWidget* parent = nullptr,
        const QString& selected_strategy = QString(),
        table* table_widget = nullptr,
        settings_shared_state* shared_state = nullptr
    );
    ~settings_template_widget() override;
    void apply_theme_settings();
    void reset_theme_selection();

private:
    void setup_ui(const QString& selected_strategy);
    void setup_strategy_ui(const QString& selected_strategy);
    void setup_appearance_ui();
    void update_strategy_details(int index);
    void update_theme_carousel(int suit_index);
    void update_theme_palette_preview(int index);
    void update_weights_carousel(int suit_index);
    void update_suit_selection(int index);

    settings_tab_kind tab_kind;
    table* table_widget;
    settings_shared_state* shared_state;
    QVector<strategy_data> strategies;
    QListWidget* strategy_list_widget;
    QLabel* strategy_title_label;
    QLabel* strategy_description_label;
    QLabel* notes_title_label;
    QLabel* notes_label;
    QLabel* references_title_label;
    QLabel* references_label;
    card_preview_carousel* weights_carousel;
    QTableWidget* general_table;
    QTableWidget* metrics_table;
    BaseComboBox* suit_combo_box;
    BaseComboBox* theme_combo_box;
    BaseComboBox* orientation_combo_box;
    BaseWidget* theme_palette_preview;
    QButtonGroup* theme_button_group;
    card_preview_carousel* theme_carousel;
};

#endif // KCUCKOOUNTER_WIDGET_SETTINGS_TEMPLATE_HPP
