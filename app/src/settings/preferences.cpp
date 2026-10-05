#include "settings/preferences.hpp"

#include "settings/strategy_data.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSet>
#include <QSettings>
#include <QVariant>

#include <algorithm>
#include <optional>

namespace {

const QString drills_key = QStringLiteral("training_drills/document");
constexpr qsizetype maximum_drills_bytes = 1024 * 1024;

bool valid_drill(const training_drill& drill) {
    if (drill.name.isEmpty() || drill.name != drill.name.trimmed()
        || drill.name.size() > training_drill_service::maximum_name_length
        || drill.quiz_type < 0 || drill.quiz_type > 1
        || (drill.quiz_type == 1 && !drill.wait_for_answers)
        || drill.dealing_mode < 0 || drill.dealing_mode > 2
        || drill.pickup_interval_ms
            < trainer_preferences::minimum_pickup_interval_ms
        || drill.pickup_interval_ms
            > trainer_preferences::maximum_pickup_interval_ms
        || drill.slot_settings.isEmpty()
        || drill.slot_settings.size() > trainer_preferences::maximum_slot_count)
        return false;
    for (const auto character : drill.name)
        if (character.category() == QChar::Other_Control)
            return false;
    return std::ranges::all_of(drill.slot_settings, [](const auto& slot) {
        return slot.deck_count >= 1 && slot.deck_count <= 16
            && !slot.strategy_slug.isEmpty() && slot.strategy_slug.size() <= 128
            && slot.strategy_slug == slot.strategy_slug.trimmed();
    });
}

bool valid_drills(const QVector<training_drill>& drills) {
    if (drills.size() > training_drill_service::maximum_drills)
        return false;
    QSet<QString> names;
    for (const auto& drill : drills) {
        const auto name = drill.name.normalized(QString::NormalizationForm_C)
                              .toCaseFolded();
        if (!valid_drill(drill) || names.contains(name))
            return false;
        names.insert(name);
    }
    return true;
}

int drill_integer(const QJsonValue& value, int minimum, int maximum) {
    if (!value.isDouble())
        return -1;
    const double number = value.toDouble();
    if (!(number >= minimum && number <= maximum))
        return -1;
    const int integer = static_cast<int>(number);
    return number == integer ? integer : -1;
}

QByteArray encode_drills(const QVector<training_drill>& drills) {
    QJsonArray entries;
    for (const auto& drill : drills) {
        QJsonArray slot_entries;
        for (const auto& slot : drill.slot_settings)
            slot_entries.append(
                QJsonObject {
                    { QStringLiteral("decks"), slot.deck_count },
                    { QStringLiteral("infinite"), slot.infinity_enabled },
                    { QStringLiteral("strategy"), slot.strategy_slug },
                    { QStringLiteral("training"), slot.training_mode },
                }
            );
        entries.append(
            QJsonObject {
                { QStringLiteral("name"), drill.name },
                { QStringLiteral("quiz"), drill.quiz_type },
                { QStringLiteral("wait"), drill.wait_for_answers },
                { QStringLiteral("skip"), drill.allow_skipping },
                { QStringLiteral("dealing"), drill.dealing_mode },
                { QStringLiteral("interval_ms"), drill.pickup_interval_ms },
                { QStringLiteral("slots"), slot_entries },
            }
        );
    }
    return QJsonDocument(
               QJsonObject { { QStringLiteral("version"), 1 },
                             { QStringLiteral("drills"), entries } }
    ).toJson(QJsonDocument::Compact);
}

std::optional<QVector<training_drill>> decode_drills(const QByteArray& bytes) {
    if (bytes.isEmpty() || bytes.size() > maximum_drills_bytes)
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(bytes);
    if (!document.isObject())
        return std::nullopt;
    const auto root = document.object();
    if (drill_integer(root.value(QStringLiteral("version")), 1, 1) != 1
        || !root.value(QStringLiteral("drills")).isArray())
        return std::nullopt;
    const auto entries = root.value(QStringLiteral("drills")).toArray();
    if (entries.size() > training_drill_service::maximum_drills)
        return std::nullopt;
    QVector<training_drill> drills;
    for (const auto& entry : entries) {
        if (!entry.isObject())
            return std::nullopt;
        const auto object = entry.toObject();
        if (!object.value(QStringLiteral("name")).isString()
            || !object.value(QStringLiteral("wait")).isBool()
            || !object.value(QStringLiteral("skip")).isBool()
            || !object.value(QStringLiteral("slots")).isArray())
            return std::nullopt;
        training_drill drill;
        drill.name = object.value(QStringLiteral("name")).toString();
        drill.quiz_type
            = drill_integer(object.value(QStringLiteral("quiz")), 0, 1);
        drill.wait_for_answers = object.value(QStringLiteral("wait")).toBool();
        drill.allow_skipping = object.value(QStringLiteral("skip")).toBool();
        drill.dealing_mode
            = drill_integer(object.value(QStringLiteral("dealing")), 0, 2);
        drill.pickup_interval_ms = drill_integer(
            object.value(QStringLiteral("interval_ms")),
            trainer_preferences::minimum_pickup_interval_ms,
            trainer_preferences::maximum_pickup_interval_ms
        );
        const auto slot_entries
            = object.value(QStringLiteral("slots")).toArray();
        if (slot_entries.size() > trainer_preferences::maximum_slot_count)
            return std::nullopt;
        for (const auto& value : slot_entries) {
            if (!value.isObject())
                return std::nullopt;
            const auto slot = value.toObject();
            if (!slot.value(QStringLiteral("infinite")).isBool()
                || !slot.value(QStringLiteral("training")).isBool()
                || !slot.value(QStringLiteral("strategy")).isString())
                return std::nullopt;
            drill.slot_settings.append(
                {
                    drill_integer(slot.value(QStringLiteral("decks")), 1, 16),
                    slot.value(QStringLiteral("infinite")).toBool(),
                    slot.value(QStringLiteral("strategy")).toString(),
                    slot.value(QStringLiteral("training")).toBool(),
                }
            );
        }
        drills.append(drill);
    }
    return valid_drills(drills) ? std::make_optional(drills) : std::nullopt;
}

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

bool is_drill_configuration_supported(
    const training_drill& drill, const strategy_catalog& catalog
) {
    return valid_drill(drill) && catalog.is_valid()
        && std::ranges::all_of(drill.slot_settings, [&](const auto& slot) {
               return std::ranges::any_of(
                   catalog.strategies, [&](const auto& strategy) {
                       return strategy.slug == slot.strategy_slug;
                   }
               );
           });
}

training_drill_service::training_drill_service(QSettings& value)
    : settings(value) { }

std::optional<QVector<training_drill>>
training_drill_service::load(QString* error) const {
    if (error)
        error->clear();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral(
                "Could not read saved drills. Stored data has not been changed."
            );
        return std::nullopt;
    }
    if (!settings.contains(drills_key))
        return QVector<training_drill> {};
    auto result = decode_drills(settings.value(drills_key).toByteArray());
    if (!result && error)
        *error = QStringLiteral(
            "Saved drills are damaged or use an unsupported format. Stored "
            "data has not been changed."
        );
    return result;
}

