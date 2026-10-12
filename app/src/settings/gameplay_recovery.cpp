#include "settings/gameplay_recovery_codec.hpp"

#include "settings/gameplay_codec.hpp"

#include <QMetaType>
#include <QSettings>
#include <QVariant>
#include <utility>

gameplay_recovery_service::gameplay_recovery_service(
    QSettings& settings_value, gameplay::slot_limits bounds,
    const gameplay::generation_policy& capacity
)
    : settings(settings_value)
    , limits(bounds)
    , resources(capacity) { }

std::optional<gameplay_progress>
gameplay_recovery_service::read_progress(QString* error) const {
    QString diagnostic;
    auto progress
        = gameplay_progress_service(settings, limits).load(&diagnostic);
    if (error)
        *error = diagnostic;
    if (!diagnostic.isEmpty())
        return std::nullopt;
    return progress ? std::move(*progress) : gameplay_progress {};
}

std::optional<gameplay_recovery>
gameplay_recovery_service::load(QString* error) const {
    if (error)
        error->clear();
    settings.sync();
    if (settings.status() != QSettings::NoError || !resources.valid()
        || !gameplay_storage::valid_limits(limits)) {
        (void)recovery_storage::fail(
            error,
            QStringLiteral(
                "Could not read identified gameplay recovery. Stored data has "
                "not been changed."
            )
        );
        return std::nullopt;
    }
    if (settings.contains(QStringLiteral("gameplay_checkpoint/document"))) {
        (void)recovery_storage::fail(
            error,
            QStringLiteral(
                "An unidentified standalone gameplay checkpoint requires "
                "explicit handling before identified recovery can be used. Its "
                "stored data has not been changed."
            )
        );
        return std::nullopt;
    }
    const auto progress = read_progress(error);
    if (!progress)
        return std::nullopt;
    if (!settings.contains(recovery_storage::key))
        return std::nullopt;
    const auto stored = settings.value(recovery_storage::key);
    auto recovery = stored.metaType().id() == QMetaType::QByteArray
        ? decode_gameplay_recovery(stored.toByteArray(), limits, resources)
        : std::nullopt;
    if (!recovery || !recovery_storage::admissible(*recovery, *progress)) {
        (void)recovery_storage::fail(
            error,
            QStringLiteral(
                "Gameplay recovery is damaged, unsupported, over capacity or "
                "conflicts with progress. Stored data has not been changed."
            )
        );
        return std::nullopt;
    }
    return recovery;
}

bool gameplay_recovery_service::write(const QVariant& value, QString* error) {
    if (error)
        error->clear();
    const auto previous = settings.value(recovery_storage::key);
    if (value.isValid())
        settings.setValue(recovery_storage::key, value);
    else
        settings.remove(recovery_storage::key);
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;
    if (previous.isValid())
        settings.setValue(recovery_storage::key, previous);
    else
        settings.remove(recovery_storage::key);
    return recovery_storage::fail(
        error,
        QStringLiteral(
            "Could not write gameplay recovery. Keep the same session identity "
            "and completion metadata for retry."
        )
    );
}
