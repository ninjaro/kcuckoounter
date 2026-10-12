#include "settings/gameplay_progress.hpp"

#include "settings/gameplay_codec.hpp"
#include "settings/gameplay_progress_codec.hpp"

#include <QMetaType>
#include <QSettings>
#include <QVariant>
#include <limits>
#include <utility>

namespace {
const QString progress_key = QStringLiteral("gameplay_progress/document");
} // namespace

gameplay_progress_service::gameplay_progress_service(
    QSettings& settings_value, gameplay::slot_limits bounds
)
    : settings(settings_value)
    , limits(bounds) { }

std::optional<gameplay_progress>
gameplay_progress_service::load(QString* error) const {
    if (error)
        error->clear();
    settings.sync();
    if (settings.status() != QSettings::NoError
        || !gameplay_storage::valid_limits(limits)) {
        if (error)
            *error = QStringLiteral(
                "Could not read gameplay progress. Stored data has not been "
                "changed."
            );
        return std::nullopt;
    }
    if (!settings.contains(progress_key))
        return std::nullopt;
    const auto stored = settings.value(progress_key);
    auto progress = stored.metaType().id() == QMetaType::QByteArray
        ? decode_gameplay_progress(stored.toByteArray(), limits)
        : std::nullopt;
    if (!progress && error)
        *error = QStringLiteral(
            "Gameplay progress is damaged, unsupported or exceeds current "
            "limits. Stored data has not been changed."
        );
    return progress;
}

bool gameplay_progress_service::record(
    const gameplay_result_record& record, QString* error
) {
    QString diagnostic;
    auto current = load(&diagnostic);
    if (error)
        *error = diagnostic;
    if (!diagnostic.isEmpty())
        return false;
    auto progress = current ? std::move(*current) : gameplay_progress {};
    if (record.sequence <= progress.completed_sessions) {
        for (const auto& entry : progress.recent_results)
            if (entry == record)
                return true;
        if (error)
            *error = QStringLiteral(
                "Gameplay result retry conflicts with history or is no longer "
                "retained."
            );
        return false;
    }
    if (!progress_storage::valid_record(record, limits)) {
        if (error)
            *error = QStringLiteral(
                "Gameplay result does not satisfy the progress format and "
                "resource limits."
            );
        return false;
    }
    if (progress.completed_sessions == std::numeric_limits<std::uint64_t>::max()
        || record.sequence != progress.completed_sessions + 1
        || !progress_storage::accumulate(
            progress.challenge, progress.timing, record.result.challenge,
            record.result.timing
        )) {
        if (error)
            *error = QStringLiteral(
                "Gameplay result sequence or lifetime counters exceed the "
                "admitted progress range."
            );
        return false;
    }
    ++progress.completed_sessions;
    progress.recent_results.insert(progress.recent_results.begin(), record);
    if (progress.recent_results.size()
        > gameplay_progress::maximum_recent_results)
        progress.recent_results.resize(
            gameplay_progress::maximum_recent_results
        );
    const auto bytes = encode_gameplay_progress(progress, limits);
    if (!bytes) {
        if (error)
            *error = QStringLiteral(
                "Gameplay result does not satisfy the progress format and "
                "resource limits."
            );
        return false;
    }
    const auto previous = settings.value(progress_key);
    settings.setValue(progress_key, *bytes);
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;
    if (previous.isValid())
        settings.setValue(progress_key, previous);
    else
        settings.remove(progress_key);
    if (error)
        *error = QStringLiteral(
            "Could not write gameplay progress. Check that settings storage is "
            "writable."
        );
    return false;
}
