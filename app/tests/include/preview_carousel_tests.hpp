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
    void navigation_controls_expose_accessible_semantics();
    void visible_cards_expose_domain_accessible_names();
    void settings_previews_share_one_queued_render_job();
    void settings_preview_results_publish_before_batch_finishes();
    void settings_preview_cutover_rejects_late_results();
    void settings_preview_pruning_releases_requests();
    void settings_preview_destruction_cancels_batch();
    void settings_preview_worker_outlives_widget();
    void settings_preview_batches_keep_generation_boundaries();
    void completed_preview_result_releases_future_data();
    void completed_preview_result_releases_future();
    void settings_preview_cancellation_recovers_returning_demand();

private:
    static void hold_preview_batch(
        settings_template_widget& editor, QPromise<QImage>& promise
    );
};

#endif // KCUCKOOUNTER_TESTS_PREVIEW_CAROUSEL_TESTS_HPP
