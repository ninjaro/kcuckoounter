#include "arch/base_clock.hpp"

#include "arch/str_label.hpp"

#include <QElapsedTimer>
#include <QTime>
#include <QTimer>

#include <utility>

struct base_clock::clock_state {
    QTimer timer;
    QElapsedTimer elapsed_timer;
    qint64 elapsed_ms = 0;
    qint64 last_tick_ms = 0;
    bool running = false;
    bool elapsed_active = false;
    bool single_shot = false;
};

base_clock::base_clock(QObject* parent)
    : QObject(parent)
    , state(std::make_unique<clock_state>()) {
    state->timer.setInterval(100);
    QObject::connect(
        &state->timer, &QTimer::timeout, this, &base_clock::on_timeout
    );
}

base_clock::~base_clock() = default;

void base_clock::set_interval(int interval_ms) {
    state->timer.setInterval(interval_ms);
}

void base_clock::set_single_shot(bool single_shot) {
    state->single_shot = single_shot;
    state->timer.setSingleShot(single_shot);
}

void base_clock::start(bool emit_immediately) {
    if (state->running) {
        return;
    }
    state->running = true;
    state->elapsed_active = true;
    state->elapsed_timer.start();
    state->timer.start();
    if (emit_immediately) {
        on_timeout();
    }
}

void base_clock::pause() {
    if (!state->running) {
        return;
    }
    state->elapsed_ms += state->elapsed_timer.elapsed();
    state->running = false;
    state->elapsed_active = false;
    state->timer.stop();
    on_timeout();
}

void base_clock::stop() {
    state->timer.stop();
    state->running = false;
    state->elapsed_active = false;
}

void base_clock::reset() {
    state->elapsed_ms = 0;
    state->last_tick_ms = 0;
    state->running = false;
    state->elapsed_active = false;
    state->timer.stop();
    on_timeout();
}

bool base_clock::is_active() const { return state->timer.isActive(); }

void base_clock::restart_elapsed() {
    state->elapsed_ms = 0;
    state->last_tick_ms = 0;
    state->running = false;
    state->elapsed_active = true;
    state->elapsed_timer.start();
}

qint64 base_clock::elapsed_time_ms() const {
    qint64 total_ms = state->elapsed_ms;
    if (state->running || state->elapsed_active) {
        total_ms += state->elapsed_timer.elapsed();
    }
    return total_ms;
}

double base_clock::elapsed_time_sec() const {
    return static_cast<double>(elapsed_time_ms()) / 1000.0;
}

QString base_clock::time_string_mm_ss() const {
    const qint64 total_ms = elapsed_time_ms();
    auto display_time = QTime(0, 0).addMSecs(static_cast<int>(total_ms));
    return display_time.toString(str_label("mm:ss"));
}

QString base_clock::time_string_hh_mm_ss() const {
    const qint64 total_ms = elapsed_time_ms();
    auto display_time = QTime(0, 0).addMSecs(static_cast<int>(total_ms));
    return display_time.toString(str_label("hh:mm:ss"));
}

void base_clock::single_shot(
    const int interval_ms, QObject* context, std::function<void()> handler
) {
    QTimer::singleShot(interval_ms, context, std::move(handler));
}

void base_clock::on_timeout() {
    emit timeout();

    const qint64 total_ms = elapsed_time_ms();
    const qint64 delta_ms = total_ms - state->last_tick_ms;
    state->last_tick_ms = total_ms;
    emit ticked(total_ms, delta_ms);
    emit ticked_seconds(
        static_cast<double>(total_ms) / 1000.0,
        static_cast<double>(delta_ms) / 1000.0
    );
}
