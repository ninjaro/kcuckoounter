// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_restore_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"
#include "settings/gameplay_recovery_fixture.hpp"
#include "shell/gameplay_reservation.hpp"
#include "table/table_fixture.hpp"

#include <QDataStream>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace recovery_test;
using outcome = gameplay_reservation::recovery_outcome;

int gameplay_restore_tests::write_failure = 0;
std::function<void()> gameplay_restore_tests::on_write;

bool gameplay_restore_tests::read_settings(
    QIODevice& device, QSettings::SettingsMap& map
) {
    QDataStream stream(&device);
    stream >> map;
    return stream.status() == QDataStream::Ok;
}

bool gameplay_restore_tests::write_settings(
    QIODevice& device, const QSettings::SettingsMap& map
) {
    if (on_write)
        on_write();
    if ((write_failure == 1 && map.contains(progress_key))
        || (write_failure == 2 && !map.contains(storage_key)))
        return false;
    QDataStream stream(&device);
    stream << map;
    return stream.status() == QDataStream::Ok;
}

void gameplay_restore_tests::pending_retries_without_scene_data() {
    QTest::addColumn<bool>("admitted");
    QTest::newRow("before-admission") << false;
    QTest::newRow("after-admission") << true;
}

void gameplay_restore_tests::pending_retries_without_scene() {
    QFETCH(bool, admitted);
    auto model = progress_test::ready_session();
    const auto record = finish(model);
    QTemporaryDir directory;
    const auto file = directory.filePath("recovery.ini");
    QVariant progress_bytes;
    {
        QSettings storage(file, QSettings::IniFormat);
        storage.setValue(
            storage_key, *encode_gameplay_recovery(record, { 1, 64 })
        );
        if (admitted) {
            QVERIFY(
                gameplay_progress_service(storage, { 1, 64 }).record(record)
            );
            progress_bytes = storage.value(progress_key);
        }
        storage.sync();
    }
    QSettings reopened(file, QSettings::IniFormat);
    gameplay_recovery_service recovery(reopened, { 1, 64 });
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(table_test::owned_clock_fixture(1)));
    QVERIFY(view.start_gameplay_runtime(9));
    QVERIFY(view.pause_gameplay_runtime());
    const auto checkpoint = view.capture_gameplay_checkpoint();
    const auto scene = view.gameplay_scene();
    auto* owner = view.active_gameplay_session();
    gameplay_reservation binding;
    QSignalSpy changed(&view, &table::gameplay_session_changed);
    QSignalSpy legacy_end(&view, &table::game_over);
    QString error("old diagnostic");
    QCOMPARE(binding.recover(view, recovery, &error), outcome::completed);
    QVERIFY(error.isEmpty() && !binding.sequence());
    QCOMPARE(view.active_gameplay_session(), owner);
    QCOMPARE(view.gameplay_scene(), scene);
    QVERIFY(view.capture_gameplay_checkpoint() == checkpoint);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(legacy_end.count(), 0);
    QVERIFY(!reopened.contains(storage_key));
    const auto progress = gameplay_progress_service(reopened, { 1, 64 }).load();
    QVERIFY(
        progress && progress->completed_sessions == 1
        && progress->recent_results
            == std::vector<gameplay_result_record> { record }
    );
    if (admitted)
        QCOMPARE(reopened.value(progress_key), progress_bytes);
    const auto bytes = reopened.value(progress_key);
    QCOMPARE(binding.recover(view, recovery, &error), outcome::absent);
    QVERIFY(error.isEmpty());
    QCOMPARE(reopened.value(progress_key), bytes);
    QCOMPARE(changed.count(), 0);
}

void gameplay_restore_tests::pending_failures_reopen_exactly_data() {
    QTest::addColumn<int>("failure");
    QTest::newRow("progress-write") << 1;
    QTest::newRow("pending-cleanup") << 2;
}

void gameplay_restore_tests::pending_failures_reopen_exactly() {
    QFETCH(int, failure);
    static const auto format = QSettings::registerFormat(
        "dispatch_failure", read_settings, write_settings
    );
    auto model = progress_test::ready_session();
    const auto record = finish(model);
    const auto pending = *encode_gameplay_recovery(record, { 1, 64 });
    QTemporaryDir directory;
    const auto file = directory.filePath("settings.dispatch_failure");
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(table_test::owned_clock_fixture(1)));
    const auto scene = view.gameplay_scene();
    auto* owner = view.active_gameplay_session();
    QSignalSpy changed(&view, &table::gameplay_session_changed);
    gameplay_reservation binding;
    {
        QSettings storage(file, format);
        storage.setValue(storage_key, pending);
        storage.sync();
        QCOMPARE(storage.status(), QSettings::NoError);
        gameplay_recovery_service recovery(storage, { 1, 64 });
        QString error;
        {
            const QScopedValueRollback<int> injection(write_failure, failure);
            QCOMPARE(binding.recover(view, recovery, &error), outcome::failed);
        }
        QVERIFY(!error.isEmpty() && !binding.sequence());
        QCOMPARE(storage.value(storage_key).toByteArray(), pending);
        QCOMPARE(storage.contains(progress_key), failure == 2);
        QCOMPARE(view.active_gameplay_session(), owner);
        QCOMPARE(view.gameplay_scene(), scene);
        QCOMPARE(changed.count(), 0);
    }
    QSettings reopened(file, format);
    gameplay_recovery_service again(reopened, { 1, 64 });
    const auto current = again.load();
    QVERIFY(current && std::get<gameplay_result_record>(*current) == record);
    QString error;
    QCOMPARE(binding.recover(view, again, &error), outcome::completed);
    QVERIFY(error.isEmpty() && !binding.sequence());
    QVERIFY(!reopened.contains(storage_key));
    const auto progress = gameplay_progress_service(reopened, { 1, 64 }).load();
    QVERIFY(
        progress && progress->completed_sessions == 1
        && progress->recent_results
            == std::vector<gameplay_result_record> { record }
    );
    QCOMPARE(view.active_gameplay_session(), owner);
    QCOMPARE(changed.count(), 0);
}

void gameplay_restore_tests::pending_dispatch_blocks_reentry() {
    static const auto format = QSettings::registerFormat(
        "dispatch_reentry", read_settings, write_settings
    );
    auto model = progress_test::ready_session();
    const auto record = finish(model);
    QTemporaryDir directory;
    QSettings storage(directory.filePath("settings.dispatch_reentry"), format);
    storage.setValue(storage_key, *encode_gameplay_recovery(record, { 1, 64 }));
    storage.sync();
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(table_test::owned_clock_fixture(1)));
    gameplay_reservation binding;
    QSignalSpy changed(&view, &table::gameplay_session_changed);
    int notifications = 0;
    bool rejected = true;
    const auto cleanup = qScopeGuard([] { on_write = {}; });
    on_write = [&] {
        ++notifications;
        QString error;
        const auto nested = binding.recover(view, recovery, &error);
        rejected = rejected && nested == outcome::failed && !error.isEmpty();
        rejected = rejected && !binding.start(view, recovery, 9, &error)
            && !error.isEmpty() && !binding.sequence();
    };
    QString error;
    QCOMPARE(binding.recover(view, recovery, &error), outcome::completed);
    QVERIFY(error.isEmpty() && rejected && notifications == 2);
    QCOMPARE(changed.count(), 0);
    QVERIFY(!storage.contains(storage_key));
    const auto progress = gameplay_progress_service(storage, { 1, 64 }).load();
    QVERIFY(
        progress && progress->completed_sessions == 1
        && progress->recent_results
            == std::vector<gameplay_result_record> { record }
    );
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
