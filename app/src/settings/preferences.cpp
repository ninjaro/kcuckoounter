#include "settings/preferences.hpp"

#include "settings/strategy_data.hpp"

#include <QMetaType>
#include <QSettings>
#include <QVariant>

#include <optional>

namespace {

const QString preferences_group = QStringLiteral("trainer_preferences");
const QString desktop_shell_group = QStringLiteral("desktop_shell");
constexpr qsizetype maximum_shell_state_bytes = 1024 * 1024;

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

bool valid_card_orientation(card_orientation_mode value) noexcept {
    switch (value) {
    case card_orientation_mode::automatic:
    case card_orientation_mode::vertical:
    case card_orientation_mode::horizontal:
        return true;
    }
    return false;
}

QString card_orientation_key(card_orientation_mode value) {
    switch (value) {
    case card_orientation_mode::automatic:
        return QStringLiteral("automatic");
    case card_orientation_mode::vertical:
        return QStringLiteral("vertical");
    case card_orientation_mode::horizontal:
        return QStringLiteral("horizontal");
    }
    return QStringLiteral("automatic");
}

std::optional<card_orientation_mode>
card_orientation_from_key(const QString& value) {
    const QString key = value.trimmed().toLower();
    if (key == QStringLiteral("automatic")) {
        return card_orientation_mode::automatic;
    }
    if (key == QStringLiteral("vertical")) {
        return card_orientation_mode::vertical;
    }
    if (key == QStringLiteral("horizontal")) {
        return card_orientation_mode::horizontal;
    }
    return std::nullopt;
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
    if (!valid_card_orientation(preferences.card_orientation)) {
        preferences.card_orientation = fallback.card_orientation;
    }
    canonicalize_strategy(&preferences, catalog);
    return preferences;
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
    result.card_orientation
        = card_orientation_from_key(
              settings.value(QStringLiteral("appearance/card_orientation"))
                  .toString()
        )
              .value_or(fallback.card_orientation);
    result.preferred_strategy_slug
        = settings.value(QStringLiteral("strategy/slug")).toString().trimmed();
    result.preferred_strategy_id = bounded_integer<int>(
        settings.value(QStringLiteral("strategy/id")), 0, 1000000,
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
    settings.remove(QString());
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
        QStringLiteral("appearance/card_orientation"),
        card_orientation_key(value.card_orientation)
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
    settings.remove(QString());
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

slot_frame_style desktop_ui_preferences::frame() const {
    return frame_override.value_or(
        preset == desktop_ui_preset::quiet ? slot_frame_style::thin
                                           : slot_frame_style::classic
    );
}

bool desktop_ui_preferences::show_speed_readout() const {
    return speed_readout_override.value_or(preset != desktop_ui_preset::quiet);
}

quiz_answer_style desktop_ui_preferences::answer() const {
    return answer_override.value_or(quiz_answer_style::numeric);
}

quiz_feedback_style desktop_ui_preferences::feedback() const {
    return feedback_override.value_or(quiz_feedback_style::classic);
}

void desktop_ui_preferences::reset_overrides() {
    frame_override.reset();
    speed_readout_override.reset();
    answer_override.reset();
    feedback_override.reset();
}

desktop_ui_preferences load_desktop_ui_preferences(QSettings& settings) {
    desktop_ui_preferences result;
    settings.beginGroup(QStringLiteral("desktop_ui"));
    if (settings.value(QStringLiteral("preset")).toString()
        == QStringLiteral("quiet")) {
        result.preset = desktop_ui_preset::quiet;
    }
    const QString frame
        = settings.value(QStringLiteral("overrides/frame")).toString();
    if (frame == QStringLiteral("classic")) {
        result.frame_override = slot_frame_style::classic;
    } else if (frame == QStringLiteral("thin")) {
        result.frame_override = slot_frame_style::thin;
    }
    const QString speed
        = settings.value(QStringLiteral("overrides/speed_readout")).toString();
    if (speed == QStringLiteral("shown")) {
        result.speed_readout_override = true;
    } else if (speed == QStringLiteral("hidden")) {
        result.speed_readout_override = false;
    }
    const QString answer
        = settings.value(QStringLiteral("overrides/answer_entry")).toString();
    if (answer == QStringLiteral("numeric")) {
        result.answer_override = quiz_answer_style::numeric;
    } else if (answer == QStringLiteral("chips")) {
        result.answer_override = quiz_answer_style::chips;
    }
    const QString feedback
        = settings.value(QStringLiteral("overrides/feedback")).toString();
    if (feedback == QStringLiteral("classic")) {
        result.feedback_override = quiz_feedback_style::classic;
    } else if (feedback == QStringLiteral("stamp")) {
        result.feedback_override = quiz_feedback_style::stamp;
    }
    settings.endGroup();
    return result;
}

bool save_desktop_ui_preferences(
    QSettings& settings, const desktop_ui_preferences& preferences
) {
    settings.beginGroup(QStringLiteral("desktop_ui"));
    settings.setValue(
        QStringLiteral("preset"),
        preferences.preset == desktop_ui_preset::quiet
            ? QStringLiteral("quiet")
            : QStringLiteral("classic")
    );
    if (preferences.frame_override) {
        settings.setValue(
            QStringLiteral("overrides/frame"),
            *preferences.frame_override == slot_frame_style::thin
                ? QStringLiteral("thin")
                : QStringLiteral("classic")
        );
    } else {
        settings.remove(QStringLiteral("overrides/frame"));
    }
    if (preferences.speed_readout_override) {
        settings.setValue(
            QStringLiteral("overrides/speed_readout"),
            *preferences.speed_readout_override ? QStringLiteral("shown")
                                                : QStringLiteral("hidden")
        );
    } else {
        settings.remove(QStringLiteral("overrides/speed_readout"));
    }
    if (preferences.answer_override) {
        settings.setValue(
            QStringLiteral("overrides/answer_entry"),
            *preferences.answer_override == quiz_answer_style::chips
                ? QStringLiteral("chips")
                : QStringLiteral("numeric")
        );
    } else {
        settings.remove(QStringLiteral("overrides/answer_entry"));
    }
    if (preferences.feedback_override) {
        settings.setValue(
            QStringLiteral("overrides/feedback"),
            *preferences.feedback_override == quiz_feedback_style::stamp
                ? QStringLiteral("stamp")
                : QStringLiteral("classic")
        );
    } else {
        settings.remove(QStringLiteral("overrides/feedback"));
    }
    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

desktop_ui_preferences load_desktop_ui_preferences() {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    return load_desktop_ui_preferences(settings);
}

bool save_desktop_ui_preferences(const desktop_ui_preferences& preferences) {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    return save_desktop_ui_preferences(settings, preferences);
}
