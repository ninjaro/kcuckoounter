#include "arch/time_interface.hpp"

time_interface::time_interface(QObject* parent)
    : base_clock(parent) { }

time_interface::~time_interface() = default;
