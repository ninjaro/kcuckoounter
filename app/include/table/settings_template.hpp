#ifndef KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_HPP
#define KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_HPP

#include "arch/widget_helpers.hpp"
#include "image/raster_cache.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "table/strategy_browser.hpp"

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QQueue>
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
    desktop_ui_preferences selected_desktop_components() const;
    void sync_theme_combo_shared_state(int index);
    void on_theme_source_button_clicked(QAbstractButton* button);
    void update_strategy_details(int index);
    void update_theme_carousel(int suit_index);
    void update_theme_palette_preview(int index);
    QString selected_theme_source_id() const;
    void update_weights_carousel(int suit_index);
    void update_suit_selection(int index);
    /** Render the selected suit, retaining the active cache generation. */
    QPixmap active_theme_card(int card_index, const QSize& size);
    QPixmap theme_card(int card_index, int suit_index, const QSize& size);
    /** Selected-suit preview with strategy weights, not a separate face cache.
     */
    QPixmap active_weighted_card(int card_index, const QSize& size);
    QPixmap weighted_card(int card_index, int suit_index, const QSize& size);
    std::optional<QImage> preview_face(
        int card_index, int suit_index, const QSize& size,
        QSet<raster_cache::entry_key>& tracked_keys
    );
    void enqueue_preview(const raster_cache::entry_key& key);
    void render_next_preview();
    void preview_render_finished();
    bool preview_key_relevant(const raster_cache::entry_key& key) const;
    void prune_preview_queue();
    void preview_cache_updated(const raster_cache::entry_key& key);
    void flush_preview_refresh();
    void clear_displayed_theme_entries();
    void mark_preview_refresh_pending(bool theme_preview, bool weights_preview);
    static void track_preview_entry(
        const raster_cache::entry_key& key,
        QSet<raster_cache::entry_key>& tracked_keys
    );
    static void
    clear_preview_entries(QSet<raster_cache::entry_key>& tracked_keys);
    void
    ensure_preview_generation(const QString& source_id, int target_bucket_px);
    void
    warm_preview_generation(const QString& source_id, int target_bucket_px);
    bool cutover_preview_generation();
    raster_cache::entry_key preview_key(
        const QString& source_id, int target_bucket_px, qint64 generation_id,
        const QString& element_id
    ) const;
    bool preview_key_ready(
        const QString& source_id, int target_bucket_px, qint64 generation_id,
        const QString& element_id
    ) const;
    void retire_preview_generation(
        const QString& source_id, int target_bucket_px, qint64 generation_id
    );
    static QImage render_preview_face(
        const QString& source_id, const QString& element_id,
        int target_bucket_px
    );

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
    BaseComboBox* ui_frame_combo = nullptr;
    BaseComboBox* ui_speed_combo = nullptr;
    BaseComboBox* ui_answer_combo = nullptr;
    BaseComboBox* ui_feedback_combo = nullptr;
    BaseComboBox* ui_actions_combo = nullptr;
    BaseComboBox* ui_settings_combo = nullptr;
    BaseComboBox* ui_toolbar_combo = nullptr;
    BaseComboBox* ui_hud_combo = nullptr;
    BaseWidget* theme_palette_preview;
    QButtonGroup* theme_button_group;
    card_preview_carousel* theme_carousel;
    int theme_suit;
    int weights_suit;
    qint64 preview_id;
    QString active_preview_source;
    int active_preview_bucket;
    qint64 active_preview_generation;
    QSet<QString> active_preview_element_ids;
    QString warming_preview_source;
    int warming_preview_bucket;
    qint64 warming_preview_generation;
    QSet<QString> warming_preview_element_ids;
    qint64 next_preview_generation;
    QFutureWatcher<QImage> preview_watcher;
    std::optional<raster_cache::entry_key> active_render_key;
    QQueue<raster_cache::entry_key> pending_preview_queue;
    QSet<raster_cache::entry_key> pending_preview_keys;
    bool preview_render_scheduled;
    bool preview_refresh_scheduled;
    bool theme_preview_needs_refresh;
    bool weights_preview_needs_refresh;
    QSet<raster_cache::entry_key> displayed_theme_entries;
    QSet<raster_cache::entry_key> displayed_weights_entries;
};

#endif // KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_HPP