bool training_drill_service::save(
    const QVector<training_drill>& drills, QString* error
) {
    if (!load(error))
        return false; // Never replace unreadable or future data with an empty
                      // list.
    if (!valid_drills(drills)) {
        if (error)
            *error = QStringLiteral(
                "Use unique names of 1–80 characters and at most 64 valid "
                "drills."
            );
        return false;
    }
    const auto bytes = encode_drills(drills);
    if (bytes.size() > maximum_drills_bytes) {
        if (error)
            *error = QStringLiteral("Saved drills exceed the storage limit.");
        return false;
    }
    const auto previous = settings.value(drills_key);
    settings.setValue(drills_key, bytes);
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;
    // Restore the in-memory value too; a later settings sync must not commit a
    // mutation for which the UI reported failure.
    if (previous.isValid())
        settings.setValue(drills_key, previous);
    else
        settings.remove(drills_key);
    if (error)
        *error = QStringLiteral(
            "Could not save drills. Check that settings storage is writable."
        );
    return false;
}

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

slot_action_style desktop_ui_preferences::actions() const {
    return actions_override.value_or(slot_action_style::classic);
}

slot_settings_style desktop_ui_preferences::settings_surface() const {
    return settings_override.value_or(slot_settings_style::classic);
}

desktop_toolbar_style desktop_ui_preferences::toolbar() const {
    return toolbar_override.value_or(desktop_toolbar_style::classic);
}

