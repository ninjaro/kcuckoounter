// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/preferences_tests.hpp"

#include "settings/preferences.hpp"
#include "settings/session_checkpoint.hpp"
#include "settings/strategy_data.hpp"
#include "settings/training_progress.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "settings/preferences_fixture.hpp"

using namespace preferences_test;

void preferences_tests::drills_round_trip_without_presentation_or_progress() {
    QTemporaryDir directory;
    auto settings = temporary_settings(directory);
    settings.setValue(
        QStringLiteral("desktop_ui/preset"), QStringLiteral("quiet")
    );
    settings.setValue(
        QStringLiteral("trainer_preferences/appearance/palette"),
        QStringLiteral("blue")
    );
    settings.setValue(QStringLiteral("training_progress/sentinel"), 17);
    settings.setValue(
        QStringLiteral("trainer_session/payload"), QByteArray("checkpoint")
    );
    const auto keys = settings.allKeys();
    QVariantMap unrelated;
    for (const auto& key : keys)
        unrelated.insert(key, settings.value(key));
    training_drill_service service(settings);
    QString error;
    const auto empty = service.load(&error);
    QVERIFY(empty && empty->isEmpty());
    QCOMPARE(settings.allKeys(), keys);
    training_drill drill;
    drill.name = QStringLiteral("Mixed shoes ♥");
    drill.quiz_type = 1;
    drill.wait_for_answers = true;
    drill.allow_skipping = false;
    drill.dealing_mode = 2;
    drill.pickup_interval_ms = 735;
    drill.slot_settings
        = { { 1, false, QStringLiteral("first_strategy"), false },
            { 16, true, QStringLiteral("renamed_strategy"), true } };
    QVERIFY(is_drill_configuration_supported(drill, test_catalog()));
    QVERIFY2(service.save({ drill }, &error), qPrintable(error));
    auto reopened = temporary_settings(directory);
    const auto loaded = training_drill_service(reopened).load(&error);
    QVERIFY(loaded);
    QCOMPARE(*loaded, QVector<training_drill> { drill });
    const auto payload
        = settings.value(QStringLiteral("training_drills/document"))
              .toByteArray();
    for (const auto* excluded :
         { "palette", "orientation", "deck_position", "score",
           "show_card_indexing", "elapsed", "preset" })
        QVERIFY(!payload.contains(excluded));
    for (auto it = unrelated.cbegin(); it != unrelated.cend(); ++it)
        QCOMPARE(settings.value(it.key()), it.value());
    auto unavailable = drill;
    unavailable.name = QStringLiteral("Old catalogue entry");
    unavailable.slot_settings.last().strategy_slug
        = QStringLiteral("removed_strategy");
    QVERIFY(!is_drill_configuration_supported(unavailable, test_catalog()));
    QVERIFY(service.save({ drill, unavailable }, &error));
    QCOMPARE(service.load()->size(), 2);
    QVERIFY(service.save({}, &error));
    QVERIFY(service.load()->isEmpty());
    QSettings unwritable(
        directory.path(), QSettings::IniFormat
    ); // path is a directory
    QVERIFY(!training_drill_service(unwritable).save({ drill }, &error));
    QVERIFY(!error.isEmpty());
}

void preferences_tests::drills_reject_invalid_storage_without_overwriting() {
    QTemporaryDir directory;
    auto settings = temporary_settings(directory);
    training_drill_service service(settings);
    training_drill drill;
    drill.name = QStringLiteral("Daily");
    drill.slot_settings
        = { { 4, false, QStringLiteral("first_strategy"), false } };
    QVERIFY(service.save({ drill }));
    const auto key = QStringLiteral("training_drills/document");
    const auto good = settings.value(key).toByteArray();
    const auto root = QJsonDocument::fromJson(good).object();
    QVector<QByteArray> bad { QByteArray(), QByteArray("{"),
                              QByteArray(1024 * 1024 + 1, ' ') };
    auto future = root;
    future.insert(QStringLiteral("version"), 2);
    bad.append(QJsonDocument(future).toJson());
    const auto entry
        = root.value(QStringLiteral("drills")).toArray().first().toObject();
    const auto with_entry = [&](QJsonObject value) {
        auto result = root;
        result.insert(QStringLiteral("drills"), QJsonArray { value });
        return QJsonDocument(result).toJson();
    };
    for (const auto& field :
         { QStringLiteral("quiz"), QStringLiteral("dealing"),
           QStringLiteral("interval_ms") }) {
        for (const auto& value : { QJsonValue(1.5), QJsonValue(1e100),
                                   QJsonValue("1"), QJsonValue(-1) }) {
            auto invalid = entry;
            invalid.insert(field, value);
            bad.append(with_entry(invalid));
        }
    }
    for (const auto& field :
         { QStringLiteral("wait"), QStringLiteral("skip") }) {
        auto invalid = entry;
        invalid.insert(field, QStringLiteral("false"));
        bad.append(with_entry(invalid));
    }
    auto invalid = entry;
    invalid.insert(QStringLiteral("slots"), QJsonArray {});
    bad.append(with_entry(invalid));
    invalid = entry;
    invalid.insert(QStringLiteral("quiz"), 1); // multi-question must pause
    bad.append(with_entry(invalid));
    for (const auto& bytes : bad) {
        settings.setValue(key, bytes);
        QString error;
        QVERIFY(!service.load(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!service.save({ drill }, &error));
        QCOMPARE(settings.value(key).toByteArray(), bytes);
    }
    settings.setValue(key, good);
    auto duplicate = drill;
    duplicate.name = QStringLiteral("DAILY");
    QVERIFY(!service.save({ drill, duplicate }));
    QCOMPARE(settings.value(key).toByteArray(), good);
    for (const auto& name :
         { QString(), QStringLiteral(" padded "), QStringLiteral("line\nbreak"),
           QString(81, 'x') }) {
        duplicate.name = name;
        QVERIFY(!service.save({ duplicate }));
    }
    for (const int decks : { 0, 17 }) {
        duplicate = drill;
        duplicate.slot_settings.first().deck_count = decks;
        QVERIFY(!service.save({ duplicate }));
    }
    duplicate = drill;
    duplicate.slot_settings
        = QVector<drill_slot_preferences>(17, drill.slot_settings.first());
    QVERIFY(!service.save({ duplicate }));
    QVector<training_drill> many;
    for (int i = 0; i < training_drill_service::maximum_drills; ++i) {
        duplicate = drill;
        duplicate.name = QString::number(i);
        many.append(duplicate);
    }
    QVERIFY(service.save(many));
    many.append(drill);
    QVERIFY(!service.save(many));
    QCOMPARE(service.load()->size(), training_drill_service::maximum_drills);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
