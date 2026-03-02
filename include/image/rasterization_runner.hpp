#ifndef KCUCKOOUNTER_IMAGE_RASTERIZATION_RUNNER_HPP
#define KCUCKOOUNTER_IMAGE_RASTERIZATION_RUNNER_HPP

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class rasterization_runner : public QObject {
    Q_OBJECT

public:
    explicit rasterization_runner(QObject* parent = nullptr);

    void set_cached_short_px(int short_px);
    int cached_short_px() const;
    int last_need_px() const;
    double last_change_time_sec() const;

    void on_need_changed(
        int new_need_px, double pickup_interval_sec, double now_sec,
        bool is_idle = false, bool over_budget = false,
        bool memory_pressure = false
    );

    void cancel_pending();

public slots:
    void on_clock_tick(qint64 elapsed_ms, qint64 delta_ms);

signals:
    void rasterization_requested(int target_cache_px);

private:
    static constexpr int k_min_short_px = 63;
    static constexpr double k_overscan = 1.75;
    static constexpr double k_bucket_k = 1.12;
    static constexpr int k_shrink_step_hyst = 2;
    static constexpr double k_shrink_waste_min = 2.2;
    static constexpr double k_upsize_start_max_sec = 0.80;

    int cached_short_px_value;
    int last_need_px_value;
    double last_change_time_sec_value;
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
    double calc_upsize_delay_sec(
        double pickup_interval_sec, bool is_urgent_upsize, int step_jump,
        double pixel_scale
    ) const;
    double calc_downsize_delay_sec(
        double pickup_interval_sec, int step_drop, double waste_ratio
    ) const;
    void on_pending_timeout();
    double current_time_sec() const;
};

#endif // KCUCKOOUNTER_IMAGE_RASTERIZATION_RUNNER_HPP
