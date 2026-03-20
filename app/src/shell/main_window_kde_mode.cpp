#include "shell/main_window.hpp"

#ifdef KC_KDE

#include "arch/str_label.hpp"

#include <KActionCollection>
#include <KGameClock>
#include <KGameHighScoreDialog>
#include <KGameStandardAction>
#include <KStandardAction>

#include <QByteArray>
#include <QCoreApplication>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QSlider>
#include <QStatusBar>

namespace main_window_kde_score_support {

QPair<QByteArray, QString> score_config_group() {
    return QPair<QByteArray, QString>(
        QByteArrayLiteral("overall"), str_label("Overall")
    );
}

QString answered_text(int score_correct, int score_total) {
    return QStringLiteral("%1/%2").arg(score_correct).arg(score_total);
}

QString accuracy_text(int score_correct, int score_total) {
    if (score_total <= 0) {
        return QStringLiteral("0%");
    }

    const int rounded_percent
        = (score_correct * 100 + (score_total / 2)) / score_total;
    return QStringLiteral("%1%").arg(rounded_percent);
}

QString elapsed_time_text(const BaseClock* clock_timer) {
    if (clock_timer == nullptr) {
        return str_label("00:00:00");
    }

    return clock_timer->time_string_hh_mm_ss();
}

QString score_comment(
    int score_correct, int score_total, const BaseClock* clock_timer
) {
    return str_label("Latest result: %1  Accuracy: %2  Time: %3")
        .arg(answered_text(score_correct, score_total))
        .arg(accuracy_text(score_correct, score_total))
        .arg(elapsed_time_text(clock_timer));
}

QString recorded_score_comment(
    int position,
    int score_correct,
    int score_total,
    const BaseClock* clock_timer
) {
    return str_label("Recorded at #%1  %2")
        .arg(position)
        .arg(score_comment(score_correct, score_total, clock_timer));
}

void configure_highscore_dialog(KGameHighScoreDialog& score_dialog) {
    score_dialog.setConfigGroup(score_config_group());
    score_dialog.addField(
        KGameHighScoreDialog::Custom1, str_label("Answered"),
        QStringLiteral("answered")
    );
    score_dialog.addField(
        KGameHighScoreDialog::Custom2, str_label("Accuracy"),
        QStringLiteral("accuracy")
    );
    score_dialog.addField(
        KGameHighScoreDialog::Custom3, str_label("Time"),
        QStringLiteral("elapsed_time")
    );
}

} // namespace main_window_kde_score_support

void main_window::setup_platform_shell() {
    primary_toolbar = new BaseToolBar(str_label("Main"), this);
    primary_toolbar->setObjectName(QStringLiteral("main_toolbar"));
    primary_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addToolBar(primary_toolbar);
}

void main_window::finalize_platform_shell() {
    auto* window_menu_bar = menuBar();
    auto* window_status_bar = statusBar();
    if (window_menu_bar == nullptr || actionCollection() == nullptr) {
        return;
    }

    window_menu_bar->clear();

    auto* game_menu = window_menu_bar->addMenu(str_label("&Game"));
    if (highscores_action == nullptr) {
        highscores_action = KGameStandardAction::highscores(
            this, &main_window::on_show_highscores_triggered, this
        );
        actionCollection()->addAction(
            QStringLiteral("game_highscores"), highscores_action
        );
    }
    if (new_game_action != nullptr) {
        game_menu->addAction(new_game_action);
    }
    if (start_pause_action != nullptr) {
        game_menu->addAction(start_pause_action);
    }
    if (finish_action != nullptr) {
        game_menu->addAction(finish_action);
    }
    if (highscores_action != nullptr) {
        game_menu->addAction(highscores_action);
    }
    game_menu->addSeparator();

    auto* quit_action = KStandardAction::quit(
        QCoreApplication::instance(), &QCoreApplication::quit, actionCollection()
    );
    if (quit_action != nullptr) {
        game_menu->addAction(quit_action);
    }

    auto* settings_menu = window_menu_bar->addMenu(str_label("&Settings"));
    if (settings_action != nullptr) {
        settings_menu->addAction(settings_action);
    }
    if (primary_toolbar != nullptr) {
        auto* toolbar_action = primary_toolbar->toggleViewAction();
        toolbar_action->setText(str_label("Show toolbar"));
        settings_menu->addAction(toolbar_action);
    }
    if (window_status_bar != nullptr) {
        auto* status_bar_action = KStandardAction::showStatusbar(
            window_status_bar, &QStatusBar::setVisible, actionCollection()
        );
        if (status_bar_action != nullptr) {
            status_bar_action->setChecked(window_status_bar->isVisible());
            settings_menu->addAction(status_bar_action);
        }
    }

    if (export_debug_snapshot_action != nullptr
        || export_process_memory_report_action != nullptr
        || add_monitor_marker_action != nullptr
        || toggle_debug_broadcaster_action != nullptr
        || realistic_cadence_mode_action != nullptr
        || instrumented_cadence_mode_action != nullptr) {
        auto* debug_menu = window_menu_bar->addMenu(str_label("&Debug"));
        if (export_debug_snapshot_action != nullptr) {
            debug_menu->addAction(export_debug_snapshot_action);
        }
        if (export_process_memory_report_action != nullptr) {
            debug_menu->addAction(export_process_memory_report_action);
        }
        if (add_monitor_marker_action != nullptr) {
            debug_menu->addAction(add_monitor_marker_action);
        }
        if (toggle_debug_broadcaster_action != nullptr) {
            debug_menu->addSeparator();
            debug_menu->addAction(toggle_debug_broadcaster_action);
        }
        if (realistic_cadence_mode_action != nullptr
            || instrumented_cadence_mode_action != nullptr) {
            debug_menu->addSeparator();
        }
        if (realistic_cadence_mode_action != nullptr) {
            debug_menu->addAction(realistic_cadence_mode_action);
        }
        if (instrumented_cadence_mode_action != nullptr) {
            debug_menu->addAction(instrumented_cadence_mode_action);
        }
    }
}

