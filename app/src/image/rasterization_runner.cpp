#include "image/rasterization_runner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

rasterization_runner::rasterization_runner(QObject* parent)
    : QObject(parent)
    , cached_short_px_value(0)
    , last_need_px_value(0)
    , last_evaluation_value()
    , pending_target_px(0)
    , pending_delay_sec_value(0.0)
    , pending_start_time_sec(0.0)
    , pending_timer()
    , monotonic_clock() {
    pending_timer.setSingleShot(true);
    QObject::connect(
        &pending_timer, &QTimer::timeout, this,
        &rasterization_runner::on_pending_timeout
    );
    monotonic_clock.start();
}

void rasterization_runner::set_cached_short_px(int short_px) {
    cached_short_px_value = std::max(0, short_px);
    if (last_need_px_value <= 0) {
        return;
    }

    const evaluation current
        = evaluate_size_need(last_need_px_value, cached_short_px_value);
    if (!current.rasterization_required) {
        cancel_pending();
    }
}

int rasterization_runner::cached_short_px() const {
    return cached_short_px_value;
}

rasterization_runner::size_window
rasterization_runner::accepted_window() const {
    return accepted_window_for_cached_size(cached_short_px_value);
}

int rasterization_runner::pending_target_cache_px() const {
    return pending_target_px;
}

rasterization_runner::evaluation rasterization_runner::on_need_changed(
    int new_need_px, double pickup_interval_sec, double now_sec, bool is_idle,
    bool over_budget, bool memory_pressure
) {
    const double effective_now
        = std::isfinite(now_sec) ? now_sec : current_time_sec();
    last_evaluation_value
        = evaluate_size_need(new_need_px, cached_short_px_value);
    last_need_px_value = last_evaluation_value.required_short_px;

    if (!last_evaluation_value.rasterization_required) {
        cancel_pending();
        return last_evaluation_value;
    }

    const int target_cache_px = last_evaluation_value.target_cache_px;
    const int safe_cached_px = std::max(1, cached_short_px_value);
    const double pixel_scale
        = static_cast<double>(last_need_px_value) / safe_cached_px;

    if (last_evaluation_value.decision == decision_kind::upsize) {
        const int step_jump = std::max(
            0, bucket_index(last_need_px_value) - bucket_index(safe_cached_px)
        );
        const double delay_sec = calc_upsize_delay_sec(
            pickup_interval_sec, pixel_scale > 1.0, step_jump, pixel_scale
        );
        schedule_stable_reraster(target_cache_px, delay_sec, effective_now);
        return last_evaluation_value;
    }

    const int step_drop = std::max(
        0, bucket_index(safe_cached_px) - bucket_index(last_need_px_value)
    );
    const double waste_ratio
        = static_cast<double>(safe_cached_px) / std::max(1, last_need_px_value);
    double delay_sec
        = calc_downsize_delay_sec(pickup_interval_sec, step_drop, waste_ratio);
    if (memory_pressure) {
        delay_sec = std::min(delay_sec, 0.08);
    } else if (over_budget) {
        delay_sec = std::min(delay_sec, 0.20);
    } else if (is_idle) {
        delay_sec = std::min(delay_sec, 0.35);
    }
    schedule_stable_reraster(target_cache_px, delay_sec, effective_now);
    return last_evaluation_value;
}

rasterization_runner::evaluation
rasterization_runner::request_immediately(int new_need_px, bool force) {
    cancel_pending();
    last_evaluation_value
        = evaluate_size_need(new_need_px, cached_short_px_value, force);
    last_need_px_value = last_evaluation_value.required_short_px;
    if (last_evaluation_value.rasterization_required) {
        emit rasterization_requested(last_evaluation_value.target_cache_px);
    }
    return last_evaluation_value;
}

bool rasterization_runner::size_window::contains(int need_px) const {
    return minimum_need_px > 0 && maximum_need_px >= minimum_need_px
        && need_px >= minimum_need_px && need_px <= maximum_need_px;
}

