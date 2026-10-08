// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "arch/str_label.hpp"
#include "settings/training_progress.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_setup.hpp"
#include "table/settings_template.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QtTest/QtTest>

#include <array>

#ifdef KC_KDE
#include <KActionCollection>
#endif

using namespace table_test;

namespace {

struct shell_storage_guard {
    QSettings settings { QStringLiteral("ninjaro"),
                         QStringLiteral("kcuckoounter") };
    desktop_shell_state shell = load_desktop_shell_state();
    QMap<QString, QVariant> documents;

    shell_storage_guard() {
        save_desktop_shell_state({});
        for (const auto& key :
             { QStringLiteral("trainer_session/payload"),
               QStringLiteral("training_drills/document"),
               QStringLiteral("training_progress/recent_results") })
            documents.insert(key, settings.value(key));
    }

    ~shell_storage_guard() {
        for (auto it = documents.cbegin(); it != documents.cend(); ++it)
            if (it.value().isValid())
                settings.setValue(it.key(), it.value());
            else
                settings.remove(it.key());
        settings.sync();
        save_desktop_shell_state(shell);
    }
};

QMap<QString, QVariant> non_shell_settings(QSettings& settings) {
    settings.sync();
    QMap<QString, QVariant> values;
    for (const auto& key : settings.allKeys())
        if (!key.startsWith(QStringLiteral("desktop_shell/")))
            values.insert(key, settings.value(key));
    return values;
}

QMenu* action_menu(main_window& window, QAction* action) {
    for (auto* menu : window.findChildren<QMenu*>())
        if (menu->actions().contains(action))
            return menu;
    return nullptr;
}

void use_menu(
    main_window& window, QMenu* menu, QAction* action,
    QMessageBox::StandardButton answer = QMessageBox::NoButton
) {
    menu->popup(menu->parentWidget()->mapToGlobal(QPoint(20, 20)));
    menu->setActiveAction(action);
    // Popup creation can process events. Schedule the modal reply only after
    // the menu is ready, immediately before dispatching the selected command.
    if (answer != QMessageBox::NoButton)
        answer_launcher_finish(window, answer);
    QTest::keyClick(menu, Qt::Key_Return);
}

} // namespace

void table_tests::shell_menus_preserve_owned_routes_data() {
    QTest::addColumn<bool>("global_pause");
    QTest::addColumn<bool>("multi");
    for (const bool global : { false, true })
        for (const bool multi : { false, true })
            QTest::newRow(qPrintable(
                QStringLiteral("global=%1/multi=%2").arg(global).arg(multi)
            )) << global
               << multi;
}

void table_tests::shell_menus_preserve_owned_routes() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop native shell compatibility");
#endif
    QFETCH(bool, global_pause);
    QFETCH(bool, multi);
    launcher_preferences_guard preferences;
    shell_storage_guard storage;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    QCoreApplication::processEvents();
    if (auto* setup = window.findChild<QDialog*>())
        setup->reject();
    auto* view = window.findChild<table*>();
    auto* toolbar
        = window.findChild<BaseToolBar*>(QStringLiteral("main_toolbar"));
    QVERIFY(view && toolbar && window.menuBar()->isVisible());
    QVERIFY(!window.windowFlags().testFlag(Qt::FramelessWindowHint));
    set_gameplay_motion_style(*view, 0);
    const auto actions = toolbar->actions();
    QCOMPARE(actions.size(), qsizetype { 4 });
    auto* new_game = actions[0];
    auto* start = actions[1];
    auto* finish = actions[2];
    auto* settings = actions[3];
    auto* game_menu = action_menu(window, start);
    auto* settings_menu = action_menu(window, settings);
    auto* entry
        = window.findChild<QAction*>(QStringLiteral("game_new_gameplay"));
    auto* review
        = window.findChild<QAction*>(QStringLiteral("game_review_corrections"));
    auto* drills
        = window.findChild<QAction*>(QStringLiteral("game_saved_drills"));
    QVERIFY(game_menu && settings_menu && entry && review && drills);
    for (auto* action : { new_game, start, finish, entry, review, drills })
        QVERIFY(game_menu->actions().contains(action));