desktop_hud_style desktop_ui_preferences::hud() const {
    return hud_override.value_or(desktop_hud_style::classic);
}

void desktop_ui_preferences::reset_overrides() {
    frame_override.reset();
    speed_readout_override.reset();
    answer_override.reset();
    feedback_override.reset();
    actions_override.reset();
    settings_override.reset();
    toolbar_override.reset();
    hud_override.reset();
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
    const QString actions
        = settings.value(QStringLiteral("overrides/slot_actions")).toString();
    if (actions == QStringLiteral("classic")) {
        result.actions_override = slot_action_style::classic;
    } else if (actions == QStringLiteral("rail")) {
        result.actions_override = slot_action_style::rail;
    } else if (actions == QStringLiteral("pills")) {
        result.actions_override = slot_action_style::pills;
    }
    const auto surface
        = settings.value(QStringLiteral("overrides/settings_surface"))
              .toString();
    if (surface == QStringLiteral("classic"))
        result.settings_override = slot_settings_style::classic;
    else if (surface == QStringLiteral("card"))
        result.settings_override = slot_settings_style::card;
    else if (surface == QStringLiteral("drawer"))
        result.settings_override = slot_settings_style::drawer;
    else if (surface == QStringLiteral("sill"))
        result.settings_override = slot_settings_style::sill;
    const auto toolbar
        = settings.value(QStringLiteral("overrides/toolbar")).toString();
    if (toolbar == QStringLiteral("classic"))
        result.toolbar_override = desktop_toolbar_style::classic;
    else if (toolbar == QStringLiteral("compact"))
        result.toolbar_override = desktop_toolbar_style::compact;
    const auto hud = settings.value(QStringLiteral("overrides/hud")).toString();
    if (hud == QStringLiteral("classic"))
        result.hud_override = desktop_hud_style::classic;
    else if (hud == QStringLiteral("instruments"))
        result.hud_override = desktop_hud_style::instruments;
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
    if (preferences.actions_override) {
        const auto style = *preferences.actions_override;
        settings.setValue(
            QStringLiteral("overrides/slot_actions"),
            style == slot_action_style::rail        ? QStringLiteral("rail")
                : style == slot_action_style::pills ? QStringLiteral("pills")
                                                    : QStringLiteral("classic")
        );
    } else {
        settings.remove(QStringLiteral("overrides/slot_actions"));
    }
    if (preferences.settings_override) {
        const auto style = *preferences.settings_override;
        settings.setValue(
            QStringLiteral("overrides/settings_surface"),
            style == slot_settings_style::card ? QStringLiteral("card")
                : style == slot_settings_style::drawer
                ? QStringLiteral("drawer")
                : style == slot_settings_style::sill ? QStringLiteral("sill")
                                                     : QStringLiteral("classic")
        );
    } else {
        settings.remove(QStringLiteral("overrides/settings_surface"));
    }
    if (preferences.toolbar_override) {
        settings.setValue(
            QStringLiteral("overrides/toolbar"),
            *preferences.toolbar_override == desktop_toolbar_style::compact
                ? QStringLiteral("compact")
                : QStringLiteral("classic")
        );
    } else {
        settings.remove(QStringLiteral("overrides/toolbar"));
    }
    if (preferences.hud_override) {
        settings.setValue(
            QStringLiteral("overrides/hud"),
            *preferences.hud_override == desktop_hud_style::instruments
                ? QStringLiteral("instruments")
                : QStringLiteral("classic")
        );
    } else {
        settings.remove(QStringLiteral("overrides/hud"));
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

int load_default_suit_preference(QSettings& settings) {
    bool valid = false;
    const int index = settings.value(QStringLiteral("appearance/default_suit"))
                          .toString()
                          .toInt(&valid);
    return valid && index >= 0 && index <= 3 ? index : 0;
}

bool save_default_suit_preference(QSettings& settings, int index) {
    if (index < 0 || index > 3)
        return false;
    settings.setValue(QStringLiteral("appearance/default_suit"), index);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

int load_default_suit_preference() {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    return load_default_suit_preference(settings);
}

bool save_default_suit_preference(int index) {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    return save_default_suit_preference(settings, index);
}
