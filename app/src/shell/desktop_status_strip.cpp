#include "shell/desktop_status_strip.hpp"

#include "arch/str_label.hpp"
#include "settings/preferences.hpp"

#include <shared_ui/qt/presentation.hpp>

#include <QLabel>
#include <QProgressBar>
#include <QSlider>

// The shared strip never selects a game speed, a range or a progress policy.
desktop_status_strip::desktop_status_strip(QWidget* parent)
    : shared_ui::qt::status_strip(parent) {
    setObjectName(QStringLiteral("desktop_status_surface"));
    progress_bar()->setRange(0, 0);
    progress_bar()->setAccessibleName(str_label("Preparing card images"));
    auto* slider = control_slider();
    slider->setRange(
        trainer_preferences::minimum_pickup_interval_ms,
        trainer_preferences::maximum_pickup_interval_ms
    );
    slider->setValue(300);
    slider->setTickInterval(100);
#ifdef KC_KDE
    slider->setMaximumWidth(160);
#else
    slider->setMaximumWidth(180);
#endif
    slider->setAccessibleName(str_label("Card pickup interval (ms)"));
    slider->setToolTip(str_label("Card pickup interval (ms)"));
    trailing_label()->setText(str_label("00:00:00"));
    trailing_label()->setObjectName(QStringLiteral("session_clock"));
#ifndef KC_KDE
    trailing_label()->hide();
#endif
}

void desktop_status_strip::set_instruments(bool enabled) {
    auto value = shared_ui::qt::presentation_for(*parentWidget());
    if (enabled)
        value.value = shared_ui::qt::material::felt;
    shared_ui::qt::apply_presentation(
        *this, value,
        enabled ? shared_ui::qt::presentation_surface::instruments
                : shared_ui::qt::presentation_surface::window
    );
    set_variant(
        enabled ? shared_ui::qt::status_strip_variant::instruments
                : shared_ui::qt::status_strip_variant::classic
    );
#ifndef KC_KDE
    trailing_label()->setVisible(enabled);
#endif
}