void main_window::setup_status_surface(BaseVBoxLayout* main_layout) {
    Q_UNUSED(main_layout);

    auto* window_status_bar = statusBar();
    if (window_status_bar == nullptr) {
        return;
    }

    status_label = new QLabel(this);
    status_label->setText(QString());
    window_status_bar->addPermanentWidget(status_label, 1);

    raster_progress = new QProgressBar(this);
    raster_progress->setTextVisible(false);
    raster_progress->setRange(0, 0);
    raster_progress->setVisible(false);
    raster_progress->setFixedWidth(120);
    window_status_bar->addPermanentWidget(raster_progress);

    pickup_interval_label = new QLabel(this);
    pickup_interval_label->setText(QString());
    window_status_bar->addPermanentWidget(pickup_interval_label);

    speed_slider = new QSlider(Qt::Horizontal, this);
    speed_slider->setRange(100, 1000);
    speed_slider->setValue(300);
    speed_slider->setToolTip(str_label("Card pickup interval (ms)"));
    speed_slider->setFixedWidth(160);
    window_status_bar->addPermanentWidget(speed_slider);

    clock_label = new QLabel(this);
    clock_label->setText(str_label("00:00:00"));
    window_status_bar->addPermanentWidget(clock_label);

    kde_clock = new KGameClock(this, KGameClock::HourMinSec);
    QObject::connect(
        kde_clock, &KGameClock::timeChanged, clock_label, &QLabel::setText
    );
}

void main_window::register_shell_action(
    BaseAction* action, const QString& action_name
) {
    if (action == nullptr) {
        return;
    }

    if (actionCollection() != nullptr) {
        actionCollection()->addAction(action_name, action);
    }
    if (primary_toolbar != nullptr) {
        primary_toolbar->addAction(action);
    }
}

void main_window::insert_shell_separator() {
    if (primary_toolbar != nullptr) {
        primary_toolbar->addSeparator();
    }
}

void main_window::refresh_clock_label() const {
    if (clock_timer == nullptr || clock_label == nullptr) {
        return;
    }

    if (kde_clock == nullptr) {
        clock_label->setText(clock_timer->time_string_hh_mm_ss());
        return;
    }

    const auto elapsed_seconds
        = static_cast<uint>(clock_timer->elapsed_time_ms() / 1000);
    kde_clock->setTime(elapsed_seconds);
    kde_clock->showTime();
}

void main_window::record_platform_score() {
    if (score_total <= 0) {
        return;
    }

    KGameHighScoreDialog score_dialog(KGameHighScoreDialog::Name, this);
    main_window_kde_score_support::configure_highscore_dialog(score_dialog);

    KGameHighScoreDialog::FieldInfo score_info;
    score_info[KGameHighScoreDialog::Score] = QString::number(score_correct);
    score_info[KGameHighScoreDialog::Custom1]
        = main_window_kde_score_support::answered_text(
            score_correct, score_total
        );
    score_info[KGameHighScoreDialog::Custom2]
        = main_window_kde_score_support::accuracy_text(
            score_correct, score_total
        );
    score_info[KGameHighScoreDialog::Custom3]
        = main_window_kde_score_support::elapsed_time_text(clock_timer);

    const int highscore_position
        = score_dialog.addScore(score_info, KGameHighScoreDialog::AskName);
    if (highscore_position <= 0) {
        return;
    }

    score_dialog.setComment(main_window_kde_score_support::recorded_score_comment(
        highscore_position, score_correct, score_total, clock_timer
    ));
    score_dialog.exec();
}

void main_window::show_platform_highscores() {
    KGameHighScoreDialog score_dialog(KGameHighScoreDialog::Name, this);
    main_window_kde_score_support::configure_highscore_dialog(score_dialog);
    score_dialog.exec();
}

#endif
