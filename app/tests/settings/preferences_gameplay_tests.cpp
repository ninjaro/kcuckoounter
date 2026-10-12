// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/preferences_tests.hpp"

#include "settings/gameplay_preferences.hpp"
#include "settings/preferences.hpp"
#include "settings/preferences_fixture.hpp"
#include "table/gameplay_session.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <initializer_list>
#include <utility>

using namespace preferences_test;

namespace {

const QString defaults_key = QStringLiteral("gameplay_preferences/document");

QVariantMap other_settings(QSettings& settings) {
    QVariantMap result;
    for (const auto& key : settings.allKeys())
        if (key != defaults_key)
            result.insert(key, settings.value(key));
    return result;
}

} // namespace

void preferences_tests::gameplay_defaults_absence_preserves_legacy() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto settings = temporary_settings(directory);
    auto trainer = preferences_service::defaults(test_catalog());
    trainer.slot_count = 16;
    trainer.quiz_type = 1;
    trainer.wait_for_answers = true;
    trainer.palette = theme_palette_id::blue;
    preferences_service(settings).save(trainer, test_catalog());
    settings.setValue(
        QStringLiteral("training_drills/document"), QByteArray("legacy drills")
    );
    settings.setValue(
        QStringLiteral("trainer_session/payload"),
        QByteArray("legacy checkpoint")
    );
    settings.setValue(
        QStringLiteral("training_progress/recent_results"),
        QByteArray("legacy history")
    );
    settings.setValue(QStringLiteral("appearance/default_suit"), 3);
    const auto before = other_settings(settings);
    for (const std::size_t recommended : { 1U, 2U, 64U }) {
        QString error = QStringLiteral("old error");
        auto value = gameplay_preferences_service(settings, { recommended, 64 })
                         .load(&error);
        QVERIFY(value && error.isEmpty());
        QCOMPARE(value->slot_count, std::min(std::size_t { 4 }, recommended));
        QCOMPARE(value->configuration, gameplay::session_configuration {});
        QCOMPARE(value->pickup_interval_ms, 300);
        QVERIFY(!settings.contains(defaults_key));
        QCOMPARE(other_settings(settings), before);
    }
    // Generic legacy saves remain unversioned and do not migrate target data.
    QCOMPARE(preferences_service(settings).load(test_catalog()), trainer);
    QVERIFY(
        !settings.contains(QStringLiteral("trainer_preferences/schema_version"))
    );
    QCOMPARE(other_settings(settings), before);
}

void preferences_tests::gameplay_defaults_round_trip_data() {
    QTest::addColumn<int>("failure");
    QTest::addColumn<int>("source");
    QTest::addColumn<int>("scope");
    QTest::addColumn<int>("dealing");
    QTest::addColumn<bool>("pause");
    for (const int failure : { 0, 1 })
        for (const int source : { 0, 1 })
            for (const int scope : { 0, 1 })
                for (const int dealing : { 0, 1, 2 })
                    for (const bool pause : { false, true }) {
                        if (failure == 0 && !pause)
                            continue; // Lives always pauses globally.
                        QTest::newRow(qPrintable(
                            QStringLiteral(
                                "f=%1/src=%2/scope=%3/deal=%4/pause=%5"
                            )
                                .arg(failure)
                                .arg(source)
                                .arg(scope)
                                .arg(dealing)
                                .arg(pause)
                        )) << failure
                           << source << scope << dealing << pause;
                    }
}

