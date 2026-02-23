#include "helpers/rasterization_runner.hpp"

#include <algorithm>
#include <cmath>

rasterization_runner::rasterization_runner(QObject* parent)
    : QObject(parent)
    , cached_short_px_value(k_min_short_px)
    , last_need_px_value(k_min_short_px)
    , last_change_time_sec_value(0.0)
    , pending_target_px(0)
    , pending_delay_sec_value(0.0)
    , pending_start_time_sec(0.0)
    , pending_timer()
    , monotonic_clock() {
    pending_timer.set_single_shot(true);
    QObject::connect(
        &pending_timer, &QTimer::timeout, this,
        &rasterization_runner::on_pending_timeout
    );
    monotonic_clock.start();
}

void rasterization_runner::set_cached_short_px(int short_px) {
    cached_short_px_value = std::max(k_min_short_px, short_px);
}

int rasterization_runner::cached_short_px() const {
    return cached_short_px_value;
}

int rasterization_runner::last_need_px() const { return last_need_px_value; }

double rasterization_runner::last_change_time_sec() const {
    return last_change_time_sec_value;
}

void rasterization_runner::on_need_changed(
    int new_need_px, double pickup_interval_sec, double now_sec, bool is_idle,
    bool over_budget, bool memory_pressure
) {
    const double effective_now
        = std::isfinite(now_sec) ? now_sec : current_time_sec();
    const int need_px = std::max(k_min_short_px, new_need_px);
    const int bucket_need_px = bucketize(need_px);
    const int target_cache_px
        = static_cast<int>(std::ceil(bucket_need_px * k_overscan));

    const int safe_cached_px = std::max(1, cached_short_px_value);
    const double pixel_scale = static_cast<double>(need_px) / safe_cached_px;
    const bool is_urgent_upsize = (pixel_scale > 1.0);

    const double cached_base_px = cached_short_px_value / k_overscan;
    const int cached_base_bucket = static_cast<int>(std::round(cached_base_px));
    int step_jump
        = bucket_index(bucket_need_px) - bucket_index(cached_base_bucket);
    step_jump = std::max(0, step_jump);

    if (target_cache_px > cached_short_px_value) {
        const double delay_sec = calc_upsize_delay_sec(
            pickup_interval_sec, is_urgent_upsize, step_jump, pixel_scale
        );
        schedule_stable_reraster(target_cache_px, delay_sec, effective_now);
        return;
    }

    const int step_drop
        = bucket_index(cached_base_bucket) - bucket_index(bucket_need_px);
    const double waste_ratio
        = static_cast<double>(cached_short_px_value) / target_cache_px;

    if (step_drop >= k_shrink_step_hyst && waste_ratio >= k_shrink_waste_min
        && (is_idle || over_budget || memory_pressure)) {
        const double delay_sec = calc_downsize_delay_sec(
            pickup_interval_sec, step_drop, waste_ratio
        );
        schedule_stable_reraster(target_cache_px, delay_sec, effective_now);
    }
}

void rasterization_runner::cancel_pending() {
    pending_target_px = 0;
    pending_delay_sec_value = 0.0;
    pending_timer.stop();
}

void rasterization_runner::on_clock_tick(qint64, qint64) {
    // Kept for API compatibility with existing signal wiring.
}

double rasterization_runner::clamp(double value, double lo, double hi) {
    return std::min(hi, std::max(lo, value));
}

int rasterization_runner::bucketize(int short_px) {
    int bucket = k_min_short_px;
    while (bucket < short_px) {
        bucket = static_cast<int>(std::round(bucket * k_bucket_k));
    }
    return bucket;
}

int rasterization_runner::bucket_index(int short_px) {
    int bucket = k_min_short_px;
    int index = 0;
    while (bucket < short_px) {
        bucket = static_cast<int>(std::round(bucket * k_bucket_k));
        ++index;
    }
    return index;
}

void rasterization_runner::schedule_stable_reraster(
    int target_cache_px, double delay_sec, double now_sec
) {
    cancel_pending();

    last_change_time_sec_value = now_sec;
    last_need_px_value = target_cache_px;
    pending_target_px = target_cache_px;
    pending_delay_sec_value = delay_sec;
    pending_start_time_sec = now_sec;

    const int delay_ms = static_cast<int>(std::round(delay_sec * 1000.0));
    pending_timer.set_interval(std::max(1, delay_ms));
    pending_timer.start();
}

double rasterization_runner::calc_upsize_delay_sec(
    double pickup_interval_sec, bool is_urgent_upsize, int step_jump,
    double pixel_scale
) const {
    const double base_sec = is_urgent_upsize
        ? clamp(1.0 * pickup_interval_sec, 0.03, 0.35)
        : clamp(2.0 * pickup_interval_sec, 0.12, 0.90);

    const double bonus = 1.0 + 0.8 * step_jump
        + (is_urgent_upsize ? 4.0 * (pixel_scale - 1.0) : 0.0);

    double delay_sec = base_sec / bonus;

    if (is_urgent_upsize) {
        delay_sec = std::min(
            delay_sec,
            std::min(2.0 * pickup_interval_sec, k_upsize_start_max_sec)
        );
    }

    return delay_sec;
}

double rasterization_runner::calc_downsize_delay_sec(
    double pickup_interval_sec, int step_drop, double waste_ratio
) const {
    const double base_sec = clamp(6.0 * pickup_interval_sec, 0.60, 6.00);

    const double bonus = 1.0 + 0.6 * std::max(0, step_drop - k_shrink_step_hyst)
        + 2.0 * std::max(0.0, waste_ratio - k_shrink_waste_min);

    return base_sec / bonus;
}

void rasterization_runner::on_pending_timeout() {
    const int target_px = pending_target_px;
    if (target_px <= 0) {
        return;
    }

    const double now = current_time_sec();
    const double elapsed = now - pending_start_time_sec;
    const double remaining = pending_delay_sec_value - elapsed;
    if (remaining > 0.0) {
        const int remaining_ms
            = std::max(1, static_cast<int>(std::ceil(remaining * 1000.0)));
        pending_timer.set_interval(remaining_ms);
        pending_timer.start();
        return;
    }

    pending_target_px = 0;
    emit rasterization_requested(target_px);
}

double rasterization_runner::current_time_sec() const {
    return static_cast<double>(monotonic_clock.elapsed()) / 1000.0;
}
