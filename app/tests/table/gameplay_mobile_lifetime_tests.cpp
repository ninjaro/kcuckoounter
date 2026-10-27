// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_mobile_tests.hpp"

#include "settings/gameplay_recovery_fixture.hpp"
#include "shell/gameplay_reservation.hpp"
#include "shell/main_window.hpp"
#include "table/table_fixture.hpp"

#include <QMessageBox>
#include <QTimer>
#include <QtTest/QtTest>

using namespace table_test;
using namespace recovery_test;

void gameplay_mobile_tests::background_during_start() {
    table_test::launcher_preferences_guard preferences;
    main_window window;
    prepare_window(window);
    auto* view = window.table_widget;
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(2, true)));
    bool backgrounded = false;
    bool deferred = false;
    connect(view, &table::gameplay_runtime_updated, &window, [&] {
        if (!backgrounded
            && view->gameplay_owner->phase()
                == gameplay::session_phase::running) {
            backgrounded = true;
            window.apply_mobile_state(Qt::ApplicationHidden);
            deferred = window.mobile_checkpoint_pending
                && window.gameplay_storage_busy;
        }
    });
    QVERIFY(window.reserve_gameplay_from_ui(73));
    QVERIFY(backgrounded && deferred && !window.mobile_checkpoint_pending);
    QCOMPARE(view->gameplay_owner->phase(), gameplay::session_phase::paused);
    QVERIFY(!view->gameplay_timer.isActive());
    QSettings storage("ninjaro", "kcuckoounter");
    const auto saved = gameplay_recovery_service(storage, { 64, 64 }).load();
    QVERIFY(saved);
    QCOMPARE(
        std::get<gameplay_active_record>(*saved).checkpoint,
        *view->capture_gameplay_checkpoint()
    );
    QVERIFY(view->finish_gameplay_runtime());
}

void gameplay_mobile_tests::background_during_restore() {
    table_test::launcher_preferences_guard preferences;
    QSettings storage("ninjaro", "kcuckoounter");
    {
        table source;
        source.resize(1200, 900);
        QVERIFY(source.install_gameplay_session(owned_clock_fixture(2, true)));
        gameplay_reservation binding;
        gameplay_recovery_service recovery(storage, { 64, 64 });
        QVERIFY(binding.start(source, recovery, 73));
    }
    main_window window;
    auto* view = window.table_widget;
    bool backgrounded = false;
    connect(view, &table::gameplay_session_changed, &window, [&] {
        if (!backgrounded && window.gameplay_binding->owns(*view)) {
            backgrounded = true;
            window.apply_mobile_state(Qt::ApplicationHidden);
            QVERIFY(window.mobile_checkpoint_pending);
        }
    });
    prepare_window(window);
    QVERIFY(backgrounded && !window.mobile_checkpoint_pending);
    QCOMPARE(view->gameplay_owner->phase(), gameplay::session_phase::paused);
    QVERIFY(!view->gameplay_timer.isActive());
    const auto saved = gameplay_recovery_service(storage, { 64, 64 }).load();
    QVERIFY(saved);
    QCOMPARE(
        std::get<gameplay_active_record>(*saved).checkpoint,
        *view->capture_gameplay_checkpoint()
    );
    QVERIFY(view->finish_gameplay_runtime());
}

void gameplay_mobile_tests::background_retirement_data() {
    QTest::addColumn<bool>("destroy");
    QTest::newRow("clear") << false;
    QTest::newRow("destroy") << true;
}

void gameplay_mobile_tests::background_retirement() {
    QFETCH(bool, destroy);
    table_test::launcher_preferences_guard preferences;
    auto window = std::make_unique<main_window>();
    start_owned(*window);
    auto* view = window->table_widget;
    const auto binding = window->gameplay_binding;
    QSettings storage("ninjaro", "kcuckoounter");
    const auto before = storage.value(storage_key);
    bool retired = false;
    connect(view, &table::gameplay_runtime_updated, view, [&] {
        if (!retired
            && view->gameplay_owner->phase()
                == gameplay::session_phase::paused) {
            retired = true;
            if (destroy)
                window.reset();
            else
                view->clear_gameplay_session();
        }
    });
    window->apply_mobile_state(Qt::ApplicationHidden);
    QVERIFY(retired);
    if (window)
        QVERIFY(!binding->owns(*view));
    QCOMPARE(storage.value(storage_key), before);
    QVERIFY(!storage.contains(progress_key));
}

void gameplay_mobile_tests::modal_background_stays_paused_data() {
    QTest::addColumn<bool>("foreground");
    QTest::newRow("owned-hidden") << false;
    QTest::newRow("owned-foreground") << true;
}

void gameplay_mobile_tests::modal_background_stays_paused() {
    QFETCH(bool, foreground);
    table_test::launcher_preferences_guard preferences;
    main_window window;
    start_owned(window);
    bool answered = false;
    QTimer reply;
    reply.setInterval(0);
    connect(&reply, &QTimer::timeout, &window, [&] {
        // Android can delegate the dialog to native UI. The backing widget
        // need not appear in topLevelWidgets, and creation can process events.
        for (auto* widget : QApplication::allWidgets()) {
            if (auto* dialog = qobject_cast<QMessageBox*>(widget);
                dialog && dialog->isVisible()) {
                reply.stop();
                window.apply_mobile_state(Qt::ApplicationHidden);
                if (foreground)
                    window.apply_mobile_state(Qt::ApplicationActive);
                answered = true;
                dialog->done(QMessageBox::No);
                return;
            }
        }
    });
    reply.start();
    window.on_finish_triggered();
    QVERIFY(answered);
    QCOMPARE(
        window.table_widget->gameplay_owner->phase(),
        gameplay::session_phase::paused
    );
    QVERIFY(!window.table_widget->gameplay_timer.isActive());
    QVERIFY(window.table_widget->finish_gameplay_runtime());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
