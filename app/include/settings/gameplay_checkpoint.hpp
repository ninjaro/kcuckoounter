#ifndef KCUCKOOUNTER_SETTINGS_GAMEPLAY_CHECKPOINT_HPP
#define KCUCKOOUNTER_SETTINGS_GAMEPLAY_CHECKPOINT_HPP

#include "table/gameplay_checkpoint.hpp"

#include <QByteArray>
#include <QString>

/// Storage envelope: exact domain state and partial host pickup cadence only.
/// No clock timestamp, geometry/history, presentation or score fields.
struct gameplay_checkpoint {
    gameplay::session_checkpoint state;
    std::uint64_t pickup_elapsed_ms = 0;
    bool operator==(const gameplay_checkpoint&) const = default;
};

inline constexpr qsizetype maximum_gameplay_checkpoint_bytes = 8 * 1024 * 1024;

[[nodiscard]] std::optional<QByteArray> encode_gameplay_checkpoint(
    const gameplay_checkpoint& checkpoint, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources = {}
);
[[nodiscard]] std::optional<gameplay_checkpoint> decode_gameplay_checkpoint(
    const QByteArray& bytes, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources = {}
);

#endif // KCUCKOOUNTER_SETTINGS_GAMEPLAY_CHECKPOINT_HPP
