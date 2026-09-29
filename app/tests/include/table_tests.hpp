#ifndef KCUCKOOUNTER_TESTS_TABLE_TESTS_HPP
#define KCUCKOOUNTER_TESTS_TABLE_TESTS_HPP

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
    /// @brief Verifies persisted orientation modes constrain packed slots.
    void card_orientation_constrains_packed_slot_geometry();
    void presentation_preserves_packed_slots_data();
    void presentation_preserves_packed_slots();
    void compact_slot_controls_remain_reachable_data();
    void compact_slot_controls_remain_reachable();
    void compact_controls_follow_slot_lifetime();
    void quiz_variants_preserve_count_semantics_data();
    void quiz_variants_preserve_count_semantics();
    void quiz_presentation_reaches_existing_and_new_slots();
    void quiz_presentation_editor_applies_and_resets();
    void action_variants_reuse_controls_data();
    void action_variants_reuse_controls();
    void action_variants_preserve_copy_and_swap_workflows();
    void settings_surfaces_stage_changes_data();
    void settings_surfaces_stage_changes();
    void settings_editor_lifecycle_cancels_stale_drafts();
    void classic_settings_dialog_preserves_transaction();
    void desktop_toolbar_preserves_table_and_commands_data();
    void desktop_toolbar_preserves_table_and_commands();
    void desktop_status_reports_existing_values();
    void desktop_hud_fits_without_changing_policy_data();
    void desktop_hud_fits_without_changing_policy();
    /// @brief Verifies shared cache rasterization populates visible slots.
    void shared_cache_rasterization_populates_visible_slots();
    /// @brief Verifies shared-cache generation cutover keeps bounded active +
    /// warming entries under rapid source churn.
    void shared_cache_generation_cutover_stays_bounded();
    /// @brief Verifies generation cutover leaves a single visible generation
    /// after warmup drains.
    void shared_generation_cutover_keeps_single_visible_generation();
    /// @brief Verifies theme apply does not leave stale shared-face in-flight
    /// state.
    void theme_apply_clears_stale_worker_state();
    /// @brief Verifies theme/resize transitions stay non-blocking while
    /// shared-face warming runs.
    void theme_and_resize_transitions_are_non_blocking();
    /// @brief Verifies invalid later-slot recovery cannot partially mutate the
    /// current table.
    void session_restore_preflight_is_transactional();
    /// @brief Verifies exact table/card progress restores without resuming
    /// dealing.
    void session_capture_restore_recovers_paused_quiz_state();
};

#endif // KCUCKOOUNTER_TESTS_TABLE_TESTS_HPP
