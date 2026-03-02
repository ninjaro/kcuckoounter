#ifndef KCUCKOOUNTER_ARCH_TIME_INTERFACE_HPP
#define KCUCKOOUNTER_ARCH_TIME_INTERFACE_HPP

#include "arch/base_clock.hpp"

class time_interface : public base_clock {
    Q_OBJECT

public:
    explicit time_interface(QObject* parent = nullptr);
    ~time_interface() override;

    time_interface(const time_interface&) = delete;
    time_interface& operator=(const time_interface&) = delete;
    time_interface(time_interface&&) = delete;
    time_interface& operator=(time_interface&&) = delete;

    using base_clock::elapsed_time_ms;
    using base_clock::elapsed_time_sec;
    using base_clock::is_active;
    using base_clock::pause;
    using base_clock::reset;
    using base_clock::restart_elapsed;
    using base_clock::set_interval;
    using base_clock::set_single_shot;
    using base_clock::single_shot;
    using base_clock::start;
    using base_clock::stop;
    using base_clock::time_string_hh_mm_ss;
    using base_clock::time_string_mm_ss;
};

#endif // KCUCKOOUNTER_ARCH_TIME_INTERFACE_HPP
