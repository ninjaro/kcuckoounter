#include "settings/preferences.hpp"

#include <QMetaType>
#include <QSettings>
#include <QVariant>

#include <algorithm>
#include <optional>

namespace {

const QString preferences_group = QStringLiteral("trainer_preferences");
const QString desktop_shell_group = QStringLiteral("desktop_shell");
constexpr qsizetype maximum_shell_state_bytes = 1024 * 1024;

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

QByteArray bounded_byte_array(const QVariant& value) {
    if (value.metaType().id() != QMetaType::QByteArray) {
        return {};
    }
    QByteArray bytes = value.toByteArray();
    if (bytes.size() > maximum_shell_state_bytes) {
        return {};
    }
    return bytes;
}

table_appearance_preferences read_current_preferences(QSettings& settings) {
    table_appearance_preferences result;
    result.palette
        = palette_from_key(
              settings.value(QStringLiteral("appearance/palette")).toString()
        )
              .value_or(result.palette);
    result.card_orientation
        = card_orientation_from_key(
              settings.value(QStringLiteral("appearance/card_orientation"))
                  .toString()
        )
              .value_or(result.card_orientation);
    return result;
}

} // namespace

preferences_service::preferences_service(QSettings& settings_value)
    : settings(settings_value) { }

table_appearance_preferences preferences_service::load() const {
    settings.beginGroup(preferences_group);
    const table_appearance_preferences result
        = read_current_preferences(settings);
    settings.endGroup();
    return result;
}

void preferences_service::save_appearance(
    const table_appearance_preferences& before,
    const table_appearance_preferences& after
) {
    settings.beginGroup(preferences_group);
    if (before.palette != after.palette)
        settings.setValue(
            QStringLiteral("appearance/palette"), palette_key(after.palette)
        );
    if (before.card_orientation != after.card_orientation
        && valid_card_orientation(after.card_orientation))
        settings.setValue(
            QStringLiteral("appearance/card_orientation"),
            card_orientation_key(after.card_orientation)
        );
    settings.endGroup();
    settings.sync();
}

