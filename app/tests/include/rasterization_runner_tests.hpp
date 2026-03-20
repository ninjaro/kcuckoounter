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
    /// @brief Verifies game clock ticks do not affect request timing semantics.
    void clock_ticks_do_not_change_behavior();
};

#endif // KCUCKOOUNTER_TESTS_RASTERIZATION_RUNNER_TESTS_HPP
