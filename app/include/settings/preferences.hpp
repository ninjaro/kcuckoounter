#ifndef KCUCKOOUNTER_SETTINGS_PREFERENCES_HPP
#define KCUCKOOUNTER_SETTINGS_PREFERENCES_HPP

#include "settings/theme_palette.hpp"

#include <shared_ui/qt/appearance_model.hpp>

#include <QByteArray>
#include <QString>
#include <QVector>
#include <optional>

class QSettings;

// Appearance only; suit order matches the existing card/preview selectors:
// Clubs, Diamonds, Hearts, Spades. No trainer/drill/checkpoint field.
[[nodiscard]] int load_default_suit_preference(QSettings& settings);
[[nodiscard]] bool save_default_suit_preference(QSettings& settings, int index);
[[nodiscard]] int load_default_suit_preference();
[[nodiscard]] bool save_default_suit_preference(int index);

enum class card_orientation_mode { automatic, vertical, horizontal };

enum class slot_frame_style { classic, thin, piping };
enum class quiz_answer_style { numeric, chips };
enum class quiz_feedback_style { classic, stamp };
enum class slot_action_style { classic, rail, pills };
enum class slot_settings_style { classic, card, drawer, sill };
enum class desktop_toolbar_style { classic, compact };
enum class desktop_hud_style { classic, instruments };
// Window/dialog material around the table; never a palette or artwork choice.
enum class desktop_chrome_style { native, felt };
enum class desktop_ui_preset { classic, quiet, felt };

// Presentation only: never put session/drill or artwork settings here.
struct desktop_ui_preferences {
    desktop_ui_preset preset = desktop_ui_preset::classic;
    std::optional<slot_frame_style> frame_override;
    std::optional<bool> speed_readout_override;
    std::optional<quiz_answer_style> answer_override;
    std::optional<quiz_feedback_style> feedback_override;
    std::optional<slot_action_style> actions_override;
    std::optional<slot_settings_style> settings_override;
    std::optional<desktop_toolbar_style> toolbar_override;
    std::optional<desktop_hud_style> hud_override;
    std::optional<desktop_chrome_style> chrome_override;

    [[nodiscard]] slot_frame_style frame() const;
    [[nodiscard]] bool show_speed_readout() const;
    [[nodiscard]] quiz_answer_style answer() const;
    [[nodiscard]] quiz_feedback_style feedback() const;
    [[nodiscard]] slot_action_style actions() const;
    [[nodiscard]] slot_settings_style settings_surface() const;
    [[nodiscard]] desktop_toolbar_style toolbar() const;
    [[nodiscard]] desktop_hud_style hud() const;
    [[nodiscard]] desktop_chrome_style chrome() const;
    // Existing typed host choices are a view of current presentation IDs.
    // Unknown IDs survive until explicitly replaced/reset, in the same keys.
    [[nodiscard]] shared_ui::qt::appearance_state appearance() const;
    [[nodiscard]] static desktop_ui_preferences
    from_appearance(const shared_ui::qt::appearance_state& state);
    void set_preset(desktop_ui_preset value);
    void reset_overrides();
    bool operator==(const desktop_ui_preferences&) const = default;

private:
    std::optional<QString> unknown_preset_;
    QHash<QString, QString> unknown_overrides_;
};

[[nodiscard]] desktop_ui_preferences
load_desktop_ui_preferences(QSettings& settings);
[[nodiscard]] bool save_desktop_ui_preferences(
    QSettings& settings, const desktop_ui_preferences& preferences
);
[[nodiscard]] desktop_ui_preferences load_desktop_ui_preferences();
[[nodiscard]] bool
save_desktop_ui_preferences(const desktop_ui_preferences& preferences);

struct table_appearance_preferences {
    theme_palette_id palette = theme_palette_id::green;
    card_orientation_mode card_orientation = card_orientation_mode::automatic;
    bool operator==(const table_appearance_preferences&) const = default;
};

struct desktop_shell_state {
    QByteArray geometry;
    QByteArray main_window_state;

    bool operator==(const desktop_shell_state&) const = default;
};

class preferences_service {
public:
    explicit preferences_service(QSettings& settings);

    [[nodiscard]] table_appearance_preferences load() const;
    void save_appearance(
        const table_appearance_preferences& before,
        const table_appearance_preferences& after
    );

private:
    QSettings& settings;
};

class desktop_shell_state_service {
public:
    explicit desktop_shell_state_service(QSettings& settings);

    [[nodiscard]] desktop_shell_state load() const;
    void save(const desktop_shell_state& state);

private:
    QSettings& settings;
};

table_appearance_preferences load_table_appearance_preferences();
void save_table_appearance_preferences(
    const table_appearance_preferences& before,
    const table_appearance_preferences& after
);
desktop_shell_state load_desktop_shell_state();
void save_desktop_shell_state(const desktop_shell_state& state);

#endif // KCUCKOOUNTER_SETTINGS_PREFERENCES_HPP
