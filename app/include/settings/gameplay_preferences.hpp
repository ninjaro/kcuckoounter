#ifndef KCUCKOOUNTER_SETTINGS_GAMEPLAY_PREFERENCES_HPP
#define KCUCKOOUNTER_SETTINGS_GAMEPLAY_PREFERENCES_HPP

#include "table/gameplay_types.hpp"

#include <QString>
#include <optional>

class QSettings;

// Global fresh-launch defaults only: no decks, live state or presentation.
struct gameplay_preferences {
    static constexpr int schema_version = 2;
    std::size_t slot_count = 4;
    gameplay::session_configuration configuration;
    int pickup_interval_ms = 300;

    bool operator==(const gameplay_preferences&) const = default;
};

class gameplay_preferences_service {
public:
    // Actual current policy, never serialized. Recommendation bounds absent
    // defaults only; saved choices are retained up to the technical cap.
    gameplay_preferences_service(
        QSettings& settings, gameplay::slot_limits limits
    );
    // Absent storage returns fresh defaults; invalid/future storage blocks
    // writes. Reads never migrate or normalize legacy application settings.
    [[nodiscard]] std::optional<gameplay_preferences>
    load(QString* error = nullptr) const;
    [[nodiscard]] bool
    save(const gameplay_preferences& preferences, QString* error = nullptr);

private:
    QSettings& settings;
    gameplay::slot_limits limits;
};

#endif // KCUCKOOUNTER_SETTINGS_GAMEPLAY_PREFERENCES_HPP
