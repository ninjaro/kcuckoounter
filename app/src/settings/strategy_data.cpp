#include "settings/strategy_data.hpp"

#include "arch/asset_locator.hpp"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace {

constexpr int card_rank_count = 13;
constexpr int maximum_absolute_weight = 10;

void add_diagnostic(
    strategy_catalog* catalog, const QString& location, const QString& message
) {
    if (catalog == nullptr) {
        return;
    }
    catalog->diagnostics.push_back(
        QStringLiteral("%1: %2").arg(location, message)
    );
}

bool integer_value(
    const QJsonObject& object, const QString& key, int minimum, int maximum,
    int* output, strategy_catalog* catalog, const QString& location
) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble())
        || std::floor(value.toDouble()) != value.toDouble()) {
        add_diagnostic(
            catalog, location,
            QStringLiteral("'%1' must be an integer").arg(key)
        );
        return false;
    }

    const double number = value.toDouble();
    if (number < minimum || number > maximum) {
        add_diagnostic(
            catalog, location,
            QStringLiteral("'%1' is outside [%2, %3]")
                .arg(key)
                .arg(minimum)
                .arg(maximum)
        );
        return false;
    }
    if (output != nullptr) {
        *output = static_cast<int>(number);
    }
    return true;
}

bool required_string(
    const QJsonObject& object, const QString& key, QString* output,
    strategy_catalog* catalog, const QString& location
) {
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        add_diagnostic(
            catalog, location,
            QStringLiteral("'%1' must be a non-empty string").arg(key)
        );
        return false;
    }
    if (output != nullptr) {
        *output = value.toString().trimmed();
    }
    return true;
}

bool optional_string(
    const QJsonObject& object, const QString& key, QString* output,
    strategy_catalog* catalog, const QString& location
) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        if (output != nullptr) {
            output->clear();
        }
        return true;
    }
    return required_string(object, key, output, catalog, location);
}

bool required_boolean(
    const QJsonObject& object, const QString& key, bool* output,
    strategy_catalog* catalog, const QString& location
) {
    const QJsonValue value = object.value(key);
    if (!value.isBool()) {
        add_diagnostic(
            catalog, location, QStringLiteral("'%1' must be a boolean").arg(key)
        );
        return false;
    }
    if (output != nullptr) {
        *output = value.toBool();
    }
    return true;
}

QStringList string_array(
    const QJsonObject& object, const QString& key, strategy_catalog* catalog,
    const QString& location
) {
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        add_diagnostic(
            catalog, location,
            QStringLiteral("'%1' must be an array of strings").arg(key)
        );
        return {};
    }

    QStringList strings;
    const QJsonArray values = value.toArray();
    strings.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QJsonValue item = values.at(index);
        if (!item.isString() || item.toString().trimmed().isEmpty()) {
            add_diagnostic(
                catalog, location,
                QStringLiteral("'%1[%2]' must be a non-empty string")
                    .arg(key)
                    .arg(index)
            );
            continue;
        }
        strings.push_back(item.toString().trimmed());
    }
    return strings;
}

QVector<int> validated_weights(
    const QJsonObject& object, strategy_catalog* catalog,
    const QString& location
) {
    const QJsonValue value = object.value(QStringLiteral("weights"));
    if (!value.isArray() || value.toArray().size() != card_rank_count) {
        add_diagnostic(
            catalog, location,
            QStringLiteral("'weights' must contain exactly %1 integers")
                .arg(card_rank_count)
        );
        return {};
    }

    QVector<int> weights;
    weights.reserve(card_rank_count);
    const QJsonArray values = value.toArray();
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QJsonValue item = values.at(index);
        if (!item.isDouble() || !std::isfinite(item.toDouble())
            || std::floor(item.toDouble()) != item.toDouble()
            || std::abs(item.toDouble()) > maximum_absolute_weight) {
            add_diagnostic(
                catalog, location,
                QStringLiteral("'weights[%1]' must be an integer in [%2, %3]")
                    .arg(index)
                    .arg(-maximum_absolute_weight)
                    .arg(maximum_absolute_weight)
            );
            return {};
        }
        weights.push_back(static_cast<int>(item.toDouble()));
    }
    return weights;
}

