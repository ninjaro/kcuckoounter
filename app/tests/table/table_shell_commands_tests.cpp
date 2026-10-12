// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#ifdef KC_KDE
#include "shell/main_window.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <KActionCollection>
#include <KCommandBar>
#include <KShortcutsDialog>

#include <QDialog>
#include <QLineEdit>
#include <QMenu>
#include <QStatusBar>
#include <QTreeView>
#endif
#include <QtTest/QtTest>

void table_tests::kde_commands_use_shared_actions() {
#if !defined(KC_KDE) || defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("KDE command search is a native-only extension");
#else
    using namespace table_test;
    launcher_preferences_guard preferences;
    const auto shell = load_desktop_shell_state();

    struct restore_shell {
        desktop_shell_state state;

        ~restore_shell() { save_desktop_shell_state(state); }
    } guard { shell };

    save_desktop_shell_state({});
    main_window window;
    window.resize(1600, 1000);
    window.show();
    QCoreApplication::processEvents();
    if (auto* setup = window.findChild<QDialog*>())
        setup->reject();
    auto* view = window.findChild<table*>();
    auto* toolbar
        = window.findChild<BaseToolBar*>(QStringLiteral("main_toolbar"));
    auto* collection = window.actionCollection();
    QVERIFY(view && toolbar && collection && window.isCommandBarEnabled());
    auto* search = collection->action(QStringLiteral("open_kcommand_bar"));
    auto* start = collection->action(QStringLiteral("game_start_pause"));
    auto* settings = collection->action(QStringLiteral("game_settings"));
    auto* shortcuts
        = collection->action(QStringLiteral("settings_configure_shortcuts"));
    QVERIFY(search && start && settings && shortcuts);
    const auto defaults = KActionCollection::defaultShortcuts(start);
    QVERIFY(defaults.contains(QKeySequence(Qt::Key_P)));
    start->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F8));
    QCOMPARE(KActionCollection::defaultShortcuts(start), defaults);
    toolbar->hide();
    collection->action(QStringLiteral("view_statusbar"))->trigger();
    QVERIFY(window.statusBar()->isHidden());
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1, 4 }
    )));
    QVERIFY(view->start_gameplay_runtime(87));
    view->stop_gameplay_clock();
    auto& owner = *view->active_gameplay_session();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    view->publish_gameplay_runtime(answers);
    auto* input = owned_input(view->slot_widgets[2]);
    QVERIFY(input && input->isEnabled());
    enter_owned_count(input, QStringLiteral("+17"));
    const auto prompt = view->gameplay_quiz_prompt({ 2 });
    const auto batch = *owner.current_quiz_batch();
    const auto timing = owner.timing();
    const auto record = *owner.deck({ 2 });
    QSignalSpy started(start, &QAction::triggered);
    // Enabled flag alone is not evidence of a populated/discoverable selector.
    search->trigger();
    QPointer<KCommandBar> bar = window.findChild<KCommandBar*>();
    QVERIFY(bar && bar->isVisible());
    auto* tree = bar->findChild<QTreeView*>();
    auto* query = bar->findChild<QLineEdit*>();
    QVERIFY(tree && query);
    QVERIFY(tree->model()->rowCount() > 0);
    const auto action_row = [](QTreeView* list, QAction* action) {
        for (int row = 0; row < list->model()->rowCount(); ++row) {
            auto index = list->model()->index(row, 0);
            if (index.data(Qt::UserRole).value<QAction*>() == action)
                return index;
        }
        return QModelIndex {};
    };
    const auto pause_row = action_row(tree, start);
    QVERIFY(pause_row.isValid());
    tree->setCurrentIndex(pause_row);
    QTest::keyClick(query, Qt::Key_Return);
    QCOMPARE(started.count(), 1);
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(bar.isNull());
    QCOMPARE(*owner.deck({ 2 }), record);
    QCOMPARE(*owner.current_quiz_batch(), batch);
    QCOMPARE(view->gameplay_quiz_prompt({ 2 }), prompt);
    QCOMPARE(owner.timing(), timing);
    QCOMPARE(input->text(), QStringLiteral("+17"));
    bool in_help = false;
    for (auto* menu : window.findChildren<QMenu*>())
        in_help |= menu->actions().contains(search);
    QVERIFY(in_help);
    // Configure Shortcuts uses the same collection. Native cancellation must
    // retain the current configured key and must not resume gameplay.
    shortcuts->trigger();
    QPointer<KShortcutsDialog> key_editor
        = window.findChild<KShortcutsDialog*>();
    QVERIFY(key_editor && key_editor->isVisible());
    QCOMPARE(key_editor->actionCollections(), QList { collection });
    QTest::keyClick(key_editor, Qt::Key_Escape);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(key_editor.isNull());
    QCOMPARE(start->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_F8));
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    window.activateWindow();
    QTest::qWait(1);
    QTest::keyClick(&window, Qt::Key_F8, Qt::ControlModifier);
    QCOMPARE(started.count(), 2);
    QCOMPARE(owner.phase(), gameplay::session_phase::running);
    view->stop_gameplay_clock();
    // The registered native command-search key works with hidden surfaces.
    QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier | Qt::AltModifier);
    bar = window.findChild<KCommandBar*>();
    QVERIFY(bar && bar->isVisible());
    tree = bar->findChild<QTreeView*>();
    query = bar->findChild<QLineEdit*>();
    QVERIFY(tree && query);
    QTest::keyClicks(query, QStringLiteral("Pause"));
    QVERIFY(action_row(tree, start).isValid());
    QCOMPARE(owner.phase(), gameplay::session_phase::running);
    QTest::keyClick(query, Qt::Key_Escape);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(bar.isNull());
    QVERIFY(view->finish_gameplay_runtime());
    const auto result = *owner.result();
    // Finish can reflow the HUD. Compare shell state around command search at
    // one settled phase, not against the Running phase's central geometry.
    for (int pass = 0; pass < 12; ++pass)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    const auto native_state
        = window.saveState(desktop_shell_state::qt_main_window_state_version);
    search->trigger();
    bar = window.findChild<KCommandBar*>();
    QVERIFY(bar && bar->isVisible());
    tree = bar->findChild<QTreeView*>();
    query = bar->findChild<QLineEdit*>();
    QVERIFY(tree && query);
    auto* new_game = collection->action(QStringLiteral("game_new"));
    const auto new_row = action_row(tree, new_game);
    QVERIFY(new_row.isValid());
    QVERIFY(!start->isEnabled());
    QCOMPARE(*owner.result(), result);
    QCOMPARE(
        window.saveState(desktop_shell_state::qt_main_window_state_version),
        native_state
    );
    tree->setCurrentIndex(new_row);
    QTest::keyClick(query, Qt::Key_Return);
    QVERIFY(!view->active_gameplay_session());
    QVERIFY(
        collection->action(QStringLiteral("game_saved_drills"))->isEnabled()
    );
    QVERIFY(toolbar->isHidden() && window.statusBar()->isHidden());
    QCOMPARE(collection->action(QStringLiteral("game_start_pause")), start);
    QCOMPARE(collection->action(QStringLiteral("game_settings")), settings);
#endif
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
