#ifndef KCUCKOOUNTER_TESTS_CARD_SHEET_TESTS_HPP
#define KCUCKOOUNTER_TESTS_CARD_SHEET_TESTS_HPP

#include <QObject>

class card_sheet_tests : public QObject {
    Q_OBJECT

private slots:
    void loads_svg();
    void contains_expected_elements();
    void logical_resource_ids_are_literal();
    void themes_include_bundled_and_available_installed();
    void source_path_switches_between_themes();
    void required_ids_and_fallback_resolution_are_deterministic();
    void alternate_svg_id_conventions_are_resolved();
    void context_reuses_parsed_sources_after_file_removal();
    void context_loads_fallback_on_first_missing_face();
    void context_preserves_placeholders_and_invalid_input();
    void context_normalizes_each_sources_base_bounds();
};

#endif // KCUCKOOUNTER_TESTS_CARD_SHEET_TESTS_HPP
