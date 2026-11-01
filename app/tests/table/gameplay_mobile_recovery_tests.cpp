// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_mobile_tests.hpp"

#include "settings/gameplay_recovery_fixture.hpp"
#include "shell/gameplay_reservation.hpp"
#include "shell/main_window.hpp"
#include "shell/mobile_shell.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolBar>
#include <QtTest/QtTest>
#include <shared_ui/qt/editor_panel.hpp>
#include <shared_ui/qt/touch_layouts.hpp>

using namespace table_test;
using namespace recovery_test;

void gameplay_mobile_tests::restore_keeps_future_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("scope");
    for (const auto mode :
         { gameplay::dealing_mode::simultaneous,
           gameplay::dealing_mode::sequential, gameplay::dealing_mode::random })
        for (const auto scope :
             { gameplay::quiz_scope::single, gameplay::quiz_scope::multi })
            QTest::newRow(qPrintable(QString("%1-%2")
                                         .arg(static_cast<int>(mode))
                                         .arg(static_cast<int>(scope)))
            ) << static_cast<int>(mode)
              << static_cast<int>(scope);
}

void gameplay_mobile_tests::restore_keeps_future() {
    QFETCH(int, mode);
    QFETCH(int, scope);
    table_test::launcher_preferences_guard preferences;
    gameplay_checkpoint exact;
    {
        main_window window;
        prepare_window(window);
        auto* view = window.table_widget;
        QVERIFY(view->install_gameplay_session(owned_clock_fixture(
            2, true, static_cast<gameplay::dealing_mode>(mode),
            static_cast<gameplay::quiz_scope>(scope), { 1, 4 }
        )));
        QVERIFY(window.reserve_gameplay_from_ui(53));
        view->stop_gameplay_clock();
        QVERIFY(tick(*view, 300));
        for (const auto id : view->gameplay_owner->unresolved_quizzes())
            QVERIFY(
                view->edit_gameplay_quiz(*view->gameplay_quiz_prompt(id), -17)
            );
        QVERIFY(tick(*view, 123));
        window.apply_mobile_state(Qt::ApplicationHidden);
        exact = *view->capture_gameplay_checkpoint();
        QVERIFY(window.close());
    }
    table baseline;
    baseline.resize(1200, 700);
    QVERIFY(baseline.restore_gameplay_checkpoint(exact));
    main_window reopened;
    prepare_window(reopened);
    auto* view = reopened.table_widget;
    QCOMPARE(view->gameplay_owner->phase(), gameplay::session_phase::paused);
    QCOMPARE(view->capture_gameplay_checkpoint(), std::optional(exact));
    QVERIFY(!view->gameplay_timer.isActive());
    QVERIFY(!view->gameplay_elapsed.isValid());
    for (const auto id : view->gameplay_owner->unresolved_quizzes()) {
        QCOMPARE(
            owned_input(view->slot_widgets[id.value])->text(), QString("-17")
        );
        QVERIFY(!owned_input(view->slot_widgets[id.value])->isEnabled());
    }
    QTest::qWait(3);
    view->on_gameplay_clock_tick();
    QCOMPARE(view->capture_gameplay_checkpoint(), std::optional(exact));
    QVERIFY(baseline.resume_gameplay_runtime());
    baseline.stop_gameplay_clock();
    reopened.start_pause_action->trigger();
    view->stop_gameplay_clock();
    // Exercise future native reveals, quiz targets, expected counts and inputs,
    // not only the saved visible cards. No background wall-time catch-up.
    for (int step = 0; step < 12; ++step) {
        for (const auto id : baseline.gameplay_owner->unresolved_quizzes()) {
            const auto count
                = baseline.gameplay_owner->deck(id)->quiz->expected_count;
            const auto first = *baseline.gameplay_quiz_prompt(id);
            const auto second = *view->gameplay_quiz_prompt(id);
            QVERIFY(baseline.edit_gameplay_quiz(first, count));
            QVERIFY(view->edit_gameplay_quiz(second, count));
            QVERIFY(baseline.check_gameplay_quiz(first));
            QVERIFY(view->check_gameplay_quiz(second));
        }
        QVERIFY(tick(baseline, 137));
        QVERIFY(tick(*view, 137));
        const auto expected = baseline.capture_gameplay_checkpoint();
        QVERIFY(expected);
        QCOMPARE(view->capture_gameplay_checkpoint(), expected);
    }
    QVERIFY(view->finish_gameplay_runtime());
    QVERIFY(reopened.gameplay_binding->acknowledged());
}

