#include "settings/gameplay_preferences.hpp"

#include "settings/gameplay_codec.hpp"
#include "table/gameplay_session.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSettings>

#include <algorithm>

namespace {

using namespace gameplay;
using namespace gameplay_storage;
const QString preferences_key = QStringLiteral("gameplay_preferences/document");
constexpr qsizetype maximum_document_bytes = 16 * 1024;

bool valid_preferences(const gameplay_preferences& value, slot_limits limits) {
    // Screen recommendations/consent are checked again by setup/Start. Storage
    // must not silently reduce a saved roster after a window/orientation
    // change.
    return valid_limits(limits)
        && valid_configuration(
               value.configuration, value.slot_count,
               { value.slot_count, limits.technical_cap }
        )
        && value.pickup_interval_ms >= session::minimum_pick_interval_ms
        && value.pickup_interval_ms <= session::maximum_pick_interval_ms;
}

QByteArray encode(const gameplay_preferences& value) {
    auto object = encode_configuration(value.configuration);
    object.insert(
        QStringLiteral("version"), gameplay_preferences::schema_version
    );
    object.insert(QStringLiteral("slots"), static_cast<int>(value.slot_count));
    object.insert(QStringLiteral("interval_ms"), value.pickup_interval_ms);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<gameplay_preferences>
decode(const QByteArray& bytes, slot_limits limits) {
    if (bytes.isEmpty() || bytes.size() > maximum_document_bytes)
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(bytes);
    if (!document.isObject())
        return std::nullopt;
    const auto root = document.object();
    if (!read_integer(
            root.value(QStringLiteral("version")),
            gameplay_preferences::schema_version,
            gameplay_preferences::schema_version
        ))
        return std::nullopt;
    const auto count = read_integer(
        root.value(QStringLiteral("slots")), 1,
        static_cast<int>(limits.technical_cap)
    );
    const auto interval = read_integer(
        root.value(QStringLiteral("interval_ms")),
        session::minimum_pick_interval_ms, session::maximum_pick_interval_ms
    );
    if (!count || !interval)
        return std::nullopt;
    const auto configuration = read_configuration(
        root, static_cast<std::size_t>(*count),
        { static_cast<std::size_t>(*count), limits.technical_cap }
    );
    if (!configuration)
        return std::nullopt;
    gameplay_preferences result {
        .slot_count = static_cast<std::size_t>(*count),
        .configuration = *configuration,
        .pickup_interval_ms = *interval,
    };
    return valid_preferences(result, limits) ? std::make_optional(result)
                                             : std::nullopt;
}

} // namespace

gameplay_preferences_service::gameplay_preferences_service(
    QSettings& settings_value, gameplay::slot_limits bounds
)
    : settings(settings_value)
    , limits(bounds) { }

std::optional<gameplay_preferences>
gameplay_preferences_service::load(QString* error) const {
    if (error)
        error->clear();
    settings.sync();
    if (settings.status() != QSettings::NoError || !valid_limits(limits)) {
        if (error)
            *error = QStringLiteral(
                "Could not read gameplay defaults. Stored data has not been "
                "changed."
            );
        return std::nullopt;
    }
    if (!settings.contains(preferences_key)) {
        gameplay_preferences defaults;
        defaults.slot_count = std::min(defaults.slot_count, limits.recommended);
        return defaults;
    }
    const auto stored = settings.value(preferences_key);
    auto result = stored.metaType().id() == QMetaType::QByteArray
        ? decode(stored.toByteArray(), limits)
        : std::nullopt;
    if (!result && error)
        *error = QStringLiteral(
            "Gameplay defaults are damaged, unsupported or exceed the current "
            "technical cap. Stored data has not been changed."
        );
    return result;
}

bool gameplay_preferences_service::save(
    const gameplay_preferences& value, QString* error
) {
    if (!load(error))
        return false;
    if (!valid_preferences(value, limits)) {
        if (error)
            *error = QStringLiteral(
                "Gameplay defaults do not satisfy the current configuration "
                "and slot limits."
            );
        return false;
    }
    const auto previous = settings.value(preferences_key);
    settings.setValue(preferences_key, encode(value));
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;
    if (previous.isValid())
        settings.setValue(preferences_key, previous);
    else
        settings.remove(preferences_key);
    if (error)
        *error = QStringLiteral(
            "Could not save gameplay defaults. Check that settings storage is "
            "writable."
        );
    return false;
}
