#ifndef KCUCKOOUNTER_TESTS_CARD_WIDGET_TESTS_HPP
#define KCUCKOOUNTER_TESTS_CARD_WIDGET_TESTS_HPP

#include <QObject>

class card_widget_tests : public QObject {
    Q_OBJECT

private slots:
    void visible_faces_preserve_variants_data();
    void visible_faces_preserve_variants();
    void local_resize_tracks_latest_demand_data();
    void local_resize_tracks_latest_demand();
    void shared_faces_disable_local_rasterization();
    void shared_faces_outlive_source_file();
    void standalone_source_validation_rejects_invalid_svg();
    void shared_faces_reject_local_notifications_data();
    void shared_faces_reject_local_notifications();
    void local_withdrawal_preserves_other_subscribers_data();
    void local_withdrawal_preserves_other_subscribers();
    void selected_presentation_borrows_current_images();
    void selected_presentation_preserves_master_pixels();
    void rotated_master_uses_smooth_single_transform_data();
    void rotated_master_uses_smooth_single_transform();
    void theme_source_change_invalidates_stale_raster_result();
    void visible_face_does_not_wait_for_unrelated_elements();
    void completion_observer_can_destroy_widget();
    void accumulated_count_survives_presentation_changes();
    void recent_stack_retains_five_transforms();
    void accessible_description_tracks_visible_card_state();
    void frame_choice_only_changes_presentation();
    void rotated_stack_stays_inside_slot();
    static void retained_transforms_fit_after_resize();
    void binding_projects_owned_deck_and_lifetime();
    void count_visibility_uses_training_choice_and_int64();
    void quiz_and_pause_keep_faces_hidden();
    void piping_moves_gameplay_strategy_to_legend();
    void terminal_states_are_distinct_geometry_is_unchanged();
    void stack_refresh_and_swap_preserve_deck_binding();
    void rollover_reads_current_buffer_and_cumulative_index();
    void gameplay_layout_rotation_fits_every_frame();
    void layout_frames_preserve_jitter_and_shared_faces();
    void gameplay_external_paint_matches_native_data();
    void gameplay_external_paint_matches_native();
    void gameplay_external_pending_paint_keeps_faces_hidden();
};

#endif // KCUCKOOUNTER_TESTS_CARD_WIDGET_TESTS_HPP
