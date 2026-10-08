#include "settings/gameplay_progress_codec.hpp"

#include "settings/gameplay_codec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <utility>

namespace {

using namespace gameplay_storage;

std::optional<gameplay_progress>
read_root(const QJsonObject& root, gameplay::slot_limits limits) {
    const auto history = root.value(QStringLiteral("recent_results"));
    const auto timing_value = root.value(QStringLiteral("timing"));
    const auto completed
        = read_unsigned(root.value(QStringLiteral("completed_sessions")));
    const auto counters
        = read_statistics(root.value(QStringLiteral("challenge")));
    if (root.size() != 5 || !history.isArray() || !timing_value.isObject()
        || !completed || !counters
        || !read_integer(
            root.value(QStringLiteral("version")),
            gameplay_progress_service::schema_version,
            gameplay_progress_service::schema_version
        ))
        return std::nullopt;
    const auto timing = timing_value.toObject();
    const auto play
        = read_unsigned(timing.value(QStringLiteral("play_time_ms")));
    const auto quiz
        = read_unsigned(timing.value(QStringLiteral("quiz_time_ms")));
    const auto entries = history.toArray();
    if (timing.size() != 2 || !play || !quiz
        || static_cast<std::size_t>(entries.size())
            > gameplay_progress::maximum_recent_results)
        return std::nullopt;
    gameplay_progress parsed { *completed, *counters, { *play, *quiz }, {} };
    parsed.recent_results.reserve(static_cast<std::size_t>(entries.size()));
    for (const auto& entry : entries) {
        if (!entry.isObject())
            return std::nullopt;
        const auto object = entry.toObject();
        const auto sequence
            = read_unsigned(object.value(QStringLiteral("sequence")));
        const auto timestamp
            = read_signed(object.value(QStringLiteral("completed_at_utc_ms")));
        auto result = progress_storage::read_result(
            object.value(QStringLiteral("result")), limits
        );
        if (object.size() != 3 || !sequence || !timestamp || !result)
            return std::nullopt;
        parsed.recent_results.push_back(
            { *sequence, *timestamp, std::move(*result) }
        );
    }
    return progress_storage::valid(parsed, limits)
        ? std::make_optional(std::move(parsed))
        : std::nullopt;
}

} // namespace

std::optional<QByteArray> encode_gameplay_progress(
    const gameplay_progress& progress, gameplay::slot_limits limits
) {
    if (!progress_storage::valid(progress, limits))
        return std::nullopt;
    QJsonArray history;
    for (const auto& entry : progress.recent_results)
        history.append(
            QJsonObject {
                { QStringLiteral("sequence"), number(entry.sequence) },
                { QStringLiteral("completed_at_utc_ms"),
                  signed_number(entry.completed_at_utc_ms) },
                { QStringLiteral("result"),
                  progress_storage::result(entry.result) },
            }
        );
    const QJsonObject root {
        { QStringLiteral("version"),
          gameplay_progress_service::schema_version },
        { QStringLiteral("completed_sessions"),
          number(progress.completed_sessions) },
        { QStringLiteral("challenge"), statistics(progress.challenge) },
        { QStringLiteral("timing"),
          QJsonObject {
              { QStringLiteral("play_time_ms"),
                number(progress.timing.play_time_ms) },
              { QStringLiteral("quiz_time_ms"),
                number(progress.timing.quiz_time_ms) },
          } },
        { QStringLiteral("recent_results"), history },
    };
    auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return bytes.size() <= gameplay_progress_service::maximum_document_bytes
        ? std::make_optional(std::move(bytes))
        : std::nullopt;
}

std::optional<gameplay_progress> decode_gameplay_progress(
    const QByteArray& bytes, gameplay::slot_limits limits
) {
    if (!valid_limits(limits) || bytes.isEmpty()
        || bytes.size() > gameplay_progress_service::maximum_document_bytes)
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(bytes);
    return document.isObject() ? read_root(document.object(), limits)
                               : std::nullopt;
}
