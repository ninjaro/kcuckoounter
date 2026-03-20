#include "include/rasterization_runner_tests.hpp"

#include "image/rasterization_runner.hpp"

#include <QtTest/QtTest>

#include <cmath>
#include <limits>

static constexpr int k_min_short_px = 63;
static constexpr double k_overscan = 1.75;
static constexpr double k_bucket_k = 1.12;

static int bucketize(int short_px) {
    int bucket = k_min_short_px;
    while (bucket < short_px) {
        bucket = static_cast<int>(std::round(bucket * k_bucket_k));
    }
    return bucket;
}

static int expected_target(int need_px) {
    return static_cast<int>(std::ceil(bucketize(need_px) * k_overscan));
}

void rasterization_runner_tests::emits_without_clock_ticks() {
    rasterization_runner runner;
    QSignalSpy spy(&runner, &rasterization_runner::rasterization_requested);

    runner.set_cached_short_px(k_min_short_px);
    runner.on_need_changed(220, 0.10, std::numeric_limits<double>::quiet_NaN());

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 600);
    QCOMPARE(spy.at(0).at(0).toInt(), expected_target(220));
}

void rasterization_runner_tests::coalesces_to_latest_pending_target() {
    rasterization_runner runner;
    QSignalSpy spy(&runner, &rasterization_runner::rasterization_requested);

    runner.set_cached_short_px(k_min_short_px);
    runner.on_need_changed(110, 0.20, std::numeric_limits<double>::quiet_NaN());
    runner.on_need_changed(220, 0.20, std::numeric_limits<double>::quiet_NaN());

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 700);
    QCOMPARE(spy.at(0).at(0).toInt(), expected_target(220));
}

void rasterization_runner_tests::clock_ticks_do_not_change_behavior() {
    rasterization_runner runner;
    QSignalSpy spy(&runner, &rasterization_runner::rasterization_requested);

    runner.set_cached_short_px(k_min_short_px);
    runner.on_clock_tick(50, 50);
    runner.on_clock_tick(100, 50);
    runner.on_need_changed(180, 0.10, std::numeric_limits<double>::quiet_NaN());

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 600);
    QCOMPARE(spy.at(0).at(0).toInt(), expected_target(180));
}
