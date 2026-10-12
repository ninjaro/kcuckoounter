#ifndef KCUCKOOUNTER_SETTINGS_GAMEPLAY_CHECKPOINT_HPP
#define KCUCKOOUNTER_SETTINGS_GAMEPLAY_CHECKPOINT_HPP

#include "table/gameplay_checkpoint.hpp"

#include <QByteArray>
#include <QString>

class QSettings;
class QVariant;

/// Storage envelope: exact domain state and partial host pickup cadence only.
/// No clock timestamp, geometry/history, presentation or legacy score fields.
struct gameplay_checkpoint {
    gameplay::session_checkpoint state;
    std::uint64_t pickup_elapsed_ms = 0;
    bool operator==(const gameplay_checkpoint&) const = default;
};

class gameplay_checkpoint_service {
public:
    static constexpr int schema_version = 2;
    static constexpr qsizetype maximum_document_bytes = 8 * 1024 * 1024;
    gameplay_checkpoint_service(
        QSettings& settings, gameplay::slot_limits limits,
        const gameplay::generation_policy& resources = {}
    );
    /// Absent returns null with no error; unreadable/unsupported returns null
    /// with an error. Neither migrates nor alters legacy recovery data.
    [[nodiscard]] std::optional<gameplay_checkpoint>
    load(QString* error = nullptr) const;
    /// Existing unreadable/future records block save AND clear. Failed sync
    /// restores the previous in-memory key. These calls do not activate
    /// recovery.
    [[nodiscard]] bool
    save(const gameplay_checkpoint& checkpoint, QString* error = nullptr);
    [[nodiscard]] bool clear(QString* error = nullptr);

private:
    [[nodiscard]] bool write(const QVariant& value, QString* error);
    QSettings& settings;
    gameplay::slot_limits limits;
    gameplay::generation_policy resources;
};

[[nodiscard]] std::optional<QByteArray> encode_gameplay_checkpoint(
    const gameplay_checkpoint& checkpoint, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources = {}
);
[[nodiscard]] std::optional<gameplay_checkpoint> decode_gameplay_checkpoint(
    const QByteArray& bytes, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources = {}
);

#endif // KCUCKOOUNTER_SETTINGS_GAMEPLAY_CHECKPOINT_HPP
