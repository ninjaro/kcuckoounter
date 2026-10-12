#ifndef KCUCKOOUNTER_SETTINGS_GAMEPLAY_RECOVERY_HPP
#define KCUCKOOUNTER_SETTINGS_GAMEPLAY_RECOVERY_HPP

#include "settings/gameplay_checkpoint.hpp"
#include "settings/gameplay_progress.hpp"

#include <variant>

struct gameplay_active_record {
    std::uint64_t sequence = 0;
    gameplay_checkpoint checkpoint;
    bool operator==(const gameplay_active_record&) const = default;
};

/// One identified active session OR one exact completion awaiting admission
/// and cleanup. A pending result can never be restored as a playable session.
using gameplay_recovery
    = std::variant<gameplay_active_record, gameplay_result_record>;

class gameplay_recovery_service {
public:
    static constexpr int schema_version = 1;
    static constexpr qsizetype maximum_document_bytes = 8 * 1024 * 1024;
    gameplay_recovery_service(
        QSettings& settings, gameplay::slot_limits limits,
        const gameplay::generation_policy& resources = {}
    );
    /// Absent/no error is distinct from unreadable/conflicting data. Old v2
    /// standalone checkpoints stay untouched and block automatic identity use.
    [[nodiscard]] std::optional<gameplay_recovery>
    load(QString* error = nullptr) const;
    /// Reserve completed-count + 1 only when recovery is absent. The active
    /// checkpoint and identity become durable together; no progress is added.
    [[nodiscard]] std::optional<std::uint64_t>
    begin(const gameplay_checkpoint& checkpoint, QString* error = nullptr);
    /// Update only the same active reservation, retaining immutable context
    /// and monotonic progress. Mutable speed, input, mapping and hints survive.
    [[nodiscard]] bool save(
        std::uint64_t sequence, const gameplay_checkpoint& checkpoint,
        QString* error = nullptr
    );
    /// Stage exact terminal metadata, admit progress, then remove matching
    /// pending recovery. Failed cleanup may already have admitted progress;
    /// retry the same record. Owner-serialized, not a cross-process
    /// transaction.
    [[nodiscard]] bool
    complete(const gameplay_result_record& record, QString* error = nullptr);

private:
    [[nodiscard]] std::optional<gameplay_progress>
    read_progress(QString* error) const;
    [[nodiscard]] bool write(const QVariant& value, QString* error);
    QSettings& settings;
    gameplay::slot_limits limits;
    gameplay::generation_policy resources;
};

[[nodiscard]] std::optional<QByteArray> encode_gameplay_recovery(
    const gameplay_recovery& recovery, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources = {}
);
[[nodiscard]] std::optional<gameplay_recovery> decode_gameplay_recovery(
    const QByteArray& bytes, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources = {}
);

#endif // KCUCKOOUNTER_SETTINGS_GAMEPLAY_RECOVERY_HPP
