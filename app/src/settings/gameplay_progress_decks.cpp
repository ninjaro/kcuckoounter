#include "settings/gameplay_progress_codec.hpp"

#include "settings/gameplay_codec.hpp"

#include <QJsonArray>
#include <limits>
#include <utility>

namespace progress_storage {

using namespace gameplay_storage;

QJsonObject schedule(const gameplay::quiz_schedule_snapshot& value) {
    QJsonArray targets;
    for (const auto target : value.targets)
        targets.append(number(target));
    return {
        { QStringLiteral("targets"), targets },
        { QStringLiteral("next_target"), number(value.next_target) },
        { QStringLiteral("continuation"), recipe(value.continuation) },
    };
}

std::optional<gameplay::quiz_schedule_snapshot>
read_schedule(const QJsonValue& value, std::size_t& capacity) {
    if (!value.isObject())
        return std::nullopt;
    const auto object = value.toObject();
    const auto targets = object.value(QStringLiteral("targets"));
    const auto cursor = read_size(object.value(QStringLiteral("next_target")));
    if (object.size() != 3 || !targets.isArray() || !cursor)
        return std::nullopt;
    const auto entries = targets.toArray();
    const auto count = static_cast<std::size_t>(entries.size());
    if (count > capacity)
        return std::nullopt;
    capacity -= count;
    gameplay::quiz_schedule_snapshot parsed;
    parsed.next_target = *cursor;
    if (!read_recipe(
            object.value(QStringLiteral("continuation")), parsed.continuation
        ))
        return std::nullopt;
    parsed.targets.reserve(count);
    for (const auto& entry : entries) {
        const auto target = read_unsigned(entry);
        if (!target)
            return std::nullopt;
        parsed.targets.push_back(*target);
    }
    return parsed;
}

QJsonObject deck(const gameplay::deck_result& value) {
    const auto& config = value.configuration;
    const auto& stats = value.statistics;
    auto counters = statistics(
        { stats.dealt_normal_cards, stats.verified_cards, stats.errors,
          stats.skips }
    );
    counters.insert(
        QStringLiteral("dealing_time_ms"), number(stats.dealing_time_ms)
    );
    counters.insert(
        QStringLiteral("answer_time_ms"), number(stats.answer_time_ms)
    );
    QJsonArray weights;
    for (const auto weight : value.rank_weights)
        weights.append(weight);
    return {
        { QStringLiteral("id"), number(value.id.value) },
        { QStringLiteral("slot"), number(value.slot.value) },
        { QStringLiteral("configuration"),
          QJsonObject {
              { QStringLiteral("strategy"),
                QString::fromStdString(config.strategy_slug) },
              { QStringLiteral("count"), number(config.deck_count) },
              { QStringLiteral("infinite"), config.infinite },
              { QStringLiteral("training"), config.training },
          } },
        { QStringLiteral("status"),
          value.status == gameplay::deck_status::active
              ? QStringLiteral("active")
              : value.status == gameplay::deck_status::completed
              ? QStringLiteral("completed")
              : QStringLiteral("failed") },
        { QStringLiteral("rank_weights"), weights },
        { QStringLiteral("initial_count"),
          signed_number(value.initial_running_count) },
        { QStringLiteral("final_count"),
          signed_number(value.final_running_count) },
        { QStringLiteral("dealt_physical_cards"),
          number(value.dealt_physical_cards) },
        { QStringLiteral("unverified_normal_cards"),
          number(value.unverified_normal_cards) },
        { QStringLiteral("statistics"), counters },
        { QStringLiteral("schedule"), schedule(value.schedule) },
        { QStringLiteral("infinite_position"),
          value.infinite_position
              ? QJsonValue(
                    QJsonObject {
                        { QStringLiteral("seed"),
                          number(value.infinite_position->seed) },
                        { QStringLiteral("chunk"),
                          number(value.infinite_position->chunk) },
                        { QStringLiteral("decks_per_chunk"),
                          number(value.infinite_position->decks_per_chunk) },
                    }
                )
              : QJsonValue::Null },
    };
}

std::optional<gameplay::deck_result>
read_deck(const QJsonValue& value, std::size_t& capacity) {
    if (!value.isObject())
        return std::nullopt;
    const auto object = value.toObject();
    const auto config_value = object.value(QStringLiteral("configuration"));
    const auto stats_value = object.value(QStringLiteral("statistics"));
    const auto weights_value = object.value(QStringLiteral("rank_weights"));
    const auto status_value = object.value(QStringLiteral("status"));
    if (object.size() != 12 || !config_value.isObject()
        || !stats_value.isObject() || !weights_value.isArray()
        || weights_value.toArray().size() != 13 || !status_value.isString())
        return std::nullopt;
    const auto config = config_value.toObject();
    const auto slug = config.value(QStringLiteral("strategy"));
    const auto count = read_size(config.value(QStringLiteral("count")));
    if (config.size() != 4 || !slug.isString() || slug.toString().size() > 128
        || !valid_slug(slug.toString().toStdString()) || !count
        || !config.value(QStringLiteral("infinite")).isBool()
        || !config.value(QStringLiteral("training")).isBool())
        return std::nullopt;
    const auto id = read_size(object.value(QStringLiteral("id")));
    const auto slot = read_size(object.value(QStringLiteral("slot")));
    const auto initial
        = read_signed(object.value(QStringLiteral("initial_count")));
    const auto final = read_signed(object.value(QStringLiteral("final_count")));
    const auto physical
        = read_unsigned(object.value(QStringLiteral("dealt_physical_cards")));
    const auto unverified = read_unsigned(
        object.value(QStringLiteral("unverified_normal_cards"))
    );
    auto stats = stats_value.toObject();
    const auto dealing
        = read_unsigned(stats.take(QStringLiteral("dealing_time_ms")));
    const auto answer
        = read_unsigned(stats.take(QStringLiteral("answer_time_ms")));
    const auto counters = read_statistics(stats);
    auto targets
        = read_schedule(object.value(QStringLiteral("schedule")), capacity);
    if (!id || !slot || !initial || !final || !physical || !unverified
        || !dealing || !answer || !counters || !targets)
        return std::nullopt;
    gameplay::deck_result parsed;
    parsed.id = { *id };
    parsed.slot = { *slot };
    parsed.configuration
        = { slug.toString().toStdString(), *count,
            config.value(QStringLiteral("infinite")).toBool(),
            config.value(QStringLiteral("training")).toBool() };
    const auto status = status_value.toString();
    if (status == QStringLiteral("active"))
        parsed.status = gameplay::deck_status::active;
    else if (status == QStringLiteral("completed"))
        parsed.status = gameplay::deck_status::completed;
    else if (status == QStringLiteral("failed"))
        parsed.status = gameplay::deck_status::failed;
    else
        return std::nullopt;
    const auto weights = weights_value.toArray();
    for (std::size_t i = 0; i < parsed.rank_weights.size(); ++i) {
        const auto weight = read_integer(
            weights.at(static_cast<qsizetype>(i)),
            std::numeric_limits<int>::min(), std::numeric_limits<int>::max()
        );
        if (!weight)
            return std::nullopt;
        parsed.rank_weights[i] = *weight;
    }
    parsed.initial_running_count = *initial;
    parsed.final_running_count = *final;
    parsed.dealt_physical_cards = *physical;
    parsed.unverified_normal_cards = *unverified;
    parsed.statistics = { counters->dealt_normal_cards,
                          counters->verified_cards,
                          counters->errors,
                          counters->skips,
                          *dealing,
                          *answer };
    parsed.schedule = std::move(*targets);
    const auto infinite_value
        = object.value(QStringLiteral("infinite_position"));
    if (!infinite_value.isNull()) {
        if (!infinite_value.isObject())
            return std::nullopt;
        const auto infinite = infinite_value.toObject();
        const auto seed = read_unsigned(infinite.value(QStringLiteral("seed")));
        const auto chunk
            = read_unsigned(infinite.value(QStringLiteral("chunk")));
        const auto quota
            = read_size(infinite.value(QStringLiteral("decks_per_chunk")));
        if (infinite.size() != 3 || !seed || !chunk || !quota)
            return std::nullopt;
        parsed.infinite_position
            = gameplay::infinite_stream_position { *seed, *chunk, *quota };
    }
    return parsed;
}

} // namespace progress_storage
