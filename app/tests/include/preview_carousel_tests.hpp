#ifndef KCUCKOOUNTER_TESTS_PREVIEW_CAROUSEL_TESTS_HPP
#define KCUCKOOUNTER_TESTS_PREVIEW_CAROUSEL_TESTS_HPP

#include <QObject>

class settings_template_widget;
class QImage;
template <typename T> class QPromise;

class preview_carousel_tests : public QObject {
    Q_OBJECT

private slots:
    void default_prefetch_loads_adjacent_cards();
    void disabled_prefetch_only_loads_visible_cards();
    void provider_window_releases_departed_cards_data();
    void provider_window_releases_departed_cards();
    void changing_prefetch_preserves_visible_cards();
    void supplied_cards_survive_window_navigation();
    void geometry_requests_use_settled_dimensions_data();
    void geometry_requests_use_settled_dimensions();
    void navigation_controls_expose_accessible_semantics();
    void visible_cards_expose_domain_accessible_names();
    void settings_previews_share_one_queued_render_job();
    void settings_preview_dispatch_uses_current_window_data();
    void settings_preview_dispatch_uses_current_window();
    void settings_preview_cutover_ignores_departed_demand();
    void settings_preview_navigation_rejects_late_results();
    void settings_preview_navigation_cancels_obsolete_batch_data();
    void settings_preview_navigation_cancels_obsolete_batch();
    void settings_preview_navigation_keeps_useful_batch();
    void settings_preview_navigation_recovers_canceled_demand();
    void settings_preview_cancellation_is_per_editor();
    void settings_preview_results_publish_before_batch_finishes();
    void settings_preview_cutover_rejects_late_results();
    void settings_preview_pruning_releases_requests();
    void settings_preview_destruction_cancels_batch();
    void settings_preview_worker_outlives_widget();
    void settings_preview_batches_keep_generation_boundaries();
    void completed_preview_result_releases_future_data();
    void completed_preview_result_releases_future();
    void publication_can_retire_preview_owner_data();
    void publication_can_retire_preview_owner();
    void settings_preview_cancellation_recovers_returning_demand();

private:
    static void hold_preview_batch(
        settings_template_widget& editor, QPromise<QImage>& promise
    );
};

#endif // KCUCKOOUNTER_TESTS_PREVIEW_CAROUSEL_TESTS_HPP
