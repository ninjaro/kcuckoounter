#include "settings/gameplay_drill.hpp"

#include "settings/gameplay_codec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaType>
#include <QSet>
#include <QSettings>

#include <algorithm>
#include <limits>

namespace {

using namespace gameplay_storage;
const QString drills_key = QStringLiteral("gameplay_drills/document");
constexpr qsizetype maximum_document_bytes = 1024 * 1024;

bool valid_catalogue(
    const QVector<gameplay_drill>& drills, gameplay::slot_limits limits
) {
    if (!valid_limits(limits)
        || drills.size() > gameplay_drill_service::maximum_drills)
        return false;
    QSet<QString> names;
    for (const auto& drill : drills) {
        const auto name = drill.name.normalized(QString::NormalizationForm_C)
                              .toCaseFolded();
        if (!valid_gameplay_drill(drill, limits) || names.contains(name))
            return false;
        names.insert(name);
    }
    return true;
}

QByteArray encode(const QVector<gameplay_drill>& drills) {
    QJsonArray entries;
    for (const auto& drill : drills) {
        auto object = encode_configuration(drill.configuration);
        object.insert(QStringLiteral("name"), drill.name);
        object.insert(QStringLiteral("interval_ms"), drill.pickup_interval_ms);
        QJsonArray decks;
        for (const auto& deck : drill.decks) {
            const auto& config = deck.configuration;
            decks.append(
                QJsonObject {
                    { QStringLiteral("strategy"),
                      QString::fromStdString(config.strategy_slug) },
                    { QStringLiteral("count"),
                      static_cast<int>(config.deck_count) },
                    { QStringLiteral("infinite"), config.infinite },
                    { QStringLiteral("training"), config.training },
                    { QStringLiteral("show_count"), deck.show_count },
                }
            );
        }
        object.insert(QStringLiteral("decks"), decks);
        entries.append(object);
    }
    return QJsonDocument(
               QJsonObject {
                   { QStringLiteral("version"),
                     gameplay_drill_service::schema_version },
                   { QStringLiteral("drills"), entries },
               }
    )
        .toJson(QJsonDocument::Compact);
}

std::optional<gameplay_drill>
decode_entry(const QJsonObject& object, gameplay::slot_limits limits) {
    if (!object.value(QStringLiteral("name")).isString()
        || !object.value(QStringLiteral("decks")).isArray())
        return std::nullopt;
    const auto entries = object.value(QStringLiteral("decks")).toArray();
    if (entries.isEmpty()
        || static_cast<std::size_t>(entries.size()) > limits.technical_cap)
        return std::nullopt;
    const auto count = static_cast<std::size_t>(entries.size());
    const auto config
        = read_configuration(object, count, { count, limits.technical_cap });
    const auto interval = read_integer(
        object.value(QStringLiteral("interval_ms")),
        gameplay::session::minimum_pick_interval_ms,
        gameplay::session::maximum_pick_interval_ms
    );
    if (!config || !interval)
        return std::nullopt;
    gameplay_drill result {
        object.value(QStringLiteral("name")).toString(), *config, *interval, {}
    };
    result.decks.reserve(count);
    for (const auto& entry : entries) {
        if (!entry.isObject())
            return std::nullopt;
        const auto deck = entry.toObject();
        const auto number = read_integer(
            deck.value(QStringLiteral("count")), 1,
            std::numeric_limits<int>::max()
        );
        if (!number || !deck.value(QStringLiteral("strategy")).isString()
            || !deck.value(QStringLiteral("infinite")).isBool()
            || !deck.value(QStringLiteral("training")).isBool()
            || !deck.value(QStringLiteral("show_count")).isBool())
            return std::nullopt;
        result.decks.push_back(
            {
                { deck.value(QStringLiteral("strategy"))
                      .toString()
                      .toStdString(),
                  static_cast<std::size_t>(*number),
                  deck.value(QStringLiteral("infinite")).toBool(),
                  deck.value(QStringLiteral("training")).toBool() },
                deck.value(QStringLiteral("show_count")).toBool(),
            }
        );
    }
    return valid_gameplay_drill(result, limits) ? std::make_optional(result)
                                                : std::nullopt;
}

std::optional<QVector<gameplay_drill>>
decode(const QByteArray& bytes, gameplay::slot_limits limits) {
    if (bytes.isEmpty() || bytes.size() > maximum_document_bytes)
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(bytes);
    if (!document.isObject())
        return std::nullopt;
    const auto root = document.object();
    if (!read_integer(
            root.value(QStringLiteral("version")),
            gameplay_drill_service::schema_version,
            gameplay_drill_service::schema_version
        )
        || !root.value(QStringLiteral("drills")).isArray())
        return std::nullopt;
    const auto entries = root.value(QStringLiteral("drills")).toArray();
    if (entries.size() > gameplay_drill_service::maximum_drills)
        return std::nullopt;
    QVector<gameplay_drill> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        if (!entry.isObject())
            return std::nullopt;
        const auto value = decode_entry(entry.toObject(), limits);
        if (!value)
            return std::nullopt;
        result.append(*value);
    }
    return valid_catalogue(result, limits) ? std::make_optional(result)
                                           : std::nullopt;
}

} // namespace

