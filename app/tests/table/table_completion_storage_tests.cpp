// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_completion_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"
#include "settings/gameplay_recovery_fixture.hpp"
#include "shell/gameplay_reservation.hpp"
#include "table/table_fixture.hpp"

#include <QDataStream>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace recovery_test;
using namespace table_test;

int gameplay_completion_tests::write_failure = 0;
QStringList gameplay_completion_tests::attempts;
std::function<void()> gameplay_completion_tests::on_write;

bool gameplay_completion_tests::read_settings(
    QIODevice& device, QSettings::SettingsMap& map
) {
    QDataStream stream(&device);
    stream >> map;
    return stream.status() == QDataStream::Ok;
}

bool gameplay_completion_tests::write_settings(
    QIODevice& device, const QSettings::SettingsMap& map
) {
    const auto phase
        = QJsonDocument::fromJson(map.value(storage_key).toByteArray())
              .object()
              .value(QStringLiteral("phase"))
              .toString();
    const bool progress = map.contains(progress_key);
    attempts.append(
        (phase.isEmpty() ? QStringLiteral("absent") : phase)
        + (progress ? QStringLiteral("+progress") : QStringLiteral("+empty"))
    );
    if (on_write)
        on_write();
    if ((write_failure == 1 && phase == "pending" && !progress)
        || (write_failure == 2 && phase == "pending" && progress)
        || (write_failure == 3 && !map.contains(storage_key) && progress))
        return false;
    QDataStream stream(&device);
    stream << map;
    return stream.status() == QDataStream::Ok;
}

void gameplay_completion_tests::write_failures_keep_record_data() {
    QTest::addColumn<int>("failure");
    QTest::newRow("terminal-staging") << 1;
    QTest::newRow("progress-admission") << 2;
    QTest::newRow("pending-cleanup") << 3;
}

void gameplay_completion_tests::write_failures_keep_record() {
    QFETCH(int, failure);
    static const auto format = QSettings::registerFormat(
        "terminal_failure", read_settings, write_settings
    );
    QTemporaryDir directory;
    const auto file = directory.filePath("settings.terminal_failure");
    auto view = std::make_unique<table>();
    view->resize(1000, 800);
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(2)));
    gameplay_reservation binding;
    gameplay_result_record exact;
    {
        QSettings storage(file, format);
        gameplay_recovery_service recovery(storage, { 1, 64 });
        QVERIFY(binding.start(*view, recovery, 91));
        const auto active = storage.value(storage_key);
        view->stop_gameplay_clock();
        QVERIFY(tick(*view, 300));
        QVERIFY(view->finish_gameplay_runtime());
        QVERIFY(binding.completion());
        exact = *binding.completion();
        attempts.clear();
        QString error;
        {
            const QScopedValueRollback<int> injection(write_failure, failure);
            QVERIFY(!binding.complete(recovery, &error));
        }
        QVERIFY(
            !error.isEmpty() && !binding.acknowledged()
            && binding.completion() == exact
        );
        QCOMPARE(storage.contains(progress_key), failure == 3);
        const auto current = decode_gameplay_recovery(
            storage.value(storage_key).toByteArray(), { 1, 64 }
        );
        QVERIFY(current);
        if (failure == 1) {
            QCOMPARE(storage.value(storage_key), active);
            QVERIFY(std::holds_alternative<gameplay_active_record>(*current));
        } else {
            QVERIFY(std::get<gameplay_result_record>(*current) == exact);
        }
        const QStringList ordered { "pending+empty", "pending+progress",
                                    "absent+progress" };
        QCOMPARE(attempts, ordered.mid(0, failure));
        // Retirement cannot erase the retry payload or grant a new Begin.
        view.reset();
        QVERIFY(binding.completion() == exact && binding.sequence() == 1);
    }
    QSettings reopened(file, format);
    gameplay_recovery_service again(reopened, { 1, 64 });
    QString error;
    const auto current = again.load(&error);
    QVERIFY(current && error.isEmpty());
    if (failure != 1)
        QVERIFY(std::get<gameplay_result_record>(*current) == exact);
    QVERIFY(
        !again.begin(snapshot(progress_test::ready_session()), &error)
        && !error.isEmpty()
    );
    attempts.clear();
    if (failure == 1) {
        // Staging never became durable. The same live shell retains its result;
        // a crash instead could only recover the last active checkpoint.
        QVERIFY(binding.complete(again, &error) && error.isEmpty());
        QCOMPARE(
            attempts,
            QStringList(
                { "pending+empty", "pending+progress", "absent+progress" }
            )
        );
    } else {
        // A fresh process-like binding retries the actual durable pending
        // record.
        table host;
        host.resize(1000, 800);
        gameplay_reservation restarted;
        QCOMPARE(
            restarted.recover(host, again, &error),
            gameplay_reservation::recovery_outcome::completed
        );
        QVERIFY(
            error.isEmpty() && !restarted.completion() && !restarted.sequence()
        );
        QCOMPARE(
            attempts,
            failure == 2
                ? QStringList({ "pending+progress", "absent+progress" })
                : QStringList({ "absent+progress" })
        );
        attempts.clear();
        QVERIFY(binding.complete(again, &error) && error.isEmpty());
        QVERIFY(attempts.isEmpty());
    }
    QVERIFY(binding.acknowledged() && binding.completion() == exact);
    QVERIFY(!reopened.contains(storage_key));
    const auto progress = gameplay_progress_service(reopened, { 1, 64 }).load();
    QVERIFY(
        progress && progress->completed_sessions == 1
        && progress->recent_results.front() == exact
    );
    const auto bytes = reopened.value(progress_key);
    attempts.clear();
    QVERIFY(binding.complete(again, &error) && error.isEmpty());
    QVERIFY(attempts.isEmpty());
    QCOMPARE(reopened.value(progress_key), bytes);
}

