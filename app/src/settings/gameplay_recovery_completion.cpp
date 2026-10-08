#include "settings/gameplay_recovery_codec.hpp"

#include "settings/gameplay_progress_codec.hpp"

#include <QVariant>

bool gameplay_recovery_service::complete(
    const gameplay_result_record& record, QString* error
) {
    QString diagnostic;
    auto current = load(&diagnostic);
    if (error)
        *error = diagnostic;
    if (!diagnostic.isEmpty())
        return false;
    const auto progress = read_progress(error);
    if (!progress)
        return false;
    if (record.sequence <= progress->completed_sessions) {
        if (!recovery_storage::contains(*progress, record))
            return recovery_storage::fail(
                error,
                QStringLiteral(
                    "Completion conflicts with admitted history or is no "
                    "longer retained."
                )
            );
        const auto* pending = current
            ? std::get_if<gameplay_result_record>(&*current)
            : nullptr;
        // An old callback must never retire another session's reservation.
        return !pending || *pending != record || write({}, error);
    }
    if (!current || !progress_storage::valid_record(record, limits))
        return recovery_storage::fail(
            error,
            QStringLiteral(
                "Completion requires a valid reserved session and exact result "
                "metadata."
            )
        );
    if (const auto* active = std::get_if<gameplay_active_record>(&*current)) {
        if (active->sequence != record.sequence
            || !recovery_storage::continues(active->checkpoint, record.result))
            return recovery_storage::fail(
                error,
                QStringLiteral(
                    "Completion does not continue the current active "
                    "reservation."
                )
            );
        const auto bytes = encode_gameplay_recovery(record, limits, resources);
        if (!bytes)
            return recovery_storage::fail(
                error,
                QStringLiteral(
                    "Completion exceeds the recovery document limits."
                )
            );
        if (!write(*bytes, error))
            return false;
    } else if (std::get<gameplay_result_record>(*current) != record) {
        return recovery_storage::fail(
            error,
            QStringLiteral(
                "A different completion is already pending. Retry its exact "
                "sequence, timestamp and result."
            )
        );
    }
    if (!gameplay_progress_service(settings, limits).record(record, error))
        return false; // The exact pending record is already durable.
    // Re-read before cleanup; never erase a replaced, future or conflicting
    // key.
    current = load(error);
    const auto* pending
        = current ? std::get_if<gameplay_result_record>(&*current) : nullptr;
    if (!pending || *pending != record)
        return recovery_storage::fail(
            error,
            QStringLiteral(
                "Progress was admitted, but matching pending recovery could "
                "not be verified for cleanup."
            )
        );
    return write({}, error);
}
