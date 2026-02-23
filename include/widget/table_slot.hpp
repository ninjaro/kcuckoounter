#ifndef KCUCKOOUNTER_WIDGETS_TABLE_SLOT_HPP
#define KCUCKOOUNTER_WIDGETS_TABLE_SLOT_HPP

#include "helpers/widget_helpers.hpp"

#include <QBoxLayout>
#include <QString>

class QStackedLayout;
class QResizeEvent;
class QLabel;
class card_widget;

class table_slot : public BaseWidget {
    Q_OBJECT

public:
    explicit table_slot(BaseWidget* parent = nullptr);
    ~table_slot() override;

    void set_swap_selected(bool selected);
    bool swap_selected() const;
    void set_rotated(bool rotated);
    void set_allow_skipping(bool allow);

    void start_quiz(int quiz_type_index);
    void clear_quiz();
    void set_paused(bool paused);
    void advance_card();
    void trigger_highlight(int duration_ms);
    void tick_highlight(int delta_ms);
    void prepare_card_faces();
    void apply_theme();
    void apply_settings_from(const table_slot& source);
    void set_copy_button_text(const QString& text);
    bool is_deck_exhausted() const;
    bool is_quiz_prompt_active() const;

signals:
    void swap_clicked(table_slot* slot);
    void copy_clicked(table_slot* slot);
    void copy_all_clicked(table_slot* slot);
    void rasterization_busy_changed(bool busy);
    void dialog_opened();
    void score_adjusted(int correct_delta, int total_delta);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void on_infinity_toggled(bool checked);
    void on_swap_button_clicked();
    void on_settings_button_clicked();
    void on_info_button_clicked();
    void on_copy_button_clicked();
    void on_copy_all_button_clicked();

private:
    enum class slot_phase { running, paused } current_phase;
    card_widget* card_widget_internal;

    BaseWidget* overlay_widget;
    BaseWidget* settings_bar_widget;
    BaseWidget* swap_bar_widget;
    QBoxLayout* overlay_layout;
    QBoxLayout* swap_layout;
    BaseCheckBox* infinity_check_box;
    BaseSpinBox* deck_count_spin_box;
    BaseComboBox* strategy_combo_box;
    BasePushButton* info_button;
    BaseCheckBox* show_card_indexing;
    BaseCheckBox* show_strategy_name;
    BaseCheckBox* training_check_box;
    BasePushButton* swap_button;
    BasePushButton* settings_button;
    BasePushButton* copy_button;
    BasePushButton* copy_all_button;
    BaseWidget* quiz_bar_widget;
    QStackedLayout* quiz_layout;
    BaseWidget* quiz_prompt_widget;
    BaseWidget* quiz_feedback_widget;
    QLabel* quiz_weight_label;
    BaseSpinBox* quiz_spin_box;
    BasePushButton* quiz_answer_button;
    BasePushButton* quiz_skip_button;
    QLabel* quiz_feedback_label;
    BasePushButton* quiz_continue_button;
    bool settings_overlay_visible;
    bool is_rotated;
    bool use_dialog_for_settings;
    int deck_count_minimum;
    bool quiz_prompt_active;
    bool quiz_feedback_active;
    bool quiz_continue_visible;
    bool allow_skipping_flag;
    int last_quiz_input_value;

    void setup_overlay();
    void update_overlay_layout();
    void update_settings_button_state(bool dialog_open = false);
    void update_infinity_state(BaseCheckBox* check_box, BaseSpinBox* spin_box);
    bool is_infinity_enabled() const;
    bool is_training_enabled() const;
    void show_quiz_prompt();
    void clear_quiz_prompt();
    void show_quiz_feedback(const QString& message, bool show_continue);
    void update_quiz_controls_visibility();
    void update_strategy_weights(const QString& strategy_name);
    void sync_card_display_settings();
    void update_action_button_state();
    void update_overlay_palette();
    void update_lockable_settings();
    void
    show_template_dialog(const QString& title, const QString& strategy_name);
};

#endif // KCUCKOOUNTER_WIDGETS_TABLE_SLOT_HPP
