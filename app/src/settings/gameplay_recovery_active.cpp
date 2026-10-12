#include "settings/gameplay_recovery_codec.hpp"

#include <QVariant>
#include <limits>

std::optional<std::uint64_t> gameplay_recovery_service::begin(
    const gameplay_checkpoint& checkpoint, QString* error
) {
    QString diagnostic;
    const auto current = load(&diagnostic);
    if (error)
        *error = diagnostic;
    if (!diagnostic.isEmpty())
        return std::nullopt;
    if (current) {
        (void)recovery_storage::fail(
            error,
            QStringLiteral(
                "An identified session or pending completion must be recovered "
                "before another session can begin."
            )
        );
        return std::nullopt;
    }
    const auto progress = read_progress(error);
    if (!progress)
        return std::nullopt;
    if (progress->completed_sessions
        == std::numeric_limits<std::uint64_t>::max()) {
        (void)recovery_storage::fail(
            error,
            QStringLiteral(
                "Gameplay result sequence is exhausted; no identity was "
                "reserved."
            )
        );
        return std::nullopt;
    }
    const auto sequence = progress->completed_sessions + 1;
    const auto bytes = recovery_storage::encode_active(
        sequence, checkpoint, limits, resources
    );
    if (!bytes) {
        (void)recovery_storage::fail(
            error,
            QStringLiteral(
                "The active checkpoint does not satisfy identified recovery "
                "limits."
            )
        );
        return std::nullopt;
    }
    return write(*bytes, error) ? std::make_optional(sequence) : std::nullopt;
}

bool gameplay_recovery_service::save(
    std::uint64_t sequence, const gameplay_checkpoint& checkpoint,
    QString* error
) {
    QString diagnostic;
    const auto current = load(&diagnostic);
    if (error)
        *error = diagnostic;
    if (!diagnostic.isEmpty())
        return false;
    const auto* active
        = current ? std::get_if<gameplay_active_record>(&*current) : nullptr;
    if (!active || active->sequence != sequence
        || !recovery_storage::continues(active->checkpoint, checkpoint))
        return recovery_storage::fail(
            error,
            QStringLiteral(
                "The checkpoint does not continue the current active "
                "reservation. Recovery has not been changed."
            )
        );
    if (active->checkpoint == checkpoint)
        return true;
    const auto bytes = recovery_storage::encode_active(
        sequence, checkpoint, limits, resources
    );
    if (!bytes)
        return recovery_storage::fail(
            error,
            QStringLiteral(
                "The checkpoint does not satisfy identified recovery limits."
            )
        );
    // Identical Pause/background notifications need not rewrite the document.
    return write(*bytes, error);
}
