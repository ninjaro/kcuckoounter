// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/asset_locator_tests.hpp"

#include "arch/asset_locator.hpp"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

class current_directory_guard {
public:
    current_directory_guard()
        : original_path(QDir::currentPath()) { }

    ~current_directory_guard() { QDir::setCurrent(original_path); }

private:
    QString original_path;
};

} // namespace

void asset_locator_tests::
    bundled_assets_resolve_from_an_unrelated_working_directory() {
    const QString expected
        = bundled_asset_path(QStringLiteral("strategies.json"));
    QVERIFY2(!expected.isEmpty(), "bundled strategy data was not staged");

    QTemporaryDir unrelated_directory;
    QVERIFY(unrelated_directory.isValid());
    current_directory_guard restore_directory;
    QVERIFY(QDir::setCurrent(unrelated_directory.path()));

    const QString actual
        = bundled_asset_path(QStringLiteral("strategies.json"));
    QCOMPARE(actual, expected);
    const QFileInfo asset_info(actual);
    QVERIFY(actual.startsWith(QStringLiteral(":/")) || asset_info.isAbsolute());
    QVERIFY(asset_info.isFile());
    QVERIFY(asset_info.isReadable());
}

void asset_locator_tests::traversal_and_absolute_names_are_rejected() {
    QVERIFY(bundled_asset_path(QString()).isEmpty());
    QVERIFY(bundled_asset_path(QStringLiteral("../strategies.json")).isEmpty());
    QVERIFY(bundled_asset_path(QStringLiteral("images/../../strategies.json"))
                .isEmpty());
    QVERIFY(bundled_asset_path(QDir::rootPath()).isEmpty());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
