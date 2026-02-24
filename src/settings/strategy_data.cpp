#include "settings/strategy_data.hpp"

#include "arch/str_label.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QMap<QString, double> parse_metrics(const QJsonObject& metrics_object) {
    QMap<QString, double> metrics;
    for (auto it = metrics_object.begin(); it != metrics_object.end(); ++it) {
        metrics.insert(it.key(), it.value().toDouble());
    }
    return metrics;
}

QVector<int> parse_weights(const QJsonArray& weights_array) {
    QVector<int> weights;
    weights.reserve(weights_array.size());
    for (const QJsonValue& value : weights_array) {
        weights.push_back(value.toInt());
    }
    return weights;
}

QMap<QString, QString> parse_unique_fields(const QJsonObject& unique_object) {
    QMap<QString, QString> fields;
    for (auto it = unique_object.begin(); it != unique_object.end(); ++it) {
        fields.insert(it.key(), it.value().toString());
    }
    return fields;
}

QVector<strategy_data::strategy_reference>
parse_references(const QJsonArray& refs_array) {
    QVector<strategy_data::strategy_reference> refs;
    refs.reserve(refs_array.size());
    for (const QJsonValue& value : refs_array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject ref_object = value.toObject();
        strategy_data::strategy_reference ref;
        ref.type = ref_object.value("type").toString();
        ref.citation = ref_object.value("citation").toString();
        ref.url = ref_object.value("url").toString();
        ref.accessed = ref_object.value("accessed").toString();
        if (!ref.citation.isEmpty()) {
            refs.push_back(ref);
        }
    }
    return refs;
}

QMap<QString, QString> parse_key_descriptions(const QJsonObject& root) {
    QMap<QString, QString> descriptions;
    if (!root.contains("key_descriptions")
        || !root.value("key_descriptions").isObject()) {
        return descriptions;
    }

    const QJsonObject key_object = root.value("key_descriptions").toObject();
    for (auto it = key_object.begin(); it != key_object.end(); ++it) {
        descriptions.insert(it.key(), it.value().toString());
    }
    return descriptions;
}
} // namespace

QVector<strategy_data> load_strategies() {
    QFile file(str_label("assets/strategies.json"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return {};
    }

    const QJsonObject root = doc.object();
    const QJsonArray strategies_array = root.value("strategies").toArray();
    QVector<strategy_data> strategies;
    strategies.reserve(strategies_array.size());

    for (const QJsonValue& value : strategies_array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        strategy_data data;
        data.id = object.value("id").toInt();
        data.slug = object.value("slug").toString();
        data.name = object.value("name").toString();
        data.date = object.value("date").toString();
        data.description = object.value("description").toString();
        data.min_decks = object.value("min_decks").toInt();
        data.balance = object.value("balance").toBool();
        data.ace_neutral = object.value("ace_neutral").toBool();

        const QJsonArray authors = object.value("authors").toArray();
        for (const QJsonValue& author : authors) {
            data.authors.append(author.toString());
        }

        const QJsonArray games = object.value("games").toArray();
        for (const QJsonValue& game : games) {
            data.games.append(game.toString());
        }

        if (object.contains("metrics") && object.value("metrics").isObject()) {
            data.metrics = parse_metrics(object.value("metrics").toObject());
        }
        if (object.contains("weights") && object.value("weights").isArray()) {
            data.weights = parse_weights(object.value("weights").toArray());
        }
        if (object.contains("unique_fields")
            && object.value("unique_fields").isObject()) {
            data.unique_fields
                = parse_unique_fields(object.value("unique_fields").toObject());
        }
        if (object.contains("refs") && object.value("refs").isArray()) {
            data.references = parse_references(object.value("refs").toArray());
        }

        if (!data.name.isEmpty()) {
            strategies.push_back(data);
        }
    }

    return strategies;
}

QMap<QString, QString> load_strategy_key_descriptions() {
    QFile file(str_label("assets/strategies.json"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return {};
    }

    const QJsonObject root = doc.object();
    return parse_key_descriptions(root);
}
