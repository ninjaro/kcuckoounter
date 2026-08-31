#include "shell/main_window.hpp"

#ifndef KC_KDE

#include "arch/android_ui.hpp"
#include "arch/str_label.hpp"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QSlider>
#include <QToolBar>

namespace {

bool is_primary_shell_action(const QString& action_name) {
    return action_name == QStringLiteral("game_new")
        || action_name == QStringLiteral("game_start_pause")
        || action_name == QStringLiteral("game_finish")
        || action_name == QStringLiteral("game_settings")
        || action_name == QStringLiteral("game_progress");
}

} // namespace

void main_window::setup_platform_shell() {
#if defined(Q_OS_ANDROID)
    menuBar()->hide();
    primary_toolbar = new BaseToolBar(str_label("Navigation"), this);
    primary_toolbar->setObjectName(QStringLiteral("mobile_navigation_toolbar"));
    primary_toolbar->setAllowedAreas(Qt::BottomToolBarArea);
    primary_toolbar->setFloatable(false);
    primary_toolbar->setMovable(false);
    android_ui::apply_toolbar_style(primary_toolbar);
    addToolBar(Qt::BottomToolBarArea, primary_toolbar);
#else
    game_menu = menuBar()->addMenu(str_label("&Game"));
    settings_menu = menuBar()->addMenu(str_label("&Settings"));

    primary_toolbar = new BaseToolBar(str_label("Main"), this);
    primary_toolbar->setObjectName(QStringLiteral("main_toolbar"));
    primary_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    android_ui::apply_toolbar_style(primary_toolbar);
    addToolBar(primary_toolbar);
#endif
}

void main_window::finalize_platform_shell() {
#if defined(Q_OS_ANDROID)
    return;
#else
    if (game_menu != nullptr) {
        game_menu->addSeparator();
        auto* quit_action = game_menu->addAction(str_label("Quit"));
        quit_action->setShortcut(QKeySequence::Quit);
        QObject::connect(
            quit_action, &QAction::triggered, QCoreApplication::instance(),
            &QCoreApplication::quit
        );
    }
#endif
}

void main_window::setup_status_surface(BaseVBoxLayout* main_layout) {
    if (main_layout == nullptr) {
        return;
    }

    auto* status_strip = new BaseWidget(this);
#if defined(Q_OS_ANDROID)
    auto* strip_layout = new QVBoxLayout;
    strip_layout->setContentsMargins(8, 4, 8, 4);
#else
    auto* strip_layout = new QHBoxLayout;
    strip_layout->setContentsMargins(0, 0, 0, 0);
#endif
    strip_layout->setSpacing(8);

    status_label = new QLabel(status_strip);
    status_label->setText(QString());
#if defined(Q_OS_ANDROID)
    status_label->setWordWrap(true);
#endif
    strip_layout->addWidget(status_label, 1);

    raster_progress = new QProgressBar(status_strip);
    raster_progress->setTextVisible(false);
    raster_progress->setRange(0, 0);
    raster_progress->setVisible(false);
#if !defined(Q_OS_ANDROID)
    raster_progress->setFixedWidth(120);
#endif
    strip_layout->addWidget(raster_progress);

#if defined(Q_OS_ANDROID)
    auto* speed_row = new QHBoxLayout;
    speed_row->setContentsMargins(0, 0, 0, 0);
    speed_row->setSpacing(8);
#endif
    pickup_interval_label = new QLabel(status_strip);
    pickup_interval_label->setText(QString());
#if defined(Q_OS_ANDROID)
    pickup_interval_label->setAccessibleName(str_label("Card pickup speed"));
    speed_row->addWidget(pickup_interval_label);
#else
    strip_layout->addWidget(pickup_interval_label);
#endif

    speed_slider = new QSlider(Qt::Horizontal, status_strip);
    speed_slider->setRange(100, 1000);
    speed_slider->setValue(300);
    speed_slider->setToolTip(str_label("Card pickup interval (ms)"));
#if !defined(Q_OS_ANDROID)
    speed_slider->setFixedWidth(180);
#endif
    android_ui::apply_slider_style(speed_slider);
#if defined(Q_OS_ANDROID)
    speed_row->addWidget(speed_slider, 1);
    strip_layout->addLayout(speed_row);
#else
    strip_layout->addWidget(speed_slider);
#endif

    status_strip->setLayout(strip_layout);
    main_layout->addWidget(status_strip);
}

void main_window::register_shell_action(
    BaseAction* action, const QString& action_name
) {
    if (action == nullptr) {
        return;
    }

#if defined(Q_OS_ANDROID)
    if (primary_toolbar != nullptr && is_primary_shell_action(action_name)) {
        primary_toolbar->addAction(action);
    }
    return;
#else
    if (primary_toolbar != nullptr && is_primary_shell_action(action_name)) {
        primary_toolbar->addAction(action);
    }
    if (action_name == QStringLiteral("game_settings")) {
        if (settings_menu != nullptr) {
            settings_menu->addAction(action);
        }
        return;
    }
    if (action_name.startsWith(QStringLiteral("game_"))) {
        if (game_menu != nullptr) {
            game_menu->addAction(action);
        }
        return;
    }
#endif
}

void main_window::insert_shell_separator() { }

void main_window::refresh_clock_label() const {
    if (clock_timer != nullptr && clock_label != nullptr) {
        clock_label->setText(clock_timer->time_string_hh_mm_ss());
    }
}

void main_window::record_platform_score() { }

void main_window::show_platform_highscores() { }

#endif
