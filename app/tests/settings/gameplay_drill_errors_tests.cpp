// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/preferences_tests.hpp"

#include "settings/gameplay_drill_fixture.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

using namespace preferences_test;

void preferences_tests::gameplay_drills_protect_invalid_storage() {
    QTemporaryDir directory;
    auto settings = temporary_settings(directory);
    gameplay_drill_service service(settings, { 1, 64 });
    QVERIFY(service.save({ sample_drill() }));
    const auto good = settings.value(gameplay_drills_key).toByteArray();
    const auto root = QJsonDocument::fromJson(good).object();
    const auto entry
        = root.value(QStringLiteral("drills")).toArray().first().toObject();
    const auto with_entry = [&](const QJsonValue& value) {
        auto result = root;
        result.insert(QStringLiteral("drills"), QJsonArray { value });
        return QJsonDocument(result).toJson();
    };
    const auto with_deck = [&](const QJsonValue& value) {
        auto result = entry;
        result.insert(
            QStringLiteral("decks"),
            QJsonArray { value,
                         entry.value(QStringLiteral("decks")).toArray().last() }
        );
        return with_entry(result);
    };
    QVector<QVariant> bad { QByteArray(),
                            QByteArray("{"),
                            QByteArray("[]"),
                            QByteArray(1024 * 1024 + 1, ' '),
                            QString::fromUtf8(good),
                            2 };
    for (const auto& key : root.keys()) {
        auto invalid = root;
        invalid.remove(key);
        bad.append(QJsonDocument(invalid).toJson());
    }
    for (const auto& version :
         { QJsonValue(1), QJsonValue(3), QJsonValue(2.5), QJsonValue("2") }) {
        auto invalid = root;
        invalid.insert(QStringLiteral("version"), version);
        bad.append(QJsonDocument(invalid).toJson());
    }
    for (const auto& key : entry.keys()) {
        auto invalid = entry;
        invalid.remove(key);
        bad.append(with_entry(invalid));
        invalid = entry;
        invalid.insert(key, QJsonValue::Null);
        bad.append(with_entry(invalid));
    }
    for (const auto* field : { "lives", "sequential_count", "interval_ms" })
        for (const auto& number :
             { QJsonValue(0), QJsonValue(-1), QJsonValue(1.5),
               QJsonValue(1e100), QJsonValue("1") }) {
            auto invalid = entry;
            invalid.insert(QString::fromLatin1(field), number);
            bad.append(with_entry(invalid));
        }
    for (const auto* field : { "failure", "source", "scope", "dealing" }) {
        auto invalid = entry;
        invalid.insert(QString::fromLatin1(field), QStringLiteral("unknown"));
        bad.append(with_entry(invalid));
    }
    for (const auto* field : { "global_pause", "skip", "allow_extra_slots" }) {
        auto invalid = entry;
        invalid.insert(QString::fromLatin1(field), QStringLiteral("false"));
        bad.append(with_entry(invalid));
    }
    auto invalid = entry;
    invalid.insert(QStringLiteral("failure"), QStringLiteral("lives"));
    bad.append(with_entry(invalid)); // Cannot silently enable global pause.
    for (const auto& name :
         { QString(), QStringLiteral(" padded "), QStringLiteral("line\nbreak"),
           QString(81, 'x') }) {
        invalid = entry;
        invalid.insert(QStringLiteral("name"), name);
        bad.append(with_entry(invalid));
    }
    bad.append(with_entry(false));
    invalid = entry;
    invalid.insert(QStringLiteral("decks"), QJsonArray {});
    bad.append(with_entry(invalid));
    const auto deck
        = entry.value(QStringLiteral("decks")).toArray().first().toObject();
    QJsonArray too_many_decks;
    for (int index = 0; index < 65; ++index)
        too_many_decks.append(deck);
    invalid.insert(QStringLiteral("decks"), too_many_decks);
    bad.append(with_entry(invalid));
    for (const auto& key : deck.keys()) {
        auto invalid_deck = deck;
        invalid_deck.remove(key);
        bad.append(with_deck(invalid_deck));
        invalid_deck = deck;
        invalid_deck.insert(key, QJsonValue::Null);
        bad.append(with_deck(invalid_deck));
    }
    for (const auto& number :
         { QJsonValue(0), QJsonValue(-1), QJsonValue(1.5), QJsonValue(1e100),
           QJsonValue(2147483648.0), QJsonValue("1") }) {
        auto invalid_deck = deck;
        invalid_deck.insert(QStringLiteral("count"), number);
        bad.append(with_deck(invalid_deck));
    }
    for (const auto* field : { "infinite", "training", "show_count" }) {
        auto invalid_deck = deck;
        invalid_deck.insert(
            QString::fromLatin1(field), QStringLiteral("false")
        );
        bad.append(with_deck(invalid_deck));
    }
    for (const auto& slug :
         { QString(), QStringLiteral(" padded "), QString(129, 'x') }) {
        auto invalid_deck = deck;
        invalid_deck.insert(QStringLiteral("strategy"), slug);
        bad.append(with_deck(invalid_deck));
    }
    auto invalid_deck = deck;
    invalid_deck.insert(QStringLiteral("show_count"), true); // Challenge deck.
    bad.append(with_deck(invalid_deck));
    bad.append(with_deck(4));
    auto duplicate = root;
    auto renamed = entry;
    renamed.insert(QStringLiteral("name"), sample_drill().name.toUpper());
    duplicate.insert(QStringLiteral("drills"), QJsonArray { entry, renamed });
    bad.append(QJsonDocument(duplicate).toJson());
    QJsonArray too_many_drills;
    for (int index = 0; index < 65; ++index) {
        renamed = entry;
        renamed.insert(QStringLiteral("name"), QString::number(index));
        too_many_drills.append(renamed);
    }
    duplicate.insert(QStringLiteral("drills"), too_many_drills);
    bad.append(QJsonDocument(duplicate).toJson());
    for (const auto& bytes : bad) {
        settings.setValue(gameplay_drills_key, bytes);
        QString error;
        QVERIFY(!service.load(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!service.save({ sample_drill() }, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(settings.value(gameplay_drills_key), bytes);
    }
    settings.setValue(gameplay_drills_key, good);
    QVERIFY(service.load());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
