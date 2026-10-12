// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_mobile_tests.hpp"

#include "settings/gameplay_recovery_fixture.hpp"
#include "shell/gameplay_reservation.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_mobile_fixture.hpp"
#include "table/table_slot.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QtTest/QtTest>

using namespace table_test;
using namespace recovery_test;

void gameplay_mobile_tests::background_stops_dealing_data() {
    QTest::addColumn<int>("state");
    QTest::newRow("inactive") << static_cast<int>(Qt::ApplicationInactive);
    QTest::newRow("hidden") << static_cast<int>(Qt::ApplicationHidden);
    QTest::newRow("suspended") << static_cast<int>(Qt::ApplicationSuspended);
}

void gameplay_mobile_tests::background_stops_dealing() {
    QFETCH(int, state);
    mobile_test::preferences_guard preferences;
    main_window window;
    start_owned(window);
    auto* view = window.table_widget;
    QVERIFY(tick(*view, 137)); // partial pickup cadence, no revealed card
    const auto before = view->capture_gameplay_checkpoint();
    QSettings storage("ninjaro", "kcuckoounter");
    const auto original = storage.value(storage_key);
    window.apply_mobile_state(static_cast<Qt::ApplicationState>(state));
    QVERIFY(window.mobile_background);
    QCOMPARE(view->gameplay_owner->phase(), gameplay::session_phase::paused);
    QVERIFY(!view->gameplay_timer.isActive());
    QVERIFY(!view->gameplay_elapsed.isValid());
    const auto paused = view->capture_gameplay_checkpoint();
    QCOMPARE(paused, before);
    const auto record = gameplay_recovery_service(storage, { 64, 64 }).load();
    QVERIFY(record);
    QCOMPARE(std::get<gameplay_active_record>(*record).checkpoint, *paused);
    QVERIFY(storage.value(storage_key) != original);
    QVERIFY(!storage.contains(mobile_test::legacy_key));
    QVERIFY(!tick(*view, 120'000)); // stale delivery cannot accrue time
    view->on_gameplay_clock_tick();
    QTest::qWait(3);
    QCOMPARE(view->capture_gameplay_checkpoint(), paused);
    window.start_pause_action->trigger(); // no background Resume
    QCOMPARE(view->gameplay_owner->phase(), gameplay::session_phase::paused);
    window.apply_mobile_state(Qt::ApplicationActive);
    QCOMPARE(view->capture_gameplay_checkpoint(), paused);
    window.start_pause_action->trigger();
    view->stop_gameplay_clock();
    QCOMPARE(view->gameplay_owner->phase(), gameplay::session_phase::running);
    QVERIFY(tick(*view, 162));
    QCOMPARE(
        view->gameplay_owner->deck({ 0 })->dealt_physical_cards,
        std::uint64_t { 0 }
    );
    QVERIFY(tick(*view, 1));
    QCOMPARE(
        view->gameplay_owner->deck({ 0 })->dealt_physical_cards,
        std::uint64_t { 1 }
    );
    QVERIFY(view->finish_gameplay_runtime());
}

void gameplay_mobile_tests::background_freezes_quiz_data() {
    QTest::addColumn<bool>("frozen");
    QTest::addColumn<int>("scope");
    for (const bool frozen : { false, true })
        for (const auto scope :
             { gameplay::quiz_scope::single, gameplay::quiz_scope::multi })
            QTest::newRow(
                qPrintable(QString("%1-%2")
                               .arg(frozen ? "frozen" : "unfrozen")
                               .arg(static_cast<int>(scope)))
            ) << frozen
              << static_cast<int>(scope);
}

void gameplay_mobile_tests::background_freezes_quiz() {
    QFETCH(bool, frozen);
    QFETCH(int, scope);
    mobile_test::preferences_guard preferences;
    main_window window;
    prepare_window(window);
    auto* view = window.table_widget;
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(
        2, frozen, gameplay::dealing_mode::simultaneous,
        static_cast<gameplay::quiz_scope>(scope), { 1, 4 }
    )));
    QVERIFY(window.reserve_gameplay_from_ui(31));
    view->stop_gameplay_clock();
    QVERIFY(tick(*view, 300));
    const auto prompt = view->gameplay_quiz_prompt({ 1 });
    QVERIFY(prompt);
    QVERIFY(view->edit_gameplay_quiz(*prompt, -9));
    QVERIFY(tick(*view, 1217));
    const auto remaining
        = view->gameplay_owner->current_quiz_batch()->remaining_ms;
    QCOMPARE(remaining.has_value(), frozen);
    window.apply_mobile_state(Qt::ApplicationHidden);
    const auto paused = view->capture_gameplay_checkpoint();
    QVERIFY(paused);
    QVERIFY(!owned_input(view->slot_widgets[1])->isEnabled());
    QVERIFY(!view->check_gameplay_quiz(*prompt));
    QVERIFY(!tick(*view, 120'000));
    view->on_gameplay_clock_tick();
    window.apply_mobile_state(Qt::ApplicationSuspended);
    window.apply_mobile_state(Qt::ApplicationActive);
    QCOMPARE(view->capture_gameplay_checkpoint(), paused);
    QCOMPARE(
        view->gameplay_owner->current_quiz_batch()->remaining_ms, remaining
    );
    QSettings storage("ninjaro", "kcuckoounter");
    const auto saved = gameplay_recovery_service(storage, { 64, 64 }).load();
    QVERIFY(saved);
    QCOMPARE(std::get<gameplay_active_record>(*saved).checkpoint, *paused);
    window.start_pause_action->trigger();
    view->stop_gameplay_clock();
    QVERIFY(owned_input(view->slot_widgets[1])->isEnabled());
    QCOMPARE(
        view->gameplay_owner->current_quiz_batch()->remaining_ms, remaining
    );
    QVERIFY(tick(*view, 7));
    if (frozen)
        QCOMPARE(
            *view->gameplay_owner->current_quiz_batch()->remaining_ms,
            *remaining - 7
        );
    QVERIFY(view->finish_gameplay_runtime());
}

void gameplay_mobile_tests::background_save_can_retry() {
    mobile_test::preferences_guard preferences;
    main_window window;
    start_owned(window);
    QVERIFY(tick(*window.table_widget, 137));
    QSettings storage("ninjaro", "kcuckoounter");
    const auto before = storage.value(storage_key);
    const auto protected_bytes = QByteArrayLiteral("{\"version\":999}");
    storage.setValue(checkpoint_key, protected_bytes);
    storage.sync();
    window.apply_mobile_state(Qt::ApplicationHidden);
    QCOMPARE(
        window.table_widget->gameplay_owner->phase(),
        gameplay::session_phase::paused
    );
    QVERIFY(!window.table_widget->gameplay_timer.isActive());
    QVERIFY(!window.gameplay_storage_error.isEmpty());
    QVERIFY(window.gameplay_retry_action->isEnabled());
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QVERIFY(window.gameplay_retry_action->isVisible());
#endif
    QCOMPARE(window.status_label->text(), window.gameplay_storage_error);
    QCOMPARE(storage.value(storage_key), before);
    QCOMPARE(storage.value(checkpoint_key).toByteArray(), protected_bytes);
    const auto exact = window.table_widget->capture_gameplay_checkpoint();
    storage.remove(checkpoint_key); // deliberate external resolution
    storage.sync();
    window.gameplay_retry_action->trigger();
    QVERIFY(window.gameplay_storage_error.isEmpty());
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QVERIFY(!window.gameplay_retry_action->isVisible());
#endif
    const auto saved = gameplay_recovery_service(storage, { 64, 64 }).load();
    QVERIFY(saved);
    QCOMPARE(std::get<gameplay_active_record>(*saved).checkpoint, *exact);
    window.apply_mobile_state(Qt::ApplicationActive);
    QCOMPARE(window.table_widget->capture_gameplay_checkpoint(), exact);
    QVERIFY(window.table_widget->finish_gameplay_runtime());
}

void gameplay_mobile_tests::background_retries_exact_result() {
    mobile_test::preferences_guard preferences;
    main_window window;
    start_owned(window);
    QSettings storage("ninjaro", "kcuckoounter");
    storage.setValue(checkpoint_key, QByteArrayLiteral("protected"));
    storage.sync();
    QVERIFY(window.table_widget->finish_gameplay_runtime());
    const auto binding = window.gameplay_binding;
    QVERIFY(binding->completion() && !binding->acknowledged());
    const auto exact = *binding->completion();
    window.table_widget->clear_gameplay_session();
    window.apply_mobile_state(Qt::ApplicationHidden);
    QCOMPARE(*binding->completion(), exact);
    QVERIFY(!binding->acknowledged());
    storage.remove(checkpoint_key);
    storage.sync();
    window.apply_mobile_state(Qt::ApplicationSuspended);
    QVERIFY(binding->acknowledged());
    const auto progress = gameplay_progress_service(storage, { 64, 64 }).load();
    QVERIFY(progress && progress->completed_sessions == 1);
    QCOMPARE(progress->recent_results.front(), exact);
    QVERIFY(!storage.contains(storage_key));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
