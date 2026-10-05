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
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "settings/preferences_fixture.hpp"

using namespace preferences_test;

void preferences_tests::
    session_checkpoint_round_trip_preserves_exact_progress() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    trainer_session_checkpoint_service service(settings);
    const trainer_session_checkpoint expected = test_checkpoint();

    QVERIFY(service.save(expected));
    const auto actual = service.load();
    QVERIFY(actual.has_value());
    QCOMPARE(*actual, expected);
}

void preferences_tests::
    session_checkpoint_rejects_duplicate_or_missing_cards() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    trainer_session_checkpoint_service service(settings);
    QVERIFY(service.save(test_checkpoint()));

    QJsonObject root = stored_checkpoint_root(settings);
    QJsonObject table_object = root.value(QStringLiteral("table")).toObject();
    QJsonArray slot_array
        = table_object.value(QStringLiteral("slots")).toArray();
    QJsonObject slot_object = slot_array.first().toObject();
    QJsonObject card_object
        = slot_object.value(QStringLiteral("card")).toObject();
    card_object.insert(
        QStringLiteral("deck"), QJsonArray { 0, 0, 0, 0, 0, 0, 0, 0 }
    );
    slot_object.insert(QStringLiteral("card"), card_object);
    slot_array[0] = slot_object;
    table_object.insert(QStringLiteral("slots"), slot_array);
    root.insert(QStringLiteral("table"), table_object);
    store_checkpoint_root(settings, root);

    QVERIFY(!service.load().has_value());
}

void preferences_tests::session_checkpoint_rejects_huge_numeric_values() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    trainer_session_checkpoint_service service(settings);
    QVERIFY(service.save(test_checkpoint()));

    QJsonObject root = stored_checkpoint_root(settings);
    root.insert(QStringLiteral("elapsed_ms"), 1e100);
    store_checkpoint_root(settings, root);
    QVERIFY(!service.load().has_value());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

void preferences_tests::session_checkpoint_rejects_inconsistent_slot_state() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    trainer_session_checkpoint_service service(settings);
    QVERIFY(service.save(test_checkpoint()));

    QJsonObject root = stored_checkpoint_root(settings);
    QJsonObject table_object = root.value(QStringLiteral("table")).toObject();
    QJsonArray slot_array
        = table_object.value(QStringLiteral("slots")).toArray();
    QJsonObject slot_object = slot_array.first().toObject();
    slot_object.insert(QStringLiteral("infinity"), true);
    slot_array[0] = slot_object;
    table_object.insert(QStringLiteral("slots"), slot_array);
    root.insert(QStringLiteral("table"), table_object);
    store_checkpoint_root(settings, root);
    QVERIFY(!service.load().has_value());

    QVERIFY(service.save(test_checkpoint()));
    root = stored_checkpoint_root(settings);
    table_object = root.value(QStringLiteral("table")).toObject();
    slot_array = table_object.value(QStringLiteral("slots")).toArray();
    slot_object = slot_array.first().toObject();
    QJsonObject card_object
        = slot_object.value(QStringLiteral("card")).toObject();
    card_object.insert(QStringLiteral("infinity"), true);
    card_object.insert(QStringLiteral("position"), 8);
    slot_object.insert(QStringLiteral("infinity"), true);
    slot_object.insert(QStringLiteral("card"), card_object);
    slot_array[0] = slot_object;
    table_object.insert(QStringLiteral("slots"), slot_array);
    root.insert(QStringLiteral("table"), table_object);
    store_checkpoint_root(settings, root);
    QVERIFY(!service.load().has_value());
}
