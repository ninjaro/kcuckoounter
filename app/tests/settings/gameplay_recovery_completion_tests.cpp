// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/gameplay_recovery_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"
#include "settings/gameplay_recovery_fixture.hpp"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <limits>

using namespace gameplay;
using namespace recovery_test;

void gameplay_recovery_tests::completion_conflicts() {
    auto model = progress_test::ready_session();
    const auto initial = snapshot(model);
    auto earlier = model;
    const auto old_result = finish(earlier);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service service(settings, { 3, 64 });
    QVERIFY(service.begin(initial) == 1);
    drive(model, 16);
    QVERIFY(service.save(1, snapshot(model)));
    const auto active_bytes = settings.value(storage_key);
    QString error;
    QVERIFY(!service.complete(old_result, &error) && !error.isEmpty());
    const auto good = finish(model);
    auto different = good;
    different.result.decks[0].configuration.strategy_slug = "replacement";
    QVERIFY(!service.complete(different, &error) && !error.isEmpty());
    different = good;
    different.sequence = 2;
    QVERIFY(!service.complete(different, &error) && !error.isEmpty());
    different = good;
    different.completed_at_utc_ms = 0;
    QVERIFY(!service.complete(different, &error) && !error.isEmpty());
    QCOMPARE(settings.value(storage_key), active_bytes);
    QVERIFY(!settings.contains(progress_key));
    // Reopen the exact staged terminal record independently of an owner.
    settings.setValue(
        storage_key, encode_gameplay_recovery(good, { 3, 64 }).value()
    );
    const auto pending_bytes = settings.value(storage_key);
    const auto pending = service.load(&error);
    QVERIFY(
        pending && std::get<gameplay_result_record>(*pending) == good
        && error.isEmpty()
    );
    QVERIFY(!service.begin(initial, &error) && !error.isEmpty());
    QVERIFY(!service.save(1, initial, &error) && !error.isEmpty());
    different = good;
    ++different.completed_at_utc_ms;
    QVERIFY(!service.complete(different, &error) && !error.isEmpty());
    different = good;
    ++different.result.timing.play_time_ms;
    QVERIFY(!service.complete(different, &error) && !error.isEmpty());
    QCOMPARE(settings.value(storage_key), pending_bytes);
    QVERIFY(service.complete(good, &error) && error.isEmpty());
    QVERIFY(!settings.contains(storage_key));
}

void gameplay_recovery_tests::duplicate_preserves_new_session() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service service(settings, { 3, 64 });
    auto first = progress_test::ready_session();
    QVERIFY(service.begin(snapshot(first)) == 1);
    const auto recorded = finish(first);
    QVERIFY(service.complete(recorded));
    const auto progress_bytes = settings.value(progress_key);
    auto second = progress_test::ready_session({}, true);
    QVERIFY(service.begin(snapshot(second)) == 2);
    const auto next = settings.value(storage_key);
    QString error("old error");
    QVERIFY(service.complete(recorded, &error) && error.isEmpty());
    QCOMPARE(settings.value(storage_key), next);
    QCOMPARE(settings.value(progress_key), progress_bytes);
    QVERIFY(std::get<gameplay_active_record>(*service.load()).sequence == 2);
    auto conflict = recorded;
    ++conflict.completed_at_utc_ms;
    QVERIFY(!service.complete(conflict, &error) && !error.isEmpty());
    QCOMPARE(settings.value(storage_key), next);
    QVERIFY(service.complete(finish(second, 2)));
    // Simulate an externally retained pending record older than bounded
    // history.
    auto result = recorded;
    for (std::uint64_t i = 3; i <= 25; ++i) {
        result.sequence = i;
        QVERIFY(gameplay_progress_service(settings, { 3, 64 }).record(result));
    }
    const auto evicted = encode_gameplay_recovery(recorded, { 3, 64 }).value();
    settings.setValue(storage_key, evicted);
    QVERIFY(!service.load(&error) && !error.isEmpty());
    QVERIFY(!service.complete(recorded, &error) && !error.isEmpty());
    QCOMPARE(settings.value(storage_key).toByteArray(), evicted);
}

void gameplay_recovery_tests::full_width_sequence() {
    auto model = progress_test::ready_session();
    auto previous = model;
    const auto old = finish(previous);
    gameplay_progress progress;
    progress.completed_sessions = std::numeric_limits<std::uint64_t>::max() - 1;
    for (std::uint64_t i = 0; i < gameplay_progress::maximum_recent_results;
         ++i) {
        auto entry = old;
        entry.sequence = progress.completed_sessions - i;
        progress.recent_results.push_back(entry);
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    settings.setValue(
        progress_key, encode_gameplay_progress(progress, { 3, 64 }).value()
    );
    gameplay_recovery_service service(settings, { 3, 64 });
    const auto sequence = service.begin(snapshot(model));
    QVERIFY(sequence && *sequence == std::numeric_limits<std::uint64_t>::max());
    const auto active = std::get<gameplay_active_record>(*service.load());
    QCOMPARE(active.sequence, *sequence);
    const auto record = finish(model, *sequence);
    QVERIFY(service.complete(record));
    QCOMPARE(
        gameplay_progress_service(settings, { 3, 64 })
            .load()
            ->completed_sessions,
        *sequence
    );
    QVERIFY(service.complete(record));
    auto next = progress_test::ready_session();
    QString error;
    QVERIFY(!service.begin(snapshot(next), &error) && !error.isEmpty());
    QVERIFY(!settings.contains(storage_key));
}

void gameplay_recovery_tests::pending_admission_overflow() {
    auto model = progress_test::ready_session();
    auto previous = model;
    const auto old = finish(previous);
    gameplay_progress progress;
    progress.completed_sessions = 21;
    progress.timing.play_time_ms = std::numeric_limits<std::uint64_t>::max();
    for (std::uint64_t i = 0; i < gameplay_progress::maximum_recent_results;
         ++i) {
        auto entry = old;
        entry.sequence = progress.completed_sessions - i;
        progress.recent_results.push_back(entry);
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    const auto original = encode_gameplay_progress(progress, { 3, 64 }).value();
    settings.setValue(progress_key, original);
    gameplay_recovery_service service(settings, { 3, 64 });
    QVERIFY(service.begin(snapshot(model)) == 22);
    QVERIFY(model.advance_time(1).accepted);
    const auto record = finish(model, 22);
    QString error;
    QVERIFY(!service.complete(record, &error) && !error.isEmpty());
    const auto pending = service.load(&error);
    QVERIFY(
        pending && std::get<gameplay_result_record>(*pending) == record
        && error.isEmpty()
    );
    QVERIFY(!service.complete(record, &error) && !error.isEmpty());
    QVERIFY(
        !service.begin(snapshot(progress_test::ready_session()), &error)
        && !error.isEmpty()
    );
    QCOMPARE(settings.value(progress_key).toByteArray(), original);
    QVERIFY(service.load());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
