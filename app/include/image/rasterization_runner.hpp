#ifndef KCUCKOOUNTER_IMAGE_RASTERIZATION_RUNNER_HPP
#define KCUCKOOUNTER_IMAGE_RASTERIZATION_RUNNER_HPP

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class rasterization_runner : public QObject {
    Q_OBJECT

public:
    enum class decision_kind {
        reuse,
        upsize,
        downsize,
        forced,
    };

    struct size_window {
        int minimum_need_px = 0;
        int maximum_need_px = 0;

        [[nodiscard]] bool contains(int need_px) const;
    };

    struct evaluation {
        decision_kind decision = decision_kind::reuse;
        int required_short_px = 0;
        int target_cache_px = 0;
        size_window accepted_window;
        bool rasterization_required = false;
    };

    explicit rasterization_runner(QObject* parent = nullptr);

    void set_cached_short_px(int short_px);
    [[nodiscard]] int cached_short_px() const;
    [[nodiscard]] size_window accepted_window() const;
    [[nodiscard]] int pending_target_cache_px() const;

    evaluation on_need_changed(
        int new_need_px, double pickup_interval_sec, double now_sec,
        bool is_idle = false, bool over_budget = false,
        bool memory_pressure = false
    );
    evaluation request_immediately(int new_need_px, bool force = false);

    static evaluation evaluate_size_need(
        int new_need_px, int cached_short_px, bool force = false
    );
    static size_window accepted_window_for_cached_size(int cached_short_px);
    static int target_cache_px_for_need(int need_px);

    void cancel_pending();

signals:
    void rasterization_requested(int target_cache_px);

private:
    static constexpr int k_min_short_px = 63;
    static constexpr double k_target_headroom = 1.25;
    static constexpr double k_max_cache_need_ratio = 1.50;
    static constexpr double k_bucket_k = 1.12;
    static constexpr double k_upsize_start_max_sec = 0.80;

    int cached_short_px_value;
    int last_need_px_value;
    evaluation last_evaluation_value;
    int pending_target_px;
    double pending_delay_sec_value;
    double pending_start_time_sec;

    QTimer pending_timer;
    QElapsedTimer monotonic_clock;

    static double clamp(double value, double lo, double hi);
    static int bucketize(int short_px);
    static int bucket_index(int short_px);

    void schedule_stable_reraster(
        int target_cache_px, double delay_sec, double now_sec
    );
    [[nodiscard]] static double calc_upsize_delay_sec(
        double pickup_interval_sec, bool is_urgent_upsize, int step_jump,
        double pixel_scale
    );
    [[nodiscard]] static double calc_downsize_delay_sec(
        double pickup_interval_sec, int step_drop, double waste_ratio
    );
    void on_pending_timeout();
    [[nodiscard]] double current_time_sec() const;
};

#endif // KCUCKOOUNTER_IMAGE_RASTERIZATION_RUNNER_HPP
