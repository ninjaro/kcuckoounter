#ifndef KCUCKOOUNTER_TESTS_TABLE_TESTS_HPP
#define KCUCKOOUNTER_TESTS_TABLE_TESTS_HPP

#include <QObject>

class table_tests : public QObject {
    Q_OBJECT

private slots:
    void gameplay_table_owns_roster_without_legacy_state();
    void gameplay_table_installation_preflight_preserves_current_scene();
    void gameplay_table_replacement_destroys_views_before_owner();
    void owned_table_projects_mixed_decks_and_swap();
    void gameplay_table_refreshes_owned_check_and_timeout();
    void gameplay_table_terminal_resize_preserves_results();
    void gameplay_table_reuses_one_bounded_raster_owner();
    void gameplay_settings_surfaces_stage_changes_data();
    void gameplay_settings_surfaces_stage_changes();
    void count_visibility_keeps_preparation_and_cache();
    void gameplay_settings_follow_deck_swap_and_repack();
    void gameplay_settings_discard_stale_and_replaced_views();
    void gameplay_settings_pause_locks_and_preparation_notifications();
    void gameplay_clock_requires_explicit_bound_start();
    void gameplay_clock_routes_dealing_modes_data();
    void gameplay_clock_routes_dealing_modes();
    void gameplay_clock_preserves_jitter_and_coalesces_stalls();
    void gameplay_clock_samples_one_monotonic_timebase();
    void gameplay_clock_pause_preserves_batch_and_actions();
    void gameplay_clock_non_global_questions_keep_dealing();
    void clock_prompt_keys_guard_batches_and_scenes();
    void clock_timeout_clips_and_rejects_stale_batches();
    void gameplay_clock_settles_deadline_before_answer();
    void gameplay_clock_rejections_pause_and_diagnose();
    void gameplay_clock_replacement_retires_timer_and_notifications();
    void gameplay_clock_finish_stops_without_replaying_results();
    void gameplay_controls_numeric_and_chips_data();
    void gameplay_controls_numeric_and_chips();
    void gameplay_controls_invalid_drafts_and_signed_boundaries();
    void gameplay_controls_pause_skip_and_timeout();
    void controls_follow_packing_focus_and_swap_data();
    void controls_follow_packing_focus_and_swap();
    void corrections_neither_gate_nor_overwrite_controls();
    void gameplay_controls_compact_correction_stays_nonblocking();
    void gameplay_controls_compact_host_and_replacement();
    void control_refresh_keeps_caret_and_raster_owner();
    void gameplay_hud_projects_owned_session_data();
    void gameplay_hud_projects_owned_session();
    void gameplay_launcher_roster_preserves_exact_choices();
    void gameplay_launcher_native_setup_and_roster();
    void gameplay_launcher_roster_rejection_keeps_borrowed_views();
    void gameplay_launcher_start_rejects_drafts_and_budgets();
    void gameplay_launcher_geometry_requires_explicit_consent();
    void gameplay_launcher_routes_runtime_and_finish_data();
    void gameplay_launcher_routes_runtime_and_finish();
    void gameplay_launcher_finish_replacement_is_not_finished();
    void launcher_natural_end_and_legacy_return_data();
    void launcher_natural_end_and_legacy_return();
    void gameplay_launcher_teardown_retires_global_view();
    void review_keeps_one_record_per_deck();
    void gameplay_review_native_timeout_data();
    void gameplay_review_native_timeout();
    void gameplay_review_native_live_mapping_and_clear();
    void gameplay_review_native_replacement_and_legacy();
    void gameplay_lives_hud_data();
    void gameplay_lives_hud();
    void gameplay_lives_suit_settings_preserve_domain();
    void gameplay_resize_paths_data();
    void gameplay_resize_paths();
    void gameplay_resize_motion_policy_and_lifetime();
    void resize_preserves_owned_clock_and_terminal_result();
    void gameplay_swap_content_paths_data();
    void gameplay_swap_content_paths();
    void gameplay_swap_pending_state_data();
    void gameplay_swap_pending_state();
    void gameplay_swap_motion_policy_and_lifetime();
    void swap_paints_moving_content_below_controls();
    void strategy_browser_preserves_identity_and_reference_values();
    void strategy_browser_handles_missing_and_filtered_metadata();
    void strategy_browser_routes_do_not_change_gameplay();
    void drill_configuration_preflight_and_fresh_shoes();
    void saved_drill_picker_preserves_and_launches_sessions();
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
    void shared_faces_track_set_and_clear();
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
