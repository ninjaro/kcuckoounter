#ifndef KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_HPP
#define KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_HPP

#include "arch/widget_helpers.hpp"
#include "image/raster_cache.hpp"
#include "settings/strategy_data.hpp"

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSet>

#include <optional>

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
    QString selected_theme_source_id() const;
    void update_weights_carousel(int suit_index);
    void update_suit_selection(int index);
    QPixmap request_theme_preview_card(
        int card_index, int suit_index, const QSize& size
    );
    QPixmap request_weighted_preview_card(
        int card_index, int suit_index, const QSize& size
    );
    void enqueue_theme_preview_render(const raster_cache::entry_key& key);
    void process_pending_theme_preview_render();
    void on_theme_preview_render_finished();
    bool
    is_theme_preview_key_relevant(const raster_cache::entry_key& key) const;
    void prune_pending_theme_preview_queue();
    void
    on_theme_preview_cache_result_updated(const raster_cache::entry_key& key);
    void flush_coalesced_preview_refresh();

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
    int active_theme_preview_suit_index;
    int active_weights_preview_suit_index;
    QFutureWatcher<QImage> theme_preview_render_watcher;
    std::optional<raster_cache::entry_key> active_theme_preview_render_key;
    QQueue<raster_cache::entry_key> pending_theme_preview_render_queue;
    QSet<raster_cache::entry_key> pending_theme_preview_render_set;
    bool theme_preview_render_scheduled;
    bool theme_preview_refresh_scheduled;
    bool theme_preview_needs_refresh;
    bool weights_preview_needs_refresh;
};

#endif // KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_HPP
