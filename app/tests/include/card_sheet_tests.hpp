#ifndef KCUCKOOUNTER_TESTS_CARD_SHEET_TESTS_HPP
#define KCUCKOOUNTER_TESTS_CARD_SHEET_TESTS_HPP

#include <QObject>

class card_sheet_tests : public QObject {
    Q_OBJECT

private slots:
    void loads_svg();
    void contains_expected_elements();
    void available_themes_include_bundled_and_installed_when_present();
    void source_path_switches_between_themes();
    void required_ids_and_fallback_resolution_are_deterministic();
};

#endif // KCUCKOOUNTER_TESTS_CARD_SHEET_TESTS_HPP
