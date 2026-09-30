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
};

#endif // KCUCKOOUNTER_TESTS_CARD_WIDGET_TESTS_HPP
