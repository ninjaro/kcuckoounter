// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/gameplay_recovery_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"
#include "settings/gameplay_recovery_fixture.hpp"

#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <limits>

using namespace gameplay;
using namespace recovery_test;

void gameplay_recovery_tests::reservation_rejects_replacement() {
    auto model = progress_test::ready_session();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    settings.setValue(
        QStringLiteral("trainer_session/payload"), QByteArray("legacy recovery")
    );
    settings.setValue(
        QStringLiteral("training_progress/recent_results"),
        QByteArray("legacy history")
    );
    settings.setValue(
        QStringLiteral("gameplay_preferences/document"), QByteArray("defaults")
    );
    QVariantMap before;
    for (const auto& key : settings.allKeys())
        before.insert(key, settings.value(key));
    gameplay_recovery_service service(settings, { 3, 64 });
    const auto initial = snapshot(model);
    QVERIFY(service.begin(initial) == 1);
    const auto stored = settings.value(storage_key);
    QString error;
    QVERIFY(!service.begin(initial, &error) && !error.isEmpty());
    QVERIFY(!service.save(2, initial, &error) && !error.isEmpty());
    QCOMPARE(settings.value(storage_key), stored);
    QCOMPARE(settings.allKeys().size(), before.size() + 1);
    for (const auto& key : before.keys())
        QCOMPARE(settings.value(key), before.value(key));
    auto invalid = initial;
    invalid.pickup_elapsed_ms = 300;
    QVERIFY(!service.save(1, invalid, &error) && !error.isEmpty());
    QCOMPARE(settings.value(storage_key), stored);
}

void gameplay_recovery_tests::save_rejects_rewind() {
    auto model = progress_test::ready_session({}, true);
    const auto initial = snapshot(model);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service service(settings, { 3, 64 });
    QVERIFY(service.begin(initial) == 1);
    drive(model, 16);
    const auto next = snapshot(model);
    QVERIFY(service.save(1, next));
    const auto stored = settings.value(storage_key);
    QString error;
    QVERIFY(!service.save(1, initial, &error) && !error.isEmpty());
    auto different = next;
    different.state.random_dealing.seed = 999;
    QVERIFY(encode_gameplay_checkpoint(different, { 3, 64 }));
    QVERIFY(!service.save(1, different, &error) && !error.isEmpty());
    different = next;
    different.state.decks[0].configuration.strategy_slug = "replacement";
    QVERIFY(encode_gameplay_checkpoint(different, { 3, 64 }));
    QVERIFY(!service.save(1, different, &error) && !error.isEmpty());
    different = next;
    ++different.state.decks[2].stream.infinite_cards->seed;
    QVERIFY(encode_gameplay_checkpoint(different, { 3, 64 }));
    QVERIFY(!service.save(1, different, &error) && !error.isEmpty());
    different = next;
    different.state.configuration.allow_skip = false;
    QVERIFY(encode_gameplay_checkpoint(different, { 3, 64 }));
    QVERIFY(!service.save(1, different, &error) && !error.isEmpty());
    different = next;
    different.state.decks[0].stream.cards.back() = 13;
    QVERIFY(encode_gameplay_checkpoint(different, { 3, 64 }));
    QVERIFY(!service.save(1, different, &error) && !error.isEmpty());
    different = next;
    different.state.decks[0].stream.rank_weights[12] = -5;
    QVERIFY(encode_gameplay_checkpoint(different, { 3, 64 }));
    QVERIFY(!service.save(1, different, &error) && !error.isEmpty());
    QCOMPARE(settings.value(storage_key), stored);
}

void gameplay_recovery_tests::codec_shapes() {
    auto model = progress_test::ready_session({}, true);
    const gameplay_recovery active
        = gameplay_active_record { 1, snapshot(model) };
    const gameplay_recovery pending = finish(model);
    for (const auto& value : { active, pending }) {
        const auto bytes = encode_gameplay_recovery(value, { 3, 64 });
        QVERIFY(bytes && decode_gameplay_recovery(*bytes, { 3, 64 }) == value);
        const auto good = QJsonDocument::fromJson(*bytes).object();
        for (const auto& key : good.keys()) {
            auto root = good;
            root.remove(key);
            QVERIFY(!decode_gameplay_recovery(document(root), { 3, 64 }));
            root = good;
            root.insert(key, QJsonValue::Null);
            QVERIFY(!decode_gameplay_recovery(document(root), { 3, 64 }));
        }
        auto root = good;
        root.insert(QStringLiteral("future"), 1);
        QVERIFY(!decode_gameplay_recovery(document(root), { 3, 64 }));
        root = good;
        root.insert(QStringLiteral("version"), 99);
        QVERIFY(!decode_gameplay_recovery(document(root), { 3, 64 }));
        root = good;
        root.insert(QStringLiteral("phase"), QStringLiteral("finished"));
        QVERIFY(!decode_gameplay_recovery(document(root), { 3, 64 }));
        root = good;
        auto payload = root.value(QStringLiteral("payload")).toObject();
        payload.insert(QStringLiteral("future"), 1);
        root.insert(QStringLiteral("payload"), payload);
        QVERIFY(!decode_gameplay_recovery(document(root), { 3, 64 }));
        root = good;
        root.insert(
            QStringLiteral("phase"),
            std::holds_alternative<gameplay_active_record>(value)
                ? QStringLiteral("pending")
                : QStringLiteral("active")
        );
        QVERIFY(!decode_gameplay_recovery(document(root), { 3, 64 }));
    }
    QVERIFY(!decode_gameplay_recovery(QByteArray("[]"), { 3, 64 }));
    QVERIFY(!decode_gameplay_recovery(QByteArray("broken"), { 3, 64 }));
}

