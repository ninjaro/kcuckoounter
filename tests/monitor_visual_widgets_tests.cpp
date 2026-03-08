#include "include/monitor_visual_widgets_tests.hpp"

#include "monitor/monitor_visual_widgets.hpp"

#include <QtTest/QtTest>

void monitor_visual_widgets_tests::
    project_size_preserving_aspect_preserves_ratio() {
    const QSize projected
        = monitor_visual_geometry::project_size_preserving_aspect(
            QSize(120, 60), QSize(240, 240), QSize(120, 120)
        );
    QCOMPARE(projected.width(), 60);
    QCOMPARE(projected.height(), 30);
}

void monitor_visual_widgets_tests::
    project_size_preserving_aspect_respects_available_bounds() {
    const QSize projected
        = monitor_visual_geometry::project_size_preserving_aspect(
            QSize(400, 200), QSize(400, 200), QSize(80, 40)
        );
    QVERIFY(projected.width() <= 80);
    QVERIFY(projected.height() <= 40);
    QCOMPARE(projected.width(), 80);
    QCOMPARE(projected.height(), 40);
}

void monitor_visual_widgets_tests::
    project_size_preserving_aspect_handles_invalid_sizes() {
    const QSize projected
        = monitor_visual_geometry::project_size_preserving_aspect(
            QSize(), QSize(200, 100), QSize(100, 100)
        );
    QVERIFY(projected.isEmpty());
}

void monitor_visual_widgets_tests::
    spread_region_prefers_outer_rect_and_inner_rect() {
    const monitor_visual_geometry::projected_spread_region spread
        = monitor_visual_geometry::resolve_projected_spread_region(
            QRect(0, 0, 100, 80), QRect(0, 0, 80, 60)
        );

    QVERIFY(spread.is_valid());
    QCOMPARE(spread.outer_rect, QRect(0, 0, 100, 80));
    QCOMPARE(spread.inner_rect, QRect(0, 0, 80, 60));
}

void monitor_visual_widgets_tests::spread_region_requires_containment() {
    const monitor_visual_geometry::projected_spread_region spread
        = monitor_visual_geometry::resolve_projected_spread_region(
            QRect(0, 0, 100, 80), QRect(40, 30, 80, 60)
        );

    QVERIFY(!spread.is_valid());
}
