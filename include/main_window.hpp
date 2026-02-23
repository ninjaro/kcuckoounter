#ifndef KCUCKOOUNTER_MAIN_WINDOW_HPP
#define KCUCKOOUNTER_MAIN_WINDOW_HPP

#include "helpers/widget_helpers.hpp"

class table;
class QLabel;
class QDialog;
class QProgressBar;
class QSlider;

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

private:
    BaseSpinBox* table_slots_count;
    BaseComboBox* quiz_type;
    BaseCheckBox* wait_for_answers;
    BaseCheckBox* allow_skipping;
    BaseComboBox* dealing_mode;
    BasePushButton* continue_button;
    table* table_widget;
    QDialog* setup_dialog;
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
    bool quiz_started;
    bool quiz_paused;
    bool quiz_finished;
    bool rasterization_busy;
    bool pending_start_after_rasterization;
    int score_correct;
    int score_total;

    void setup_ui();
    void update_status_text();
    void refresh_clock_label() const;
    void update_start_pause_action(bool paused) const;
    void reset_game_state(bool show_setup_dialog, bool mark_finished = false);
    void show_game_over_dialog();
    void start_quiz_from_ui();
    void pause_for_dialog();
};

#endif // KCUCKOOUNTER_MAIN_WINDOW_HPP
