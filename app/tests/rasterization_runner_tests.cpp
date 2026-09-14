// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/rasterization_runner_tests.hpp"

#include "image/rasterization_runner.hpp"

#include <QtTest/QtTest>

#include <cmath>
#include <limits>

static constexpr int k_min_short_px = 63;
static constexpr double k_target_headroom = 1.25;
static constexpr double k_bucket_k = 1.12;

static int bucketize(int short_px) {
    int bucket = k_min_short_px;
    while (bucket < short_px) {
        bucket = static_cast<int>(std::round(bucket * k_bucket_k));
    }
    return bucket;
}

static int expected_target(int need_px) {
    return static_cast<int>(std::ceil(bucketize(need_px) * k_target_headroom));
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

void rasterization_runner_tests::reuses_cache_inside_accepted_size_window() {
    rasterization_runner runner;
    QSignalSpy spy(&runner, &rasterization_runner::rasterization_requested);
    const int cached_px = expected_target(100);
    runner.set_cached_short_px(cached_px);

    const rasterization_runner::evaluation evaluation = runner.on_need_changed(
        110, 0.10, std::numeric_limits<double>::quiet_NaN()
    );

    QCOMPARE(evaluation.decision, rasterization_runner::decision_kind::reuse);
    QVERIFY(!evaluation.rasterization_required);
    QCOMPARE(evaluation.target_cache_px, cached_px);
    QVERIFY(evaluation.accepted_window.contains(110));
    QCOMPARE(runner.pending_target_cache_px(), 0);
    QCOMPARE(spy.count(), 0);
}

void rasterization_runner_tests::rerasterizes_above_and_below_size_window() {
    const int cached_px = expected_target(120);
    const rasterization_runner::size_window window
        = rasterization_runner::accepted_window_for_cached_size(cached_px);

    const rasterization_runner::evaluation smaller
        = rasterization_runner::evaluate_size_need(
            window.minimum_need_px - 1, cached_px
        );
    QCOMPARE(smaller.decision, rasterization_runner::decision_kind::downsize);
    QVERIFY(smaller.rasterization_required);
    QVERIFY(smaller.target_cache_px < cached_px);

    const rasterization_runner::evaluation larger
        = rasterization_runner::evaluate_size_need(
            window.maximum_need_px + 1, cached_px
        );
    QCOMPARE(larger.decision, rasterization_runner::decision_kind::upsize);
    QVERIFY(larger.rasterization_required);
    QVERIFY(larger.target_cache_px > cached_px);
}

void rasterization_runner_tests::reentry_cancels_stale_pending_rasterization() {
    rasterization_runner runner;
    QSignalSpy spy(&runner, &rasterization_runner::rasterization_requested);
    const int cached_px = expected_target(120);
    runner.set_cached_short_px(cached_px);
    const rasterization_runner::size_window window = runner.accepted_window();

    const rasterization_runner::evaluation downsize = runner.on_need_changed(
        window.minimum_need_px - 1, 0.10,
        std::numeric_limits<double>::quiet_NaN()
    );
    QVERIFY(downsize.rasterization_required);
    QVERIFY(runner.pending_target_cache_px() > 0);

    const rasterization_runner::evaluation reuse = runner.on_need_changed(
        window.minimum_need_px, 0.10, std::numeric_limits<double>::quiet_NaN()
    );
    QVERIFY(!reuse.rasterization_required);
    QCOMPARE(runner.pending_target_cache_px(), 0);
    QTest::qWait(500);
    QCOMPARE(spy.count(), 0);
}

void rasterization_runner_tests::immediate_requests_use_same_size_policy() {
    rasterization_runner runner;
    QSignalSpy spy(&runner, &rasterization_runner::rasterization_requested);
    const int cached_px = expected_target(100);
    runner.set_cached_short_px(cached_px);

    const rasterization_runner::evaluation reuse
        = runner.request_immediately(110);
    QVERIFY(!reuse.rasterization_required);
    QCOMPARE(spy.count(), 0);

    const rasterization_runner::evaluation upsize
        = runner.request_immediately(cached_px + 1);
    QVERIFY(upsize.rasterization_required);
    QCOMPARE(upsize.decision, rasterization_runner::decision_kind::upsize);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), upsize.target_cache_px);
    QVERIFY(upsize.target_cache_px != cached_px + 1);

    const rasterization_runner::evaluation forced
        = runner.request_immediately(110, true);
    QCOMPARE(forced.decision, rasterization_runner::decision_kind::forced);
    QVERIFY(forced.rasterization_required);
    QCOMPARE(spy.count(), 2);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