void gameplay_completion_tests::completion_blocks_reentry() {
    static const auto format = QSettings::registerFormat(
        "terminal_reentry", read_settings, write_settings
    );
    QTemporaryDir directory;
    QSettings storage(directory.filePath("settings.terminal_reentry"), format);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    gameplay_reservation binding;
    QVERIFY(binding.start(view, recovery, 91));
    QVERIFY(view.finish_gameplay_runtime());
    const auto exact = *binding.completion();
    bool rejected = true;
    const auto cleanup = qScopeGuard([] { on_write = {}; });
    on_write = [&] {
        QString error;
        rejected = rejected && !binding.complete(recovery, &error)
            && !error.isEmpty();
        rejected = rejected && !binding.start(view, recovery, 92, &error)
            && !error.isEmpty();
        rejected = rejected
            && binding.recover(view, recovery, &error)
                == gameplay_reservation::recovery_outcome::failed
            && !error.isEmpty() && binding.completion() == exact
            && !binding.acknowledged();
    };
    attempts.clear();
    QString error;
    QVERIFY(binding.complete(recovery, &error) && error.isEmpty());
    QVERIFY(
        rejected && binding.acknowledged() && binding.completion() == exact
    );
    QCOMPARE(
        attempts,
        QStringList({ "pending+empty", "pending+progress", "absent+progress" })
    );
}

void gameplay_completion_tests::aborted_start_retires_hook() {
    static const auto format = QSettings::registerFormat(
        "terminal_abort", read_settings, write_settings
    );
    QTemporaryDir directory;
    QSettings storage(directory.filePath("settings.terminal_abort"), format);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    gameplay_reservation binding;
    bool mutated = false;
    const auto cleanup = qScopeGuard([] { on_write = {}; });
    on_write = [&] {
        mutated = view.active_gameplay_session()->set_pick_interval_ms(500);
    };
    QString error;
    const bool started = binding.start(view, recovery, 91, &error);
    on_write = {};
    QVERIFY(mutated && !started && !error.isEmpty() && binding.sequence() == 1);
    QVERIFY(!view.gameplay_completion);
    const auto active = storage.value(storage_key);
    QVERIFY(view.start_gameplay_runtime(92));
    QVERIFY(!binding.owns(view));
    QVERIFY(view.finish_gameplay_runtime());
    QVERIFY(!binding.completion());
    QCOMPARE(storage.value(storage_key), active);
    QVERIFY(!storage.contains(progress_key));
}

void gameplay_completion_tests::conflicting_pending_preserves_result() {
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath("completion.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    gameplay_reservation binding;
    QVERIFY(binding.start(view, recovery, 91));
    QVERIFY(view.finish_gameplay_runtime());
    const auto exact = *binding.completion();
    auto conflict = exact;
    ++conflict.completed_at_utc_ms;
    const auto different = *encode_gameplay_recovery(conflict, { 1, 64 });
    storage.setValue(storage_key, different);
    storage.sync();
    QString error;
    QVERIFY(!binding.complete(recovery, &error) && !error.isEmpty());
    QVERIFY(binding.completion() == exact && !binding.acknowledged());
    QCOMPARE(storage.value(storage_key).toByteArray(), different);
    QVERIFY(!storage.contains(progress_key));
    storage.setValue(storage_key, *encode_gameplay_recovery(exact, { 1, 64 }));
    QVERIFY(binding.complete(recovery, &error) && error.isEmpty());
    QVERIFY(recovery.begin(snapshot(progress_test::ready_session())) == 2);
    const auto next = storage.value(storage_key);
    const auto progress = storage.value(progress_key);
    QVERIFY(binding.complete(recovery, &error) && error.isEmpty());
    QCOMPARE(storage.value(storage_key), next);
    QCOMPARE(storage.value(progress_key), progress);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
