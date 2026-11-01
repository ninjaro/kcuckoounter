#ifndef KCUCKOOUNTER_TABLE_TABLE_SLOT_HPP
#define KCUCKOOUNTER_TABLE_TABLE_SLOT_HPP

#include "arch/widget_helpers.hpp"
#include "settings/preferences.hpp"
#include "table/gameplay_session.hpp"
#include "table/table.hpp"

#include <QHash>
#include <QImage>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QVector>
#include <optional>
#include <shared_ui/qt/control_overlay.hpp>

class QAction;
class QAbstractButton;
class QTimer;
class QResizeEvent;
class QLabel;
class QColor;
class card_widget;
class QDialog;
class QToolButton;
class QLineEdit;

namespace gameplay {
class deck_setup_widget;
}

namespace shared_ui::qt {
class action_group;
class answer_panel;
class adaptive_surface;
class editor_panel;
}

class table_slot : public BaseWidget {
    Q_OBJECT
    friend class table;

public:
    explicit table_slot(BaseWidget* parent = nullptr);
    ~table_slot() override;
    [[nodiscard]] bool has_cards() const;
    [[nodiscard]] std::optional<gameplay::deck_id> gameplay_deck_id() const;

    void set_swap_selected(bool selected);
    [[nodiscard]] bool swap_selected() const;
    void set_rotated(bool rotated);
    void set_gameplay_layout_rotation(std::optional<qreal> degrees);
    void set_frame_style(slot_frame_style style);
    void set_action_style(slot_action_style style);
    void set_settings_style(slot_settings_style style);
    void set_quiz_presentation(
        quiz_answer_style answer, quiz_feedback_style feedback
    );

    void trigger_highlight(int duration_ms);
    void tick_highlight(int delta_ms);
    void prepare_card_faces();
    [[nodiscard]] int card_face_need_short_px() const;
    void set_presented_card_images(const QHash<QString, QImage>& images);
    void clear_presented_card_images();
    [[nodiscard]] bool has_presented_card_images() const;
    void set_presented_image_mode(bool enabled);
    void apply_theme();
    [[nodiscard]] bool is_quiz_prompt_active() const;
    // Called by table only after whole-drill preflight and clearing the quiz.

    // Touch profile: the slot draws Appearance's deck frame and marks, and
    // its questions are answered in the host's sheet instead of an overlay.
    void set_touch_presentation(bool enabled);

    [[nodiscard]] bool touch_presentation() const { return touch_mode; }

    // Position of this deck's question in the open frozen batch; empty when
    // none. Done marks an already answered question of the same batch.
    void set_touch_ordinal(const QString& ordinal, bool done);
    // Other decks keep dealing while a question waits.
    void set_touch_dealing(bool dealing);

    // The latest answer's band, if shown.
    [[nodiscard]] bool has_touch_verdict() const {
        return touch_verdict.has_value();
    }

    void dismiss_touch_verdict();

signals:
    void swap_clicked(table_slot* slot);
    void touch_tapped(table_slot* slot);
    void rasterization_busy_changed(bool busy);
    // Target setup invalidation and hint changes have different host duties.
    void gameplay_configuration_changed();
    void gameplay_show_count_changed();
    void gameplay_correction_dismissed();

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void on_swap_button_clicked();
    void on_settings_button_clicked();
    void on_quiz_answer_button_clicked();
    void on_quiz_skip_button_clicked();
    void on_quiz_continue_button_clicked();

private:
    gameplay::session* gameplay_owner = nullptr;
    gameplay::deck_id gameplay_id { 0 };
    table* gameplay_host = nullptr;
    QLineEdit* gameplay_input = nullptr;
    QLabel* gameplay_input_error = nullptr;
    QLabel* gameplay_correction_label = nullptr;
    std::optional<table::gameplay_prompt_key> gameplay_prompt;
    std::optional<std::int64_t> gameplay_projected_input;
    std::optional<gameplay::quiz_answer> gameplay_feedback;
    // Table-only lifetime: views are destroyed before the owned session.
    [[nodiscard]] bool bind_gameplay_deck(
        table& host, gameplay::session& owner, gameplay::deck_id id
    );
    void refresh_gameplay_deck();
    void setup_gameplay_input();
    void refresh_gameplay_quiz();
    [[nodiscard]] std::optional<std::int64_t> gameplay_input_value() const;
    void on_gameplay_input_edited();
    void show_gameplay_answer(const gameplay::quiz_answer& answer);
    void focus_gameplay_input();
    card_widget* card_widget_internal;

    shared_ui::qt::control_overlay* overlay_widget;
    QToolButton* compact_controls_button = nullptr;
    shared_ui::qt::adaptive_surface* controls_surface = nullptr;
    BaseWidget* swap_bar_widget;
    slot_action_style action_style = slot_action_style::classic;
    slot_settings_style settings_style = slot_settings_style::classic;
    QPointer<shared_ui::qt::editor_panel> settings_editor_panel;
    gameplay::deck_setup_widget* gameplay_editor_fields = nullptr;
    // The slot's commands. The slot owns their meaning, availability and
    // checked state; slot_actions (Appearance) only presents them.
    QAction* details_action = nullptr;
    QAction* swap_action = nullptr;
    shared_ui::qt::action_group* slot_actions = nullptr;
    shared_ui::qt::answer_panel* quiz_bar_widget;
    QLabel* quiz_weight_label;
    BaseWidget* quiz_chip_widget = nullptr;
    QLabel* quiz_feedback_heading = nullptr;
    // Felt D-FEED-2 band: submitted and expected accumulated counts.
    quiz_answer_style answer_style = quiz_answer_style::numeric;
    quiz_feedback_style feedback_style = quiz_feedback_style::classic;
    BasePushButton* quiz_answer_button;
    BasePushButton* quiz_skip_button;
    QLabel* quiz_feedback_label;
    BasePushButton* quiz_continue_button;
    bool is_rotated;
    bool quiz_prompt_active;
    bool quiz_feedback_active;
    bool touch_mode = false;
    QString touch_ordinal;
    bool touch_ordinal_done = false;
    bool touch_live = false;
    // Presentation of the latest answer, including right ones; corrections
    // for review stay in gameplay_feedback.
    std::optional<gameplay::quiz_answer> touch_verdict;
    QAbstractButton* touch_dismiss = nullptr;
    QTimer* touch_verdict_timer = nullptr;
    void refresh_touch_marks();
    void place_touch_dismiss();

    void setup_overlay();
    void setup_quiz_chips();
    void update_quiz_presentation();
    void update_feedback_stamp();
    void update_overlay_layout();
    void update_overlay_margins();
    void update_action_presentation();
    // Keyboard focus to the Details command, if it is shown.
    bool focus_details_action();
    void update_compact_controls();
    void update_settings_button_state();
    void update_quiz_controls_visibility();
    void update_action_button_state();
    void update_overlay_palette();
    void open_settings_editor();
    void place_settings_editor();
    void finish_settings_editor(bool apply);
};

#endif // KCUCKOOUNTER_TABLE_TABLE_SLOT_HPP