void gameplay_mobile_tests::mobile_callback_is_connected() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    QSKIP("Application-state adapter is enabled only in the Android profile");
#endif
    table_test::launcher_preferences_guard preferences;
    main_window window;
    start_owned(window);
    QVERIFY(tick(*window.table_widget, 137));
    QVERIFY(QMetaObject::invokeMethod(
        qApp, "applicationStateChanged", Qt::DirectConnection,
        Q_ARG(Qt::ApplicationState, Qt::ApplicationHidden)
    ));
    QVERIFY(window.mobile_background);
    QCOMPARE(
        window.table_widget->gameplay_owner->phase(),
        gameplay::session_phase::paused
    );
    QVERIFY(!window.mobile_checkpoint_pending);
    auto* toolbar = window.findChild<QToolBar*>("mobile_navigation_toolbar");
    QVERIFY(
        toolbar && toolbar->actions().contains(window.gameplay_retry_action)
    );
    QVERIFY(!window.gameplay_retry_action->isVisible());
    QVERIFY(window.table_widget->finish_gameplay_runtime());
}

void gameplay_mobile_tests::mobile_new_game_uses_owned_session() {
    table_test::launcher_preferences_guard preferences;
    main_window window;
    prepare_window(window);
    window.new_game_action->trigger();
    auto* view = window.table_widget;
    QVERIFY(view->active_gameplay_session());
    QCOMPARE(
        view->active_gameplay_session()->phase(), gameplay::session_phase::setup
    );
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("game_new_gameplay")));
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    const auto* commands
        = window.findChild<QMenu*>(QStringLiteral("mobile_commands_menu"));
    QVERIFY(
        commands && commands->actions().contains(window.gameplay_review_action)
    );
    QVERIFY(commands->actions().contains(window.saved_drills_action));
    // Phones configure on the touch setup screen, not in a native dialog.
    QVERIFY(window.mobile);
    QCOMPARE(window.mobile->current_page(), mobile_shell::page::setup);
    QVERIFY(!window.gameplay_setup_dialog);
    QVERIFY(window.gameplay_setup_error);
    window.on_gameplay_roster_changed(1);
    QCOMPARE(view->active_gameplay_session()->size(), std::size_t { 1 });
#else
    QVERIFY(window.gameplay_setup_fields);
    QVERIFY(window.gameplay_setup_dialog->isVisible());
    window.gameplay_slot_count->setValue(1);
    window.gameplay_setup_dialog->accept();
#endif
    window.start_pause_action->trigger();
    QVERIFY(view->has_gameplay_runtime());
    QCOMPARE(
        view->active_gameplay_session()->phase(),
        gameplay::session_phase::running
    );
    window.apply_mobile_state(Qt::ApplicationHidden);
    const auto exact = view->capture_gameplay_checkpoint();
    QVERIFY(exact);
    window.apply_mobile_state(Qt::ApplicationActive);
    QCOMPARE(view->capture_gameplay_checkpoint(), exact);
    QCOMPARE(
        view->active_gameplay_session()->phase(),
        gameplay::session_phase::paused
    );
    QVERIFY(view->finish_gameplay_runtime());
    QVERIFY(window.gameplay_binding->acknowledged());
    window.new_game_action->trigger();
    QCOMPARE(
        view->active_gameplay_session()->phase(), gameplay::session_phase::setup
    );
    QVERIFY(!view->active_gameplay_session()->result());
}

