// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_host_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"
#include "settings/gameplay_recovery_fixture.hpp"
#include "settings/training_progress.hpp"
#include "shell/gameplay_reservation.hpp"
#include "shell/main_window.hpp"
#include "table/table_fixture.hpp"

#include <QDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QtTest/QtTest>

using namespace table_test;
using namespace recovery_test;

void gameplay_host_tests::menu_start_pause_finish() {
    launcher_preferences_guard preferences;
    const auto legacy = load_training_progress();
    main_window window;
    launch(window);
    auto* view = window.findChild<table*>();
    const auto binding = window.gameplay_binding;
    QVERIFY(binding && binding->owns(*view) && binding->sequence() == 1);
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    const auto initial = storage.value(storage_key);
    QVERIFY(initial.isValid());
    QVERIFY(!storage.contains(progress_key));
    QVERIFY(tick(*view, 650));
    QCOMPARE(storage.value(storage_key), initial); // runtime ticks do no I/O
    auto* start = launcher_start_action(window);
    start->trigger();
    QCOMPARE(
        view->active_gameplay_session()->phase(),
        gameplay::session_phase::paused
    );
    const auto paused = view->capture_gameplay_checkpoint();
    QVERIFY(paused);
    const auto saved = gameplay_recovery_service(storage, { 64, 64 }).load();
    QVERIFY(saved);
    QCOMPARE(std::get<gameplay_active_record>(*saved).checkpoint, *paused);
    start->trigger();
    QCOMPARE(
        view->active_gameplay_session()->phase(),
        gameplay::session_phase::running
    );
    answer_launcher_finish(window, QMessageBox::Yes);
    window.finish_action->trigger(); // actual toolbar/menu action
    QVERIFY(binding->completion() && binding->acknowledged());
    const auto exact = *binding->completion();
    const auto progress = gameplay_progress_service(storage, { 64, 64 }).load();
    QVERIFY(progress && progress->completed_sessions == 1);
    QCOMPARE(progress->recent_results.front(), exact);
    QVERIFY(!storage.contains(storage_key));
    QCOMPARE(load_training_progress(), legacy);
    const auto before = storage.value(progress_key);
    window.retry_gameplay_from_ui();
    QCOMPARE(storage.value(progress_key), before);
    window.new_gameplay_action->trigger();
    QCOMPARE(
        view->active_gameplay_session()->phase(), gameplay::session_phase::setup
    );
    start->trigger();
    QVERIFY(window.gameplay_binding->sequence() == 2);
    QVERIFY(view->finish_gameplay_runtime());
    QVERIFY(window.close());
}

void gameplay_host_tests::startup_restores_paused() {
    launcher_preferences_guard preferences;
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    gameplay_checkpoint exact;
    {
        table source;
        source.resize(1000, 800);
        QVERIFY(source.install_gameplay_session(owned_clock_fixture(3, true)));
        gameplay_reservation binding;
        gameplay_recovery_service recovery(storage, { 64, 64 });
        QVERIFY(binding.start(source, recovery, 73));
        source.stop_gameplay_clock();
        QVERIFY(tick(source, 750));
        QVERIFY(source.pause_gameplay_runtime());
        exact = *source.capture_gameplay_checkpoint();
        QVERIFY(recovery.save(*binding.sequence(), exact));
    }
    const auto bytes = storage.value(storage_key);
    main_window window;
    window.resize(1200, 700);
    window.show();
    QCoreApplication::processEvents();
    auto* view = window.findChild<table*>();
    QVERIFY(
        window.gameplay_recovery_ready && window.gameplay_binding->owns(*view)
    );
    QCOMPARE(
        view->active_gameplay_session()->phase(),
        gameplay::session_phase::paused
    );
    QVERIFY(!view->gameplay_timer.isActive());
    QCOMPARE(*view->capture_gameplay_checkpoint(), exact);
    QCOMPARE(storage.value(storage_key), bytes);
    QVERIFY(!window.setup_dialog->isVisible());
    launcher_start_action(window)->trigger();
    QVERIFY(view->finish_gameplay_runtime());
    QVERIFY(window.gameplay_binding->acknowledged());
}

