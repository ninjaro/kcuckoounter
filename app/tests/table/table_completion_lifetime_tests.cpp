// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_completion_tests.hpp"

#include "settings/gameplay_recovery_fixture.hpp"
#include "shell/gameplay_reservation.hpp"
#include "table/table_fixture.hpp"

#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <type_traits>

using namespace gameplay;
using namespace recovery_test;
using namespace table_test;

static_assert(!std::is_copy_constructible_v<gameplay_reservation>);
static_assert(!std::is_move_constructible_v<gameplay_reservation>);

void gameplay_completion_tests::terminal_precedes_retirement_data() {
    QTest::addColumn<int>("retirement");
    QTest::newRow("updated-clear") << 0;
    QTest::newRow("updated-replacement") << 1;
    QTest::newRow("updated-destruction") << 2;
    QTest::newRow("answer-destruction") << 3;
}

void gameplay_completion_tests::terminal_precedes_retirement() {
    QFETCH(int, retirement);
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath("completion.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service recovery(storage, { 1, 64 });
    auto view = std::make_unique<table>();
    view->resize(1000, 800);
    QVERIFY(view->install_gameplay_session(
        retirement == 3 ? lives_fixture() : owned_clock_fixture(1)
    ));
    gameplay_reservation binding;
    QVERIFY(binding.start(*view, recovery, 91));
    view->stop_gameplay_clock();
    if (retirement == 3) {
        QVERIFY(tick(*view, 300));
        auto* owner = view->active_gameplay_session();
        for (const auto id : owner->unresolved_quizzes())
            QVERIFY(view->edit_gameplay_quiz(
                *view->gameplay_quiz_prompt(id),
                owner->deck(id)->running_count - 1
            ));
    }
    bool retained = false;
    bool retired = false;
    const auto retire = [&] {
        if (!binding.completion() || retired)
            return;
        retained = binding.completion()->result
            == *view->active_gameplay_session()->result();
        retired = true; // Ignore the replacement's own notification.
        if (retirement == 0)
            view->clear_gameplay_session();
        else if (retirement == 1)
            retired = view->install_gameplay_session(owned_clock_fixture(2));
        else
            view.reset();
    };
    if (retirement == 3)
        QObject::connect(
            view.get(), &table::gameplay_quiz_resolved, view.get(), retire
        );
    else
        QObject::connect(
            view.get(), &table::gameplay_runtime_updated, view.get(), retire
        );
    if (retirement == 3)
        QVERIFY(tick(*view, session::quiz_initial_ms));
    else
        QVERIFY(view->finish_gameplay_runtime());
    QVERIFY(retained && retired && binding.completion());
    if (view)
        QVERIFY(!binding.owns(*view));
    const auto exact = *binding.completion();
    QVERIFY(binding.complete(recovery));
    QVERIFY(binding.acknowledged() && binding.completion() == exact);
    const auto progress = gameplay_progress_service(storage, { 1, 64 }).load();
    QVERIFY(progress && progress->recent_results.front() == exact);
    if (view && retirement == 1) {
        QVERIFY(view->start_gameplay_runtime(101));
        QVERIFY(view->finish_gameplay_runtime());
        QVERIFY(
            binding.completion() == exact
        ); // Never adopt replacement output.
    }
}

void gameplay_completion_tests::finish_during_start_retains_result() {
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath("completion.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    gameplay_reservation binding;
    bool finished = false;
    bool reentry_refused = false;
    QObject::connect(&view, &table::gameplay_runtime_updated, &view, [&] {
        if (!finished) {
            finished = true;
            finished = view.finish_gameplay_runtime();
            QString error;
            reentry_refused
                = !binding.complete(recovery, &error) && !error.isEmpty();
        }
    });
    QVERIFY(binding.start(view, recovery, 91));
    QVERIFY(finished && reentry_refused && binding.completion());
    QCOMPARE(view.active_gameplay_session()->phase(), session_phase::finished);
    const auto exact = *binding.completion();
    QVERIFY(binding.complete(recovery));
    QVERIFY(binding.completion() == exact && binding.acknowledged());
}

void gameplay_completion_tests::stale_scene_cannot_complete() {
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath("completion.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    gameplay_reservation old;
    QVERIFY(old.start(view, recovery, 91));
    QVERIFY(view.pause_gameplay_runtime());
    const auto active = storage.value(storage_key);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    QVERIFY(view.start_gameplay_runtime(92));
    QVERIFY(view.finish_gameplay_runtime());
    QVERIFY(!old.owns(view) && !old.completion());
    QString error;
    QVERIFY(!old.complete(recovery, &error) && !error.isEmpty());
    QCOMPARE(storage.value(storage_key), active);
    QVERIFY(!storage.contains(progress_key));
}

void gameplay_completion_tests::binding_teardown_retires_hook() {
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath("completion.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    {
        gameplay_reservation binding;
        QVERIFY(binding.start(view, recovery, 91));
        QVERIFY(view.gameplay_completion);
    }
    QVERIFY(!view.gameplay_completion);
    QVERIFY(view.finish_gameplay_runtime()); // No dangling binding callback.
    QVERIFY(!storage.contains(progress_key));
    view.clear_gameplay_session();
    // A retiring old binding cannot disconnect a newly recovered scene.
    auto old = std::make_unique<gameplay_reservation>();
    QCOMPARE(
        old->recover(view, recovery),
        gameplay_reservation::recovery_outcome::paused
    );
    view.clear_gameplay_session();
    gameplay_reservation current;
    QCOMPARE(
        current.recover(view, recovery),
        gameplay_reservation::recovery_outcome::paused
    );
    old.reset();
    QVERIFY(view.gameplay_completion);
    QVERIFY(view.finish_gameplay_runtime());
    QVERIFY(current.completion() && current.complete(recovery));
}

void gameplay_completion_tests::completion_requires_terminal() {
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath("completion.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    gameplay_reservation unbound;
    QString error;
    QVERIFY(!unbound.complete(recovery, &error) && !error.isEmpty());
    QVERIFY(view.start_gameplay_runtime(91));
    QVERIFY(view.finish_gameplay_runtime());
    QVERIFY(!unbound.completion());
    QVERIFY(!unbound.complete(recovery, &error) && !error.isEmpty());
    QVERIFY(storage.allKeys().isEmpty());
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    gameplay_reservation bound;
    QVERIFY(bound.start(view, recovery, 92));
    const auto bytes = storage.value(storage_key);
    QVERIFY(!bound.complete(recovery, &error) && !error.isEmpty());
    QVERIFY(view.pause_gameplay_runtime());
    QVERIFY(!bound.complete(recovery, &error) && !error.isEmpty());
    QVERIFY(!bound.completion() && !bound.acknowledged());
    QCOMPARE(storage.value(storage_key), bytes);
    QVERIFY(!storage.contains(progress_key));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