void gameplay_recovery_tests::codec_identity_bounds() {
    auto model = progress_test::ready_session();
    const auto active
        = gameplay_active_record { std::numeric_limits<std::uint64_t>::max(),
                                   snapshot(model) };
    const auto bytes = encode_gameplay_recovery(active, { 3, 64 });
    QVERIFY(
        bytes
        && decode_gameplay_recovery(*bytes, { 3, 64 })
            == gameplay_recovery { active }
    );
    const auto root = QJsonDocument::fromJson(*bytes).object();
    for (const auto& value : QList<QJsonValue> {
             1, true, QJsonValue::Null, QStringLiteral("0"),
             QStringLiteral("01"), QStringLiteral("+1"), QStringLiteral(" 1"),
             QStringLiteral("1 "), QStringLiteral("-0"), QStringLiteral("-1"),
             QStringLiteral("1e0"), QStringLiteral("18446744073709551616") }) {
        auto bad = root;
        bad.insert(QStringLiteral("sequence"), value);
        QVERIFY(!decode_gameplay_recovery(document(bad), { 3, 64 }));
    }
    QVERIFY(!encode_gameplay_recovery(
        gameplay_active_record { 0, active.checkpoint }, { 3, 64 }
    ));
    QVERIFY(!decode_gameplay_recovery(*bytes, { 1, 2 }));
    generation_policy restricted;
    restricted.maximum_cards = 1;
    QVERIFY(!decode_gameplay_recovery(*bytes, { 3, 64 }, restricted));
    QVERIFY(!decode_gameplay_recovery(
        QByteArray(gameplay_recovery_service::maximum_document_bytes + 1, ' '),
        { 3, 64 }
    ));
    auto pending = finish(model);
    pending.completed_at_utc_ms = std::numeric_limits<std::int64_t>::max();
    const auto terminal = encode_gameplay_recovery(pending, { 3, 64 });
    QVERIFY(
        terminal
        && decode_gameplay_recovery(*terminal, { 3, 64 })
            == gameplay_recovery { pending }
    );
    auto bad = QJsonDocument::fromJson(*terminal).object();
    auto payload = bad.value(QStringLiteral("payload")).toObject();
    payload.insert(QStringLiteral("completed_at_utc_ms"), QStringLiteral("-0"));
    bad.insert(QStringLiteral("payload"), payload);
    QVERIFY(!decode_gameplay_recovery(document(bad), { 3, 64 }));
}

void gameplay_recovery_tests::legacy_checkpoint_policy() {
    auto model = progress_test::ready_session();
    const auto checkpoint = snapshot(model);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    gameplay_checkpoint_service legacy(settings, { 3, 64 });
    QVERIFY(legacy.save(checkpoint));
    const auto before = settings.value(checkpoint_key);
    gameplay_recovery_service service(settings, { 3, 64 });
    QString error;
    QVERIFY(!service.load(&error) && !error.isEmpty());
    QVERIFY(!service.begin(checkpoint, &error) && !error.isEmpty());
    QVERIFY(!service.save(1, checkpoint, &error) && !error.isEmpty());
    QVERIFY(!service.complete(finish(model), &error) && !error.isEmpty());
    QCOMPARE(settings.value(checkpoint_key), before);
    QVERIFY(!settings.contains(storage_key));
    QVERIFY(legacy.load() == checkpoint);
    // Damaged/future standalone records also remain protected, not adopted.
    settings.setValue(checkpoint_key, QByteArray("future checkpoint"));
    QVERIFY(!service.begin(checkpoint, &error) && !error.isEmpty());
    QCOMPARE(
        settings.value(checkpoint_key).toByteArray(),
        QByteArray("future checkpoint")
    );
}

void gameplay_recovery_tests::storage_conflicts() {
    auto model = progress_test::ready_session();
    const auto checkpoint = snapshot(model);
    const auto record = finish(model);
    const auto good = encode_gameplay_recovery(
                          gameplay_active_record { 1, checkpoint }, { 3, 64 }
    )
                          .value();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service service(settings, { 3, 64 });
    auto root = QJsonDocument::fromJson(good).object();
    root.insert(QStringLiteral("version"), 99);
    QList<QVariant> damaged { "wrong type", 7, QByteArray("broken"),
                              document(root) };
    root = QJsonDocument::fromJson(good).object();
    root.insert(QStringLiteral("sequence"), QStringLiteral("2"));
    damaged.append(document(root));
    for (const auto& value : damaged) {
        settings.setValue(storage_key, value);
        QString error;
        QVERIFY(!service.load(&error) && !error.isEmpty());
        QVERIFY(!service.begin(checkpoint, &error) && !error.isEmpty());
        QVERIFY(!service.save(1, checkpoint, &error) && !error.isEmpty());
        QVERIFY(!service.complete(record, &error) && !error.isEmpty());
        QCOMPARE(settings.value(storage_key), value);
    }
    settings.setValue(storage_key, good);
    QVERIFY(gameplay_progress_service(settings, { 3, 64 }).record(record));
    QString error;
    // An active sequence already admitted is stale, never allocated anew.
    QVERIFY(!service.load(&error) && !error.isEmpty());
    QVERIFY(!service.complete(record, &error) && !error.isEmpty());
    QCOMPARE(settings.value(storage_key).toByteArray(), good);
    settings.setValue(progress_key, QByteArray("future progress"));
    QVERIFY(!service.load(&error) && !error.isEmpty());
    QVERIFY(!service.begin(checkpoint, &error) && !error.isEmpty());
    QCOMPARE(
        settings.value(progress_key).toByteArray(),
        QByteArray("future progress")
    );
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
