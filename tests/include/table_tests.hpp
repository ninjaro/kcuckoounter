#ifndef KCUCKOOUNTER_TABLE_TESTS_HPP
#define KCUCKOOUNTER_TABLE_TESTS_HPP

#include <QObject>

class table_tests : public QObject {
    Q_OBJECT

private slots:
    /// @brief Verifies overlay palette applies to settings and swap bars.
    void overlay_palette_applies_to_bars();
    /// @brief Verifies gold text is used for overlay frame palettes.
    void overlay_palette_uses_gold_text_on_frames();
    /// @brief Verifies overlay frames enable auto-fill background.
    void overlay_frames_enable_auto_fill();
    /// @brief Verifies skip button hides when skipping is disabled.
    void quiz_hides_skip_when_skipping_disabled();
    /// @brief Verifies training mode avoids score adjustments.
    void quiz_training_mode_does_not_adjust_score();
    /// @brief Verifies wrong answers exhaust deck outside training.
    void quiz_wrong_answer_exhausts_deck_without_training();
    /// @brief Verifies continue feedback when training answers are wrong.
    void quiz_wrong_answer_shows_continue_in_training();
    /// @brief Verifies continue feedback after skipping a question.
    void quiz_skip_shows_continue_feedback();
    /// @brief Verifies quiz spin box remembers the last input.
    void quiz_spin_box_remembers_last_input();
    /// @brief Verifies shared card-face presence tracks set and clear calls.
    void shared_card_faces_presence_tracks_set_and_clear();
};

#endif // KCUCKOOUNTER_TABLE_TESTS_HPP
