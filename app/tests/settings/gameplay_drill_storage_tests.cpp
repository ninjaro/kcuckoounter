// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/preferences_tests.hpp"

#include "settings/gameplay_drill_fixture.hpp"
#include "settings/gameplay_preferences.hpp"
#include "settings/preferences.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include <limits>

using namespace preferences_test;

void preferences_tests::gameplay_drills_round_trip_data() {
    gameplay_defaults_round_trip_data(); // Same independent global policy
                                         // matrix.
}

void preferences_tests::gameplay_drills_round_trip() {
    QFETCH(int, failure);
    QFETCH(int, source);
    QFETCH(int, scope);
    QFETCH(int, dealing);
    QFETCH(bool, pause);
    QTemporaryDir directory;
    auto settings = temporary_settings(directory);
    gameplay_drill_service service(settings, { 1, 64 });
    auto expected = sample_drill();
    expected.configuration = {
        static_cast<gameplay::failure_policy>(failure),
        12,
        static_cast<gameplay::quiz_source>(source),
        static_cast<gameplay::quiz_scope>(scope),
        pause,
        source == 0,
        static_cast<gameplay::dealing_mode>(dealing),
        2,
        scope == 1,
    };
    QString error = QStringLiteral("old error");
    QVERIFY2(service.save({ expected }, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    auto reopened = temporary_settings(directory);
    const auto actual
        = gameplay_drill_service(reopened, { 64, 64 }).load(&error);
    QVERIFY2(actual && error.isEmpty(), qPrintable(error));
    QCOMPARE(*actual, QVector<gameplay_drill> { expected });
    const auto bytes = settings.value(gameplay_drills_key).toByteArray();
    const auto root = QJsonDocument::fromJson(bytes).object();
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 2);
    const auto entry
        = root.value(QStringLiteral("drills")).toArray().first().toObject();
    QCOMPARE(
        entry.size(), 12
    ); // Name, speed, deck array and nine global fields.
    const auto decks = entry.value(QStringLiteral("decks")).toArray();
    QCOMPARE(decks.size(), 2);
    QCOMPARE(decks.first().toObject().size(), 5);
    for (const auto* excluded :
         { "cards", "score", "position", "slot_mapping", "input", "elapsed",
           "seed", "targets", "palette", "orientation", "preset", "phase",
           "remaining_lives" })
        QVERIFY2(!bytes.contains(excluded), excluded);
}

void preferences_tests::gameplay_drills_keep_legacy_storage() {
    QTemporaryDir directory;
    auto settings = temporary_settings(directory);
    training_drill legacy;
    legacy.name = QStringLiteral("Daily");
    legacy.slot_settings
        = { { 1, false, QStringLiteral("first_strategy"), true } };
    training_drill_service old_service(settings);
    QVERIFY(old_service.save({ legacy }));
    QVERIFY(gameplay_preferences_service(settings, { 4, 64 }).save({}));
    for (const auto* key :
         { "trainer_preferences/sentinel", "desktop_ui/sentinel",
           "trainer_session/payload", "training_progress/sentinel",
           "desktop_shell/sentinel", "appearance/default_suit" })
        settings.setValue(QString::fromLatin1(key), QByteArray("untouched"));
    const auto before = other_drill_settings(settings);
    gameplay_drill_service service(settings, { 1, 64 });
    QString error = QStringLiteral("old error");
    const auto absent = service.load(&error);
    QVERIFY(absent && absent->isEmpty() && error.isEmpty());
    QVERIFY(!settings.contains(gameplay_drills_key));
    auto drill = sample_drill();
    drill.name
        = legacy.name; // Separate catalogue namespace, not v1 replacement.
    QVERIFY(service.save({ drill }));
    QCOMPARE(other_drill_settings(settings), before);
    QCOMPARE(*old_service.load(), QVector<training_drill> { legacy });
    const auto target_bytes = settings.value(gameplay_drills_key);
    legacy.name = QStringLiteral("Renamed old drill");
    QVERIFY(old_service.save({ legacy }));
    QCOMPARE(settings.value(gameplay_drills_key), target_bytes);
    const auto after_rename = other_drill_settings(settings);
    drill.decks.back().configuration.strategy_slug = "removed_strategy";
    QVERIFY(service.save({ drill }));
    QVERIFY(!supports_drill(drill, drill_catalog(), { 1, 64 }));
    QCOMPARE(service.load()->front(), drill);
    drill.name = QStringLiteral("Retained unavailable drill");
    QVERIFY(service.save({ drill }));
    QVERIFY(service.save({}));
    QVERIFY(service.load()->isEmpty());
    QCOMPARE(other_drill_settings(settings), after_rename);
    // An unrelated corrupt/future v1 document does not block target storage.
    settings.setValue(
        QStringLiteral("training_drills/document"), QByteArray("future v1 data")
    );
    QVERIFY(service.save({ sample_drill() }));
    QVERIFY(!old_service.load());
    QVERIFY(!old_service.save({ legacy }));
    QCOMPARE(
        settings.value(QStringLiteral("training_drills/document"))
            .toByteArray(),
        QByteArray("future v1 data")
    );
}

void preferences_tests::gameplay_drills_validate_configuration() {
    QTemporaryDir directory;
    auto settings = temporary_settings(directory);
    gameplay_drill_service service(settings, { 1, 64 });
    auto drill = sample_drill();
    for (const std::size_t count : { 1U, 24U, 32U, 64U }) {
        drill.decks.resize(count, drill.decks.back());
        drill.configuration.sequential_count = count;
        QVERIFY(service.save({ drill }));
        QCOMPARE(service.load()->front(), drill);
    }
    drill.decks.front().configuration.deck_count
        = static_cast<std::size_t>(std::numeric_limits<int>::max());
    QVERIFY(
        service.save({ drill })
    ); // Representation bound, not a shoe allocation.
    const auto before = settings.value(gameplay_drills_key);
    QString error;
    for (const gameplay::slot_limits limits :
         { gameplay::slot_limits { 0, 64 },
           { 65, 64 },
           { 1, 16 },
           { 1, std::numeric_limits<std::size_t>::max() } }) {
        gameplay_drill_service restricted(settings, limits);
        QVERIFY(!restricted.load(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!restricted.save({}, &error));
        QCOMPARE(settings.value(gameplay_drills_key), before);
    }
    QVector<gameplay_drill> invalid;
    for (const std::size_t count : { 0U, 65U }) {
        auto value = sample_drill();
        value.decks.resize(count, value.decks.back());
        invalid.append(value);
    }
    for (const std::size_t n : { 0U, 3U }) {
        auto value = sample_drill();
        value.configuration.sequential_count = n;
        invalid.append(value);
    }
    for (const int lives : { 0, 13 }) {
        auto value = sample_drill();
        value.configuration.initial_lives = lives;
        invalid.append(value);
    }
    for (const int interval : { 99, 1001 }) {
        auto value = sample_drill();
        value.pickup_interval_ms = interval;
        invalid.append(value);
    }
    for (const auto& name :
         { QString(), QStringLiteral(" padded "), QStringLiteral("line\nbreak"),
           QString(81, 'x') }) {
        auto value = sample_drill();
        value.name = name;
        invalid.append(value);
    }
    for (const std::size_t count :
         { std::size_t { 0 },
           static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1 }) {
        auto value = sample_drill();
        value.decks.front().configuration.deck_count = count;
        invalid.append(value);
    }
    for (const auto& slug : { std::string {}, std::string { " padded " },
                              std::string(129, 'x'), std::string(1, '\xff') }) {
        auto value = sample_drill();
        value.decks.front().configuration.strategy_slug = slug;
        invalid.append(value);
    }
    auto value = sample_drill();
    value.decks.front().show_count = true; // Never display Challenge answers.
    invalid.append(value);
    value = sample_drill();
    value.configuration.failure = gameplay::failure_policy::lives;
    invalid.append(value); // Lives requires global pause.
    value = sample_drill();
    value.configuration.failure = static_cast<gameplay::failure_policy>(99);
    invalid.append(value);
    value = sample_drill();
    value.configuration.source = static_cast<gameplay::quiz_source>(99);
    invalid.append(value);
    value = sample_drill();
    value.configuration.scope = static_cast<gameplay::quiz_scope>(99);
    invalid.append(value);
    value = sample_drill();
    value.configuration.dealing = static_cast<gameplay::dealing_mode>(99);
    invalid.append(value);
    for (const auto& candidate : invalid) {
        QVERIFY(!service.save({ candidate }, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(settings.value(gameplay_drills_key), before);
    }
    value = sample_drill();
    value.name = QStringLiteral("École");
    auto duplicate = value;
    duplicate.name = QStringLiteral("e\u0301COLE");
    QVERIFY(!service.save({ value, duplicate }));
    QCOMPARE(settings.value(gameplay_drills_key), before);
    QVector<gameplay_drill> many;
    for (int index = 0; index < gameplay_drill_service::maximum_drills;
         ++index) {
        value = sample_drill();
        value.name = QString::number(index);
        many.append(value);
    }
    QVERIFY(service.save(many));
    const auto catalogue_bytes = settings.value(gameplay_drills_key);
    many.append(sample_drill());
    QVERIFY(!service.save(many));
    QCOMPARE(settings.value(gameplay_drills_key), catalogue_bytes);
    many.removeLast();
    for (auto& entry : many) {
        auto deck = entry.decks.back();
        deck.configuration.strategy_slug
            = QString(128, QChar(0x4e00)).toStdString();
        entry.decks.assign(64, deck);
    }
    QVERIFY(
        !service.save(many, &error)
    ); // Valid values, but UTF-8 exceeds 1MiB.
    QVERIFY(error.contains(QStringLiteral("storage limit")));
    QCOMPARE(settings.value(gameplay_drills_key), catalogue_bytes);
}

void preferences_tests::gameplay_drills_report_failed_write() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings unwritable(directory.path(), QSettings::IniFormat);
    QString error;
    QVERIFY(!gameplay_drill_service(unwritable, { 1, 64 })
                 .save({ sample_drill() }, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!unwritable.contains(gameplay_drills_key));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
