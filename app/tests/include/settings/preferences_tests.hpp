#ifndef KCUCKOOUNTER_TESTS_PREFERENCES_TESTS_HPP
#define KCUCKOOUNTER_TESTS_PREFERENCES_TESTS_HPP

#include <QObject>

class preferences_tests : public QObject {
    Q_OBJECT

private slots:
    void default_suit_is_independent_persistent_appearance();
    void default_suit_rejects_invalid_storage_without_rewriting();
    void round_trip_preserves_valid_values();
    void invalid_values_fall_back_independently();
    void strategy_id_repairs_renamed_or_removed_slug();
    void shell_round_trip_preserves_qt_state();
    void session_checkpoint_round_trip_preserves_exact_progress();
    void session_checkpoint_rejects_duplicate_or_missing_cards();
    void session_checkpoint_rejects_huge_numeric_values();
    void session_checkpoint_rejects_inconsistent_slot_state();
    void training_progress_is_bounded();
    void training_progress_rejects_huge_history_values();
    void desktop_components_preserve_domain_settings();
    void drills_round_trip_without_presentation_or_progress();
    void drills_reject_invalid_storage_without_overwriting();
    void gameplay_setup_defaults_and_accessible_controls();
    void gameplay_setup_loads_without_normalizing_configuration();
    void gameplay_setup_enforces_lives_pause_and_bounds();
    void setup_keeps_source_scope_and_pause_independent();
    void gameplay_setup_preserves_sequential_n_across_modes();
    void gameplay_setup_invalidates_only_through_the_domain();
    void gameplay_setup_rejects_live_and_invalid_edits();
    void gameplay_setup_does_not_write_legacy_storage();
    void slot_recommendation_and_override_are_domain_owned();
    void slot_resize_preserves_preparation_and_choice();
    void slot_geometry_rejects_invalid_and_live_updates();
    void deck_setup_preserves_choices_shows_advisory_guidance();
    void deck_setup_edits_only_the_selected_deck();
    void gameplay_deck_setup_fresh_defaults_remain_explicit();
    void deck_setup_rejects_unavailable_and_invalid_choices();
    void gameplay_deck_setup_preserves_unrepresentable_counts();
    void
    gameplay_deck_setup_locks_configuration_but_allows_paused_training_count();
    void deck_setup_follows_identity_through_swap();
    void deck_draft_stages_and_commits_before_notifications();
    void gameplay_deck_draft_count_only_preserves_preparation();
    void deck_draft_rejects_stale_and_phase_changes();
    void deck_draft_interprets_text_preserves_large_counts();
    void deck_draft_follows_swap_and_training_conversion();
};

#endif // KCUCKOOUNTER_TESTS_PREFERENCES_TESTS_HPP
