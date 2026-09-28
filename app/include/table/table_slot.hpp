#ifndef KCUCKOOUNTER_TABLE_TABLE_SLOT_HPP
#define KCUCKOOUNTER_TABLE_TABLE_SLOT_HPP

#include "arch/widget_helpers.hpp"
#include "settings/preferences.hpp"
#include "settings/session_checkpoint.hpp"

#include <QBoxLayout>
#include <QImage>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QVector>

class QStackedLayout;
class QResizeEvent;
class QLabel;
class QColor;
class card_widget;
class QDialog;
class QToolButton;
class slot_settings;

class table_slot : public BaseWidget {
    Q_OBJECT

public:
    explicit table_slot(BaseWidget* parent = nullptr);
    ~table_slot() override;

    void set_swap_selected(bool selected);
    [[nodiscard]] bool swap_selected() const;
    void set_rotated(bool rotated);
    void set_frame_style(slot_frame_style style);
    void set_action_style(slot_action_style style);
    void set_settings_style(slot_settings_style style);
    void set_quiz_presentation(
        quiz_answer_style answer, quiz_feedback_style feedback
    );
    void set_allow_skipping(bool allow);

    void start_quiz(int quiz_type_index);
    void clear_quiz();
    void set_paused(bool paused);
    void advance_card();
    void trigger_highlight(int duration_ms);
    void tick_highlight(int delta_ms);
    void prepare_card_faces();
    [[nodiscard]] int card_face_need_short_px() const;
    void set_shared_card_faces(
        const QVector<QImage>& face_images, const QSize& raster_size
    );
    void clear_shared_card_faces();
    [[nodiscard]] bool has_shared_card_faces() const;
    void set_shared_card_faces_mode(bool enabled);
    void apply_theme();
    void apply_settings_from(const table_slot& source);
    enum class copy_action { copy, cancel, apply };
    void set_copy_action(copy_action action);
    [[nodiscard]] bool is_deck_exhausted() const;
    [[nodiscard]] bool is_quiz_prompt_active() const;
    [[nodiscard]] table_slot_session_state capture_session_state() const;
    [[nodiscard]] static bool
    is_session_state_valid(const table_slot_session_state& state);
    bool restore_session_state(const table_slot_session_state& state);

signals:
    void swap_clicked(table_slot* slot);
    void copy_clicked(table_slot* slot);
    void copy_all_clicked(table_slot* slot);
    void rasterization_busy_changed(bool busy);
    void dialog_opened();
    void score_adjusted(int correct_delta, int total_delta);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void on_infinity_toggled(bool checked);
    void on_swap_button_clicked();
    void on_settings_button_clicked();
    void on_info_button_clicked();
    void on_copy_button_clicked();
    void on_copy_all_button_clicked();
    void on_quiz_answer_button_clicked();
    void on_quiz_skip_button_clicked();
    void on_quiz_continue_button_clicked();
    void on_show_card_indexing_toggled(bool checked);
    void on_show_strategy_name_toggled(bool checked);
    void on_training_check_box_toggled(bool checked);
    void on_strategy_name_changed(const QString& text);

private:
    enum class slot_phase { running, paused } current_phase;
    card_widget* card_widget_internal;

    BaseWidget* overlay_widget;
    QToolButton* compact_controls_button = nullptr;
    QPointer<QDialog> controls_dialog;
    BaseWidget* settings_bar_widget;
    BaseWidget* swap_bar_widget;
    QBoxLayout* overlay_layout;
    QBoxLayout* swap_layout;
    slot_action_style action_style = slot_action_style::classic;
    copy_action current_copy_action = copy_action::copy;
    slot_settings_style settings_style = slot_settings_style::classic;
    QPointer<BaseWidget> settings_editor_panel;
    QPointer<QDialog> settings_editor_host;
    slot_settings* settings_editor_fields = nullptr;
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
    BaseWidget* quiz_chip_widget = nullptr;
    QLabel* quiz_feedback_heading = nullptr;
    quiz_answer_style answer_style = quiz_answer_style::numeric;
    quiz_feedback_style feedback_style = quiz_feedback_style::classic;
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
    void setup_quiz_chips();
    void update_quiz_presentation();
    void update_overlay_layout();
    void update_action_presentation();
    void update_compact_controls();
    void show_compact_controls();
    void update_settings_button_state(bool dialog_open = false);
    static void
    update_infinity_state(BaseCheckBox* check_box, BaseSpinBox* spin_box);
    [[nodiscard]] bool is_infinity_enabled() const;
    [[nodiscard]] bool is_training_enabled() const;
    void show_quiz_prompt();
    void clear_quiz_prompt();
    void show_quiz_feedback(const QString& message, bool show_continue);
    void update_quiz_controls_visibility();
    void update_strategy_weights();
    void sync_card_display_settings();
    void update_action_button_state();
    static void apply_palette_to_widget(
        BaseWidget* widget, const QColor& panel_color,
        const QColor& accent_color, const QColor& input_color
    );
    void update_overlay_palette();
    void update_lockable_settings();
    void populate_settings_editor(slot_settings* editor);
    void apply_settings_editor(const slot_settings* editor);
    void open_settings_editor();
    void place_settings_editor();
    void finish_settings_editor(bool apply);
    void
    show_template_dialog(const QString& title, const QString& strategy_name);
};

#endif // KCUCKOOUNTER_TABLE_TABLE_SLOT_HPP
