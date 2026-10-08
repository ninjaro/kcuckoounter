// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_host_tests.hpp"

#include "settings/gameplay_recovery_fixture.hpp"
#include "shell/gameplay_reservation.hpp"
#include "shell/main_window.hpp"
#include "table/drill_fixture.hpp"
#include "table/table_fixture.hpp"

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QScopeGuard>
#include <QtTest/QtTest>

using namespace table_test;
using namespace recovery_test;

void gameplay_host_tests::failed_result_blocks_retirement_data() {
    QTest::addColumn<bool>("retire");
    QTest::newRow("finished-table") << false;
    QTest::newRow("retired-table") << true;
}

void gameplay_host_tests::failed_result_blocks_retirement() {
    QFETCH(bool, retire);
    launcher_preferences_guard preferences;
    main_window window;
    launch(window);
    auto* view = window.table_widget;
    const auto binding = window.gameplay_binding;
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    const auto before = storage.value(storage_key);
    const auto protected_bytes = QByteArrayLiteral("{\"version\":999}");
    storage.setValue(checkpoint_key, protected_bytes);
    storage.sync();
    QVERIFY(view->finish_gameplay_runtime());
    QVERIFY(binding->completion() && !binding->acknowledged());
    const auto exact = *binding->completion();
    if (retire)
        view->clear_gameplay_session(); // low-level retirement cannot lose
                                        // retry
    const auto scene = view->gameplay_scene();
    const auto* owner = view->active_gameplay_session();
    QVERIFY(window.gameplay_retry_action->isEnabled());
    QVERIFY(!window.new_gameplay_action->isEnabled());
    // Direct slot invocation exercises admission even for a stale enabled
    // action.
    QVERIFY(QMetaObject::invokeMethod(&window, "on_new_gameplay_triggered"));
    window.new_game_action->trigger();
    QVERIFY(!window.load_gameplay_drill(target_drill(2)));
    QVERIFY(!window.launch_training_drill(legacy_drill()));
    QAction* quit = nullptr;
    for (auto* action : window.game_menu->actions())
        if (action->shortcut() == QKeySequence(QKeySequence::Quit))
            quit = action;
    QVERIFY(quit); // KDE owns this standard action in its action collection.
    quit->trigger();
    QVERIFY(window.isVisible() && !window.close());
    QCOMPARE(view->gameplay_scene(), scene);
    QCOMPARE(view->active_gameplay_session(), owner);
    QCOMPARE(*binding->completion(), exact);
    QCOMPARE(storage.value(storage_key), before);
    QCOMPARE(storage.value(checkpoint_key).toByteArray(), protected_bytes);
    QVERIFY(!storage.contains(progress_key));
    window.on_table_rasterization_busy_changed(false);
    QCOMPARE(window.status_label->text(), window.gameplay_storage_error);
    // Explicit external resolution, not an application-side automatic
    // migration.
    storage.remove(checkpoint_key);
    storage.sync();
    window.gameplay_retry_action->trigger();
    QVERIFY(binding->acknowledged());
    const auto progress = gameplay_progress_service(storage, { 64, 64 }).load();
    QVERIFY(progress && progress->completed_sessions == 1);
    QCOMPARE(progress->recent_results.front(), exact);
    QVERIFY(window.gameplay_storage_error.isEmpty());
    window.new_gameplay_action->trigger();
    QCOMPARE(
        view->active_gameplay_session()->phase(), gameplay::session_phase::setup
    );
    QVERIFY(window.close());
}

void gameplay_host_tests::close_saves_continuation() {
    launcher_preferences_guard preferences;
    gameplay_checkpoint exact;
    {
        main_window window;
        launch(window);
        auto* view = window.table_widget;
        QVERIFY(tick(*view, 750));
        QVERIFY(view->set_gameplay_pick_interval(650));
        QVERIFY(window.close());
        QCOMPARE(
            view->active_gameplay_session()->phase(),
            gameplay::session_phase::paused
        );
        exact = *view->capture_gameplay_checkpoint();
    }
    main_window restored;
    restored.resize(900, 600);
    restored.show();
    QCoreApplication::processEvents();
    QVERIFY(
        restored.gameplay_binding && restored.gameplay_binding->sequence() == 1
    );
    QCOMPARE(*restored.table_widget->capture_gameplay_checkpoint(), exact);
    QVERIFY(!restored.table_widget->gameplay_timer.isActive());
    launcher_start_action(restored)->trigger();
    QVERIFY(restored.table_widget->finish_gameplay_runtime());
    QVERIFY(restored.gameplay_binding->acknowledged());
}

void gameplay_host_tests::close_io_failure_keeps_paused() {
#if !defined(Q_OS_UNIX)
    QSKIP("This I/O injection requires file-backed native settings");
#endif
    launcher_preferences_guard preferences;
    main_window window;
    launch(window);
    QVERIFY(tick(*window.table_widget, 750));
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    const auto before = storage.value(storage_key);
    // Make the native settings parent a regular file, preserving the original
    // directory for restoration. This interrupts the real settings path,
    // without chmod/root assumptions or a fake service used only by tests.
    const auto directory = QFileInfo(storage.fileName()).absolutePath();
    const auto backup = directory + QStringLiteral(".host-test-backup");
    QVERIFY(QDir().rename(directory, backup));
    QFile blocker(directory);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    bool restored = false;
    const auto cleanup = qScopeGuard([&] {
        if (!restored) {
            QFile::remove(directory);
            QDir().rename(backup, directory);
        }
    });
    QVERIFY(!window.close());
    QVERIFY(window.isVisible());
    QCOMPARE(
        window.table_widget->active_gameplay_session()->phase(),
        gameplay::session_phase::paused
    );
    QVERIFY(!window.table_widget->gameplay_timer.isActive());
    QVERIFY(!window.gameplay_storage_error.isEmpty());
    QSettings archived(
        QDir(backup).filePath(QFileInfo(storage.fileName()).fileName()),
        QSettings::NativeFormat
    );
    QCOMPARE(archived.value(storage_key), before);
    QVERIFY(QFile::remove(directory));
    QVERIFY(QDir().rename(backup, directory));
    restored = true;
    window.gameplay_retry_action->trigger();
    QVERIFY(window.gameplay_storage_error.isEmpty());
    QVERIFY(window.close());
    QSettings reopened(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    const auto saved = gameplay_recovery_service(reopened, { 64, 64 }).load();
    QVERIFY(saved);
    QCOMPARE(
        std::get<gameplay_active_record>(*saved).checkpoint,
        *window.table_widget->capture_gameplay_checkpoint()
    );
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
