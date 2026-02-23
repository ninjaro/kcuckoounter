#ifndef KCUCKOOUNTER_HELPERS_TIME_INTERFACE_HPP
#define KCUCKOOUNTER_HELPERS_TIME_INTERFACE_HPP

#include "helpers/base_clock.hpp"

class time_interface : public BaseClock {
    Q_OBJECT

public:
    explicit time_interface(QObject* parent = nullptr);
    ~time_interface() override;

    time_interface(const time_interface&) = delete;
    time_interface& operator=(const time_interface&) = delete;
    time_interface(time_interface&&) = delete;
    time_interface& operator=(time_interface&&) = delete;

    using BaseClock::elapsed_time_ms;
    using BaseClock::elapsed_time_sec;
    using BaseClock::is_active;
    using BaseClock::pause;
    using BaseClock::reset;
    using BaseClock::restart_elapsed;
    using BaseClock::set_interval;
    using BaseClock::set_single_shot;
    using BaseClock::single_shot;
    using BaseClock::start;
    using BaseClock::stop;
    using BaseClock::time_string_hh_mm_ss;
    using BaseClock::time_string_mm_ss;
};

#endif // KCUCKOOUNTER_HELPERS_TIME_INTERFACE_HPP