#ifdef KC_KDE
    auto* collection = window.actionCollection();
    QCOMPARE(collection->action(QStringLiteral("game_start_pause")), start);
    QCOMPARE(collection->action(QStringLiteral("game_settings")), settings);
    QCOMPARE(
        collection->action(QStringLiteral("game_review_corrections")), review
    );
    auto* status_toggle = collection->action(QStringLiteral("view_statusbar"));
    QVERIFY(status_toggle && status_toggle->isChecked());
    status_toggle->trigger();
    QVERIFY(window.statusBar()->isHidden());
#else
    QVERIFY(!window.findChild<QStatusBar*>()); // Qt retains its central strip.
#endif
    auto* toolbar_toggle = toolbar->toggleViewAction();
    QVERIFY(toolbar_toggle->isChecked());
    toolbar_toggle->trigger();
    QVERIFY(toolbar->isHidden());
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(
        4, global_pause, gameplay::dealing_mode::simultaneous,
        multi ? gameplay::quiz_scope::multi : gameplay::quiz_scope::single,
        { 1, 4 }
    )));
    auto& owner = *view->active_gameplay_session();
    QVERIFY(drills->isEnabled());
    QVERIFY(view->start_gameplay_runtime(85));
    view->stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    view->publish_gameplay_runtime(answers);
    auto prompt = view->gameplay_quiz_prompt({ 0 });
    QVERIFY(
        prompt && view->check_gameplay_quiz(*prompt)
    ); // Training correction
    prompt = view->gameplay_quiz_prompt({ 1 });
    QVERIFY(prompt && view->skip_gameplay_quiz(*prompt));
    QPointer<QLineEdit> input = owned_input(view->slot_widgets[2]);
    QVERIFY(input && input->isEnabled());
    enter_owned_count(input, QStringLiteral("+17"));
    input->setSelection(1, 2);
    const auto cursor = input->cursorPosition();
    const auto selection = input->selectedText();
    prompt = view->gameplay_quiz_prompt({ 2 });
    const auto batch = *owner.current_quiz_batch();
    const auto timing = owner.timing();
    const auto statistics = owner.statistics();
    const std::array records { *owner.deck({ 0 }), *owner.deck({ 1 }),
                               *owner.deck({ 2 }), *owner.deck({ 3 }) };
    const auto trainer = load_trainer_preferences();
    const auto progress = load_training_progress();
    QSignalSpy started(start, &QAction::triggered);
    QSignalSpy reviewed(review, &QAction::triggered);
    QSignalSpy menu_triggered(game_menu, &QMenu::triggered);
    QVERIFY(review->isEnabled());
    use_menu(window, game_menu, review);
    QCOMPARE(reviewed.count(), 1);
    QCOMPARE(menu_triggered.count(), 1);
    auto* review_dialog
        = window.findChild<QDialog*>(QStringLiteral("gameplay_review_dialog"));
    QVERIFY(
        review_dialog && review_dialog->isVisible() && !review_dialog->isModal()
    );
    QCOMPARE(owner.phase(), gameplay::session_phase::running);
    QTest::keyClick(review_dialog, Qt::Key_Escape);
    QVERIFY(!review_dialog->isVisible());
    use_menu(window, settings_menu, settings);
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    settings_template_widget* editor = nullptr;
    for (auto* candidate : window.findChildren<settings_template_widget*>())
        if (candidate->findChild<QComboBox*>(
                QStringLiteral("desktop_ui_toolbar")
            ))
            editor = candidate;
    QVERIFY(editor);
    editor->findChild<QComboBox*>(QStringLiteral("desktop_ui_toolbar"))
        ->setCurrentIndex(2);
    editor->findChild<QComboBox*>(QStringLiteral("desktop_ui_hud"))
        ->setCurrentIndex(2);
    QVERIFY(editor->apply_theme_settings());
    QVERIFY(toolbar->isHidden());
#ifdef KC_KDE
    QVERIFY(window.statusBar()->isHidden());
    QVERIFY(!status_toggle->isChecked());
#else
    QVERIFY(window
                .findChild<QWidget*>(QStringLiteral("desktop_status_surface"))
                ->isVisible());