rasterization_runner::evaluation rasterization_runner::evaluate_size_need(
    int new_need_px, int cached_short_px, bool force
) {
    const int need_px = std::max(k_min_short_px, new_need_px);
    const int safe_cached_px = std::max(0, cached_short_px);
    const size_window window = accepted_window_for_cached_size(safe_cached_px);
    const int desired_target_px = target_cache_px_for_need(need_px);

    if (force) {
        return evaluation {
            .decision = decision_kind::forced,
            .required_short_px = need_px,
            .target_cache_px = desired_target_px,
            .accepted_window = window,
            .rasterization_required = true,
        };
    }

    if (window.contains(need_px)) {
        return evaluation {
            .decision = decision_kind::reuse,
            .required_short_px = need_px,
            .target_cache_px = safe_cached_px,
            .accepted_window = window,
            .rasterization_required = false,
        };
    }

    const decision_kind decision
        = safe_cached_px <= 0 || need_px > window.maximum_need_px
        ? decision_kind::upsize
        : decision_kind::downsize;

    return evaluation {
        .decision = decision,
        .required_short_px = need_px,
        .target_cache_px = desired_target_px,
        .accepted_window = window,
        .rasterization_required = true,
    };
}

rasterization_runner::size_window
rasterization_runner::accepted_window_for_cached_size(int cached_short_px) {
    if (cached_short_px <= 0) {
        return {};
    }

    return size_window {
        .minimum_need_px = std::max(
            1,
            static_cast<int>(std::ceil(
                static_cast<double>(cached_short_px) / k_max_cache_need_ratio
            ))
        ),
        .maximum_need_px = cached_short_px,
    };
}

int rasterization_runner::target_cache_px_for_need(int need_px) {
    const int bucket_need_px = bucketize(std::max(k_min_short_px, need_px));
    const double target
        = std::ceil(static_cast<double>(bucket_need_px) * k_target_headroom);
    return target >= static_cast<double>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(target);
}

void rasterization_runner::cancel_pending() {
    pending_target_px = 0;
    pending_delay_sec_value = 0.0;
    pending_timer.stop();
}

double rasterization_runner::clamp(double value, double lo, double hi) {
    return std::min(hi, std::max(lo, value));
}

int rasterization_runner::bucketize(int short_px) {
    int bucket = k_min_short_px;
    while (bucket < short_px) {
        const double next_value
            = std::round(static_cast<double>(bucket) * k_bucket_k);
        if (next_value
            >= static_cast<double>(std::numeric_limits<int>::max())) {
            return std::numeric_limits<int>::max();
        }
        bucket = std::max(bucket + 1, static_cast<int>(next_value));
    }
    return bucket;
}

int rasterization_runner::bucket_index(int short_px) {
    int bucket = k_min_short_px;
    int index = 0;
    while (bucket < short_px) {
        const double next_value
            = std::round(static_cast<double>(bucket) * k_bucket_k);
        if (next_value
            >= static_cast<double>(std::numeric_limits<int>::max())) {
            break;
        }
        bucket = std::max(bucket + 1, static_cast<int>(next_value));
        ++index;
    }
    return index;
}

void rasterization_runner::schedule_stable_reraster(
    int target_cache_px, double delay_sec, double now_sec
) {
    cancel_pending();

    pending_target_px = target_cache_px;
    pending_delay_sec_value = delay_sec;
    pending_start_time_sec = now_sec;

    const int delay_ms = static_cast<int>(std::round(delay_sec * 1000.0));
    pending_timer.setInterval(std::max(1, delay_ms));
    pending_timer.start();
}

double rasterization_runner::calc_upsize_delay_sec(
    double pickup_interval_sec, bool is_urgent_upsize, int step_jump,
    double pixel_scale
) {
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
) {
    const double base_sec = clamp(4.0 * pickup_interval_sec, 0.35, 2.50);

    const double bonus = 1.0 + 0.35 * std::max(0, step_drop - 1)
        + 0.8 * std::max(0.0, waste_ratio - 1.5);

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
        pending_timer.setInterval(remaining_ms);
        pending_timer.start();
        return;
    }

    pending_target_px = 0;
    emit rasterization_requested(target_px);
}

double rasterization_runner::current_time_sec() const {
    return static_cast<double>(monotonic_clock.elapsed()) / 1000.0;
}
