#ifndef KCUCKOOUNTER_SETTINGS_GAMEPLAY_RECOVERY_CODEC_HPP
#define KCUCKOOUNTER_SETTINGS_GAMEPLAY_RECOVERY_CODEC_HPP

#include "settings/gameplay_recovery.hpp"

// Implementation-only admission and monotonic continuation checks.
namespace recovery_storage {

inline const QString key = QStringLiteral("gameplay_recovery/document");
[[nodiscard]] bool fail(QString* error, const QString& message);
[[nodiscard]] bool contains(
    const gameplay_progress& progress, const gameplay_result_record& record
);
[[nodiscard]] bool admissible(
    const gameplay_recovery& recovery, const gameplay_progress& progress
);
[[nodiscard]] bool
continues(const gameplay_checkpoint& previous, const gameplay_checkpoint& next);
[[nodiscard]] bool continues(
    const gameplay_checkpoint& previous, const gameplay::session_result& next
);
[[nodiscard]] std::optional<QByteArray> encode_active(
    std::uint64_t sequence, const gameplay_checkpoint& checkpoint,
    gameplay::slot_limits limits, const gameplay::generation_policy& resources
);

} // namespace recovery_storage

#endif // KCUCKOOUNTER_SETTINGS_GAMEPLAY_RECOVERY_CODEC_HPP
