// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/gameplay_progress_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"
#include "settings/training_progress.hpp"

#include <QDataStream>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <limits>

using namespace progress_test;

namespace {
bool reject_writes = false;

bool read_settings(QIODevice& device, QSettings::SettingsMap& map) {
    QDataStream stream(&device);
    stream >> map;
    return stream.status() == QDataStream::Ok;
}

bool write_settings(QIODevice& device, const QSettings::SettingsMap& map) {
    if (reject_writes)
        return false;
    QDataStream stream(&device);
    stream << map;
    return stream.status() == QDataStream::Ok;
}
} // namespace

void gameplay_progress_tests::history_reopen_and_retries() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto file = directory.filePath("settings.ini");
    QSettings settings(file, QSettings::IniFormat);
    gameplay_progress_service service(settings, { 1, 64 });
    QString error("old error");
    QVERIFY(!service.load(&error) && error.isEmpty());
    QVERIFY(!settings.contains(storage_key));
    const auto result = sample_result();
    for (std::uint64_t i = 1; i <= 25; ++i) {
        const gameplay_result_record record {
            i, 1'700'000'000'000 - static_cast<std::int64_t>(i), result
        };
        QVERIFY(service.record(record, &error) && error.isEmpty());
        const auto before = settings.value(storage_key);
        QVERIFY(service.record(record, &error) && error.isEmpty());
        QCOMPARE(settings.value(storage_key), before);
    }
    QSettings reopened(file, QSettings::IniFormat);
    gameplay_progress_service again(reopened, { 3, 64 });
    const auto restored = again.load(&error);
    QVERIFY(restored && error.isEmpty());
    QCOMPARE(restored->completed_sessions, 25U);
    QCOMPARE(restored->recent_results.size(), 20U);
    QCOMPARE(restored->recent_results.front().sequence, 25U);
    QCOMPARE(restored->recent_results.back().sequence, 6U);
    QCOMPARE(restored->timing.play_time_ms, 25 * result.timing.play_time_ms);
    const auto before = reopened.value(storage_key);
    QVERIFY(again.record(restored->recent_results.back()));
    auto conflict = restored->recent_results.front();
    ++conflict.completed_at_utc_ms;
    QVERIFY(!again.record(conflict, &error) && !error.isEmpty());
    QVERIFY(
        !again.record({ 1, 1'700'000'000'000, result }, &error)
        && !error.isEmpty()
    );
    QVERIFY(
        !again.record({ 27, 1'700'000'000'000, result }, &error)
        && !error.isEmpty()
    );
    QCOMPARE(reopened.value(storage_key), before);
}

void gameplay_progress_tests::legacy_history_remains_readable() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    training_progress_service legacy(settings);
    QVERIFY(legacy.record({ 7, 10, 1000, 1'700'000'000'000 }));
    const auto old = legacy.load();
    settings.setValue(
        QStringLiteral("trainer_session/payload"), QByteArray("legacy recovery")
    );
    settings.setValue(
        QStringLiteral("gameplay_checkpoint/document"),
        QByteArray("future checkpoint")
    );
    settings.setValue(
        QStringLiteral("gameplay_preferences/document"), QByteArray("defaults")
    );
    settings.setValue(
        QStringLiteral("gameplay_drills/document"), QByteArray("drills")
    );
    QVariantMap before;
    for (const auto& key : settings.allKeys())
        before.insert(key, settings.value(key));
    gameplay_progress_service service(settings, { 3, 64 });
    QVERIFY(!service.load());
    QVERIFY(service.record(progress_for(sample_result()).recent_results[0]));
    QCOMPARE(legacy.load(), old);
    QCOMPARE(settings.allKeys().size(), before.size() + 1);
    for (const auto& key : before.keys())
        QCOMPARE(settings.value(key), before.value(key));
    const auto target = service.load();
    const auto target_bytes = settings.value(storage_key);
    QVERIFY(legacy.record({ 9, 10, 900, 1'700'000'001'000 }));
    QVERIFY(service.load() == target);
    QCOMPARE(settings.value(storage_key), target_bytes);
    QCOMPARE(legacy.load().correct_answers, 16);
}