#endif
    QCOMPARE(toolbar->actions(), actions);
    for (std::size_t id = 0; id < records.size(); ++id)
        QCOMPARE(*owner.deck({ id }), records[id]);
    QCOMPARE(view->gameplay_quiz_prompt({ 2 }), prompt);
    QCOMPARE(*owner.current_quiz_batch(), batch);
    QCOMPARE(owner.timing(), timing);
    QCOMPARE(owner.statistics(), statistics);
    QCOMPARE(input->text(), QStringLiteral("+17"));
    QCOMPARE(input->cursorPosition(), cursor);
    QCOMPARE(input->selectedText(), selection);
    // The shared picker is available without enabling the legacy score route.
    QTimer::singleShot(0, &window, [&] {
        auto* picker
            = window.findChild<QDialog*>(QStringLiteral("saved_drills_dialog"));
        QVERIFY(picker);
        picker->reject();
    });
    QVERIFY(QMetaObject::invokeMethod(&window, "on_saved_drills_triggered"));
    QVERIFY(QMetaObject::invokeMethod(&window, "on_show_highscores_triggered"));
    QVERIFY(!window.findChild<QDialog*>(QStringLiteral("saved_drills_dialog")));
    use_menu(window, game_menu, new_game, QMessageBox::No);
    QCOMPARE(view->active_gameplay_session(), &owner); // Finish first
    editor->window()->hide();
    window.activateWindow();
    QTest::qWait(1);
    QTest::keyClick(&window, Qt::Key_P);
    QCOMPARE(started.count(), 1);
    QCOMPARE(owner.phase(), gameplay::session_phase::running);
    view->stop_gameplay_clock();
    QTest::keyClick(&window, Qt::Key_P);
    QCOMPARE(started.count(), 2);
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    use_menu(window, game_menu, finish, QMessageBox::No);
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    use_menu(window, game_menu, finish, QMessageBox::Yes);
    const auto result = *owner.result();
    QVERIFY(!start->isEnabled() && !finish->isEnabled());
    QVERIFY(entry->isEnabled() && drills->isEnabled());
    QCOMPARE(owner.timing(), timing);
    QCOMPARE(load_trainer_preferences(), trainer);
    QCOMPARE(load_training_progress(), progress);
    QCOMPARE(*owner.result(), result);
    use_menu(window, game_menu, new_game);
    QVERIFY(!view->active_gameplay_session());
    QVERIFY(input.isNull());
    QVERIFY(drills->isEnabled());
#ifdef KC_KDE
    QVERIFY(collection->action(QStringLiteral("game_highscores"))->isEnabled());
#endif
    QVERIFY(toolbar->isHidden());
    QCOMPARE(toolbar->actions(), actions);
    QVERIFY(!view->gameplay_timer.isActive());
}

void table_tests::shell_state_keeps_gameplay_ephemeral_data() {
    QTest::addColumn<int>("phase");
    for (const auto phase :
         { gameplay::session_phase::setup, gameplay::session_phase::running,
           gameplay::session_phase::paused, gameplay::session_phase::finished })
        QTest::newRow(
            qPrintable(QStringLiteral("phase=%1").arg(static_cast<int>(phase)))
        ) << static_cast<int>(phase);
}

