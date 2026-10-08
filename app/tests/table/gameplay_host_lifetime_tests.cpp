// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_host_tests.hpp"

#include "settings/gameplay_recovery_fixture.hpp"
#include "shell/gameplay_reservation.hpp"
#include "shell/main_window.hpp"
#include "table/table_fixture.hpp"

#include <QDialog>
#include <QtTest/QtTest>

using namespace table_test;
using namespace recovery_test;

void gameplay_host_tests::start_retirement_keeps_reservation_data() {
    QTest::addColumn<bool>("destroy");
    QTest::newRow("clear-scene") << false;
    QTest::newRow("destroy-window") << true;
}

void gameplay_host_tests::start_retirement_keeps_reservation() {
    QFETCH(bool, destroy);
    launcher_preferences_guard preferences;
    auto window = std::make_unique<main_window>();
    window->resize(1200, 800);
    window->show();
    window->new_gameplay_action->trigger();
    auto* view = window->table_widget;
    bool retired = false;
    connect(view, &table::gameplay_runtime_updated, view, [&] {
        if (!retired) {
            retired = true;
            if (destroy)
                window.reset();
            else
                view->clear_gameplay_session();
        }
    });
    launcher_start_action(*window)->trigger();
    QVERIFY(retired);
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    const auto record = gameplay_recovery_service(storage, { 64, 64 }).load();
    QVERIFY(record && std::holds_alternative<gameplay_active_record>(*record));
    QCOMPARE(
        std::get<gameplay_active_record>(*record).sequence, std::uint64_t { 1 }
    );
    if (window) {
        QVERIFY(!window->gameplay_binding->owns(*view));
        QVERIFY(!window->gameplay_storage_error.isEmpty());
        window->gameplay_retry_action->trigger();
        QVERIFY(window->gameplay_binding->owns(*view));
        QCOMPARE(
            view->active_gameplay_session()->phase(),
            gameplay::session_phase::paused
        );
    }
}

void gameplay_host_tests::terminal_retirement_keeps_retry_data() {
    QTest::addColumn<bool>("replace");
    QTest::newRow("clear-before-runtime-notification") << false;
    QTest::newRow("replace-before-runtime-notification") << true;
}

void gameplay_host_tests::terminal_retirement_keeps_retry() {
    QFETCH(bool, replace);
    launcher_preferences_guard preferences;
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    {
        table source;
        source.resize(1000, 800);
        auto model = owned_clock_fixture(
            3, true, gameplay::dealing_mode::simultaneous,
            gameplay::quiz_scope::multi, { 1 }
        );
        auto config = model->configuration();
        config.failure = gameplay::failure_policy::lives;
        config.initial_lives = 1;
        QVERIFY(model->configure(config));
        QVERIFY(source.install_gameplay_session(std::move(model)));
        gameplay_reservation initial;
        gameplay_recovery_service recovery(storage, { 64, 64 });
        QVERIFY(initial.start(source, recovery, 73));
        source.stop_gameplay_clock();
        QVERIFY(tick(source, 300));
        QVERIFY(source.pause_gameplay_runtime());
        QVERIFY(recovery.save(1, *source.capture_gameplay_checkpoint()));
    }
    main_window window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();
    auto* view = window.table_widget;
    const auto binding = window.gameplay_binding;
    QVERIFY(binding && binding->owns(*view));
    launcher_start_action(window)->trigger();
    view->stop_gameplay_clock();
    auto* owner = view->active_gameplay_session();
    for (const auto id : owner->unresolved_quizzes())
        QVERIFY(view->edit_gameplay_quiz(
            *view->gameplay_quiz_prompt(id), owner->deck(id)->running_count - 1
        ));
    bool retired = false;
    connect(view, &table::gameplay_quiz_resolved, view, [&] {
        if (!retired && view->active_gameplay_session()->result()) {
            retired = true;
            if (replace)
                QVERIFY(view->install_gameplay_session(owned_clock_fixture(2)));
            else
                view->clear_gameplay_session();
        }
    });
    QVERIFY(tick(*view, gameplay::session::quiz_initial_ms));
    QVERIFY(retired && binding->completion() && binding->acknowledged());
    const auto progress = gameplay_progress_service(storage, { 64, 64 }).load();
    QVERIFY(progress && progress->completed_sessions == 1);
    QCOMPARE(progress->recent_results.front(), *binding->completion());
    QVERIFY(!storage.contains(storage_key));
    QVERIFY(!binding->owns(*view));
    if (replace) {
        QCOMPARE(
            view->active_gameplay_session()->phase(),
            gameplay::session_phase::setup
        );
        QVERIFY(!view->has_gameplay_runtime());
    }
}

void gameplay_host_tests::recovery_finish_is_admitted() {
    launcher_preferences_guard preferences;
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    {
        table source;
        source.resize(1000, 800);
        QVERIFY(source.install_gameplay_session(owned_clock_fixture(1)));
        gameplay_reservation initial;
        gameplay_recovery_service recovery(storage, { 64, 64 });
        QVERIFY(initial.start(source, recovery, 73));
    }
    main_window window;
    window.resize(1200, 800);
    bool finished = false;
    connect(
        window.table_widget, &table::gameplay_session_changed, &window, [&] {
            if (!finished && window.table_widget->has_gameplay_runtime()) {
                finished = true;
                QVERIFY(window.table_widget->finish_gameplay_runtime());
            }
        }
    );
    window.show();
    QCoreApplication::processEvents();
    QVERIFY(finished && window.gameplay_recovery_ready);
    QVERIFY(window.gameplay_binding->acknowledged());
    QVERIFY(window.gameplay_storage_error.isEmpty());
    QVERIFY(!storage.contains(storage_key));
    const auto progress = gameplay_progress_service(storage, { 64, 64 }).load();
    QVERIFY(progress && progress->completed_sessions == 1);
}

void gameplay_host_tests::nested_finish_is_admitted() {
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1200, 800);
    window.show();
    window.new_gameplay_action->trigger();
    auto* view = window.table_widget;
    bool finished = false;
    connect(view, &table::gameplay_runtime_updated, view, [&] {
        if (!finished
            && view->active_gameplay_session()->phase()
                == gameplay::session_phase::running) {
            finished = true;
            QVERIFY(view->finish_gameplay_runtime());
        }
    });
    launcher_start_action(window)->trigger();
    QVERIFY(finished);
    QVERIFY(
        window.gameplay_binding->completion()
        && window.gameplay_binding->acknowledged()
    );
    QVERIFY(!view->gameplay_timer.isActive());
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    const auto progress = gameplay_progress_service(storage, { 64, 64 }).load();
    QVERIFY(progress && progress->completed_sessions == 1);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
