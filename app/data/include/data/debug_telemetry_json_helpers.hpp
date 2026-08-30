#ifndef KCUCKOOUNTER_DATA_DEBUG_TELEMETRY_JSON_HELPERS_HPP
#define KCUCKOOUNTER_DATA_DEBUG_TELEMETRY_JSON_HELPERS_HPP

#include <QJsonArray>
#include <QJsonObject>
#include <QSize>
#include <QStringList>

namespace debug_probe_json_helpers {

inline QJsonObject size_to_json(const QSize& size) {
    QJsonObject object;
    object.insert(QStringLiteral("width"), size.width());
    object.insert(QStringLiteral("height"), size.height());
    return object;
}

inline QJsonArray string_list_to_json_array(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.push_back(value);
    }
    return array;
}

} // namespace debug_probe_json_helpers

#endif // KCUCKOOUNTER_DATA_DEBUG_TELEMETRY_JSON_HELPERS_HPP
