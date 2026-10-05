#ifndef KCUCKOOUNTER_TESTS_CARD_WIDGET_TESTS_HPP
#define KCUCKOOUNTER_TESTS_CARD_WIDGET_TESTS_HPP

#include <QObject>

class card_widget_tests : public QObject {
    Q_OBJECT

private slots:
    void stretches_between_raster_intervals();
    void memory_cache_tracks_resize();
    void shared_faces_disable_local_rasterization();
    void selected_presentation_reuses_and_invalidates();
    void theme_source_change_invalidates_stale_raster_result();
    void accumulated_count_survives_presentation_changes();
    void recent_stack_retains_five_transforms();
    void accessible_description_tracks_visible_card_state();
    void frame_choice_only_changes_presentation();
    void rotated_stack_stays_inside_slot();
    void retained_transforms_fit_after_resize();
    void binding_reads_owned_decks_rejects_legacy_mutation();
    void count_visibility_uses_training_choice_and_int64();
    void quiz_and_pause_keep_faces_hidden();
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