void gameplay_host_tests::startup_retries_pending() {
    launcher_preferences_guard preferences;
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    gameplay_result_record exact;
    {
        table source;
        source.resize(1000, 800);
        QVERIFY(source.install_gameplay_session(owned_clock_fixture(1)));
        gameplay_reservation binding;
        gameplay_recovery_service recovery(storage, { 64, 64 });
        QVERIFY(binding.start(source, recovery, 73));
        QVERIFY(source.finish_gameplay_runtime());
        exact = *binding.completion();
        storage.setValue(
            storage_key, *encode_gameplay_recovery(exact, { 64, 64 })
        );
        storage
            .sync(); // simulate a successful stage before interrupted admission
    }
    main_window window;
    window.show();
    QCoreApplication::processEvents();
    QVERIFY(window.gameplay_recovery_ready);
    QVERIFY(!window.gameplay_binding->sequence());
    QVERIFY(!window.table_widget->active_gameplay_session());
    QVERIFY(!storage.contains(storage_key));
    const auto progress = gameplay_progress_service(storage, { 64, 64 }).load();
    QVERIFY(progress && progress->completed_sessions == 1);
    QCOMPARE(progress->recent_results.front(), exact);
    QVERIFY(window.setup_dialog->isVisible());
}

void gameplay_host_tests::startup_protects_records_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("standalone-v2") << 0;
    QTest::newRow("future-recovery") << 1;
    QTest::newRow("future-progress") << 2;
    QTest::newRow("conflicting-pending") << 3;
}

void gameplay_host_tests::startup_protects_records() {
    QFETCH(int, kind);
    launcher_preferences_guard preferences;
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    if (kind == 3) {
        const auto result = progress_test::sample_result();
        QVERIFY(gameplay_progress_service(storage, { 64, 64 })
                    .record({ 1, 100, result }));
        storage.setValue(
            storage_key,
            *encode_gameplay_recovery(
                gameplay_result_record { 1, 101, result }, { 64, 64 }
            )
        );
    } else {
        const auto key = kind == 0
            ? QStringLiteral("gameplay_checkpoint/document")
            : kind == 1 ? storage_key
                        : progress_key;
        storage.setValue(key, QByteArrayLiteral("{\"version\":999}"));
    }
    storage.sync();
    QVariantMap before;
    for (const auto& key : storage.allKeys())
        if (key.startsWith("gameplay_"))
            before.insert(key, storage.value(key));
    main_window window;
    window.show();
    QCoreApplication::processEvents();
    QVERIFY(
        window.gameplay_recovery_checked && !window.gameplay_recovery_ready
    );
    QVERIFY(!window.gameplay_storage_error.isEmpty());
    QVERIFY(!window.new_gameplay_action->isEnabled());
    QVERIFY(window.gameplay_retry_action->isEnabled());
    bool in_menu = false;
    for (auto* menu : window.findChildren<QMenu*>())
        in_menu
            = in_menu || menu->actions().contains(window.gameplay_retry_action);
    QVERIFY(in_menu);
    window.gameplay_retry_action->trigger();
    window.on_table_rasterization_busy_changed(false);
    QCOMPARE(window.status_label->text(), window.gameplay_storage_error);
    for (auto value = before.cbegin(); value != before.cend(); ++value)
        QCOMPARE(storage.value(value.key()), *value);
    // Unidentified legacy play is not relabeled or prohibited by target data.
    window.on_continue_button_clicked();
    launcher_start_action(window)->trigger();
    QVERIFY(
        window.quiz_started && !window.table_widget->active_gameplay_session()
    );
    QVERIFY(
        window.close()
    ); // protected records are still durable, not discarded
}

void gameplay_host_tests::start_rejection_keeps_choices() {
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1200, 800);
    window.show();
    window.new_gameplay_action->trigger();
    auto* view = window.table_widget;
    auto* owner = view->active_gameplay_session();
    const auto deck = owner->deck({ 0 })->configuration;
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    const auto bytes = QByteArrayLiteral("{\"version\":999}");
    storage.setValue(storage_key, bytes);
    storage.sync();
    launcher_start_action(window)->trigger();
    QCOMPARE(view->active_gameplay_session(), owner);
    QCOMPARE(owner->phase(), gameplay::session_phase::setup);
    QCOMPARE(owner->deck({ 0 })->configuration, deck);
    QVERIFY(!owner->deck({ 0 })->stream.cards.empty());
    QVERIFY(
        !view->gameplay_timer.isActive() && !window.gameplay_binding->sequence()
    );
    QVERIFY(!window.gameplay_storage_error.isEmpty());
    QCOMPARE(storage.value(storage_key).toByteArray(), bytes);
    // Simulate deliberate external resolution; the application never deletes
    // it.
    storage.remove(storage_key);
    storage.sync();
    window.gameplay_retry_action->trigger();
    launcher_start_action(window)->trigger();
    QVERIFY(window.gameplay_binding->owns(*view));
    QVERIFY(view->finish_gameplay_runtime());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
