#ifndef KCUCKOOUNTER_SETTINGS_PREFERENCES_HPP
#define KCUCKOOUNTER_SETTINGS_PREFERENCES_HPP

#include "settings/theme_palette.hpp"

#include <QByteArray>
#include <QString>
#include <optional>

class QSettings;
struct strategy_catalog;

enum class card_orientation_mode { automatic, vertical, horizontal };

enum class slot_frame_style { classic, thin };
enum class quiz_answer_style { numeric, chips };
enum class quiz_feedback_style { classic, stamp };
enum class slot_action_style { classic, rail, pills };
enum class slot_settings_style { classic, card, drawer, sill };
enum class desktop_toolbar_style { classic, compact };
enum class desktop_hud_style { classic, instruments };
enum class desktop_ui_preset { classic, quiet };

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

    [[nodiscard]] slot_frame_style frame() const;
    [[nodiscard]] bool show_speed_readout() const;
    [[nodiscard]] quiz_answer_style answer() const;
    [[nodiscard]] quiz_feedback_style feedback() const;
    [[nodiscard]] slot_action_style actions() const;
    [[nodiscard]] slot_settings_style settings_surface() const;
    [[nodiscard]] desktop_toolbar_style toolbar() const;
    [[nodiscard]] desktop_hud_style hud() const;
    void reset_overrides();
    bool operator==(const desktop_ui_preferences&) const = default;
};

[[nodiscard]] desktop_ui_preferences
load_desktop_ui_preferences(QSettings& settings);
[[nodiscard]] bool save_desktop_ui_preferences(
    QSettings& settings, const desktop_ui_preferences& preferences
);
[[nodiscard]] desktop_ui_preferences load_desktop_ui_preferences();
[[nodiscard]] bool
save_desktop_ui_preferences(const desktop_ui_preferences& preferences);

struct trainer_preferences {
    static constexpr int minimum_slot_count = 1;
    static constexpr int maximum_slot_count = 16;
    static constexpr int minimum_pickup_interval_ms = 100;
    static constexpr int maximum_pickup_interval_ms = 1000;

    int slot_count = 4;
    int quiz_type = 0;
    bool wait_for_answers = false;
    bool allow_skipping = true;
    int dealing_mode = 0;
    int pickup_interval_ms = 300;
    theme_palette_id palette = theme_palette_id::green;
    card_orientation_mode card_orientation = card_orientation_mode::automatic;
    QString preferred_strategy_slug;
    int preferred_strategy_id = 0;

    bool operator==(const trainer_preferences&) const = default;
};

struct desktop_shell_state {
    // Qt validates its opaque saveState()/restoreState() payload with this
    // value. It is current UI-state identity, not schema negotiation.
    static constexpr int qt_main_window_state_version = 1;

    QByteArray geometry;
    QByteArray main_window_state;

    bool operator==(const desktop_shell_state&) const = default;
};

class preferences_service {
public:
    explicit preferences_service(QSettings& settings);

    trainer_preferences load(const strategy_catalog& catalog);
    void save(
        const trainer_preferences& preferences, const strategy_catalog& catalog
    );

    static trainer_preferences defaults(const strategy_catalog& catalog);

private:
    QSettings& settings;
};

class desktop_shell_state_service {
public:
    explicit desktop_shell_state_service(QSettings& settings);

    desktop_shell_state load() const;
    void save(const desktop_shell_state& state);

private:
    QSettings& settings;
};

trainer_preferences load_trainer_preferences();
void save_trainer_preferences(const trainer_preferences& preferences);
desktop_shell_state load_desktop_shell_state();
void save_desktop_shell_state(const desktop_shell_state& state);

#endif // KCUCKOOUNTER_SETTINGS_PREFERENCES_HPP
