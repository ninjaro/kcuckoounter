#ifndef KCUCKOOUNTER_SETTINGS_GAMEPLAY_DRILL_HPP
#define KCUCKOOUNTER_SETTINGS_GAMEPLAY_DRILL_HPP

#include "table/gameplay_session.hpp"

#include <QString>
#include <QVector>
#include <vector>

class QSettings;
struct strategy_catalog;
struct training_drill;

struct drill_deck {
    gameplay::deck_configuration configuration;
    bool show_count = false;
    bool operator==(const drill_deck&) const = default;
};

/// Fresh setup only. Deck order is creation-ID order, not a live slot mapping.
struct gameplay_drill {
    QString name;
    gameplay::session_configuration configuration;
    int pickup_interval_ms = 300;
    std::vector<drill_deck> decks;
    bool operator==(const gameplay_drill&) const = default;
};

/// Storage preflight ignores screen recommendations, not caps or field rules.
/// Strategy availability is checked separately by supports_drill.
[[nodiscard]] bool
valid_gameplay_drill(const gameplay_drill& drill, gameplay::slot_limits limits);

class gameplay_drill_service {
public:
    static constexpr int schema_version = 2;
    static constexpr int maximum_drills = 64;
    static constexpr int maximum_name_length = 80;
    gameplay_drill_service(QSettings& settings, gameplay::slot_limits limits);
    [[nodiscard]] std::optional<QVector<gameplay_drill>>
    load(QString* error = nullptr) const;
    [[nodiscard]] bool
    save(const QVector<gameplay_drill>& drills, QString* error = nullptr);

private:
    QSettings& settings;
    gameplay::slot_limits limits;
};

[[nodiscard]] bool supports_drill(
    const gameplay_drill& drill, const strategy_catalog& catalog,
    gameplay::slot_limits limits
);
/// No cards, schedules or progress. Actual geometry may require Start consent.
[[nodiscard]] std::optional<gameplay::session> create_drill_session(
    const gameplay_drill& drill, const strategy_catalog& catalog,
    gameplay::slot_limits limits
);
[[nodiscard]] std::optional<gameplay_drill> capture_gameplay_drill(
    const gameplay::session& game, const QString& name,
    gameplay::slot_limits limits
);
/// Explicit, non-equivalent draft; no I/O. Caller must review new policies.
/// Copy v1 skip/dealing/speed/decks; N=1. Other global fields come from target.
[[nodiscard]] std::optional<gameplay_drill> draft_legacy_drill(
    const training_drill& legacy, const gameplay::session_configuration& target,
    gameplay::slot_limits limits
);

#endif // KCUCKOOUNTER_SETTINGS_GAMEPLAY_DRILL_HPP
