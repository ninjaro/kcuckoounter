#include <QApplication>
#include <QtTest/QtTest>

#include "include/card_packer_tests.hpp"
#include "include/card_preview_carousel_tests.hpp"
#include "include/card_sheet_tests.hpp"
#include "include/card_widget_tests.hpp"
#include "include/image_cacher_tests.hpp"
#include "include/infinity_spinbox_tests.hpp"
#include "include/rasterization_runner_tests.hpp"
#include "include/svg_raster_cache_service_tests.hpp"
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
        card_preview_carousel_tests t;
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
        svg_raster_cache_service_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        image_cacher_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        table_tests t;
        status |= QTest::qExec(&t, argc, argv);
    }

    return status;
}
