#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "include/asset_locator_tests.hpp"
#include "include/card_sheet_tests.hpp"
#include "include/card_widget_tests.hpp"
#include "include/image_cacher_tests.hpp"
#include "include/infinity_spinbox_tests.hpp"
#include "include/preferences_tests.hpp"
#include "include/preview_carousel_tests.hpp"
#include "include/raster_cache_tests.hpp"
#include "include/rasterization_runner_tests.hpp"
#include "include/strategy_data_tests.hpp"
#include "include/table_tests.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Application-facing settings tests must never write the user's trainer
    // preferences. Explicit INI fixtures in individual tests remain
    // independent.
    QTemporaryDir settings_directory;
    if (!settings_directory.isValid()) {
        qFatal("Could not create isolated test settings directory");
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat, QSettings::UserScope, settings_directory.path()
    );

    int status = 0;

    {
        asset_locator_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        card_sheet_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        card_widget_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        preview_carousel_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        preferences_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        infinity_spinbox_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        rasterization_runner_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        raster_cache_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        image_cacher_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        strategy_data_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        table_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }

    return status;
}
