#include "shell/main_window.hpp"

#ifdef KC_KDE

#include "arch/str_label.hpp"
#include "table/table.hpp"

#include <KActionCollection>
#include <KGameClock>
#include <KGameHighScoreDialog>
#include <KGameStandardAction>
#include <KHelpMenu>
#include <KShortcutsDialog>
#include <KStandardAction>
#include <KStandardShortcut>

#include <QByteArray>
#include <QCoreApplication>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
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
    int position, int score_correct, int score_total,
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

namespace {

bool is_primary_shell_action(const QString& action_name) {
    return action_name == QStringLiteral("game_new")
        || action_name == QStringLiteral("game_start_pause")
        || action_name == QStringLiteral("game_finish")
        || action_name == QStringLiteral("game_settings");
}

void preserve_action_shortcut_defaults(KActionCollection* collection) {
    if (collection == nullptr) {
        return;
    }
    for (QAction* action : collection->actions()) {
        if (action == nullptr
            || !KActionCollection::defaultShortcuts(action).isEmpty()
            || action->shortcuts().isEmpty()) {
            continue;
        }
        KActionCollection::setDefaultShortcuts(action, action->shortcuts());
    }
}

} // namespace

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

    if (settings_action != nullptr) {
        BaseAction* previous_settings_action = settings_action;
        settings_action = KStandardAction::preferences(
            this, &main_window::on_settings_triggered, this
        );
        settings_action->setToolTip(previous_settings_action->toolTip());
        if (primary_toolbar != nullptr) {
            primary_toolbar->insertAction(
                previous_settings_action, settings_action
            );
        }
        actionCollection()->takeAction(previous_settings_action);
        delete previous_settings_action;
        actionCollection()->addAction(
            QStringLiteral("game_settings"), settings_action
        );
    }

    game_menu = window_menu_bar->addMenu(str_label("&Game"));
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
    if (new_gameplay_action != nullptr)
        game_menu->addAction(new_gameplay_action);
    if (start_pause_action != nullptr) {
        game_menu->addAction(start_pause_action);
    }
    if (finish_action != nullptr) {
        game_menu->addAction(finish_action);
    }
    if (gameplay_review_action != nullptr)
        game_menu->addAction(gameplay_review_action);
    if (saved_drills_action != nullptr) {
        game_menu->addAction(saved_drills_action);
    }
    if (highscores_action != nullptr) {
        game_menu->addAction(highscores_action);
    }
    game_menu->addSeparator();

    auto* quit_action = KStandardAction::quit(
        QCoreApplication::instance(), &QCoreApplication::quit,
        actionCollection()
    );
    if (quit_action != nullptr) {
        actionCollection()->addAction(QStringLiteral("file_quit"), quit_action);
        game_menu->addAction(quit_action);
    }

    settings_menu = window_menu_bar->addMenu(str_label("&Settings"));
    if (settings_action != nullptr) {
        KActionCollection::setDefaultShortcuts(
            settings_action, KStandardShortcut::preferences()
        );
        settings_menu->addAction(settings_action);
    }
    if (primary_toolbar != nullptr) {
        auto* toolbar_action = primary_toolbar->toggleViewAction();
        toolbar_action->setText(str_label("Show toolbar"));
        actionCollection()->addAction(
            QStringLiteral("view_main_toolbar"), toolbar_action
        );
        settings_menu->addAction(toolbar_action);
    }
    if (window_status_bar != nullptr) {
        auto* status_bar_action = KStandardAction::showStatusbar(
            window_status_bar, &QStatusBar::setVisible, actionCollection()
        );
        if (status_bar_action != nullptr) {
            actionCollection()->addAction(
                QStringLiteral("view_statusbar"), status_bar_action
            );
            // The main window has not been shown yet. Effective visibility is
            // false even when the status bar has not been explicitly hidden.
            status_bar_action->setChecked(!window_status_bar->isHidden());
            settings_menu->addAction(status_bar_action);
        }
    }
    settings_menu->addSeparator();
    auto* configure_shortcuts_action = KStandardAction::keyBindings(
        this,
        [this]() {
            KShortcutsDialog::showDialog(
                actionCollection(), KShortcutsEditor::LetterShortcutsAllowed,
                this
            );
        },
        actionCollection()
    );
    if (configure_shortcuts_action != nullptr) {
        actionCollection()->addAction(
            QStringLiteral("settings_configure_shortcuts"),
            configure_shortcuts_action
        );
        settings_menu->addAction(configure_shortcuts_action);
    }

    auto* help_menu = new KHelpMenu(this);
    help_menu->setShowWhatsThis(true);
    window_menu_bar->addMenu(help_menu->menu());

    setCommandBarEnabled(true);
    actionCollection()->setConfigGroup(QStringLiteral("Shortcuts"));
    preserve_action_shortcut_defaults(actionCollection());
    actionCollection()->readSettings();
}

void main_window::setup_status_surface(BaseVBoxLayout* main_layout) {
    Q_UNUSED(main_layout);

    auto* window_status_bar = statusBar();
    if (window_status_bar == nullptr) {
        return;
    }

    // KMainWindow's single-line status bar is vertically Fixed. Let its native
    // layout use the strip's height-for-width when controls need another row.
    auto policy = window_status_bar->sizePolicy();
    policy.setVerticalPolicy(QSizePolicy::Preferred);
    window_status_bar->setSizePolicy(policy);
    window_status_bar->addPermanentWidget(create_desktop_status_surface(), 1);

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
    if (primary_toolbar != nullptr && is_primary_shell_action(action_name)) {
        primary_toolbar->addAction(action);
    }
}

void main_window::insert_shell_separator() { }

void main_window::refresh_clock_label() const {
    if (table_widget && table_widget->active_gameplay_session()) {
        if (clock_label)
            clock_label->setText(table_widget->gameplay_clock_text());
        return;
    }
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

    score_dialog.setComment(
        main_window_kde_score_support::recorded_score_comment(
            highscore_position, score_correct, score_total, clock_timer
        )
    );
    score_dialog.exec();
}

void main_window::show_platform_highscores() {
    KGameHighScoreDialog score_dialog(KGameHighScoreDialog::Name, this);
    main_window_kde_score_support::configure_highscore_dialog(score_dialog);
    score_dialog.exec();
}

#endif
