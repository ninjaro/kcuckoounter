#ifndef KCUCKOOUNTER_DESKTOP_STATUS_STRIP_HPP
#define KCUCKOOUNTER_DESKTOP_STATUS_STRIP_HPP

#include <shared_ui/qt/status_strip.hpp>

// Host policy only: pickup limits, accessible control names, the platform's
// clock visibility and the selected HUD/material combination.
class desktop_status_strip final : public shared_ui::qt::status_strip {
public:
    explicit desktop_status_strip(QWidget* parent);
    void set_instruments(bool enabled);
};

#endif
