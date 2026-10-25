#ifndef KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_HPP
#define KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_HPP

#include "arch/widget_helpers.hpp"
#include "image/card_images.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "table/strategy_browser.hpp"

#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QtGlobal>

#include <optional>

class settings_shared_state : public QObject {
    Q_OBJECT

public:
    explicit settings_shared_state(QObject* parent = nullptr);

    void set_default_suit(int index);
    [[nodiscard]] int default_suit() const;

    void set_table_color_index(int index);
    [[nodiscard]] int table_color_index() const;

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
class QAbstractButton;
class card_preview_carousel;
class QTableWidget;
class table;

enum class settings_tab_kind { appearance, strategies };

class settings_template_widget : public BaseWidget {
    Q_OBJECT
    friend class preview_carousel_tests;

public:
    explicit settings_template_widget(
        settings_tab_kind tab_kind_value, BaseWidget* parent = nullptr,
        const QString& selected_strategy = QString(),
        table* table_widget_ptr = nullptr,
        settings_shared_state* shared_state_ptr = nullptr
    );
    ~settings_template_widget() override;
    bool apply_theme_settings();
    void reset_theme_selection();

signals:
    void desktop_presentation_applied();

private:
    void setup_ui(const QString& selected_strategy);
    void setup_strategy_ui(const QString& selected_strategy);
    void setup_appearance_ui();
    void setup_desktop_components(QFormLayout* layout);
    void reset_desktop_component_selection();
    [[nodiscard]] desktop_ui_preferences selected_desktop_components() const;
    [[nodiscard]] QList<QPair<QString, QComboBox*>>
    desktop_component_boxes() const;
    void sync_theme_combo_shared_state(int index);
    void on_theme_source_button_clicked(QAbstractButton* button);
    void update_strategy_details(int index);
    void update_theme_carousel(int suit_index);
    void update_theme_palette_preview(int index);
    [[nodiscard]] QString selected_theme_source_id() const;
    void update_weights_carousel(int suit_index);
    void update_suit_selection(int index);
    /** Presentation lookup for the selected suit. */
    [[nodiscard]] QPixmap
    active_theme_card(int card_index, const QSize& size) const;
    [[nodiscard]] QPixmap
    theme_card(int card_index, int suit_index, const QSize& size) const;
    /** Selected-suit preview with strategy weights, not a separate face cache.
     */
    [[nodiscard]] QPixmap
    active_weighted_card(int card_index, const QSize& size) const;
    [[nodiscard]] QPixmap
    weighted_card(int card_index, int suit_index, const QSize& size) const;
    [[nodiscard]] QImage preview_face(int card_index, int suit_index) const;
    void update_preview_demands();
    void preview_images_changed();
    void flush_preview_refresh();
    void mark_preview_refresh_pending(bool theme_preview, bool weights_preview);

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
    BaseComboBox* ui_preset_combo = nullptr;
    shared_ui::qt::appearance_state desktop_appearance_;
    BaseComboBox* ui_frame_combo = nullptr;
    BaseComboBox* ui_speed_combo = nullptr;
    BaseComboBox* ui_answer_combo = nullptr;
    BaseComboBox* ui_feedback_combo = nullptr;
    BaseComboBox* ui_actions_combo = nullptr;
    BaseComboBox* ui_settings_combo = nullptr;
    BaseComboBox* ui_toolbar_combo = nullptr;
    BaseComboBox* ui_hud_combo = nullptr;
    BaseComboBox* ui_chrome_combo = nullptr;
    BaseWidget* theme_palette_preview;
    QButtonGroup* theme_button_group;
    card_preview_carousel* theme_carousel;
    int theme_suit;
    int weights_suit;
    card_image_consumer preview_images;
    QString preview_source;
    QSize preview_physical_size;
    QStringList preview_requested_ids;
    QHash<QString, QImage> displayed_preview_images;
    QString displayed_preview_source;
    QSize displayed_preview_physical_size;
    bool preview_refresh_scheduled;
    bool theme_preview_needs_refresh;
    bool weights_preview_needs_refresh;
};

#endif // KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_HPP