void preferences_tests::gameplay_defaults_round_trip() {
    QFETCH(int, failure);
    QFETCH(int, source);
    QFETCH(int, scope);
    QFETCH(int, dealing);
    QFETCH(bool, pause);
    QTemporaryDir directory;
    auto settings = temporary_settings(directory);
    settings.setValue(
        QStringLiteral("desktop_shell/window/main_state"),
        QByteArray("native state")
    );
    settings.setValue(
        QStringLiteral("trainer_preferences/appearance/palette"),
        QStringLiteral("blue")
    );
    desktop_ui_preferences ui;
    ui.preset = desktop_ui_preset::quiet;
    ui.hud_override = desktop_hud_style::instruments;
    QVERIFY(save_desktop_ui_preferences(settings, ui));
    const auto before = other_settings(settings);
    gameplay_preferences expected;
    expected.slot_count = 24;
    expected.configuration = { static_cast<gameplay::failure_policy>(failure),
                               12,
                               static_cast<gameplay::quiz_source>(source),
                               static_cast<gameplay::quiz_scope>(scope),
                               pause,
                               source == 0,
                               static_cast<gameplay::dealing_mode>(dealing),
                               17,
                               scope == 1 };
    expected.pickup_interval_ms = 735;
    QString error;
    QVERIFY2(
        gameplay_preferences_service(settings, { 4, 64 })
            .save(expected, &error),
        qPrintable(error)
    );
    auto reopened = temporary_settings(directory);
    auto actual
        = gameplay_preferences_service(reopened, { 1, 64 }).load(&error);
    QVERIFY2(actual.has_value(), qPrintable(error));
    QCOMPARE(*actual, expected); // retain M/N even below current recommendation
    QVERIFY(error.isEmpty());
    const auto root
        = QJsonDocument::fromJson(settings.value(defaults_key).toByteArray())
              .object();
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 2);
    QCOMPARE(root.value(QStringLiteral("slots")).toInt(), 24);
    QStringList fields { QStringLiteral("version"),
                         QStringLiteral("slots"),
                         QStringLiteral("failure"),
                         QStringLiteral("lives"),
                         QStringLiteral("source"),
                         QStringLiteral("scope"),
                         QStringLiteral("global_pause"),
                         QStringLiteral("skip"),
                         QStringLiteral("dealing"),
                         QStringLiteral("sequential_count"),
                         QStringLiteral("allow_extra_slots"),
                         QStringLiteral("interval_ms") };
    fields.sort();
    QCOMPARE(root.keys(), fields); // no presentation, decks, cards or progress
    QCOMPARE(other_settings(settings), before);
    QCOMPARE(load_desktop_ui_preferences(settings), ui);
    ui.reset_overrides();
    QVERIFY(save_desktop_ui_preferences(settings, ui));
    QCOMPARE(
        *gameplay_preferences_service(settings, { 1, 64 }).load(), expected
    );
}