void save_table_appearance_preferences(
    const table_appearance_preferences& before,
    const table_appearance_preferences& after
) {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    preferences_service(settings).save_appearance(before, after);
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

table_appearance_preferences load_table_appearance_preferences() {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    preferences_service service(settings);
    return service.load();
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
        preset == desktop_ui_preset::felt        ? slot_frame_style::piping
            : preset == desktop_ui_preset::quiet ? slot_frame_style::thin
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
    return feedback_override.value_or(
        preset == desktop_ui_preset::felt ? quiz_feedback_style::stamp
                                          : quiz_feedback_style::classic
    );
}

slot_action_style desktop_ui_preferences::actions() const {
    return actions_override.value_or(
        preset == desktop_ui_preset::felt ? slot_action_style::pills
                                          : slot_action_style::classic
    );
}

slot_settings_style desktop_ui_preferences::settings_surface() const {
    return settings_override.value_or(
        preset == desktop_ui_preset::felt ? slot_settings_style::card
                                          : slot_settings_style::classic
    );
}

desktop_toolbar_style desktop_ui_preferences::toolbar() const {
    return toolbar_override.value_or(desktop_toolbar_style::classic);
}

desktop_hud_style desktop_ui_preferences::hud() const {
    return hud_override.value_or(
        preset == desktop_ui_preset::felt ? desktop_hud_style::instruments
                                          : desktop_hud_style::classic
    );
}

desktop_chrome_style desktop_ui_preferences::chrome() const {
    return chrome_override.value_or(
        preset == desktop_ui_preset::felt ? desktop_chrome_style::felt
                                          : desktop_chrome_style::native
    );
}

void desktop_ui_preferences::reset_overrides() {
    unknown_overrides_.clear();
    frame_override.reset();
    speed_readout_override.reset();
    answer_override.reset();
    feedback_override.reset();
    actions_override.reset();
    settings_override.reset();
    toolbar_override.reset();
    hud_override.reset();
    chrome_override.reset();
}

desktop_ui_preferences desktop_ui_preferences::from_appearance(
    const shared_ui::qt::appearance_state& state
) {
    desktop_ui_preferences result;
    result.unknown_preset_ = state.preset;
    result.unknown_overrides_ = state.overrides;
    const QString preset = state.preset;
    if (preset == QStringLiteral("classic")) {
        result.unknown_preset_.reset();
    } else if (preset == QStringLiteral("quiet")) {
        result.unknown_preset_.reset();
        result.preset = desktop_ui_preset::quiet;
    } else if (preset == QStringLiteral("felt")) {
        result.unknown_preset_.reset();
        result.preset = desktop_ui_preset::felt;
    }
    const QString frame = state.overrides.value(QStringLiteral("frame"));
    if (frame == QStringLiteral("classic")) {
        result.frame_override = slot_frame_style::classic;
    } else if (frame == QStringLiteral("thin")) {
        result.frame_override = slot_frame_style::thin;
    } else if (frame == QStringLiteral("piping")) {
        result.frame_override = slot_frame_style::piping;
    }
    const QString speed
        = state.overrides.value(QStringLiteral("speed_readout"));
    if (speed == QStringLiteral("shown")) {
        result.speed_readout_override = true;
    } else if (speed == QStringLiteral("hidden")) {
        result.speed_readout_override = false;
    }
    const QString answer
        = state.overrides.value(QStringLiteral("answer_entry"));
    if (answer == QStringLiteral("numeric")) {
        result.answer_override = quiz_answer_style::numeric;
    } else if (answer == QStringLiteral("chips")) {
        result.answer_override = quiz_answer_style::chips;
    }
    const QString feedback = state.overrides.value(QStringLiteral("feedback"));
    if (feedback == QStringLiteral("classic")) {
        result.feedback_override = quiz_feedback_style::classic;
    } else if (feedback == QStringLiteral("stamp")) {
        result.feedback_override = quiz_feedback_style::stamp;
    }
    const QString actions
        = state.overrides.value(QStringLiteral("slot_actions"));
    if (actions == QStringLiteral("classic")) {
        result.actions_override = slot_action_style::classic;
    } else if (actions == QStringLiteral("rail")) {
        result.actions_override = slot_action_style::rail;
    } else if (actions == QStringLiteral("pills")) {
        result.actions_override = slot_action_style::pills;
    }
    const auto surface
        = state.overrides.value(QStringLiteral("settings_surface"));
    if (surface == QStringLiteral("classic"))
        result.settings_override = slot_settings_style::classic;
    else if (surface == QStringLiteral("card"))
        result.settings_override = slot_settings_style::card;
    else if (surface == QStringLiteral("drawer"))
        result.settings_override = slot_settings_style::drawer;
    else if (surface == QStringLiteral("sill"))
        result.settings_override = slot_settings_style::sill;
    const auto toolbar = state.overrides.value(QStringLiteral("toolbar"));
    if (toolbar == QStringLiteral("classic"))
        result.toolbar_override = desktop_toolbar_style::classic;
    else if (toolbar == QStringLiteral("compact"))
        result.toolbar_override = desktop_toolbar_style::compact;
    const auto hud = state.overrides.value(QStringLiteral("hud"));
    if (hud == QStringLiteral("classic"))
        result.hud_override = desktop_hud_style::classic;
    else if (hud == QStringLiteral("instruments"))
        result.hud_override = desktop_hud_style::instruments;
    const auto chrome = state.overrides.value(QStringLiteral("chrome"));
    if (chrome == QStringLiteral("native"))
        result.chrome_override = desktop_chrome_style::native;
    else if (chrome == QStringLiteral("felt"))
        result.chrome_override = desktop_chrome_style::felt;
    const auto recognized = [&](const QString& key, bool known) {
        if (known)
            result.unknown_overrides_.remove(key);
    };
    recognized(QStringLiteral("frame"), result.frame_override.has_value());
    recognized(
        QStringLiteral("speed_readout"),
        result.speed_readout_override.has_value()
    );
    recognized(
        QStringLiteral("answer_entry"), result.answer_override.has_value()
    );
    recognized(
        QStringLiteral("feedback"), result.feedback_override.has_value()
    );
    recognized(
        QStringLiteral("slot_actions"), result.actions_override.has_value()
    );
    recognized(
        QStringLiteral("settings_surface"), result.settings_override.has_value()
    );
    recognized(QStringLiteral("toolbar"), result.toolbar_override.has_value());
    recognized(QStringLiteral("hud"), result.hud_override.has_value());
    recognized(QStringLiteral("chrome"), result.chrome_override.has_value());
    return result;
}

shared_ui::qt::appearance_state desktop_ui_preferences::appearance() const {
    shared_ui::qt::appearance_state result { {}, unknown_overrides_ };
    result.preset = preset == desktop_ui_preset::classic && unknown_preset_
        ? *unknown_preset_
        : (preset == desktop_ui_preset::quiet      ? QStringLiteral("quiet")
               : preset == desktop_ui_preset::felt ? QStringLiteral("felt")
                                                   : QStringLiteral("classic"));
    if (frame_override) {
        const auto style = *frame_override;
        result.overrides.insert(
            QStringLiteral("frame"),
            style == slot_frame_style::thin         ? QStringLiteral("thin")
                : style == slot_frame_style::piping ? QStringLiteral("piping")
                                                    : QStringLiteral("classic")
        );
    }
    if (speed_readout_override) {
        result.overrides.insert(
            QStringLiteral("speed_readout"),
            *speed_readout_override ? QStringLiteral("shown")
                                    : QStringLiteral("hidden")
        );
    }
    if (answer_override) {
        result.overrides.insert(
            QStringLiteral("answer_entry"),
            *answer_override == quiz_answer_style::chips
                ? QStringLiteral("chips")
                : QStringLiteral("numeric")
        );
    }
    if (feedback_override) {
        result.overrides.insert(
            QStringLiteral("feedback"),
            *feedback_override == quiz_feedback_style::stamp
                ? QStringLiteral("stamp")
                : QStringLiteral("classic")
        );
    }
    if (actions_override) {
        const auto style = *actions_override;
        result.overrides.insert(
            QStringLiteral("slot_actions"),
            style == slot_action_style::rail        ? QStringLiteral("rail")
                : style == slot_action_style::pills ? QStringLiteral("pills")
                                                    : QStringLiteral("classic")
        );
    }
    if (settings_override) {
        const auto style = *settings_override;
        result.overrides.insert(
            QStringLiteral("settings_surface"),
            style == slot_settings_style::card ? QStringLiteral("card")
                : style == slot_settings_style::drawer
                ? QStringLiteral("drawer")
                : style == slot_settings_style::sill ? QStringLiteral("sill")
                                                     : QStringLiteral("classic")
        );
    }
    if (toolbar_override) {
        result.overrides.insert(
            QStringLiteral("toolbar"),
            *toolbar_override == desktop_toolbar_style::compact
                ? QStringLiteral("compact")
                : QStringLiteral("classic")
        );
    }
    if (hud_override) {
        result.overrides.insert(
            QStringLiteral("hud"),
            *hud_override == desktop_hud_style::instruments
                ? QStringLiteral("instruments")
                : QStringLiteral("classic")
        );
    }
    if (chrome_override) {
        result.overrides.insert(
            QStringLiteral("chrome"),
            *chrome_override == desktop_chrome_style::felt
                ? QStringLiteral("felt")
                : QStringLiteral("native")
        );
    }
    return result;
}

void desktop_ui_preferences::set_preset(desktop_ui_preset value) {
    preset = value;
    unknown_preset_.reset();
}

desktop_ui_preferences load_desktop_ui_preferences(QSettings& settings) {
    shared_ui::qt::appearance_state state;
    settings.beginGroup(QStringLiteral("desktop_ui"));
    state.preset
        = settings.value(QStringLiteral("preset"), QStringLiteral("classic"))
              .toString();
    settings.beginGroup(QStringLiteral("overrides"));
    for (const auto& key : settings.allKeys())
        state.overrides.insert(key, settings.value(key).toString());
    settings.endGroup();
    settings.endGroup();
    return desktop_ui_preferences::from_appearance(state);
}

bool save_desktop_ui_preferences(
    QSettings& settings, const desktop_ui_preferences& preferences
) {
    const auto state = preferences.appearance();
    settings.beginGroup(QStringLiteral("desktop_ui"));
    if (!settings.contains(QStringLiteral("preset"))
        || settings.value(QStringLiteral("preset")).toString() != state.preset)
        settings.setValue(QStringLiteral("preset"), state.preset);
    settings.beginGroup(QStringLiteral("overrides"));
    // Keep the exact existing QVariant for retained choices, including future
    // types and nested keys. This is the existing store, not a second schema.
    QHash<QString, QVariant> existing;
    for (const auto& key : settings.allKeys())
        existing.insert(key, settings.value(key));
    for (const auto& key : existing.keys())
        if (!state.overrides.contains(key))
            settings.remove(key);
    for (auto it = state.overrides.cbegin(); it != state.overrides.cend();
         ++it) {
        const QVariant value = existing.contains(it.key())
                && existing.value(it.key()).toString() == it.value()
            ? existing.value(it.key())
            : QVariant(it.value());
        if (!settings.contains(it.key()) || settings.value(it.key()) != value)
            settings.setValue(it.key(), value);
    }
    settings.endGroup();
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
