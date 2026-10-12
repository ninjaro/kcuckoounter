// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/gameplay_checkpoint_tests.hpp"

#include "settings/gameplay_checkpoint_fixture.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest/QtTest>

#include <limits>

using namespace checkpoint_test;

namespace {

QJsonValue change(
    QJsonValue node, const QStringList& path, qsizetype depth,
    const QJsonValue& replacement, bool remove = false
) {
    if (depth == path.size())
        return replacement;
    if (node.isObject()) {
        auto object = node.toObject();
        const auto& key = path[depth];
        if (remove && depth + 1 == path.size())
            object.remove(key);
        else
            object.insert(
                key,
                change(object.value(key), path, depth + 1, replacement, remove)
            );
        return object;
    }
    auto array = node.toArray();
    const auto index = path[depth].toInt();
    array.replace(
        index, change(array[index], path, depth + 1, replacement, remove)
    );
    return array;
}

void paths(
    const QJsonValue& node, const QStringList& path, QList<QStringList>& fields,
    QList<QStringList>& objects
) {
    if (node.isObject()) {
        objects.append(path);
        const auto object = node.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            auto nested = path;
            nested.append(it.key());
            fields.append(nested);
            paths(it.value(), nested, fields, objects);
        }
    } else if (node.isArray() && !node.toArray().isEmpty()) {
        auto nested = path;
        nested.append(QStringLiteral("0"));
        // One representative member per array, including card/target values.
        fields.append(nested);
        paths(node.toArray().first(), nested, fields, objects);
    }
}

QJsonValue at(QJsonValue node, const QStringList& path) {
    for (const auto& part : path)
        node = node.isObject() ? node.toObject().value(part)
                               : node.toArray()[part.toInt()];
    return node;
}

} // namespace

void gameplay_checkpoint_tests::required_fields_and_shapes() {
    const auto bytes
        = encode_gameplay_checkpoint(sample_checkpoint(), { 3, 64 }).value();
    const auto root = QJsonDocument::fromJson(bytes).object();
    QList<QStringList> fields;
    QList<QStringList> objects;
    paths(root, {}, fields, objects);
    // Include all optional nested shapes, not just the first (finite) deck.
    const auto decks = root.value(QStringLiteral("decks")).toArray();
    for (qsizetype i = 1; i < decks.size(); ++i)
        paths(
            decks[i], { QStringLiteral("decks"), QString::number(i) }, fields,
            objects
        );
    for (const auto& path : fields) {
        const auto original = at(root, path);
        const QJsonValue wrong = original.isBool() ? QJsonValue("false")
            : original.isString()                  ? QJsonValue(0)
                                                   : QJsonValue(false);
        QVERIFY2(
            !decode_gameplay_checkpoint(
                document(change(root, path, 0, wrong).toObject()), { 3, 64 }
            ),
            qPrintable(path.join('/'))
        );
        // Array members are positional, not required object keys.
        const auto parent = at(root, path.mid(0, path.size() - 1));
        if (parent.isObject())
            QVERIFY2(
                !decode_gameplay_checkpoint(
                    document(change(root, path, 0, {}, true).toObject()),
                    { 3, 64 }
                ),
                qPrintable(path.join('/'))
            );
    }
    for (const auto& path : objects) {
        auto extended = at(root, path).toObject();
        extended.insert(QStringLiteral("future_field"), 1);
        QVERIFY(!decode_gameplay_checkpoint(
            document(change(root, path, 0, extended).toObject()), { 3, 64 }
        ));
    }
    for (const QByteArray& invalid :
         { QByteArray(), QByteArray("null"), QByteArray("[]"), QByteArray("{}"),
           QByteArray("broken"), bytes.left(bytes.size() - 1) })
        QVERIFY(!decode_gameplay_checkpoint(invalid, { 3, 64 }));
    auto invalid_utf8 = bytes;
    invalid_utf8.replace("external_strategy", QByteArray(1, '\xff'));
    QVERIFY(!decode_gameplay_checkpoint(invalid_utf8, { 3, 64 }));
    for (const QByteArray& surrogate :
         { QByteArray("\\ud800"), QByteArray("\\udfff") }) {
        auto invalid_text = bytes;
        invalid_text.replace("external_strategy", surrogate);
        QVERIFY(!decode_gameplay_checkpoint(invalid_text, { 3, 64 }));
    }
    auto paired_text = bytes;
    paired_text.replace("external_strategy", "\\ud83d\\ude00");
    const auto paired = decode_gameplay_checkpoint(paired_text, { 3, 64 });
    QVERIFY(paired);
    QCOMPARE(
        paired->state.decks[0].configuration.strategy_slug,
        QString::fromUtf8("\xf0\x9f\x98\x80").toStdString()
    );
    auto state_error = root;
    auto mapping = state_error.value(QStringLiteral("deck_to_slot")).toArray();
    mapping[1] = mapping[0];
    state_error.insert(QStringLiteral("deck_to_slot"), mapping);
    QVERIFY(!decode_gameplay_checkpoint(document(state_error), { 3, 64 }));
    for (const auto* version : { "version", "continuation_revision" }) {
        state_error = root;
        state_error.insert(QString::fromLatin1(version), 99);
        QVERIFY(!decode_gameplay_checkpoint(document(state_error), { 3, 64 }));
    }
    QCOMPARE(
        *encode_gameplay_checkpoint(sample_checkpoint(), { 3, 64 }), bytes
    );
}

