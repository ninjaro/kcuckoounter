#ifndef KCUCKOOUNTER_TESTS_RASTERIZATION_RUNNER_TESTS_HPP
#define KCUCKOOUNTER_TESTS_RASTERIZATION_RUNNER_TESTS_HPP

#include <QObject>

class rasterization_runner_tests : public QObject {
    Q_OBJECT

private slots:
    /// @brief Verifies requests are emitted without relying on game clock
    /// ticks.
    void emits_without_clock_ticks();
    /// @brief Verifies rapid updates keep only the latest pending target.
    void coalesces_to_latest_pending_target();
    /// @brief Verifies a cached raster is reused while the need stays inside
    /// its accepted size window.
    void reuses_cache_inside_accepted_size_window();
    /// @brief Verifies crossing either edge of the window requests an
    /// appropriately sized replacement.
    void rerasterizes_above_and_below_size_window();
    /// @brief Verifies returning to the active window cancels stale pending
    /// work.
    void reentry_cancels_stale_pending_rasterization();
    /// @brief Verifies immediate callers use the adaptive sizing policy rather
    /// than sending the raw display need as a cache target.
    void immediate_requests_use_same_size_policy();
};

#endif // KCUCKOOUNTER_TESTS_RASTERIZATION_RUNNER_TESTS_HPP