void table_tests::shell_state_keeps_gameplay_ephemeral() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop shell state is not gameplay recovery");
#endif
    QFETCH(int, phase);
    launcher_preferences_guard preferences;
    shell_storage_guard storage;
    auto trainer = load_trainer_preferences();
    trainer.slot_count = 2; // distinguish persisted legacy setup from the owner
    save_trainer_preferences(trainer);
    auto ui = load_desktop_ui_preferences();
    ui.toolbar_override = desktop_toolbar_style::compact;
    ui.hud_override = desktop_hud_style::instruments;
    QVERIFY(save_desktop_ui_preferences(ui));
    // Unknown/corrupt legacy documents must remain byte-for-byte untouched by
    // owned play and shell shutdown, rather than getting silently migrated.
    for (auto it = storage.documents.cbegin(); it != storage.documents.cend();
         ++it)
        storage.settings.setValue(
            it.key(), QByteArray("legacy document sentinel")
        );
    const auto documents = non_shell_settings(storage.settings);
    QPointer<table> old_view;
    QPointer<gameplay::setup_widget> old_fields;
    QPointer<QLineEdit> old_input;
    QSize saved_size;
    QByteArray saved_state;
    {
        main_window window;
        window.resize(1600, 1000);
        window.show();
        QCoreApplication::processEvents();
        if (auto* setup = window.findChild<QDialog*>())
            setup->reject();
        old_view = window.findChild<table*>();
        QVERIFY(old_view);
        set_gameplay_motion_style(*old_view, 0);
        window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))
            ->trigger();
        auto* dialog = window.findChild<QDialog*>(
            QStringLiteral("gameplay_setup_dialog")
        );
        QVERIFY(dialog);
        dialog->hide();
        QVERIFY(old_view->install_gameplay_session(owned_clock_fixture(
            4, true, gameplay::dealing_mode::simultaneous,
            gameplay::quiz_scope::multi, { 1, 4 }
        )));
        old_fields = dialog->findChild<gameplay::setup_widget*>();
        QVERIFY(old_fields);
        auto& owner = *old_view->active_gameplay_session();
        if (phase != static_cast<int>(gameplay::session_phase::setup)) {
            QVERIFY(old_view->start_gameplay_runtime(86));
            old_view->stop_gameplay_clock();
            std::vector<gameplay::quiz_answer> answers;
            QVERIFY(old_view->advance_gameplay_runtime(300, answers));
            old_view->publish_gameplay_runtime(answers);
            old_input = owned_input(old_view->slot_widgets[2]);
            QVERIFY(old_input);
            enter_owned_count(old_input, QStringLiteral("+17"));
            if (phase == static_cast<int>(gameplay::session_phase::paused))
                QVERIFY(old_view->pause_gameplay_runtime());
            if (phase == static_cast<int>(gameplay::session_phase::finished))
                QVERIFY(old_view->finish_gameplay_runtime());
        }
        QCOMPARE(static_cast<int>(owner.phase()), phase);
        auto* toolbar
            = window.findChild<BaseToolBar*>(QStringLiteral("main_toolbar"));
        QVERIFY(toolbar);
        window.addToolBar(Qt::BottomToolBarArea, toolbar);
        toolbar->hide();
        window.resize(640, 480);
        for (int pass = 0; pass < 12; ++pass)
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        saved_size = window.size();
        saved_state = window.saveState(
            desktop_shell_state::qt_main_window_state_version
        );
        QCOMPARE(non_shell_settings(storage.settings), documents);
    }
    QVERIFY(old_view.isNull() && old_fields.isNull() && old_input.isNull());
    QCOMPARE(load_desktop_shell_state().main_window_state, saved_state);
    QCOMPARE(non_shell_settings(storage.settings), documents);
    main_window restored;
    restored.show();
    QCoreApplication::processEvents();
    QCOMPARE(restored.size(), saved_size);
    auto* view = restored.findChild<table*>();
    auto* toolbar
        = restored.findChild<BaseToolBar*>(QStringLiteral("main_toolbar"));
    auto* legacy_clock
        = restored.findChild<BaseClock*>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(view && toolbar && legacy_clock);
    QVERIFY(
        !view->active_gameplay_session() && !view->gameplay_timer.isActive()
    );
    QVERIFY(!legacy_clock->is_active());
    QCOMPARE(view->slot_widgets.size(), std::size_t { 2 });
    QVERIFY(!view->capture_session_state().quiz_running);
    QVERIFY(toolbar->isHidden());
    QCOMPARE(restored.toolBarArea(toolbar), Qt::BottomToolBarArea);
    QCOMPARE(toolbar->actions().size(), qsizetype { 4 });
    QVERIFY(restored.menuBar()->isVisible());
    QVERIFY(restored.findChild<QAction*>(QStringLiteral("game_saved_drills"))
                ->isEnabled());
    QVERIFY(
        !restored.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"))
    );
    QCOMPARE(load_trainer_preferences(), trainer);
    QCOMPARE(load_desktop_ui_preferences(), ui);
    QCOMPARE(non_shell_settings(storage.settings), documents);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