void gameplay_progress_tests::lifetime_sequence_bounds() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    const auto result = sample_result();
    gameplay_progress progress;
    progress.completed_sessions = std::numeric_limits<std::uint64_t>::max();
    progress.timing.play_time_ms = std::numeric_limits<std::uint64_t>::max();
    for (std::uint64_t i = 0; i < gameplay_progress::maximum_recent_results;
         ++i)
        progress.recent_results.push_back(
            { progress.completed_sessions - i, 1, result }
        );
    const auto bytes = encode_gameplay_progress(progress, { 3, 64 });
    QVERIFY(bytes && decode_gameplay_progress(*bytes, { 3, 64 }) == progress);
    settings.setValue(storage_key, *bytes);
    gameplay_progress_service service(settings, { 3, 64 });
    QVERIFY(service.record(progress.recent_results.front()));
    QString error;
    QVERIFY(!service.record({ 0, 1, result }, &error) && !error.isEmpty());
    QCOMPARE(settings.value(storage_key).toByteArray(), *bytes);
    // The full retained sequence window still needs all retained time.
    progress.timing.play_time_ms = result.timing.play_time_ms * 19;
    QVERIFY(!encode_gameplay_progress(progress, { 3, 64 }));
}

void gameplay_progress_tests::storage_protects_unreadable_records() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    const auto progress = progress_for(sample_result());
    const auto bytes = encode_gameplay_progress(progress, { 3, 64 }).value();
    QList<QVariant> bad { "wrong type", 7, QByteArray("broken") };
    auto root = QJsonDocument::fromJson(bytes).object();
    root.insert(QStringLiteral("version"), 99);
    bad.append(document(root));
    root = QJsonDocument::fromJson(bytes).object();
    root.insert(QStringLiteral("future_state"), 1);
    bad.append(document(root));
    gameplay_progress_service service(settings, { 3, 64 });
    for (const auto& value : bad) {
        settings.setValue(storage_key, value);
        QString error;
        QVERIFY(!service.load(&error) && !error.isEmpty());
        QVERIFY(
            !service.record(progress.recent_results[0], &error)
            && !error.isEmpty()
        );
        QCOMPARE(settings.value(storage_key), value);
    }
    settings.setValue(storage_key, bytes);
    gameplay_progress_service restricted(settings, { 1, 2 });
    QString error;
    QVERIFY(
        !restricted.record({ 2, 1'700'000'000'001, sample_result() }, &error)
        && !error.isEmpty()
    );
    QCOMPARE(settings.value(storage_key).toByteArray(), bytes);
}

void gameplay_progress_tests::storage_overflow_is_atomic() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    auto result = sample_result();
    result.timing.play_time_ms = std::numeric_limits<std::uint64_t>::max();
    gameplay_progress_service service(settings, { 3, 64 });
    QVERIFY(service.record({ 1, 1, result }));
    const auto before = settings.value(storage_key);
    QString error;
    QVERIFY(
        !service.record({ 2, 2, sample_result() }, &error) && !error.isEmpty()
    );
    QCOMPARE(settings.value(storage_key), before);
    auto invalid = sample_result();
    invalid.challenge.errors = 1;
    QVERIFY(!service.record({ 2, 2, invalid }, &error) && !error.isEmpty());
    QCOMPARE(settings.value(storage_key), before);
}

void gameplay_progress_tests::storage_reports_failed_writes() {
    static const auto format = QSettings::registerFormat(
        "progress_test", read_settings, write_settings
    );
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto file = directory.filePath("settings.progress_test");
    QSettings settings(file, format);
    gameplay_progress_service service(settings, { 3, 64 });
    const auto first = progress_for(sample_result()).recent_results[0];
    QVERIFY(service.record(first));
    const auto before = settings.value(storage_key);
    auto second = first;
    second.sequence = 2;
    QString error;
    reject_writes = true;
    const bool saved = service.record(second, &error);
    reject_writes = false;
    QVERIFY(!saved && !error.isEmpty());
    QCOMPARE(settings.value(storage_key), before);
    QSettings reopened(file, format);
    QVERIFY(
        gameplay_progress_service(reopened, { 3, 64 }).load()
        == progress_for(first.result)
    );
    QSettings empty(directory.filePath("empty.progress_test"), format);
    gameplay_progress_service absent(empty, { 3, 64 });
    reject_writes = true;
    const bool created = absent.record(first, &error);
    reject_writes = false;
    QVERIFY(!created && !error.isEmpty());
    QVERIFY(!empty.contains(storage_key));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
