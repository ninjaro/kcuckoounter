#ifndef KCUCKOOUNTER_MAIN_WINDOW_HPP
#define KCUCKOOUNTER_MAIN_WINDOW_HPP

#include "arch/widget_helpers.hpp"

#include <QString>

class table;
class QLabel;
class QDialog;
class QProgressBar;
class QSlider;
class QCheckBox;
class QGroupBox;
class QPlainTextEdit;
class QTabWidget;
class resource_monitor;
class monitor_line_chart_widget;
class monitor_pie_chart_widget;
class monitor_geometry_schematic_widget;
class monitor_resize_history_widget;

class main_window : public BaseMainWindow {
    Q_OBJECT

public:
    explicit main_window(BaseWidget* parent = nullptr);
    ~main_window() override;

private slots:
    void on_continue_button_clicked();
    void on_new_game_triggered();
    void on_start_pause_triggered();
    void on_finish_triggered();
    void on_settings_triggered();
#ifndef NDEBUG
    void on_export_debug_snapshot_triggered();
    void on_export_process_memory_report_triggered();
    void on_export_monitor_charts_triggered();
    void on_add_monitor_marker_triggered();
    void on_set_realistic_cadence_mode_triggered();
    void on_set_instrumented_cadence_mode_triggered();
    void on_toggle_debug_broadcaster_triggered(bool checked);
    void on_toggle_resource_monitor_triggered(bool checked);
    void on_resource_monitor_visibility_changed(bool visible);
    void on_resource_monitor_data_changed();
#endif

private:
    BaseSpinBox* table_slots_count;
    BaseComboBox* quiz_type;
    BaseCheckBox* wait_for_answers;
    BaseCheckBox* allow_skipping;
    BaseComboBox* dealing_mode;
    BasePushButton* continue_button;
    table* table_widget;
    QDialog* setup_dialog;
    QDialog* settings_dialog;
    BaseWidget* setup_widget;
    BaseClock* clock_timer;
    QLabel* clock_label;
    QLabel* status_label;
    QLabel* pickup_interval_label;
    QProgressBar* raster_progress;
    QSlider* speed_slider;
    BaseAction* new_game_action;
    BaseAction* start_pause_action;
    BaseAction* finish_action;
    BaseAction* settings_action;
#ifndef NDEBUG
    BaseAction* export_debug_snapshot_action;
    BaseAction* export_process_memory_report_action;
    BaseAction* export_monitor_charts_action;
    BaseAction* add_monitor_marker_action;
    BaseAction* realistic_cadence_mode_action;
    BaseAction* instrumented_cadence_mode_action;
    BaseAction* toggle_debug_broadcaster_action;
    BaseAction* toggle_resource_monitor_action;
    QDialog* resource_monitor_window;
    QTabWidget* resource_monitor_tabs;
    QLabel* resource_monitor_summary_memory_card;
    QLabel* resource_monitor_summary_process_card;
    QLabel* resource_monitor_summary_stock_card;
    QLabel* resource_monitor_summary_activity_card;
    QCheckBox* resource_monitor_show_cache_bytes_series;
    QCheckBox* resource_monitor_show_widget_local_series;
    QCheckBox* resource_monitor_show_process_rss_series;
    QCheckBox* resource_monitor_show_gap_bytes_series;
    QCheckBox* resource_monitor_show_ratio_series;
    QCheckBox* resource_monitor_show_displayed_series;
    QCheckBox* resource_monitor_show_pending_series;
    QCheckBox* resource_monitor_show_flight_series;
    QCheckBox* resource_monitor_show_event_series;
    monitor_line_chart_widget* resource_monitor_primary_chart_view;
    monitor_line_chart_widget* resource_monitor_ratio_chart_view;
    monitor_line_chart_widget* resource_monitor_secondary_chart_view;
    monitor_pie_chart_widget* resource_monitor_composition_chart_view;
    QGroupBox* resource_monitor_timeline_group;
    QGroupBox* resource_monitor_diagnostics_group;
    QPlainTextEdit* resource_monitor_timeline_text;
    QPlainTextEdit* resource_monitor_diagnostics_text;
    monitor_geometry_schematic_widget* resource_monitor_geometry_view;
    monitor_resize_history_widget* resource_monitor_resize_history_view;
    QPlainTextEdit* resource_monitor_geometry_text;
    QPlainTextEdit* resource_monitor_resize_history_text;
    bool resource_monitor_refresh_pending;
#endif
    bool quiz_started;
    bool quiz_paused;
    bool quiz_finished;
    bool rasterization_busy;
    int score_correct;
    int score_total;
    resource_monitor* debug_telemetry_collector;

    void setup_ui();
    void update_status_text();
    void refresh_clock_label() const;
    void update_start_pause_action(bool paused) const;
    void reset_game_state(bool show_setup_dialog, bool mark_finished = false);
    void show_game_over_dialog();
    void start_quiz_from_ui();
    void pause_for_dialog();
#ifndef NDEBUG
    void add_debug_marker(const QString& label) const;
    void sync_debug_cadence_mode_actions() const;
    void setup_debug_resource_monitor_ui();
    void refresh_resource_monitor_view();
    void dump_debug_telemetry_on_exit() const;
#endif
};

#endif // KCUCKOOUNTER_MAIN_WINDOW_HPP
