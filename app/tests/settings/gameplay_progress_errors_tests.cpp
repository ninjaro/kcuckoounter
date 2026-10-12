// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/gameplay_progress_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest/QtTest>
#include <functional>
#include <limits>

using namespace progress_test;

namespace {

// Return each one-field mutation through its original nested document path.
void mutate_fields(
    const QJsonValue& value, const std::function<void(const QJsonValue&)>& check
) {
    if (value.isObject()) {
        const auto object = value.toObject();
        for (const auto& key : object.keys()) {
            auto missing = object;
            missing.remove(key);
            check(missing);
            auto extra = object;
            extra.insert(QStringLiteral("future_field"), 1);
            check(extra);
            mutate_fields(object.value(key), [&](const QJsonValue& changed) {
                auto nested = object;
                nested.insert(key, changed);
                check(nested);
            });
        }
    } else if (value.isArray()) {
        const auto array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i)
            mutate_fields(array.at(i), [&](const QJsonValue& changed) {
                auto nested = array;
                nested[i] = changed;
                check(nested);
            });
    } else {
        check(QJsonArray {});
        check(QJsonObject {});
        check(
            value.isBool() ? QJsonValue(QStringLiteral("true"))
                           : QJsonValue(true)
        );
    }
}

} // namespace

void gameplay_progress_tests::required_fields_and_shapes() {
    const auto good
        = encode_gameplay_progress(progress_for(sample_result()), { 3, 64 })
              .value();
    const auto root = QJsonDocument::fromJson(good).object();
    int mutations = 0;
    mutate_fields(root, [&](const QJsonValue& changed) {
        ++mutations;
        QVERIFY(
            !decode_gameplay_progress(document(changed.toObject()), { 3, 64 })
        );
    });
    QVERIFY(mutations > 200);
    QVERIFY(!decode_gameplay_progress(QByteArray("[]"), { 3, 64 }));
    QVERIFY(!decode_gameplay_progress(QByteArray("broken"), { 3, 64 }));
}

void gameplay_progress_tests::canonical_integer_validation() {
    const auto good
        = QJsonDocument::fromJson(
              encode_gameplay_progress(progress_for(sample_result()), { 3, 64 })
                  .value()
        )
              .object();
    for (const auto& value : QList<QJsonValue> {
             1, 1.5, true, QJsonValue::Null, QStringLiteral(""),
             QStringLiteral("01"), QStringLiteral("+1"), QStringLiteral(" 1"),
             QStringLiteral("1 "), QStringLiteral("-0"), QStringLiteral("-1"),
             QStringLiteral("1e0"), QStringLiteral("18446744073709551616") }) {
        auto root = good;
        root.insert(QStringLiteral("completed_sessions"), value);
        QVERIFY(!decode_gameplay_progress(document(root), { 3, 64 }));
    }
    auto root = good;
    auto history = root.value(QStringLiteral("recent_results")).toArray();
    auto entry = history[0].toObject();
    for (const auto& value : { QStringLiteral("-0"), QStringLiteral("+1"),
                               QStringLiteral("9223372036854775808"),
                               QStringLiteral("-9223372036854775809") }) {
        entry.insert(QStringLiteral("completed_at_utc_ms"), value);
        history[0] = entry;
        root.insert(QStringLiteral("recent_results"), history);
        QVERIFY(!decode_gameplay_progress(document(root), { 3, 64 }));
    }
}

void gameplay_progress_tests::bounds_and_revisions() {
    const auto expected = progress_for(sample_result());
    const auto bytes = encode_gameplay_progress(expected, { 3, 64 }).value();
    QVERIFY(!decode_gameplay_progress(bytes, { 1, 2 }));
    QVERIFY(!decode_gameplay_progress(bytes, { 0, 64 }));
    QVERIFY(!decode_gameplay_progress(
        QByteArray(gameplay_progress_service::maximum_document_bytes + 1, ' '),
        { 3, 64 }
    ));
    auto root = QJsonDocument::fromJson(bytes).object();
    root.insert(QStringLiteral("version"), 99);
    QVERIFY(!decode_gameplay_progress(document(root), { 3, 64 }));
    auto bad = expected;
    bad.recent_results[0].result.cadence_revision = 99;
    QVERIFY(!encode_gameplay_progress(bad, { 3, 64 }));
    bad = expected;
    bad.recent_results[0].result.decks[0].configuration.strategy_slug = " bad ";
    QVERIFY(!encode_gameplay_progress(bad, { 3, 64 }));
    bad = expected;
    bad.recent_results[0].result.decks[0].configuration.strategy_slug
        = std::string("invalid\xff", 8);
    QVERIFY(!encode_gameplay_progress(bad, { 3, 64 }));
    bad = expected;
    auto& targets = bad.recent_results[0].result.decks[0].schedule.targets;
    targets.resize(gameplay_progress_service::maximum_result_targets + 1);
    for (std::size_t i = 0; i < targets.size(); ++i)
        targets[i] = i + 1;
    QVERIFY(!encode_gameplay_progress(bad, { 3, 64 }));
    auto history = QJsonDocument::fromJson(bytes).object();
    auto records = history.value(QStringLiteral("recent_results")).toArray();
    for (int i = 0; i < 20; ++i)
        records.append(records[0]);
    history.insert(QStringLiteral("recent_results"), records);
    QVERIFY(!decode_gameplay_progress(document(history), { 3, 64 }));
    history = QJsonDocument::fromJson(bytes).object();
    records = history.value(QStringLiteral("recent_results")).toArray();
    auto entry = records[0].toObject();
    auto result = entry.value(QStringLiteral("result")).toObject();
    auto decks = result.value(QStringLiteral("decks")).toArray();
    auto deck = decks[0].toObject();
    auto schedule = deck.value(QStringLiteral("schedule")).toObject();
    QJsonArray oversized;
    for (std::size_t i = 0;
         i <= gameplay_progress_service::maximum_result_targets; ++i)
        oversized.append(QString::number(static_cast<qulonglong>(i + 1)));
    schedule.insert(QStringLiteral("targets"), oversized);
    deck.insert(QStringLiteral("schedule"), schedule);
    decks[0] = deck;
    result.insert(QStringLiteral("decks"), decks);
    entry.insert(QStringLiteral("result"), result);
    records[0] = entry;
    history.insert(QStringLiteral("recent_results"), records);
    QVERIFY(!decode_gameplay_progress(document(history), { 3, 64 }));
}

void gameplay_progress_tests::history_accounting() {
    const auto good = progress_for(sample_result());
    const auto reject = [](const gameplay_progress& value) {
        return !encode_gameplay_progress(value, { 3, 64 });
    };
    auto bad = good;
    bad.completed_sessions = 0;
    QVERIFY(reject(bad));
    bad = good;
    bad.completed_sessions = 2;
    QVERIFY(reject(bad));
    bad = good;
    bad.recent_results[0].sequence = 0;
    QVERIFY(reject(bad));
    bad = good;
    bad.recent_results[0].completed_at_utc_ms = 0;
    QVERIFY(reject(bad));
    bad = good;
    bad.challenge.errors = 1;
    QVERIFY(reject(bad));
    bad = good;
    bad.timing.play_time_ms += 1;
    QVERIFY(reject(bad));
    const auto empty = encode_gameplay_progress({}, { 3, 64 });
    QVERIFY(
        empty
        && decode_gameplay_progress(*empty, { 3, 64 }) == gameplay_progress {}
    );
    bad = {};
    bad.challenge.errors = 1;
    QVERIFY(reject(bad));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
