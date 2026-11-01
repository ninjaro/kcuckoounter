#ifndef KCUCKOOUNTER_MAIN_WINDOW_HPP
#define KCUCKOOUNTER_MAIN_WINDOW_HPP

#include "arch/widget_helpers.hpp"
#include "settings/preferences.hpp"

#include <QList>
#include <QMargins>
#include <QPointer>
#include <QString>
#include <QtCore/Qt>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

class table;
class QLabel;
class QDialog;
class QMenu;
class QProgressBar;
class QSlider;
class settings_template_widget;
class QTimer;
class QVBoxLayout;
class QTableWidget;
class QCloseEvent;
class gameplay_reservation;
class mobile_shell;

namespace shared_ui::qt {
class status_strip;
}

namespace gameplay {
class setup_widget;
}
struct gameplay_drill;

class main_window : public BaseMainWindow {
    Q_OBJECT

public:
    explicit main_window(BaseWidget* parent = nullptr);
    ~main_window() override;

private slots:
    void on_table_rasterization_busy_changed(bool busy);
    void on_speed_slider_value_changed(int value);
    void on_new_game_triggered();
    void on_gameplay_review_triggered();
    void on_gameplay_roster_changed(int count);
    void on_saved_drills_triggered();
    void on_start_pause_triggered();
    void on_finish_triggered();
    void on_progress_triggered();
    void on_settings_triggered();
    void on_settings_commit_requested();
    void on_settings_dialog_finished(int result);

private:
    friend class gameplay_host_tests;
    friend class gameplay_mobile_tests;
    // The phone screens drive the same guarded operations as the toolbar.
    friend class mobile_shell;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    BaseToolBar* primary_toolbar;
    QMenu* game_menu;
    QMenu* settings_menu;
    table* table_widget;
    QDialog* settings_dialog;
    settings_template_widget* appearance_settings_widget;
    QLabel* clock_label;
    QLabel* status_label;
    QLabel* pickup_interval_label;
    QProgressBar* raster_progress;
    QSlider* speed_slider;
    BaseAction* new_game_action;
    BaseAction* start_pause_action;
    BaseAction* finish_action;
    BaseAction* progress_action;
    BaseAction* settings_action;
    BaseAction* saved_drills_action = nullptr;
    BaseAction* gameplay_review_action = nullptr;
    QPointer<QDialog> gameplay_review_dialog;
    QTableWidget* gameplay_review_rows = nullptr;
    QPointer<QDialog> gameplay_setup_dialog;
    QPointer<gameplay::setup_widget> gameplay_setup_fields;
    BaseSpinBox* gameplay_slot_count = nullptr;
    QVBoxLayout* gameplay_setup_layout = nullptr;
    QLabel* gameplay_setup_error = nullptr;
    QTimer* gameplay_geometry_timer = nullptr;
    bool gameplay_geometry_ready = false;
    bool rasterization_busy;
    bool felt_chrome = false;
    QLabel* kit_wordmark = nullptr;
    // D-TOP-1 one-bar pieces: brand at the toolbar start and, where menus
    // live inside the window, the existing menus at its end.
    QAction* kit_brand_action = nullptr;
    QList<QAction*> kit_menu_actions;
    std::optional<std::pair<QMargins, int>> classic_central_spacing;
    shared_ui::qt::status_strip* desktop_status = nullptr;
    desktop_hud_style gameplay_hud_style = desktop_hud_style::classic;
    int gameplay_default_suit = 0;
    // Operations retain a local copy through callbacks retiring this window.
    std::shared_ptr<gameplay_reservation> gameplay_binding;
    BaseAction* gameplay_retry_action = nullptr;
    bool gameplay_recovery_checked = false;
    bool gameplay_recovery_ready = false;
    bool gameplay_storage_busy = false;
    bool gameplay_completion_attempted = false;
    QString gameplay_storage_error;
    bool mobile_background = false;
    bool mobile_checkpoint_pending = false;
    std::uint64_t mobile_pause_revision = 0;
    // Phone profile only: the touch screens around the table.
    mobile_shell* mobile = nullptr;

    void setup_ui();
    void apply_desktop_presentation();
    void apply_kit_one_bar();
    void setup_game_actions();
    void setup_platform_shell();
    void finalize_platform_shell();
    void setup_status_surface(BaseVBoxLayout* main_layout);
    BaseWidget* create_desktop_status_surface();
    void register_shell_action(BaseAction* action, const QString& action_name);
    void update_status_text();
    void set_pickup_readout(int interval_ms) const;
    void refresh_clock_label() const;
    void update_start_pause_action(bool paused) const;
    void open_gameplay_setup_dialog();
    void close_gameplay_setup_dialog();
    void close_gameplay_review_dialog();
    void refresh_gameplay_review();
    void rebuild_gameplay_setup_fields();
    void refresh_gameplay_setup();
    void refresh_gameplay_actions();
    void report_gameplay_error(const QString& message);
    [[nodiscard]] bool start_gameplay_from_ui();
    bool recover_gameplay_from_ui();
    void retry_gameplay_from_ui();
    bool gameplay_storage_ready();
    bool gameplay_retirement_allowed();
    bool reserve_gameplay_from_ui(std::uint64_t seed);
    bool complete_gameplay_from_ui();
    bool save_gameplay_from_ui();
    void report_gameplay_storage(const QString& error);
    bool load_gameplay_drill(const gameplay_drill& drill);
    void pause_for_dialog();
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    void persist_desktop_shell_state() const;
    void restore_desktop_shell_state();
#endif
    void apply_mobile_state(Qt::ApplicationState state);
    void pause_mobile_gameplay();
};

#endif // KCUCKOOUNTER_MAIN_WINDOW_HPP
