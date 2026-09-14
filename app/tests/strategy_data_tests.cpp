// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/strategy_data_tests.hpp"

#include "settings/strategy_data.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

namespace {

QJsonObject valid_strategy(int id, const QString& slug) {
    QJsonArray weights;
    for (int index = 0; index < 13; ++index) {
        weights.push_back(index == 0 ? -1 : 1);
    }

    QJsonObject strategy;
    strategy.insert(QStringLiteral("id"), id);
    strategy.insert(QStringLiteral("slug"), slug);
    strategy.insert(QStringLiteral("name"), QStringLiteral("Test strategy"));
    strategy.insert(QStringLiteral("date"), QStringLiteral("2026"));
    strategy.insert(
        QStringLiteral("description"), QStringLiteral("Test description")
    );
    strategy.insert(
        QStringLiteral("authors"), QJsonArray { QStringLiteral("Author") }
    );
    strategy.insert(
        QStringLiteral("games"), QJsonArray { QStringLiteral("blackjack") }
    );
    strategy.insert(QStringLiteral("min_decks"), 1);
    strategy.insert(QStringLiteral("balance"), true);
    strategy.insert(QStringLiteral("ace_neutral"), false);
    strategy.insert(QStringLiteral("weights"), weights);
    return strategy;
}

QJsonObject valid_root() {
    QJsonArray rank_order;
    for (int index = 0; index < 13; ++index) {
        rank_order.push_back(QString::number(index));
    }

    QJsonObject root;
    root.insert(
        QStringLiteral("key_descriptions"),
        QJsonObject { { QStringLiteral("id"), QStringLiteral("Identifier") } }
    );
    root.insert(QStringLiteral("card_rank_order"), rank_order);
    root.insert(
        QStringLiteral("strategies"),
        QJsonArray { valid_strategy(1, QStringLiteral("test_strategy")) }
    );
    return root;
}

strategy_catalog parse_root(const QJsonObject& root) {
    return parse_strategy_catalog(
        QJsonDocument(root).toJson(QJsonDocument::Compact)
    );
}

} // namespace

void strategy_data_tests::bundled_catalog_is_valid_and_cached() {
    const strategy_catalog& first = strategy_repository();
    const strategy_catalog& second = strategy_repository();

    QVERIFY2(first.is_valid(), qPrintable(first.diagnostic_summary()));
    QCOMPARE(&first, &second);
    QVERIFY(!first.strategies.isEmpty());
    for (const strategy_data& strategy : first.strategies) {
        QCOMPARE(strategy.weights.size(), 13);
        QVERIFY(strategy.id > 0);
        QVERIFY(!strategy.slug.isEmpty());
    }
}

void strategy_data_tests::parser_accepts_optional_publication_date() {
    QJsonObject root = valid_root();
    QJsonArray strategies = root.value(QStringLiteral("strategies")).toArray();
    QJsonObject strategy = strategies.first().toObject();
    strategy.remove(QStringLiteral("date"));
    strategies[0] = strategy;
    root.insert(QStringLiteral("strategies"), strategies);

    const strategy_catalog catalog = parse_root(root);
    QVERIFY2(catalog.is_valid(), qPrintable(catalog.diagnostic_summary()));
    QVERIFY(catalog.strategies.first().date.isEmpty());
}

void strategy_data_tests::
    parser_rejects_malformed_and_semantically_invalid_documents() {
    const strategy_catalog malformed
        = parse_strategy_catalog(QByteArrayLiteral("{not-json"));
    QVERIFY(!malformed.is_valid());
    QVERIFY(!malformed.diagnostics.isEmpty());

    QJsonObject root = valid_root();
    QJsonArray strategies = root.value(QStringLiteral("strategies")).toArray();
    QJsonObject strategy = strategies.first().toObject();
    strategy.insert(QStringLiteral("weights"), QJsonArray { 1, 2 });
    strategies[0] = strategy;
    root.insert(QStringLiteral("strategies"), strategies);

    const strategy_catalog invalid = parse_root(root);
    QVERIFY(!invalid.is_valid());
    QVERIFY(invalid.diagnostic_summary().contains(QStringLiteral("weights")));
}

void strategy_data_tests::parser_rejects_duplicate_ids_and_slugs() {
    QJsonObject root = valid_root();
    QJsonArray strategies;
    strategies.push_back(valid_strategy(1, QStringLiteral("duplicate")));
    strategies.push_back(valid_strategy(1, QStringLiteral("duplicate")));
    root.insert(QStringLiteral("strategies"), strategies);

    const strategy_catalog duplicate = parse_root(root);
    QVERIFY(!duplicate.is_valid());
    QVERIFY(
        duplicate.diagnostic_summary().contains(QStringLiteral("duplicate"))
    );
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
