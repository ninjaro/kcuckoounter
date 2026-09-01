#ifndef KCUCKOOUNTER_SETTINGS_PREFERENCES_HPP
#define KCUCKOOUNTER_SETTINGS_PREFERENCES_HPP

#include "settings/theme_palette.hpp"

#include <QByteArray>
#include <QString>

class QSettings;
struct strategy_catalog;

enum class card_orientation_mode { automatic, vertical, horizontal };

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
