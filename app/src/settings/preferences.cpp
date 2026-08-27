#include "settings/preferences.hpp"

#include "settings/strategy_data.hpp"

#include <QColor>
#include <QMetaType>
#include <QSettings>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <optional>

namespace {

const QString preferences_group = QStringLiteral("trainer_preferences");
const QString schema_version_key = QStringLiteral("schema_version");
const QString desktop_shell_group = QStringLiteral("desktop_shell");
constexpr qsizetype maximum_shell_state_bytes = 1024 * 1024;

const QStringList legacy_keys = {
    QStringLiteral("table_slots"),      QStringLiteral("quiz_type"),
    QStringLiteral("wait_for_answers"), QStringLiteral("allow_skipping"),
    QStringLiteral("dealing_mode"),     QStringLiteral("pickup_interval_ms"),
    QStringLiteral("palette"),          QStringLiteral("theme_color"),
    QStringLiteral("strategy_slug"),    QStringLiteral("strategy_id"),
};

template <typename T>
T bounded_integer(const QVariant& value, T minimum, T maximum, T fallback) {
    bool ok = false;
    const qlonglong converted = value.toLongLong(&ok);
    if (!ok || converted < static_cast<qlonglong>(minimum)
        || converted > static_cast<qlonglong>(maximum)) {
        return fallback;
    }
    return static_cast<T>(converted);
}

bool validated_bool(const QVariant& value, bool fallback) {
    if (!value.isValid()) {
        return fallback;
    }
    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool();
    }

    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1")) {
        return true;
    }
    if (text == QStringLiteral("false") || text == QStringLiteral("0")) {
        return false;
    }
    return fallback;
}

QString palette_key(theme_palette_id palette) {
    switch (palette) {
    case theme_palette_id::red:
        return QStringLiteral("red");
    case theme_palette_id::green:
        return QStringLiteral("green");
    case theme_palette_id::blue:
        return QStringLiteral("blue");
    }
    return QStringLiteral("green");
}

std::optional<theme_palette_id> palette_from_key(const QString& value) {
    const QString key = value.trimmed().toLower();
    if (key == QStringLiteral("red")) {
        return theme_palette_id::red;
    }
    if (key == QStringLiteral("green")) {
        return theme_palette_id::green;
    }
    if (key == QStringLiteral("blue")) {
        return theme_palette_id::blue;
    }
    return std::nullopt;
}

theme_palette_id palette_from_legacy_value(
    const QVariant& palette_value, const QVariant& color_value,
    theme_palette_id fallback
) {
    const auto stored_palette = palette_from_key(palette_value.toString());
    if (stored_palette.has_value()) {
        return *stored_palette;
    }

    const auto stored_color = color_value.value<QColor>();
    if (stored_color.isValid()) {
        return theme_palette_registry::id_from_color(stored_color);
    }
    return fallback;
}

void canonicalize_strategy(
    trainer_preferences* preferences, const strategy_catalog& catalog
) {
    if (preferences == nullptr) {
        return;
    }
    if (!catalog.is_valid() || catalog.strategies.isEmpty()) {
        preferences->preferred_strategy_slug.clear();
        preferences->preferred_strategy_id = 0;
        return;
    }

    for (const strategy_data& strategy : catalog.strategies) {
        if (!preferences->preferred_strategy_slug.isEmpty()
            && strategy.slug == preferences->preferred_strategy_slug) {
            preferences->preferred_strategy_slug = strategy.slug;
            preferences->preferred_strategy_id = strategy.id;
            return;
        }
    }
    for (const strategy_data& strategy : catalog.strategies) {
        if (preferences->preferred_strategy_id > 0
            && strategy.id == preferences->preferred_strategy_id) {
            preferences->preferred_strategy_slug = strategy.slug;
            preferences->preferred_strategy_id = strategy.id;
            return;
        }
    }

    preferences->preferred_strategy_slug = catalog.strategies.first().slug;
    preferences->preferred_strategy_id = catalog.strategies.first().id;
}

trainer_preferences validated_preferences(
    trainer_preferences preferences, const strategy_catalog& catalog
) {
    const trainer_preferences fallback = preferences_service::defaults(catalog);
    if (preferences.slot_count < trainer_preferences::minimum_slot_count
        || preferences.slot_count > trainer_preferences::maximum_slot_count) {
        preferences.slot_count = fallback.slot_count;
    }
    if (preferences.quiz_type < 0 || preferences.quiz_type > 1) {
        preferences.quiz_type = fallback.quiz_type;
    }
    if (preferences.dealing_mode < 0 || preferences.dealing_mode > 2) {
        preferences.dealing_mode = fallback.dealing_mode;
    }
    if (preferences.pickup_interval_ms
            < trainer_preferences::minimum_pickup_interval_ms
        || preferences.pickup_interval_ms
            > trainer_preferences::maximum_pickup_interval_ms) {
        preferences.pickup_interval_ms = fallback.pickup_interval_ms;
    }
    canonicalize_strategy(&preferences, catalog);
    return preferences;
}

