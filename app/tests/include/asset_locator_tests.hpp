#ifndef KCUCKOOUNTER_TESTS_ASSET_LOCATOR_TESTS_HPP
#define KCUCKOOUNTER_TESTS_ASSET_LOCATOR_TESTS_HPP

#include <QObject>

class asset_locator_tests : public QObject {
    Q_OBJECT

private slots:
    void bundled_assets_resolve_from_an_unrelated_working_directory();
    void traversal_and_absolute_names_are_rejected();
};

#endif // KCUCKOOUNTER_TESTS_ASSET_LOCATOR_TESTS_HPP