void gameplay_checkpoint_tests::canonical_integer_validation() {
    const auto root
        = QJsonDocument::fromJson(
              encode_gameplay_checkpoint(sample_checkpoint(), { 3, 64 }).value()
        )
              .object();
    const QList<QJsonValue> bad_unsigned {
        0,
        -1,
        1.5,
        1e100,
        true,
        QJsonValue::Null,
        "",
        "+1",
        "01",
        "-0",
        "-1",
        " 1",
        "1 ",
        "1.0",
        "1e1",
        "18446744073709551616",
        QString(100, '9'),
    };
    for (const auto& value : bad_unsigned) {
        auto invalid = root;
        auto random
            = invalid.value(QStringLiteral("random_dealing")).toObject();
        random.insert(QStringLiteral("seed"), value);
        invalid.insert(QStringLiteral("random_dealing"), random);
        QVERIFY(!decode_gameplay_checkpoint(document(invalid), { 3, 64 }));
    }
    const QList<QJsonValue> bad_signed {
        0,
        -1,
        true,
        QJsonValue::Null,
        "",
        "+1",
        "01",
        "-0",
        "-01",
        " 1",
        "1 ",
        "1.0",
        "9223372036854775808",
        "-9223372036854775809",
        QString(100, '9'),
    };
    for (const auto& value : bad_signed) {
        const QStringList path { "decks", "0", "latest_input" };
        QVERIFY(!decode_gameplay_checkpoint(
            document(change(root, path, 0, value).toObject()), { 3, 64 }
        ));
    }
    for (const QJsonValue& value :
         { QJsonValue(-1), QJsonValue(54), QJsonValue(1.5), QJsonValue(1e100),
           QJsonValue("1") }) {
        const QStringList path { "decks", "0", "stream", "cards", "0" };
        QVERIFY(!decode_gameplay_checkpoint(
            document(change(root, path, 0, value).toObject()), { 3, 64 }
        ));
    }
    for (const QJsonValue& value :
         { QJsonValue(-2147483649.0), QJsonValue(2147483648.0), QJsonValue(1.5),
           QJsonValue("1") }) {
        const QStringList path { "decks", "0", "stream", "rank_weights", "0" };
        QVERIFY(!decode_gameplay_checkpoint(
            document(change(root, path, 0, value).toObject()), { 3, 64 }
        ));
    }
}

void gameplay_checkpoint_tests::budgets_caps_and_text() {
    const auto expected = sample_checkpoint();
    const auto bytes = encode_gameplay_checkpoint(expected, { 3, 64 }).value();
    gameplay::generation_policy policy;
    policy.maximum_cards = 216; // Finite 8 + two current/lookahead pairs of 52.
    policy.maximum_targets = 3; // One table page.
    QVERIFY(encode_gameplay_checkpoint(expected, { 1, 3 }, policy));
    QVERIFY(decode_gameplay_checkpoint(bytes, { 1, 3 }, policy));
    --policy.maximum_cards;
    QVERIFY(!encode_gameplay_checkpoint(expected, { 3, 3 }, policy));
    QVERIFY(!decode_gameplay_checkpoint(bytes, { 3, 3 }, policy));
    ++policy.maximum_cards;
    --policy.maximum_targets;
    QVERIFY(!encode_gameplay_checkpoint(expected, { 3, 3 }, policy));
    QVERIFY(!decode_gameplay_checkpoint(bytes, { 3, 3 }, policy));
    auto single = mixed_session({});
    const gameplay_checkpoint per_deck {
        gameplay::capture_checkpoint(single).value(), 0
    };
    const auto single_bytes
        = encode_gameplay_checkpoint(per_deck, { 3, 3 }).value();
    policy.maximum_targets
        = 10; // Four finite + two independent three-target pages.
    QVERIFY(decode_gameplay_checkpoint(single_bytes, { 3, 3 }, policy));
    --policy.maximum_targets;
    QVERIFY(!encode_gameplay_checkpoint(per_deck, { 3, 3 }, policy));
    QVERIFY(!decode_gameplay_checkpoint(single_bytes, { 3, 3 }, policy));
    for (const gameplay::slot_limits bounds :
         { gameplay::slot_limits { 0, 3 },
           { 3, 2 },
           { 1, 2 },
           { 1, std::numeric_limits<std::size_t>::max() } }) {
        QVERIFY(!encode_gameplay_checkpoint(expected, bounds));
        QVERIFY(!decode_gameplay_checkpoint(bytes, bounds));
    }
    for (const auto& slug :
         { std::string(), std::string(" padded "), std::string(129, 'x'),
           std::string(1, '\xff'), std::string("bad\nslug") }) {
        auto invalid = expected;
        invalid.state.decks[0].configuration.strategy_slug = slug;
        QVERIFY(!encode_gameplay_checkpoint(invalid, { 3, 64 }));
    }
    auto invalid = expected;
    invalid.pickup_elapsed_ms
        = static_cast<std::uint64_t>(invalid.state.pickup_interval_ms);
    QVERIFY(!encode_gameplay_checkpoint(invalid, { 3, 64 }));
    auto root = QJsonDocument::fromJson(bytes).object();
    root.insert(QStringLiteral("pickup_elapsed_ms"), QStringLiteral("300"));
    QVERIFY(!decode_gameplay_checkpoint(document(root), { 3, 64 }));
    auto oversized = bytes;
    oversized.append(
        QByteArray(gameplay_checkpoint_service::maximum_document_bytes, ' ')
    );
    QVERIFY(!decode_gameplay_checkpoint(oversized, { 3, 64 }));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
