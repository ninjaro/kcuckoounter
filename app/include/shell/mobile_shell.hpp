#ifndef KCUCKOOUNTER_SHELL_MOBILE_SHELL_HPP
#define KCUCKOOUNTER_SHELL_MOBILE_SHELL_HPP

#include "settings/gameplay_drill.hpp"
#include "table/gameplay_session.hpp"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <map>
#include <optional>
#include <vector>

class main_window;
class table;
class QLabel;
class QStackedLayout;
class QTimer;
class QVBoxLayout;
class QHBoxLayout;
class QLineEdit;

namespace shared_ui::qt {
class touch_banner;
class touch_button;
class touch_choice;
class touch_deck_card;
class touch_entry;
class touch_hud;
class touch_keypad;
class touch_label;
class touch_nudge_row;
class touch_panel;
class touch_radio_list;
class touch_sheet;
class touch_stepper;
class touch_switch;
class touch_setting_row;
class touch_target_strip;
class touch_top_bar;
class touch_tag_flow;
class touch_stat_tile;
}

// Phone (Android/iOS) navigation and presentation over the same main window
// operations, session and table as the desktop shell. It owns screens and
// drafts of what is being typed; gameplay state stays in the session, and
// every command goes through the window's existing guarded paths.
class mobile_shell final : public QWidget {
    Q_OBJECT
    friend class gameplay_mobile_tests;
    friend class table_tests;

public:
    enum class page { home, setup, strategy, table, result, progress };

    explicit mobile_shell(main_window& window);

    // The table lives on the table page; the window still owns it.
    [[nodiscard]] QWidget* table_host() const;
    void attach_table(table& view);
    // Usable table extent of the table page for this shell size, also while
    // another page is shown: recommendations and packing use it.
    [[nodiscard]] QSize table_extent() const;

    void show_page(page target);

    [[nodiscard]] page current_page() const { return current; }

    // Window callbacks.
    void show_setup();
    void show_table();
    void report_setup_error(const QString& message);
    void refresh();

    // The setup page's message line, used by the window's setup paths.
    [[nodiscard]] QLabel* setup_message() const;
    // System back (Android) or Escape: one step up the screen hierarchy. A
    // running table pauses first; a paused one returns Home, still paused.
    void go_back();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    main_window& window;
    QPointer<table> view;
    QStackedLayout* pages = nullptr;
    page current = page::home;
    bool landscape = false;

    // Home
    QWidget* home_page = nullptr;
    QVBoxLayout* home_body = nullptr;
    shared_ui::qt::touch_panel* home_session = nullptr;
    QLabel* home_session_title = nullptr;
    QLabel* home_session_time = nullptr;
    shared_ui::qt::touch_tag_flow* home_session_chips = nullptr;
    QWidget* home_drills = nullptr;
    QVBoxLayout* home_drill_rows = nullptr;
    QLabel* home_drill_count = nullptr;
    QVector<gameplay_drill> drills;
    void build_home();
    void refresh_home();

    // Setup (rules and decks)
    QWidget* setup_page = nullptr;
    shared_ui::qt::touch_choice* setup_steps = nullptr;
    QStackedLayout* setup_stack = nullptr;
    QLabel* setup_error = nullptr;
    shared_ui::qt::touch_button* setup_next = nullptr;
    shared_ui::qt::touch_choice* penalty = nullptr;
    shared_ui::qt::touch_setting_row* lives_row = nullptr;
    shared_ui::qt::touch_stepper* lives = nullptr;
    QLabel* lives_hint = nullptr;
    shared_ui::qt::touch_choice* source = nullptr;
    shared_ui::qt::touch_choice* scope = nullptr;
    shared_ui::qt::touch_setting_row* freeze_row = nullptr;
    shared_ui::qt::touch_switch* freeze = nullptr;
    shared_ui::qt::touch_switch* skip = nullptr;
    shared_ui::qt::touch_radio_list* dealing = nullptr;
    shared_ui::qt::touch_stepper* per_step = nullptr;
    shared_ui::qt::touch_setting_row* decks_row = nullptr;
    shared_ui::qt::touch_stepper* deck_count = nullptr;
    shared_ui::qt::touch_switch* allow_more = nullptr;
    shared_ui::qt::touch_stepper* setup_interval = nullptr;
    QVBoxLayout* deck_cards = nullptr;
    bool refreshing_setup = false;
    void build_setup();
    void refresh_setup();
    void apply_rules();
    void save_drill();

    // Deck sheet and strategy chooser
    shared_ui::qt::touch_sheet* deck_sheet = nullptr;
    std::optional<gameplay::deck_id> sheet_deck;
    gameplay::deck_configuration sheet_draft;
    bool sheet_show_count = false;
    bool sheet_live = false;
    QLabel* sheet_title = nullptr;
    shared_ui::qt::touch_setting_row* sheet_strategy = nullptr;
    QLabel* sheet_strategy_name = nullptr;
    shared_ui::qt::touch_choice* sheet_shoe = nullptr;
    shared_ui::qt::touch_setting_row* sheet_count_row = nullptr;
    shared_ui::qt::touch_stepper* sheet_count = nullptr;
    QLabel* sheet_hint = nullptr;
    shared_ui::qt::touch_switch* sheet_training = nullptr;
    shared_ui::qt::touch_setting_row* sheet_show_row = nullptr;
    shared_ui::qt::touch_switch* sheet_show = nullptr;
    shared_ui::qt::touch_button* sheet_copy = nullptr;
    void build_deck_sheet();
    void open_deck_sheet(gameplay::deck_id id);
    void refresh_deck_sheet();
    void apply_deck_sheet(bool every_deck);

