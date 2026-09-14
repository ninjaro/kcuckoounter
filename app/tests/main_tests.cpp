#include <QApplication>
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