bool has_legacy_preferences(QSettings& settings) {
    return std::ranges::any_of(legacy_keys, [&settings](const QString& key) {
        return settings.contains(key);
    });
}

QByteArray bounded_byte_array(const QVariant& value) {
    if (value.metaType().id() != QMetaType::QByteArray) {
        return {};
    }
    const QByteArray bytes = value.toByteArray();
    if (bytes.size() > maximum_shell_state_bytes) {
        return {};
    }
    return bytes;
}

trainer_preferences
read_current_preferences(QSettings& settings, const strategy_catalog& catalog) {
    const trainer_preferences fallback = preferences_service::defaults(catalog);
    trainer_preferences result = fallback;
    result.slot_count = bounded_integer<int>(
        settings.value(QStringLiteral("setup/slot_count")),
        trainer_preferences::minimum_slot_count,
        trainer_preferences::maximum_slot_count, fallback.slot_count
    );
    result.quiz_type = bounded_integer<int>(
        settings.value(QStringLiteral("setup/quiz_type")), 0, 1,
        fallback.quiz_type
    );
    result.wait_for_answers = validated_bool(
        settings.value(QStringLiteral("setup/wait_for_answers")),
        fallback.wait_for_answers
    );
    result.allow_skipping = validated_bool(
        settings.value(QStringLiteral("setup/allow_skipping")),
        fallback.allow_skipping
    );
    result.dealing_mode = bounded_integer<int>(
        settings.value(QStringLiteral("setup/dealing_mode")), 0, 2,
        fallback.dealing_mode
    );
    result.pickup_interval_ms = bounded_integer<int>(
        settings.value(QStringLiteral("setup/pickup_interval_ms")),
        trainer_preferences::minimum_pickup_interval_ms,
        trainer_preferences::maximum_pickup_interval_ms,
        fallback.pickup_interval_ms
    );
    result.palette
        = palette_from_key(
              settings.value(QStringLiteral("appearance/palette")).toString()
        )
              .value_or(fallback.palette);
    result.preferred_strategy_slug
        = settings.value(QStringLiteral("strategy/slug")).toString().trimmed();
    result.preferred_strategy_id = bounded_integer<int>(
        settings.value(QStringLiteral("strategy/id")), 0, 1000000,
        fallback.preferred_strategy_id
    );
    canonicalize_strategy(&result, catalog);
    return result;
}

trainer_preferences
read_legacy_preferences(QSettings& settings, const strategy_catalog& catalog) {
    const trainer_preferences fallback = preferences_service::defaults(catalog);
    trainer_preferences result = fallback;
    result.slot_count = bounded_integer<int>(
        settings.value(QStringLiteral("table_slots")),
        trainer_preferences::minimum_slot_count,
        trainer_preferences::maximum_slot_count, fallback.slot_count
    );
    result.quiz_type = bounded_integer<int>(
        settings.value(QStringLiteral("quiz_type")), 0, 1, fallback.quiz_type
    );
    result.wait_for_answers = validated_bool(
        settings.value(QStringLiteral("wait_for_answers")),
        fallback.wait_for_answers
    );
    result.allow_skipping = validated_bool(
        settings.value(QStringLiteral("allow_skipping")),
        fallback.allow_skipping
    );
    result.dealing_mode = bounded_integer<int>(
        settings.value(QStringLiteral("dealing_mode")), 0, 2,
        fallback.dealing_mode
    );
    result.pickup_interval_ms = bounded_integer<int>(
        settings.value(QStringLiteral("pickup_interval_ms")),
        trainer_preferences::minimum_pickup_interval_ms,
        trainer_preferences::maximum_pickup_interval_ms,
        fallback.pickup_interval_ms
    );
    result.palette = palette_from_legacy_value(
        settings.value(QStringLiteral("palette")),
        settings.value(QStringLiteral("theme_color")), fallback.palette
    );
    result.preferred_strategy_slug
        = settings.value(QStringLiteral("strategy_slug")).toString().trimmed();
    result.preferred_strategy_id = bounded_integer<int>(
        settings.value(QStringLiteral("strategy_id")), 0, 1000000,
        fallback.preferred_strategy_id
    );
    canonicalize_strategy(&result, catalog);
    return result;
}

} // namespace

preferences_service::preferences_service(QSettings& settings_value)
    : settings(settings_value) { }

trainer_preferences
preferences_service::defaults(const strategy_catalog& catalog) {
    trainer_preferences result;
    canonicalize_strategy(&result, catalog);
    return result;
}