    QWidget* strategy_page = nullptr;
    shared_ui::qt::touch_top_bar* strategy_bar = nullptr;
    QLineEdit* strategy_search = nullptr;
    shared_ui::qt::touch_choice* strategy_filter = nullptr;
    QVBoxLayout* strategy_rows = nullptr;
    shared_ui::qt::touch_button* strategy_use = nullptr;
    QString strategy_choice;
    bool strategy_for_deck = false;
    void build_strategy();
    void open_strategy(bool for_deck);
    void refresh_strategy();

    // Table
    QWidget* table_page = nullptr;
    QWidget* table_area = nullptr;
    QHBoxLayout* table_row = nullptr;
    shared_ui::qt::touch_hud* hud = nullptr;
    QWidget* footer = nullptr;
    QStackedLayout* footer_stack = nullptr;
    QLabel* footer_interval = nullptr;
    shared_ui::qt::touch_button* footer_pause = nullptr;
    QLabel* mode_hint = nullptr;
    QVBoxLayout* banners = nullptr;
    QWidget* banner_area = nullptr;
    shared_ui::qt::touch_banner* storage_banner = nullptr;
    shared_ui::qt::touch_banner* event_banner = nullptr;
    QTimer* event_timer = nullptr;
    void build_table();
    void refresh_table();
    void place_banners();
    void show_event(const QString& title, const QString& text);

    // Questions
    std::uint64_t batch_id = 0;
    std::vector<gameplay::deck_id> batch_decks;
    std::optional<gameplay::deck_id> focused;
    std::map<std::size_t, QString> drafts;
    shared_ui::qt::touch_sheet* quiz_sheet = nullptr;
    QWidget* quiz_side = nullptr;
    QLabel* quiz_side_title = nullptr;
    shared_ui::qt::touch_target_strip* quiz_targets = nullptr;
    shared_ui::qt::touch_target_strip* side_targets = nullptr;
    shared_ui::qt::touch_entry* quiz_entry = nullptr;
    shared_ui::qt::touch_keypad* quiz_keypad = nullptr;
    shared_ui::qt::touch_keypad* side_keypad = nullptr;
    shared_ui::qt::touch_button* quiz_skip = nullptr;
    shared_ui::qt::touch_button* quiz_check = nullptr;
    shared_ui::qt::touch_button* side_skip = nullptr;
    shared_ui::qt::touch_button* side_check = nullptr;
    QWidget* tray = nullptr;
    QLabel* tray_caption = nullptr;
    shared_ui::qt::touch_target_strip* tray_targets = nullptr;
    shared_ui::qt::touch_nudge_row* tray_nudge = nullptr;
    shared_ui::qt::touch_button* tray_skip = nullptr;
    shared_ui::qt::touch_button* tray_check = nullptr;
    bool keypad_requested = false;
    void build_quiz();
    void refresh_quiz();
    void focus_deck(gameplay::deck_id id);
    [[nodiscard]] QString draft_for(gameplay::deck_id id) const;
    void edit_draft(const QString& text);
    void press_digit(int digit);
    void press_sign();
    void press_delete();
    void nudge(int delta);
    void check_focused();
    void skip_focused();

    // Pause sheet and table modes
    enum class table_mode { play, swap, details };
    table_mode mode = table_mode::play;
    shared_ui::qt::touch_sheet* pause_sheet = nullptr;
    QWidget* pause_locks = nullptr;
    QHBoxLayout* pause_lock_row = nullptr;
    shared_ui::qt::touch_button* pause_corrections = nullptr;
    shared_ui::qt::touch_stepper* pause_interval = nullptr;
    shared_ui::qt::touch_sheet* finish_sheet = nullptr;
    bool restored_notice = false;
    void build_pause();
    void refresh_pause();
    void set_mode(table_mode next);
    void deck_tapped(int deck);
    void pause_or_resume();

    // Result and progress
    QWidget* result_page = nullptr;
    shared_ui::qt::touch_label* result_reason = nullptr;
    shared_ui::qt::touch_stat_tile* result_points = nullptr;
    shared_ui::qt::touch_stat_tile* result_errors = nullptr;
    shared_ui::qt::touch_stat_tile* result_skips = nullptr;
    shared_ui::qt::touch_stat_tile* result_lives = nullptr;
    shared_ui::qt::touch_stat_tile* result_answering = nullptr;
    QVBoxLayout* result_decks = nullptr;
    shared_ui::qt::touch_tag_flow* result_chips = nullptr;
    void build_result();
    void refresh_result();

    QWidget* progress_page = nullptr;
    shared_ui::qt::touch_choice* progress_kind = nullptr;
    QHBoxLayout* progress_totals = nullptr;
    QVBoxLayout* progress_rows = nullptr;
    void build_progress();
    void refresh_progress();

    // Drill name prompt
    shared_ui::qt::touch_sheet* name_sheet = nullptr;
    QLineEdit* name_field = nullptr;
    void build_name_sheet();

    std::optional<gameplay::session_phase> seen_phase;
    std::vector<gameplay::deck_status> seen_status;
    void observe_session();
};

#endif // KCUCKOOUNTER_SHELL_MOBILE_SHELL_HPP