void preferences_tests::gameplay_defaults_validate_domain() {
    QTemporaryDir directory;
    auto settings = temporary_settings(directory);
    gameplay_preferences_service service(settings, { 1, 64 });
    for (const std::size_t count : { 1U, 24U, 32U, 64U }) {
        gameplay_preferences value;
        value.slot_count = count;
        value.configuration.failure = gameplay::failure_policy::block;
        value.configuration.global_quiz_pause = false;
        value.configuration.sequential_count = count;
        value.pickup_interval_ms = 1000;
        QVERIFY(service.save(value));
        QCOMPARE(*service.load(), value);
    }
    const auto before = settings.value(defaults_key);
    QString error;
    gameplay_preferences_service lower_cap(settings, { 1, 16 });
    QVERIFY(!lower_cap.load(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!lower_cap.save(gameplay_preferences {}, &error));
    QCOMPARE(settings.value(defaults_key), before);
    QVector<gameplay_preferences> invalid;
    for (const std::size_t count : { 0U, 65U }) {
        auto value = gameplay_preferences {};
        value.slot_count = count;
        invalid.append(value);
    }
    for (const std::size_t n : { 0U, 5U }) {
        auto value = gameplay_preferences {};
        value.configuration.sequential_count = n;
        invalid.append(value);
    }
    for (const int lives : { 0, 13 }) {
        auto value = gameplay_preferences {};
        value.configuration.initial_lives = lives;
        invalid.append(value);
    }
    for (const int interval : { 99, 1001 }) {
        auto value = gameplay_preferences {};
        value.pickup_interval_ms = interval;
        invalid.append(value);
    }
    auto value = gameplay_preferences {};
    value.configuration.global_quiz_pause = false;
    invalid.append(value);
    value = {};
    value.configuration.failure = static_cast<gameplay::failure_policy>(99);
    invalid.append(value);
    value = {};
    value.configuration.source = static_cast<gameplay::quiz_source>(99);
    invalid.append(value);
    value = {};
    value.configuration.scope = static_cast<gameplay::quiz_scope>(99);
    invalid.append(value);
    value = {};
    value.configuration.dealing = static_cast<gameplay::dealing_mode>(99);
    invalid.append(value);
    for (const auto& candidate : invalid) {
        QVERIFY(!service.save(candidate, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(settings.value(defaults_key), before);
    }
    for (const auto bounds :
         { gameplay::slot_limits { 0, 64 }, { 65, 64 }, { 1, 0 } }) {
        gameplay_preferences_service invalid_policy(settings, bounds);
        QVERIFY(!invalid_policy.load(&error));
        QVERIFY(!invalid_policy.save(gameplay_preferences {}, &error));
        QCOMPARE(settings.value(defaults_key), before);
    }
}

void preferences_tests::gameplay_defaults_reject_invalid_storage() {
    QTemporaryDir directory;
    auto settings = temporary_settings(directory);
    gameplay_preferences_service service(settings, { 4, 64 });
    QVERIFY(service.save(gameplay_preferences {}));
    const auto good = settings.value(defaults_key).toByteArray();
    const auto root = QJsonDocument::fromJson(good).object();
    QVector<QByteArray> invalid { QByteArray(), QByteArray("{"),
                                  QByteArray("[]"),
                                  QByteArray(16 * 1024 + 1, ' ') };
    const auto append = [&](QJsonObject object) {
        invalid.append(QJsonDocument(object).toJson(QJsonDocument::Compact));
    };
    for (const auto& key : root.keys()) {
        auto object = root;
        object.remove(key);
        append(object);
    }
    for (const auto* field :
         { "version", "slots", "lives", "sequential_count", "interval_ms" })
        for (const auto& number :
             { QJsonValue(true), QJsonValue("2"), QJsonValue(1.5),
               QJsonValue(-1), QJsonValue(1e100) }) {
            auto object = root;
            object.insert(QString::fromLatin1(field), number);
            append(object);
        }
    for (const auto* field : { "global_pause", "skip", "allow_extra_slots" })
        for (const auto& flag :
             { QJsonValue(1), QJsonValue("false"), QJsonValue() }) {
            auto object = root;
            object.insert(QString::fromLatin1(field), flag);
            append(object);
        }
    for (const auto* field : { "failure", "source", "scope", "dealing" })
        for (const auto& choice :
             { QJsonValue("unknown"), QJsonValue(0), QJsonValue(false) }) {
            auto object = root;
            object.insert(QString::fromLatin1(field), choice);
            append(object);
        }
    for (const auto& [key, number] :
         std::initializer_list<std::pair<QString, int>> {
             { QStringLiteral("version"), 1 },
             { QStringLiteral("version"), 3 },
             { QStringLiteral("slots"), 0 },
             { QStringLiteral("slots"), 65 },
             { QStringLiteral("lives"), 0 },
             { QStringLiteral("lives"), 13 },
             { QStringLiteral("sequential_count"), 0 },
             { QStringLiteral("sequential_count"), 5 },
             { QStringLiteral("interval_ms"), 99 },
             { QStringLiteral("interval_ms"), 1001 } }) {
        auto object = root;
        object.insert(key, number);
        append(object);
    }
    auto impossible = root;
    impossible.insert(QStringLiteral("global_pause"), false); // Lives invariant
    append(impossible);
    for (const auto& bytes : invalid) {
        settings.setValue(defaults_key, bytes);
        QString error;
        QVERIFY(!service.load(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!service.save(gameplay_preferences {}, &error));
        QCOMPARE(settings.value(defaults_key).toByteArray(), bytes);
    }
    settings.setValue(defaults_key, QString::fromUtf8(good));
    QVERIFY(!service.load()); // QByteArray storage, not a coercible string
    QVERIFY(!service.save(gameplay_preferences {}));
    QCOMPARE(settings.value(defaults_key).toString(), QString::fromUtf8(good));
}

void preferences_tests::gameplay_defaults_report_failed_write() {
    QTemporaryDir directory;
    // A directory cannot be replaced by an INI settings file. Exercise actual
    // QSettings I/O failure and rollback of the newly introduced in-memory key.
    QSettings settings(directory.path(), QSettings::IniFormat);
    QString error;
    QVERIFY(!gameplay_preferences_service(settings, { 4, 64 })
                 .save(gameplay_preferences {}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!settings.contains(defaults_key));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
