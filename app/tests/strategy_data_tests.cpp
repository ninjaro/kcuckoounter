// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/strategy_data_tests.hpp"

#include "settings/strategy_data.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include <limits>
#include <numeric>

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
    for (const auto& rank : {
             "A",
             "2",
             "3",
             "4",
             "5",
             "6",
             "7",
             "8",
             "9",
             "10",
             "J",
             "Q",
             "K",
         }) {
        rank_order.push_back(QString::fromLatin1(rank));
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

strategy_catalog parse_formula(const QString& formula) {
    auto root = valid_root();
    auto strategies = root.value(QStringLiteral("strategies")).toArray();
    auto strategy = strategies.first().toObject();
    strategy.insert(
        QStringLiteral("unique_fields"),
        QJsonObject {
            { QStringLiteral("initial_running_count_formula"), formula } }
    );
    strategies[0] = strategy;
    root.insert(QStringLiteral("strategies"), strategies);
    return parse_root(root);
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

void strategy_data_tests::initial_count_formulas_are_decoded_and_evaluated() {
    struct sample {
        const char* formula;
        std::int64_t value;
        bool per_deck;
        std::int64_t at_four_decks;
    };

    for (const auto& sample : {
             sample { "0", 0, false, 0 },
             sample { "+7", 7, false, 7 },
             sample { "-9223372036854775808",
                      std::numeric_limits<std::int64_t>::min(), false,
                      std::numeric_limits<std::int64_t>::min() },
             sample { "-4 * decks", -4, true, -16 },
             sample { "  +2*decks  ", 2, true, 8 },
             sample { "0 * decks", 0, true, 0 },
         }) {
        const auto catalog = parse_formula(QString::fromLatin1(sample.formula));
        QVERIFY2(catalog.is_valid(), qPrintable(catalog.diagnostic_summary()));
        const auto& strategy = catalog.strategies.first();
        QVERIFY(strategy.initial_count);
        QCOMPARE(strategy.initial_count->value, sample.value);
        QCOMPARE(strategy.initial_count->per_deck, sample.per_deck);
        QVERIFY(strategy.initial_running_count_for(4) == sample.at_four_decks);
        QVERIFY(!strategy.initial_running_count_for(0));
    }
    const auto zero = parse_root(valid_root()).strategies.first();
    QVERIFY(!zero.initial_count);
    QVERIFY(zero.initial_running_count_for(1) == 0);
    QVERIFY(!zero.initial_running_count_for(0));
    const auto negative
        = parse_formula(QStringLiteral("-4 * decks")).strategies.first();
    const auto positive
        = parse_formula(QStringLiteral("4 * decks")).strategies.first();
    const auto boundary
        = (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
           + 1)
        / 4;
    if (boundary <= std::numeric_limits<std::size_t>::max()) {
        QVERIFY(
            negative.initial_running_count_for(
                static_cast<std::size_t>(boundary)
            )
            == std::numeric_limits<std::int64_t>::min()
        );
        QVERIFY(!positive.initial_running_count_for(
            static_cast<std::size_t>(boundary)
        ));
        QVERIFY(!negative.initial_running_count_for(
            static_cast<std::size_t>(boundary + 1)
        ));
    }
    const auto minimum
        = parse_formula(QStringLiteral("-9223372036854775808 * decks"))
              .strategies.first();
    QVERIFY(
        minimum.initial_running_count_for(1)
        == std::numeric_limits<std::int64_t>::min()
    );
    QVERIFY(!minimum.initial_running_count_for(2));
    const auto maximum
        = parse_formula(QStringLiteral("9223372036854775807 * decks"))
              .strategies.first();
    QVERIFY(
        maximum.initial_running_count_for(1)
        == std::numeric_limits<std::int64_t>::max()
    );
    QVERIFY(!maximum.initial_running_count_for(2));
}

void strategy_data_tests::parser_rejects_unsupported_initial_count_formulas() {
    for (const auto& formula : {
             "",
             "decks",
             "-4 * deck",
             "-4 * DECKS",
             "1.5 * decks",
             "4 / decks",
             "4 - 4 * decks",
             "-2 * decks (note)",
             "1e3",
             "NaN",
             "decks; run()",
             "9223372036854775808",
             "-9223372036854775809",
             "0x10",
             "--4 * decks",
         }) {
        const auto catalog = parse_formula(QString::fromLatin1(formula));
        QVERIFY(!catalog.is_valid());
        QVERIFY(catalog.strategies.isEmpty());
        QVERIFY(catalog.diagnostic_summary().contains(
            QStringLiteral("initial_running_count_formula")
        ));
    }
    auto root = valid_root();
    auto strategies = root.value(QStringLiteral("strategies")).toArray();
    auto strategy = strategies.first().toObject();
    strategy.insert(
        QStringLiteral("unique_fields"),
        QJsonObject { { QStringLiteral("initial_running_count_formula"), 4 } }
    );
    strategies[0] = strategy;
    root.insert(QStringLiteral("strategies"), strategies);
    QVERIFY(!parse_root(root).is_valid());
}

void strategy_data_tests::parser_requires_the_canonical_rank_order() {
    auto root = valid_root();
    auto ranks = root.value(QStringLiteral("card_rank_order")).toArray();
    const auto ace = ranks[0].toString();
    ranks[0] = ranks[1].toString();
    ranks[1] = ace;
    root.insert(QStringLiteral("card_rank_order"), ranks);
    const auto reordered = parse_root(root);
    QVERIFY(!reordered.is_valid());
    QVERIFY(reordered.diagnostic_summary().contains(
        QStringLiteral("card_rank_order")
    ));
    ranks[0] = QStringLiteral("unknown");
    root.insert(QStringLiteral("card_rank_order"), ranks);
    QVERIFY(!parse_root(root).is_valid());
}

void strategy_data_tests::
    bundled_initial_counts_and_recommendations_are_audited() {
    const auto& catalog = strategy_repository();
    QVERIFY2(catalog.is_valid(), qPrintable(catalog.diagnostic_summary()));
    QCOMPARE(catalog.strategies.size(), 20);
    int defined_formulas = 0;
    for (const auto& strategy : catalog.strategies) {
        QVERIFY(strategy.min_decks >= 1);
        const auto rank_sum = std::accumulate(
            strategy.weights.begin(), strategy.weights.end(), 0
        );
        QCOMPARE(strategy.balance, rank_sum == 0);
        QCOMPARE(strategy.ace_neutral, strategy.weights[0] == 0);
        if (strategy.initial_count) {
            ++defined_formulas;
            QCOMPARE(strategy.slug, QStringLiteral("uston_ss"));
            QCOMPARE(strategy.initial_count->value, -4);
            QVERIFY(strategy.initial_count->per_deck);
            for (const std::size_t decks : { 1U, 2U, 4U, 6U, 8U, 16U }) {
                const auto starting = strategy.initial_running_count_for(decks);
                QVERIFY(starting == -4 * static_cast<std::int64_t>(decks));
                // Arithmetic cross-check, not a general rule for every system.
                QCOMPARE(
                    *starting + 4 * rank_sum * static_cast<std::int64_t>(decks),
                    0
                );
            }
        } else {
            QVERIFY(strategy.initial_running_count_for(1) == 0);
            QVERIFY(strategy.initial_running_count_for(16) == 0);
        }
    }
    QCOMPARE(defined_formulas, 1);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