QMap<QString, double> validated_metrics(
    const QJsonObject& object, strategy_catalog* catalog,
    const QString& location
) {
    QMap<QString, double> metrics;
    const QJsonValue value = object.value(QStringLiteral("metrics"));
    if (value.isUndefined()) {
        return metrics;
    }
    if (!value.isObject()) {
        add_diagnostic(
            catalog, location, QStringLiteral("'metrics' must be an object")
        );
        return metrics;
    }

    const QJsonObject values = value.toObject();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!it.value().isDouble() || !std::isfinite(it.value().toDouble())) {
            add_diagnostic(
                catalog, location,
                QStringLiteral("metric '%1' must be a finite number")
                    .arg(it.key())
            );
            continue;
        }
        metrics.insert(it.key(), it.value().toDouble());
    }
    return metrics;
}

QMap<QString, QString> optional_string_map(
    const QJsonObject& object, const QString& key, strategy_catalog* catalog,
    const QString& location
) {
    QMap<QString, QString> values;
    const QJsonValue raw = object.value(key);
    if (raw.isUndefined()) {
        return values;
    }
    if (!raw.isObject()) {
        add_diagnostic(
            catalog, location, QStringLiteral("'%1' must be an object").arg(key)
        );
        return values;
    }
    const QJsonObject map = raw.toObject();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        if (!it.value().isString()) {
            add_diagnostic(
                catalog, location,
                QStringLiteral("'%1.%2' must be a string").arg(key, it.key())
            );
            continue;
        }
        values.insert(it.key(), it.value().toString());
    }
    return values;
}

QVector<strategy_data::strategy_reference> validated_references(
    const QJsonObject& object, strategy_catalog* catalog,
    const QString& location
) {
    QVector<strategy_data::strategy_reference> references;
    const QJsonValue raw = object.value(QStringLiteral("refs"));
    if (raw.isUndefined()) {
        return references;
    }
    if (!raw.isArray()) {
        add_diagnostic(
            catalog, location, QStringLiteral("'refs' must be an array")
        );
        return references;
    }

    const QJsonArray values = raw.toArray();
    references.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        if (!values.at(index).isObject()) {
            add_diagnostic(
                catalog, location,
                QStringLiteral("'refs[%1]' must be an object").arg(index)
            );
            continue;
        }
        const QJsonObject value = values.at(index).toObject();
        strategy_data::strategy_reference reference;
        reference.type = value.value(QStringLiteral("type")).toString();
        reference.citation
            = value.value(QStringLiteral("citation")).toString().trimmed();
        reference.url = value.value(QStringLiteral("url")).toString();
        reference.accessed = value.value(QStringLiteral("accessed")).toString();
        if (reference.citation.isEmpty()) {
            add_diagnostic(
                catalog, location,
                QStringLiteral("'refs[%1].citation' must be a non-empty string")
                    .arg(index)
            );
            continue;
        }
        references.push_back(reference);
    }
    return references;
}

} // namespace

bool strategy_catalog::is_valid() const {
    return diagnostics.isEmpty() && !strategies.isEmpty();
}

QString strategy_catalog::diagnostic_summary() const {
    return diagnostics.join(QStringLiteral("; "));
}

