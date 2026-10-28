#ifndef KCUCKOOUNTER_TESTS_PREVIEW_CAROUSEL_TESTS_HPP
#define KCUCKOOUNTER_TESTS_PREVIEW_CAROUSEL_TESTS_HPP

#include <QObject>

class settings_template_widget;
class QString;

class preview_carousel_tests : public QObject {
    Q_OBJECT

private slots:
    void settings_previews_share_master_images();
    void table_and_preview_share_master_images();
    void settings_preview_window_replaces_desired();
    void settings_preview_source_cutover_is_atomic();
    void rapid_source_switch_preserves_latest_artwork();
    void speculative_neighbors_do_not_hold_visible_cutover();
    void initial_previews_publish_before_all_requests_finish();
    void settings_preview_retirement_preserves_survivor_data();
    void settings_preview_retirement_preserves_survivor();
    void settings_preview_returns_to_cached_demand();
    void settings_preview_geometry_has_physical_quality();
    void weighted_preview_does_not_mutate_shared_pixels();
    void preview_publication_can_destroy_owner_data();
    void preview_publication_can_destroy_owner();

private:
    static bool
    select_source(settings_template_widget& editor, const QString& path);
};

#endif // KCUCKOOUNTER_TESTS_PREVIEW_CAROUSEL_TESTS_HPP
