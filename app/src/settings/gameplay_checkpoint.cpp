#include "settings/gameplay_checkpoint.hpp"

#include "settings/gameplay_codec.hpp"

#include <QMetaType>
#include <QSettings>
#include <QVariant>

namespace {
const QString checkpoint_key = QStringLiteral("gameplay_checkpoint/document");
} // namespace

gameplay_checkpoint_service::gameplay_checkpoint_service(
    QSettings& settings_value, gameplay::slot_limits bounds,
    const gameplay::generation_policy& capacity
)
    : settings(settings_value)
    , limits(bounds)
    , resources(capacity) { }

std::optional<gameplay_checkpoint>
gameplay_checkpoint_service::load(QString* error) const {
    if (error)
        error->clear();
    settings.sync();
    if (settings.status() != QSettings::NoError
        || !gameplay_storage::valid_limits(limits) || !resources.valid()) {
        if (error)
            *error = QStringLiteral(
                "Could not read gameplay recovery. Stored data has not been "
                "changed."
            );
        return std::nullopt;
    }
    if (!settings.contains(checkpoint_key))
        return std::nullopt;
    const auto stored = settings.value(checkpoint_key);
    auto result = stored.metaType().id() == QMetaType::QByteArray
        ? decode_gameplay_checkpoint(stored.toByteArray(), limits, resources)
        : std::nullopt;
    if (!result && error)
        *error = QStringLiteral(
            "Gameplay recovery is damaged, unsupported or exceeds the current "
            "limits. Stored data has not been changed."
        );
    return result;
}

bool gameplay_checkpoint_service::save(
    const gameplay_checkpoint& checkpoint, QString* error
) {
    QString diagnostic;
    (void)load(&diagnostic);
    if (!diagnostic.isEmpty()) {
        if (error)
            *error = diagnostic;
        return false;
    }
    const auto bytes
        = encode_gameplay_checkpoint(checkpoint, limits, resources);
    if (!bytes) {
        if (error)
            *error = QStringLiteral(
                "Gameplay recovery does not satisfy the checkpoint format and "
                "resource limits."
            );
        return false;
    }
    return write(*bytes, error);
}

bool gameplay_checkpoint_service::clear(QString* error) {
    QString diagnostic;
    const auto current = load(&diagnostic);
    if (error)
        *error = diagnostic;
    if (!diagnostic.isEmpty())
        return false;
    return !current || write({}, error);
}

bool gameplay_checkpoint_service::write(const QVariant& value, QString* error) {
    if (error)
        error->clear();
    const auto previous = settings.value(checkpoint_key);
    if (value.isValid())
        settings.setValue(checkpoint_key, value);
    else
        settings.remove(checkpoint_key);
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;
    if (previous.isValid())
        settings.setValue(checkpoint_key, previous);
    else
        settings.remove(checkpoint_key);
    if (error)
        *error = QStringLiteral(
            "Could not write gameplay recovery. Check that settings storage is "
            "writable."
        );
    return false;
}