bool valid_gameplay_drill(
    const gameplay_drill& drill, gameplay::slot_limits limits
) {
    if (!valid_limits(limits) || drill.name.isEmpty()
        || drill.name != drill.name.trimmed()
        || drill.name.size() > gameplay_drill_service::maximum_name_length
        || !gameplay::valid_configuration(
            drill.configuration, drill.decks.size(),
            { drill.decks.size(), limits.technical_cap }
        )
        || drill.pickup_interval_ms
            < gameplay::session::minimum_pick_interval_ms
        || drill.pickup_interval_ms
            > gameplay::session::maximum_pick_interval_ms)
        return false;
    for (const auto character : drill.name)
        if (character.category() == QChar::Other_Control)
            return false;
    return std::ranges::all_of(drill.decks, [](const auto& deck) {
        const auto& config = deck.configuration;
        const auto slug = QString::fromStdString(config.strategy_slug);
        return config.deck_count > 0
            && config.deck_count
            <= static_cast<std::size_t>(std::numeric_limits<int>::max())
            && !slug.isEmpty() && slug == slug.trimmed() && slug.size() <= 128
            && slug.toStdString() == config.strategy_slug
            && (!deck.show_count || config.training);
    });
}

gameplay_drill_service::gameplay_drill_service(
    QSettings& settings_value, gameplay::slot_limits bounds
)
    : settings(settings_value)
    , limits(bounds) { }

std::optional<QVector<gameplay_drill>>
gameplay_drill_service::load(QString* error) const {
    if (error)
        error->clear();
    settings.sync();
    if (settings.status() != QSettings::NoError || !valid_limits(limits)) {
        if (error)
            *error = QStringLiteral(
                "Could not read gameplay drills. Stored data has not been "
                "changed."
            );
        return std::nullopt;
    }
    if (!settings.contains(drills_key))
        return QVector<gameplay_drill> {};
    const auto stored = settings.value(drills_key);
    auto result = stored.metaType().id() == QMetaType::QByteArray
        ? decode(stored.toByteArray(), limits)
        : std::nullopt;
    if (!result && error)
        *error = QStringLiteral(
            "Gameplay drills are damaged, unsupported or exceed the current "
            "technical cap. Stored data has not been changed."
        );
    return result;
}

bool gameplay_drill_service::save(
    const QVector<gameplay_drill>& drills, QString* error
) {
    if (!load(error))
        return false;
    if (!valid_catalogue(drills, limits)) {
        if (error)
            *error = QStringLiteral(
                "Use unique names of 1–80 characters and at most 64 gameplay "
                "drills within the current technical cap."
            );
        return false;
    }
    const auto bytes = encode(drills);
    if (bytes.size() > maximum_document_bytes) {
        if (error)
            *error
                = QStringLiteral("Gameplay drills exceed the storage limit.");
        return false;
    }
    const auto previous = settings.value(drills_key);
    settings.setValue(drills_key, bytes);
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;
    if (previous.isValid())
        settings.setValue(drills_key, previous);
    else
        settings.remove(drills_key);
    if (error)
        *error = QStringLiteral(
            "Could not save gameplay drills. Check that settings storage is "
            "writable."
        );
    return false;
}
