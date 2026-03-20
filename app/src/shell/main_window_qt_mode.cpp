#include "shell/main_window.hpp"

#ifndef KC_KDE

#include "arch/android_ui.hpp"
#include "arch/str_label.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QSlider>
#include <QToolBar>

void main_window::setup_platform_shell() {
    primary_toolbar = new BaseToolBar(str_label("Main"), this);
    primary_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    android_ui::apply_toolbar_style(primary_toolbar);
    addToolBar(primary_toolbar);
}

void main_window::finalize_platform_shell() { }

void main_window::setup_status_surface(BaseVBoxLayout* main_layout) {
    if (main_layout == nullptr) {
        return;
    }

    auto* status_strip = new BaseWidget(this);
    auto* strip_layout = new QHBoxLayout;
    strip_layout->setContentsMargins(0, 0, 0, 0);
    strip_layout->setSpacing(8);

    status_label = new QLabel(status_strip);
    status_label->setText(QString());
    strip_layout->addWidget(status_label, 1);

    raster_progress = new QProgressBar(status_strip);
    raster_progress->setTextVisible(false);
    raster_progress->setRange(0, 0);
    raster_progress->setVisible(false);
    raster_progress->setFixedWidth(120);
    strip_layout->addWidget(raster_progress);

    pickup_interval_label = new QLabel(status_strip);
    pickup_interval_label->setText(QString());
    strip_layout->addWidget(pickup_interval_label);

    speed_slider = new QSlider(Qt::Horizontal, status_strip);
    speed_slider->setRange(100, 1000);
    speed_slider->setValue(300);
    speed_slider->setToolTip(str_label("Card pickup interval (ms)"));
    speed_slider->setFixedWidth(180);
    android_ui::apply_slider_style(speed_slider);
    strip_layout->addWidget(speed_slider);

    status_strip->setLayout(strip_layout);
    main_layout->addWidget(status_strip);
}

void main_window::register_shell_action(
    BaseAction* action, const QString& action_name
) {
    Q_UNUSED(action_name);

    if (action == nullptr || primary_toolbar == nullptr) {
        return;
    }

    primary_toolbar->addAction(action);
}

void main_window::insert_shell_separator() {
    if (primary_toolbar != nullptr) {
        primary_toolbar->addSeparator();
    }
}

void main_window::refresh_clock_label() const {
    if (clock_timer != nullptr && clock_label != nullptr) {
        clock_label->setText(clock_timer->time_string_hh_mm_ss());
    }
}

void main_window::record_platform_score() { }

void main_window::show_platform_highscores() { }

#endif