strategy_catalog parse_strategy_catalog(const QByteArray& json_data) {
    strategy_catalog catalog;
    QJsonParseError parse_error;
    const QJsonDocument document
        = QJsonDocument::fromJson(json_data, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        add_diagnostic(
            &catalog, QStringLiteral("document"),
            parse_error.error == QJsonParseError::NoError
                ? QStringLiteral("root must be an object")
                : parse_error.errorString()
        );
        return catalog;
    }

    const QJsonObject root = document.object();
    const QJsonValue rank_order_value
        = root.value(QStringLiteral("card_rank_order"));
    if (!rank_order_value.isArray()
        || rank_order_value.toArray().size() != card_rank_count) {
        add_diagnostic(
            &catalog, QStringLiteral("document"),
            QStringLiteral("'card_rank_order' must contain exactly %1 strings")
                .arg(card_rank_count)
        );
    } else {
        QSet<QString> ranks;
        const QJsonArray rank_order = rank_order_value.toArray();
        for (qsizetype index = 0; index < rank_order.size(); ++index) {
            const QJsonValue rank = rank_order.at(index);
            if (!rank.isString() || rank.toString().trimmed().isEmpty()) {
                add_diagnostic(
                    &catalog, QStringLiteral("document"),
                    QStringLiteral("'card_rank_order[%1]' must be a string")
                        .arg(index)
                );
                continue;
            }
            if (ranks.contains(rank.toString())) {
                add_diagnostic(
                    &catalog, QStringLiteral("document"),
                    QStringLiteral("'card_rank_order' contains duplicates")
                );
                continue;
            }
            ranks.insert(rank.toString());
        }
    }

    const QJsonValue key_descriptions_value
        = root.value(QStringLiteral("key_descriptions"));
    if (!key_descriptions_value.isObject()) {
        add_diagnostic(
            &catalog, QStringLiteral("document"),
            QStringLiteral("'key_descriptions' must be an object")
        );
    } else {
        const QJsonObject descriptions = key_descriptions_value.toObject();
        for (auto it = descriptions.constBegin(); it != descriptions.constEnd();
             ++it) {
            if (!it.value().isString()) {
                add_diagnostic(
                    &catalog, QStringLiteral("document"),
                    QStringLiteral("key description '%1' must be a string")
                        .arg(it.key())
                );
                continue;
            }
            catalog.key_descriptions.insert(it.key(), it.value().toString());
        }
    }

    const QJsonValue strategies_value
        = root.value(QStringLiteral("strategies"));
    if (!strategies_value.isArray() || strategies_value.toArray().isEmpty()) {
        add_diagnostic(
            &catalog, QStringLiteral("document"),
            QStringLiteral("'strategies' must be a non-empty array")
        );
        return catalog;
    }

    QSet<int> ids;
    QSet<QString> slugs;
    const QRegularExpression slug_pattern(
        QStringLiteral("^[a-z0-9]+(?:_[a-z0-9]+)*$")
    );
    const QJsonArray strategies = strategies_value.toArray();
    catalog.strategies.reserve(strategies.size());
    for (qsizetype index = 0; index < strategies.size(); ++index) {
        const QString location = QStringLiteral("strategies[%1]").arg(index);
        if (!strategies.at(index).isObject()) {
            add_diagnostic(
                &catalog, location, QStringLiteral("entry must be an object")
            );
            continue;
        }

        const qsizetype diagnostics_before = catalog.diagnostics.size();
        const QJsonObject object = strategies.at(index).toObject();
        strategy_data data;
        integer_value(
            object, QStringLiteral("id"), 1, 1'000'000, &data.id, &catalog,
            location
        );
        required_string(
            object, QStringLiteral("slug"), &data.slug, &catalog, location
        );
        required_string(
            object, QStringLiteral("name"), &data.name, &catalog, location
        );
        optional_string(
            object, QStringLiteral("date"), &data.date, &catalog, location
        );
        required_string(
            object, QStringLiteral("description"), &data.description, &catalog,
            location
        );
        integer_value(
            object, QStringLiteral("min_decks"), 1, 16, &data.min_decks,
            &catalog, location
        );
        required_boolean(
            object, QStringLiteral("balance"), &data.balance, &catalog, location
        );
        required_boolean(
            object, QStringLiteral("ace_neutral"), &data.ace_neutral, &catalog,
            location
        );
        data.authors = string_array(
            object, QStringLiteral("authors"), &catalog, location
        );
        data.games
            = string_array(object, QStringLiteral("games"), &catalog, location);
        data.weights = validated_weights(object, &catalog, location);
        data.metrics = validated_metrics(object, &catalog, location);
        data.unique_fields = optional_string_map(
            object, QStringLiteral("unique_fields"), &catalog, location
        );
        data.references = validated_references(object, &catalog, location);

        if (!data.slug.isEmpty() && !slug_pattern.match(data.slug).hasMatch()) {
            add_diagnostic(
                &catalog, location,
                QStringLiteral("'slug' must use lowercase snake_case")
            );
        }
        if (data.id > 0 && ids.contains(data.id)) {
            add_diagnostic(
                &catalog, location, QStringLiteral("duplicate strategy id")
            );
        }
        if (!data.slug.isEmpty() && slugs.contains(data.slug)) {
            add_diagnostic(
                &catalog, location, QStringLiteral("duplicate strategy slug")
            );
        }

        if (catalog.diagnostics.size() != diagnostics_before) {
            continue;
        }
        ids.insert(data.id);
        slugs.insert(data.slug);
        catalog.strategies.push_back(data);
    }

    return catalog;
}

const strategy_catalog& strategy_repository() {
    static const strategy_catalog repository = [] {
        strategy_catalog result;
        const QString source_path
            = bundled_asset_path(QStringLiteral("strategies.json"));
        if (source_path.isEmpty()) {
            add_diagnostic(
                &result, QStringLiteral("document"),
                QStringLiteral("bundled strategies.json was not found")
            );
            return result;
        }

        QFile file(source_path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            add_diagnostic(
                &result, QStringLiteral("document"), file.errorString()
            );
            return result;
        }
        result = parse_strategy_catalog(file.readAll());
        if (!result.is_valid()) {
            qWarning().noquote() << "Unable to load strategy catalog:"
                                 << result.diagnostic_summary();
        }
        return result;
    }();
    return repository;
}