void gameplay_mobile_tests::mobile_deck_details_are_usable() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    QSKIP("The touch deck sheet belongs to the phone profile");
#else
    table_test::launcher_preferences_guard preferences;
    main_window window;
    prepare_window(window);
    window.resize(390, 844);
    QCoreApplication::processEvents();
    window.new_game_action->trigger();
    auto* shell = window.mobile;
    QVERIFY(shell);
    const auto configuration = [&window] {
        return window.table_widget->active_gameplay_session()
            ->deck({ 0 })
            ->configuration;
    };
    const auto before = configuration();
    shell->open_deck_sheet({ 0 });
    QVERIFY(shell->deck_sheet->is_open());
    QCoreApplication::processEvents();
    // Every command a finger can reach is at least 44 px tall.
    for (auto* button : shell->deck_sheet->findChildren<QAbstractButton*>())
        if (button->isVisible()
            && !button->testAttribute(Qt::WA_TransparentForMouseEvents))
            QVERIFY2(button->height() >= 44, qPrintable(button->objectName()));
    auto* more = shell->sheet_count->findChild<QAbstractButton*>(
        QStringLiteral("touch_stepper_more")
    );
    QVERIFY(more && more->isEnabled());
    more->click();
    shell->deck_sheet
        ->findChild<QAbstractButton*>(QStringLiteral("mobile_sheet_cancel"))
        ->click();
    QVERIFY(!shell->deck_sheet->is_open());
    QCOMPARE(configuration(), before); // staged: Cancel discards
    shell->open_deck_sheet({ 0 });
    more->click();
    shell->deck_sheet
        ->findChild<QAbstractButton*>(QStringLiteral("mobile_sheet_ok"))
        ->click();
    QCOMPARE(configuration().deck_count, before.deck_count + 1);
#endif
}

void gameplay_mobile_tests::mobile_back_pauses_then_leaves() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    QSKIP("System back belongs to the phone profile");
#else
    table_test::launcher_preferences_guard preferences;
    main_window window;
    start_owned(window);
    auto* shell = window.mobile;
    shell->show_table();
    auto* view = window.table_widget;
    QCOMPARE(
        view->active_gameplay_session()->phase(),
        gameplay::session_phase::running
    );
    shell->go_back(); // a running table pauses first
    QCOMPARE(shell->current_page(), mobile_shell::page::table);
    QCOMPARE(
        view->active_gameplay_session()->phase(),
        gameplay::session_phase::paused
    );
    shell->go_back(); // a paused table returns Home, still paused
    QCOMPARE(shell->current_page(), mobile_shell::page::home);
    QCOMPARE(
        view->active_gameplay_session()->phase(),
        gameplay::session_phase::paused
    );
    QVERIFY(view->finish_gameplay_runtime());
#endif
}

void gameplay_mobile_tests::mobile_restores_owned_session() {
    table_test::launcher_preferences_guard preferences;
    gameplay_checkpoint exact;
    {
        main_window window;
        start_owned(window);
        QVERIFY(tick(*window.table_widget, 137));
        window.apply_mobile_state(Qt::ApplicationHidden);
        exact = *window.table_widget->capture_gameplay_checkpoint();
    }
    main_window reopened;
    prepare_window(reopened);
    QVERIFY(reopened.gameplay_binding->owns(*reopened.table_widget));
    QCOMPARE(
        reopened.table_widget->capture_gameplay_checkpoint(),
        std::optional(exact)
    );
    QVERIFY(!reopened.gameplay_setup_dialog);
    reopened.apply_mobile_state(Qt::ApplicationActive);
    QCOMPARE(
        reopened.table_widget->active_gameplay_session()->phase(),
        gameplay::session_phase::paused
    );
    reopened.start_pause_action->trigger();
    QCOMPARE(
        reopened.table_widget->active_gameplay_session()->phase(),
        gameplay::session_phase::running
    );
    QVERIFY(reopened.table_widget->finish_gameplay_runtime());
}

void gameplay_mobile_tests::mobile_early_action_restores() {
    table_test::launcher_preferences_guard preferences;
    gameplay_checkpoint exact;
    {
        main_window window;
        start_owned(window);
        window.apply_mobile_state(Qt::ApplicationHidden);
        exact = *window.table_widget->capture_gameplay_checkpoint();
    }
    main_window reopened;
    // An explicit Start before queued startup recovers the reserved session,
    // rather than reserving a second one. This is the only explicit Resume.
    reopened.on_start_pause_triggered();
    QVERIFY(reopened.gameplay_recovery_checked);
    auto* owner = reopened.table_widget->active_gameplay_session();
    QVERIFY(owner);
    QCOMPARE(owner->phase(), gameplay::session_phase::running);
    reopened.table_widget->stop_gameplay_clock();
    QVERIFY(reopened.table_widget->pause_gameplay_runtime());
    QCOMPARE(
        reopened.table_widget->capture_gameplay_checkpoint(),
        std::optional(exact)
    );
    QVERIFY(reopened.table_widget->finish_gameplay_runtime());
}
