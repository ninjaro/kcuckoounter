#ifndef KCUCKOOUNTER_TESTS_CARD_SHEET_TESTS_HPP
#define KCUCKOOUNTER_TESTS_CARD_SHEET_TESTS_HPP

#include <QObject>

class card_sheet_tests : public QObject {
    Q_OBJECT

private slots:
    void rasterflow_metadata_follows_current_aliases_and_frame();
    void rasterflow_aliases_resolve_explicit_demands();
    void rasterflow_fallback_and_missing_elements();
    void rasterflow_base_frame_preserves_aspect();
    void rasterflow_consumers_share_and_withdraw();
    void rasterflow_required_readiness_ignores_speculative_fallback();
    void rasterflow_source_replacement_rejects_stale_pixels();
    void rasterflow_invalid_demand_preserves_subscription();
    void rasterflow_observer_can_destroy_consumer();
    void metadata_selection_rejects_stale_completion();
    void loads_svg();
    void contains_expected_elements();
    void logical_resource_ids_are_literal();
    void bundled_selection_is_independent_of_picker();
    void themes_include_bundled_and_available_installed();
    void source_path_switches_between_themes();
    void required_ids_and_fallback_resolution_are_deterministic();
    void retained_themes_render_all_logical_cards_data();
    void retained_themes_render_all_logical_cards();
    void removed_bundled_sources_fall_back_to_default();
    void alternate_svg_id_conventions_are_resolved();
    void registered_sources_survive_file_removal();
    void fallback_loads_on_first_missing_face();
    void missing_faces_and_invalid_demand();
    void base_frames_follow_each_selected_source();
    void sheet_ratio_uses_the_base_trimmed_face();
};

#endif // KCUCKOOUNTER_TESTS_CARD_SHEET_TESTS_HPP