trainer_preferences preferences_service::load(const strategy_catalog& catalog) {
    settings.beginGroup(preferences_group);
    if (!settings.contains(schema_version_key)) {
        if (!has_legacy_preferences(settings)) {
            settings.endGroup();
            return defaults(catalog);
        }

        const trainer_preferences migrated
            = read_legacy_preferences(settings, catalog);
        settings.endGroup();
        save(migrated, catalog);
        return migrated;
    }

    bool version_ok = false;
    const int version = settings.value(schema_version_key).toInt(&version_ok);
    if (!version_ok || version != trainer_preferences::schema_version) {
        settings.endGroup();
        return defaults(catalog);
    }

    const trainer_preferences result
        = read_current_preferences(settings, catalog);
    const QString stored_strategy_slug
        = settings.value(QStringLiteral("strategy/slug")).toString().trimmed();
    bool stored_strategy_id_ok = false;
    const int stored_strategy_id = settings.value(QStringLiteral("strategy/id"))
                                       .toInt(&stored_strategy_id_ok);
    settings.endGroup();
    if (stored_strategy_slug != result.preferred_strategy_slug
        || !stored_strategy_id_ok
        || stored_strategy_id != result.preferred_strategy_id) {
        save(result, catalog);
    }
    return result;
}

void preferences_service::save(
    const trainer_preferences& preferences, const strategy_catalog& catalog
) {
    const trainer_preferences value
        = validated_preferences(preferences, catalog);

    settings.beginGroup(preferences_group);
    if (settings.contains(schema_version_key)) {
        bool stored_version_ok = false;
        const int stored_version
            = settings.value(schema_version_key).toInt(&stored_version_ok);
        if (stored_version_ok
            && stored_version > trainer_preferences::schema_version) {
            settings.endGroup();
            return;
        }
    }
    settings.remove(QString());
    settings.setValue(schema_version_key, trainer_preferences::schema_version);
    settings.setValue(QStringLiteral("setup/slot_count"), value.slot_count);
    settings.setValue(QStringLiteral("setup/quiz_type"), value.quiz_type);
    settings.setValue(
        QStringLiteral("setup/wait_for_answers"), value.wait_for_answers
    );
    settings.setValue(
        QStringLiteral("setup/allow_skipping"), value.allow_skipping
    );
    settings.setValue(QStringLiteral("setup/dealing_mode"), value.dealing_mode);
    settings.setValue(
        QStringLiteral("setup/pickup_interval_ms"), value.pickup_interval_ms
    );
    settings.setValue(
        QStringLiteral("appearance/palette"), palette_key(value.palette)
    );
    settings.setValue(
        QStringLiteral("strategy/slug"), value.preferred_strategy_slug
    );
    settings.setValue(
        QStringLiteral("strategy/id"), value.preferred_strategy_id
    );
    settings.endGroup();
    settings.sync();
}

desktop_shell_state_service::desktop_shell_state_service(
    QSettings& settings_value
)
    : settings(settings_value) { }

desktop_shell_state desktop_shell_state_service::load() const {
    settings.beginGroup(desktop_shell_group);
    bool version_ok = false;
    const int version = settings.value(schema_version_key).toInt(&version_ok);
    if (!version_ok || version != desktop_shell_state::schema_version) {
        settings.endGroup();
        return {};
    }

    desktop_shell_state result;
    result.geometry
        = bounded_byte_array(settings.value(QStringLiteral("window/geometry")));
    result.main_window_state = bounded_byte_array(
        settings.value(QStringLiteral("window/main_state"))
    );
    settings.endGroup();
    return result;
}

void desktop_shell_state_service::save(const desktop_shell_state& state) {
    settings.beginGroup(desktop_shell_group);
    if (settings.contains(schema_version_key)) {
        bool stored_version_ok = false;
        const int stored_version
            = settings.value(schema_version_key).toInt(&stored_version_ok);
        if (stored_version_ok
            && stored_version > desktop_shell_state::schema_version) {
            settings.endGroup();
            return;
        }
    }

    settings.remove(QString());
    settings.setValue(schema_version_key, desktop_shell_state::schema_version);
    settings.setValue(QStringLiteral("window/geometry"), state.geometry);
    settings.setValue(
        QStringLiteral("window/main_state"), state.main_window_state
    );
    settings.endGroup();
    settings.sync();
}

trainer_preferences load_trainer_preferences() {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    preferences_service service(settings);
    return service.load(strategy_repository());
}

void save_trainer_preferences(const trainer_preferences& preferences) {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    preferences_service service(settings);
    service.save(preferences, strategy_repository());
}

desktop_shell_state load_desktop_shell_state() {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    desktop_shell_state_service service(settings);
    return service.load();
}

void save_desktop_shell_state(const desktop_shell_state& state) {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    desktop_shell_state_service service(settings);
    service.save(state);
}
