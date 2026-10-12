// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_mobile_tests.hpp"

#include "settings/gameplay_recovery_fixture.hpp"
#include "settings/session_checkpoint.hpp"
#include "shell/gameplay_reservation.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_mobile_fixture.hpp"
#include "table/table_slot.hpp"

#include <QDialog>
#include <QLineEdit>
#include <QToolBar>
#include <QtTest/QtTest>

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
            QTest::newRow(
                qPrintable(QString("%1-%2")
                               .arg(static_cast<int>(mode))
                               .arg(static_cast<int>(scope)))
            ) << static_cast<int>(mode)
              << static_cast<int>(scope);
}

void gameplay_mobile_tests::restore_keeps_future() {
    QFETCH(int, mode);
    QFETCH(int, scope);
    mobile_test::preferences_guard preferences;
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
    mobile_test::preferences_guard preferences;
    main_window window;
    start_owned(window);
    QVERIFY(tick(*window.table_widget, 137));
    QVERIFY(
        QMetaObject::invokeMethod(
            qApp, "applicationStateChanged", Qt::DirectConnection,
            Q_ARG(Qt::ApplicationState, Qt::ApplicationHidden)
        )
    );
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

void gameplay_mobile_tests::mobile_restores_legacy() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    QSKIP("Legacy lifecycle storage is enabled only in the Android profile");
#endif
    mobile_test::preferences_guard preferences;
    trainer_session_checkpoint exact;
    {
        main_window window;
        prepare_window(window);
        window.table_slots_count->setValue(1);
        window.wait_for_answers->setChecked(false);
        window.on_continue_button_clicked();
        window.start_pause_action->trigger();
        QVERIFY(window.quiz_started && !window.quiz_paused);
        window.on_table_score_adjusted(2, 3);
        window.clock_timer->pause();
        window.clock_timer->set_elapsed_time_ms(1234);
        window.apply_mobile_state(Qt::ApplicationHidden);
        QVERIFY(window.quiz_paused && !window.clock_timer->is_active());
        const auto saved = load_trainer_session_checkpoint();
        QVERIFY(saved && saved->was_running);
        exact = *saved;
    }
    main_window reopened;
    prepare_window(reopened);
    QVERIFY(reopened.mobile_checkpoint_restored && reopened.quiz_paused);
    QVERIFY(!reopened.table_widget->active_gameplay_session());
    QCOMPARE(reopened.table_widget->capture_session_state(), exact.table);
    QCOMPARE(reopened.score_correct, 2);
    QCOMPARE(reopened.score_total, 3);
    QCOMPARE(reopened.clock_timer->elapsed_time_ms(), qint64 { 1234 });
    QVERIFY(!reopened.clock_timer->is_active());
    QVERIFY(!reopened.setup_dialog->isVisible());
    reopened.apply_mobile_state(Qt::ApplicationActive);
    QVERIFY(reopened.quiz_paused);
    reopened.start_pause_action->trigger();
    QVERIFY(!reopened.quiz_paused && reopened.clock_timer->is_active());
    reopened.apply_mobile_state(Qt::ApplicationHidden);
}

void gameplay_mobile_tests::mobile_preserves_both_records() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    QSKIP("Ordered legacy/owned recovery is enabled only on Android");
#endif
    mobile_test::preferences_guard preferences;
    {
        main_window legacy;
        prepare_window(legacy);
        legacy.table_slots_count->setValue(1);
        legacy.wait_for_answers->setChecked(false);
        legacy.on_continue_button_clicked();
        legacy.start_pause_action->trigger();
        legacy.apply_mobile_state(Qt::ApplicationHidden);
    }
    QSettings storage("ninjaro", "kcuckoounter");
    const auto legacy_bytes = storage.value(mobile_test::legacy_key);
    QVERIFY(legacy_bytes.isValid() && load_trainer_session_checkpoint());
    gameplay_checkpoint exact;
    {
        table source;
        source.resize(1000, 800);
        QVERIFY(source.install_gameplay_session(owned_clock_fixture(2, true)));
        gameplay_reservation binding;
        gameplay_recovery_service recovery(storage, { 64, 64 });
        QVERIFY(binding.start(source, recovery, 73));
        source.stop_gameplay_clock();
        QVERIFY(tick(source, 137));
        QVERIFY(source.pause_gameplay_runtime());
        exact = *source.capture_gameplay_checkpoint();
        QVERIFY(recovery.save(*binding.sequence(), exact));
    }
    main_window reopened;
    prepare_window(reopened);
    QVERIFY(reopened.gameplay_binding->owns(*reopened.table_widget));
    QVERIFY(!reopened.quiz_started && !reopened.mobile_checkpoint_restored);
    QCOMPARE(
        reopened.table_widget->capture_gameplay_checkpoint(),
        std::optional(exact)
    );
    QCOMPARE(storage.value(mobile_test::legacy_key), legacy_bytes);
    reopened.apply_mobile_state(Qt::ApplicationHidden);
    QCOMPARE(storage.value(mobile_test::legacy_key), legacy_bytes);
    QVERIFY(reopened.table_widget->finish_gameplay_runtime());
    QCOMPARE(storage.value(mobile_test::legacy_key), legacy_bytes);
}

void gameplay_mobile_tests::mobile_early_action_restores() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    QSKIP("Early mobile action shares ordered owned/legacy recovery");
#endif
    mobile_test::preferences_guard preferences;
    {
        main_window legacy;
        prepare_window(legacy);
        legacy.table_slots_count->setValue(1);
        legacy.wait_for_answers->setChecked(false);
        legacy.on_continue_button_clicked();
        legacy.start_pause_action->trigger();
        legacy.on_table_score_adjusted(2, 3);
        legacy.apply_mobile_state(Qt::ApplicationHidden);
    }
    const auto before = load_trainer_session_checkpoint();
    QVERIFY(before);
    main_window reopened;
    // A stale/native Start arriving before queued startup must Resume the old
    // checkpoint, not prepare a fresh shoe and overwrite it.
    reopened.on_start_pause_triggered();
    QVERIFY(reopened.gameplay_recovery_checked && reopened.quiz_started);
    QVERIFY(!reopened.quiz_paused && reopened.clock_timer->is_active());
    QCOMPARE(reopened.score_correct, 2);
    QCOMPARE(reopened.score_total, 3);
    const auto resumed = load_trainer_session_checkpoint();
    QVERIFY(resumed && resumed->was_running);
    QCOMPARE(
        resumed->table.slot_states[0].card, before->table.slot_states[0].card
    );
    reopened.apply_mobile_state(Qt::ApplicationHidden);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
