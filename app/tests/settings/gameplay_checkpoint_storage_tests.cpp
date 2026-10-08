// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/gameplay_checkpoint_tests.hpp"

#include "settings/gameplay_checkpoint_fixture.hpp"
#include "settings/session_checkpoint.hpp"

#include <QDataStream>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace checkpoint_test;

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

void gameplay_checkpoint_tests::storage_absence_and_legacy_isolation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    const QByteArray legacy(
        "legacy recovery remains unsupported for target gameplay"
    );
    settings.setValue(QStringLiteral("trainer_session/payload"), legacy);
    settings.setValue(
        QStringLiteral("gameplay_drills/document"), QByteArray("future drills")
    );
    settings.setValue(
        QStringLiteral("gameplay_preferences/document"), QByteArray("defaults")
    );
    settings.setValue(
        QStringLiteral("training_progress/recent_results"),
        QByteArray("history")
    );
    settings.setValue(
        QStringLiteral("desktop_shell/window/state"), QByteArray("geometry")
    );
    QVariantMap before;
    for (const auto& key : settings.allKeys())
        before.insert(key, settings.value(key));
    gameplay_checkpoint_service service(settings, { 1, 64 });
    QString error("old error");
    QVERIFY(!service.load(&error) && error.isEmpty());
    QVERIFY(!settings.contains(storage_key));
    QVERIFY(service.clear(&error) && error.isEmpty());
    QVERIFY(service.save(sample_checkpoint(), &error) && error.isEmpty());
    for (const auto& key : before.keys())
        QCOMPARE(settings.value(key), before.value(key));
    QCOMPARE(settings.allKeys().size(), before.size() + 1);
    QVERIFY(service.clear(&error) && error.isEmpty());
    QCOMPARE(settings.allKeys().size(), before.size());
    for (const auto& key : before.keys())
        QCOMPARE(settings.value(key), before.value(key));
}

void gameplay_checkpoint_tests::storage_reopen_and_clear() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto file = directory.filePath("settings.ini");
    QSettings settings(file, QSettings::IniFormat);
    trainer_session_checkpoint old;
    table_slot_session_state slot;
    slot.card = { 4, 1, false, { 0, 1, 2, 3 }, 1 };
    slot.strategy_slug = QStringLiteral("legacy_strategy");
    old.table.slot_states.append(slot);
    old.table.quiz_running = true; // Legacy means an active resumable game.
    QVERIFY(trainer_session_checkpoint_service(settings).save(old));
    const auto legacy_bytes
        = settings.value(QStringLiteral("trainer_session/payload"));
    gameplay_checkpoint_service service(settings, { 1, 64 });
    const auto expected = sample_checkpoint();
    QVERIFY(service.save(expected));
    QSettings reopened(file, QSettings::IniFormat);
    QString error;
    const auto actual
        = gameplay_checkpoint_service(reopened, { 3, 64 }).load(&error);
    QVERIFY(actual && error.isEmpty() && *actual == expected);
    QVERIFY(trainer_session_checkpoint_service(reopened).load() == old);
    QCOMPARE(
        reopened.value(QStringLiteral("trainer_session/payload")), legacy_bytes
    );
    QVERIFY(service.clear());
    reopened.sync();
    QVERIFY(!reopened.contains(storage_key));
    QVERIFY(trainer_session_checkpoint_service(reopened).load() == old);
}

void gameplay_checkpoint_tests::storage_protects_unreadable_records() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    const auto good
        = encode_gameplay_checkpoint(sample_checkpoint(), { 3, 64 }).value();
    QList<QVariant> damaged { QVariant("wrong storage type"), QVariant(7),
                              QByteArray("broken") };
    for (const auto* field : { "version", "continuation_revision" }) {
        auto root = QJsonDocument::fromJson(good).object();
        root.insert(QString::fromLatin1(field), 99);
        damaged.append(document(root));
    }
    auto root = QJsonDocument::fromJson(good).object();
    root.insert(QStringLiteral("future_state"), 1);
    damaged.append(document(root));
    gameplay_checkpoint_service service(settings, { 3, 64 });
    for (const auto& value : damaged) {
        settings.setValue(storage_key, value);
        QString error;
        QVERIFY(!service.load(&error) && !error.isEmpty());
        QVERIFY(!service.save(sample_checkpoint(), &error) && !error.isEmpty());
        QVERIFY(!service.clear(&error) && !error.isEmpty());
        QCOMPARE(settings.value(storage_key), value);
    }
    settings.setValue(storage_key, good);
    gameplay_checkpoint_service restricted(settings, { 1, 2 });
    QString error;
    QVERIFY(!restricted.load(&error) && !error.isEmpty());
    QVERIFY(!restricted.save(sample_checkpoint(), &error));
    QVERIFY(!restricted.clear(&error));
    QCOMPARE(settings.value(storage_key).toByteArray(), good);
    QVERIFY(service.load(&error) && error.isEmpty());
    auto invalid = sample_checkpoint();
    invalid.pickup_elapsed_ms = 300;
    QVERIFY(!service.save(invalid, &error) && !error.isEmpty());
    QCOMPARE(settings.value(storage_key).toByteArray(), good);
}

void gameplay_checkpoint_tests::storage_reports_failed_writes() {
    // Fail the actual QSettings writer, independently of OS/user permissions.
    static const auto format = QSettings::registerFormat(
        "checkpoint_test", read_settings, write_settings
    );
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto file = directory.filePath("settings.checkpoint_test");
    QSettings settings(file, format);
    gameplay_checkpoint_service service(settings, { 3, 64 });
    const auto original = sample_checkpoint();
    QVERIFY(service.save(original));
    const auto before = settings.value(storage_key);
    auto replacement = original;
    replacement.pickup_elapsed_ms = 1;
    reject_writes = true;
    QString error;
    const bool saved = service.save(replacement, &error);
    reject_writes = false;
    QVERIFY(!saved && !error.isEmpty());
    QCOMPARE(settings.value(storage_key), before);
    QSettings reopened(file, format);
    gameplay_checkpoint_service again(reopened, { 3, 64 });
    QVERIFY(again.load() == original);
    reject_writes = true;
    const bool cleared = again.clear(&error);
    reject_writes = false;
    QVERIFY(!cleared && !error.isEmpty());
    QCOMPARE(reopened.value(storage_key), before);
    QSettings empty(directory.filePath("empty.checkpoint_test"), format);
    gameplay_checkpoint_service absent(empty, { 3, 64 });
    reject_writes = true;
    const bool first_write = absent.save(original, &error);
    reject_writes = false;
    QVERIFY(!first_write && !error.isEmpty());
    QVERIFY(!empty.contains(storage_key));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
