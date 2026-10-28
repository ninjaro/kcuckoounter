#include "shell/main_window.hpp"

#ifdef KC_KDE

#include "arch/str_label.hpp"
#include "table/table.hpp"

#include <KActionCollection>
#include <KCommandBar>
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
    if (new_game_action != nullptr) {
        game_menu->addAction(new_game_action);
    }
    if (start_pause_action != nullptr) {
        game_menu->addAction(start_pause_action);
    }
    if (finish_action != nullptr) {
        game_menu->addAction(finish_action);
    }
    if (gameplay_review_action != nullptr)
        game_menu->addAction(gameplay_review_action);
    if (progress_action)
        game_menu->addAction(progress_action);
    if (saved_drills_action != nullptr) {
        game_menu->addAction(saved_drills_action);
    }
    if (gameplay_retry_action != nullptr)
        game_menu->addAction(gameplay_retry_action);
    game_menu->addSeparator();

    auto* quit_action
        = KStandardAction::quit(this, [this] { close(); }, actionCollection());
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
    // Our menus/toolbars are built manually, not by an XMLGUI factory. Keep
    // KDE's launcher/default shortcut, but give its native selector the actual
    // shared actions instead of an unregistered factory's client collections.
    if (auto* command
        = actionCollection()->action(QStringLiteral("open_kcommand_bar"))) {
        QObject::disconnect(command, &QAction::triggered, this, nullptr);
        connect(command, &QAction::triggered, this, [this] {
            if (!isCommandBarEnabled())
                return;
            auto* bar = new KCommandBar(this);
            bar->setActions(
                { { windowTitle(), actionCollection()->actions() } }
            );
            bar->show();
        });
        help_menu->menu()->addAction(command);
    }
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

void main_window::refresh_clock_label() const {
    if (clock_label)
        clock_label->setText(table_widget->gameplay_clock_text());
}

#endif
