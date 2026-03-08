#include <QApplication>
#include <QtTest/QtTest>

#include "include/card_packer_tests.hpp"
#include "include/card_sheet_tests.hpp"
#include "include/card_widget_tests.hpp"
#include "include/cli_scripts_tests.hpp"
#include "include/debug_broadcaster_tests.hpp"
#include "include/debug_probe_core_tests.hpp"
#include "include/image_cacher_tests.hpp"
#include "include/infinity_spinbox_tests.hpp"
#include "include/monitor_parity_checker_tests.hpp"
#include "include/monitor_visual_widgets_tests.hpp"
#include "include/preview_carousel_tests.hpp"
#include "include/raster_cache_tests.hpp"
#include "include/rasterization_runner_tests.hpp"
#include "include/resource_monitor_tests.hpp"
#include "include/table_tests.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    int status = 0;

    {
        card_packer_tests t;
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
        cli_scripts_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        debug_broadcaster_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        debug_probe_core_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        monitor_visual_widgets_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        monitor_parity_checker_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        preview_carousel_tests t;
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
        resource_monitor_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        table_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }

    return status;
}
