#include "settings/gameplay_progress_codec.hpp"

#include "settings/gameplay_codec.hpp"
#include "table/gameplay_result.hpp"

#include <QJsonArray>
#include <utility>

namespace progress_storage {

using namespace gameplay_storage;

QJsonObject result(const gameplay::session_result& value) {
    QJsonArray decks;
    for (const auto& entry : value.decks)
        decks.append(deck(entry));
    return {
        { QStringLiteral("reason"),
          value.reason == gameplay::session_end_reason::manual_finish
              ? QStringLiteral("manual_finish")
              : value.reason == gameplay::session_end_reason::lives_exhausted
              ? QStringLiteral("lives_exhausted")
              : QStringLiteral("no_live_decks") },
        { QStringLiteral("configuration"),
          encode_configuration(value.configuration) },
        { QStringLiteral("challenge"), statistics(value.challenge) },
        { QStringLiteral("timing"),
          QJsonObject {
              { QStringLiteral("play_time_ms"),
                number(value.timing.play_time_ms) },
              { QStringLiteral("quiz_time_ms"),
                number(value.timing.quiz_time_ms) },
          } },
        { QStringLiteral("remaining_lives"), value.remaining_lives },
        { QStringLiteral("active_decks"), number(value.active_decks) },
        { QStringLiteral("completed_decks"), number(value.completed_decks) },
        { QStringLiteral("failed_decks"), number(value.failed_decks) },
        { QStringLiteral("cadence_revision"),
          static_cast<int>(value.cadence_revision) },
        { QStringLiteral("table_schedule"), schedule(value.table_schedule) },
        { QStringLiteral("random_dealing"),
          QJsonObject {
              { QStringLiteral("seed"), number(value.random_dealing.seed) },
              { QStringLiteral("next_step"),
                number(value.random_dealing.next_step) },
          } },
        { QStringLiteral("decks"), decks },
    };
}

std::optional<gameplay::session_result>
read_result(const QJsonValue& value, gameplay::slot_limits limits) {
    if (!value.isObject())
        return std::nullopt;
    const auto object = value.toObject();
    const auto entries_value = object.value(QStringLiteral("decks"));
    const auto config_value = object.value(QStringLiteral("configuration"));
    const auto timing_value = object.value(QStringLiteral("timing"));
    const auto random_value = object.value(QStringLiteral("random_dealing"));
    const auto reason_value = object.value(QStringLiteral("reason"));
    if (object.size() != 12 || !entries_value.isArray()
        || !config_value.isObject() || !timing_value.isObject()
        || !random_value.isObject() || !reason_value.isString())
        return std::nullopt;
    const auto entries = entries_value.toArray();
    const auto count = static_cast<std::size_t>(entries.size());
    if (count == 0 || count > limits.technical_cap)
        return std::nullopt;
    const auto config = config_value.toObject();
    const auto configuration
        = read_configuration(config, count, { count, limits.technical_cap });
    const auto lives
        = read_integer(object.value(QStringLiteral("remaining_lives")), 0, 12);
    const auto revision
        = read_integer(object.value(QStringLiteral("cadence_revision")), 1, 1);
    const auto active = read_size(object.value(QStringLiteral("active_decks")));
    const auto completed
        = read_size(object.value(QStringLiteral("completed_decks")));
    const auto failed = read_size(object.value(QStringLiteral("failed_decks")));
    const auto counters
        = read_statistics(object.value(QStringLiteral("challenge")));
    const auto timing = timing_value.toObject();
    const auto play
        = read_unsigned(timing.value(QStringLiteral("play_time_ms")));
    const auto quiz
        = read_unsigned(timing.value(QStringLiteral("quiz_time_ms")));
    const auto random = random_value.toObject();
    const auto seed = read_unsigned(random.value(QStringLiteral("seed")));
    const auto step = read_unsigned(random.value(QStringLiteral("next_step")));
    std::size_t capacity = gameplay_progress_service::maximum_result_targets;
    auto targets = read_schedule(
        object.value(QStringLiteral("table_schedule")), capacity
    );
    if (config.size() != 9 || timing.size() != 2 || random.size() != 2
        || !configuration || !lives || !revision || !active || !completed
        || !failed || !counters || !play || !quiz || !seed || !step || !targets)
        return std::nullopt;
    gameplay::session_result parsed;
    const auto reason = reason_value.toString();
    if (reason == QStringLiteral("manual_finish"))
        parsed.reason = gameplay::session_end_reason::manual_finish;
    else if (reason == QStringLiteral("lives_exhausted"))
        parsed.reason = gameplay::session_end_reason::lives_exhausted;
    else if (reason == QStringLiteral("no_live_decks"))
        parsed.reason = gameplay::session_end_reason::no_live_decks;
    else
        return std::nullopt;
    parsed.configuration = *configuration;
    parsed.challenge = *counters;
    parsed.timing = { *play, *quiz };
    parsed.remaining_lives = *lives;
    parsed.active_decks = *active;
    parsed.completed_decks = *completed;
    parsed.failed_decks = *failed;
    parsed.cadence_revision = static_cast<std::uint32_t>(*revision);
    parsed.table_schedule = std::move(*targets);
    parsed.random_dealing = { *seed, *step };
    parsed.decks.reserve(count);
    for (const auto& entry : entries) {
        auto owner = read_deck(entry, capacity);
        if (!owner)
            return std::nullopt;
        parsed.decks.push_back(std::move(*owner));
    }
    return gameplay::valid_result(
               parsed, limits, gameplay_progress_service::maximum_result_targets
           )
        ? std::make_optional(std::move(parsed))
        : std::nullopt;
}

} // namespace progress_storage
