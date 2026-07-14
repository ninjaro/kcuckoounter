#ifndef KCUCKOOUNTER_MAIN_WINDOW_HPP
#define KCUCKOOUNTER_MAIN_WINDOW_HPP

#include "arch/widget_helpers.hpp"

#include <QString>

class table;
class KGameClock;
class QLabel;
class QDialog;
class QProgressBar;
class QSlider;
class resource_monitor;
class settings_template_widget;

class main_window : public BaseMainWindow {
    Q_OBJECT

public:
    explicit main_window(BaseWidget* parent = nullptr);
    ~main_window() override;

private slots:
    void on_clock_ticked(qint64 elapsed_ms, qint64 delta_ms);
    void on_table_rasterization_busy_changed(bool busy);
    void on_table_score_adjusted(int correct_delta, int total_delta);
    void on_speed_slider_value_changed(int value);
    void on_dealing_mode_changed(int index);
    void on_quiz_type_changed(int index);
    void on_table_slot_count_changed(int value);
    void open_setup_dialog();
    void on_setup_dialog_rejected();
    void on_continue_button_clicked();
    void on_new_game_triggered();
    void on_start_pause_triggered();
    void on_finish_triggered();
    void on_show_highscores_triggered();
    void on_settings_triggered();
    void on_settings_commit_requested();
    void on_settings_dialog_finished(int result);
    void on_export_debug_snapshot_triggered();
    void on_export_process_report();
    void on_add_monitor_marker_triggered();
    void on_realistic_cadence_selected();
    void on_instrumented_cadence_selected();
    void on_toggle_debug_broadcaster_triggered(bool checked);
    void on_application_about_to_quit();

private:
    BaseSpinBox* table_slots_count;
    BaseComboBox* quiz_type;
    BaseCheckBox* wait_for_answers;
    BaseCheckBox* allow_skipping;
    BaseComboBox* dealing_mode;
    BasePushButton* continue_button;
    BaseToolBar* primary_toolbar;
    table* table_widget;
    QDialog* setup_dialog;
    QDialog* settings_dialog;
    settings_template_widget* appearance_settings_widget;
    BaseWidget* setup_widget;
    BaseClock* clock_timer;
    KGameClock* kde_clock;
    QLabel* clock_label;
    QLabel* status_label;
    QLabel* pickup_interval_label;
    QProgressBar* raster_progress;
    QSlider* speed_slider;
    BaseAction* new_game_action;
    BaseAction* start_pause_action;
    BaseAction* finish_action;
    BaseAction* highscores_action;
    BaseAction* settings_action;
    BaseAction* export_debug_snapshot_action;
    BaseAction* export_process_memory_report_action;
    BaseAction* add_monitor_marker_action;
    BaseAction* realistic_cadence_mode_action;
    BaseAction* instrumented_cadence_mode_action;
    BaseAction* toggle_debug_broadcaster_action;
    bool quiz_started;
    bool quiz_paused;
    bool quiz_finished;
    bool rasterization_busy;
    int score_correct;
    int score_total;
    resource_monitor* debug_telemetry_collector;

    void setup_ui();
    void setup_game_actions();
    void setup_platform_shell();
    void finalize_platform_shell();
    void setup_status_surface(BaseVBoxLayout* main_layout);
    void register_shell_action(BaseAction* action, const QString& action_name);
    void insert_shell_separator();
    void update_status_text();
    void refresh_clock_label() const;
    void record_platform_score();
    void show_platform_highscores();
    void update_start_pause_action(bool paused) const;
    void reset_game_state(bool show_setup_dialog, bool mark_finished = false);
    void show_game_over_dialog();
    void start_quiz_from_ui();
    void pause_for_dialog();
    [[nodiscard]] QString debug_status_suffix() const;
    void add_debug_marker(const QString& label) const;
    void sync_debug_cadence_mode_actions() const;
    void setup_debug_monitoring();
    void dump_debug_telemetry_on_exit() const;
    void persist_setup_preferences() const;
};

#endif // KCUCKOOUNTER_MAIN_WINDOW_HPP
