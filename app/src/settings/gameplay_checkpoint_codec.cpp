#include "settings/gameplay_checkpoint_codec.hpp"

#include "settings/gameplay_codec.hpp"

#include <QJsonDocument>

#include <algorithm>
#include <utility>

namespace {

using namespace checkpoint_storage;
using namespace gameplay_storage;

bool valid(
    const gameplay_checkpoint& value, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources
) {
    return gameplay_storage::valid_limits(limits)
        && gameplay::valid_checkpoint(value.state, limits, resources)
        && value.pickup_elapsed_ms
        < static_cast<std::uint64_t>(value.state.pickup_interval_ms)
        && std::ranges::all_of(
               value.state.decks,
               [](const auto& state) {
                   return valid_slug(state.configuration.strategy_slug);
               }
        );
}

QJsonValue batch(const std::optional<gameplay::quiz_batch>& value) {
    if (!value)
        return QJsonValue::Null;
    return QJsonObject {
        { QStringLiteral("id"), number(value->id) },
        { QStringLiteral("remaining_ms"),
          value->remaining_ms ? QJsonValue(number(*value->remaining_ms))
                              : QJsonValue::Null },
    };
}

bool read_batch(
    const QJsonValue& value, std::optional<gameplay::quiz_batch>& output
) {
    if (value.isNull()) {
        output.reset();
        return true;
    }
    if (!value.isObject())
        return false;
    const auto object = value.toObject();
    const auto id = read_unsigned(object.value(QStringLiteral("id")));
    const auto remaining_value = object.value(QStringLiteral("remaining_ms"));
    const auto remaining = read_unsigned(remaining_value);
    if (object.size() != 2 || !id || (!remaining_value.isNull() && !remaining))
        return false;
    output = gameplay::quiz_batch { *id, remaining };
    return true;
}

std::optional<gameplay_checkpoint> read_root(
    const QJsonObject& root, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources
) {
    if (root.size() != 15
        || !gameplay_storage::read_integer(
            root.value(QStringLiteral("version")),
            gameplay_checkpoint_service::schema_version,
            gameplay_checkpoint_service::schema_version
        )
        || !gameplay_storage::read_integer(
            root.value(QStringLiteral("continuation_revision")),
            static_cast<int>(gameplay::session_checkpoint::current_revision),
            static_cast<int>(gameplay::session_checkpoint::current_revision)
        ))
        return std::nullopt;
    const auto decks_value = root.value(QStringLiteral("decks"));
    const auto map_value = root.value(QStringLiteral("deck_to_slot"));
    const auto config_value = root.value(QStringLiteral("configuration"));
    const auto schedule_value = root.value(QStringLiteral("table_schedule"));
    const auto random_value = root.value(QStringLiteral("random_dealing"));
    const auto timing_value = root.value(QStringLiteral("timing"));
    if (!decks_value.isArray() || !map_value.isArray()
        || !config_value.isObject() || !schedule_value.isObject()
        || !random_value.isObject() || !timing_value.isObject())
        return std::nullopt;
    const auto entries = decks_value.toArray();
    const auto count = static_cast<std::size_t>(entries.size());
    const auto mapping = map_value.toArray();
    if (count == 0 || count > limits.technical_cap
        || mapping.size() != entries.size())
        return std::nullopt;
    const auto config = config_value.toObject();
    const auto configuration = gameplay_storage::read_configuration(
        config, count, { count, limits.technical_cap }
    );
    const auto interval = gameplay_storage::read_integer(
        root.value(QStringLiteral("interval_ms")),
        gameplay::session::minimum_pick_interval_ms,
        gameplay::session::maximum_pick_interval_ms
    );
    const auto lives = gameplay_storage::read_integer(
        root.value(QStringLiteral("remaining_lives")), 0, 12
    );
    const auto elapsed
        = read_unsigned(root.value(QStringLiteral("pickup_elapsed_ms")));
    const auto last
        = read_unsigned(root.value(QStringLiteral("last_batch_id")));
    const auto anchor_value = root.value(QStringLiteral("sequential_anchor"));
    const auto anchor = read_size(anchor_value);
    const auto random = random_value.toObject();
    const auto seed = read_unsigned(random.value(QStringLiteral("seed")));
    const auto step = read_unsigned(random.value(QStringLiteral("next_step")));
    const auto timing = timing_value.toObject();
    const auto play
        = read_unsigned(timing.value(QStringLiteral("play_time_ms")));
    const auto quiz
        = read_unsigned(timing.value(QStringLiteral("quiz_time_ms")));
    const auto challenge
        = read_statistics(root.value(QStringLiteral("challenge")));
    if (config.size() != 9 || random.size() != 2 || timing.size() != 2
        || !configuration || !interval || !lives || !elapsed || !last || !seed
        || !step || !play || !quiz || !challenge
        || (!anchor_value.isNull() && !anchor))
        return std::nullopt;
    gameplay_checkpoint result;
    auto& state = result.state;
    state.configuration = *configuration;
    state.remaining_lives = *lives;
    state.pickup_interval_ms = *interval;
    state.last_batch_id = *last;
    state.random_dealing = { *seed, *step };
    state.timing = { *play, *quiz };
    state.challenge = *challenge;
    if (anchor)
        state.sequential_anchor = gameplay::physical_slot_id { *anchor };
    result.pickup_elapsed_ms = *elapsed;
    const auto schedule = schedule_value.toObject();
    budget capacity { resources.maximum_cards, resources.maximum_targets };
    auto schedule_targets
        = read_targets(schedule.value(QStringLiteral("targets")), capacity);
    const auto cursor
        = read_size(schedule.value(QStringLiteral("next_target")));
    if (schedule.size() != 3 || !schedule_targets || !cursor
        || !read_recipe(
            schedule.value(QStringLiteral("continuation")),
            state.table_schedule.continuation
        )
        || !read_batch(root.value(QStringLiteral("batch")), state.batch))
        return std::nullopt;
    state.table_schedule.targets = std::move(*schedule_targets);
    state.table_schedule.next_target = *cursor;
    state.decks.reserve(count);
    state.deck_to_slot.reserve(count);
    for (const auto& entry : entries) {
        auto parsed = read_deck(entry, capacity);
        if (!parsed)
            return std::nullopt;
        state.decks.push_back(std::move(*parsed));
    }
    for (const auto& entry : mapping) {
        const auto index = read_size(entry);
        if (!index || *index >= count)
            return std::nullopt;
        state.deck_to_slot.push_back({ *index });
    }
    return valid(result, limits, resources)
        ? std::make_optional(std::move(result))
        : std::nullopt;
}

} // namespace

std::optional<QByteArray> encode_gameplay_checkpoint(
    const gameplay_checkpoint& checkpoint, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources
) {
    if (!valid(checkpoint, limits, resources))
        return std::nullopt;
    const auto& state = checkpoint.state;
    QJsonArray entries;
    QJsonArray mapping;
    for (const auto& record : state.decks)
        entries.append(deck(record));
    for (const auto slot : state.deck_to_slot)
        mapping.append(number(slot.value));
    const QJsonObject root {
        { QStringLiteral("version"),
          gameplay_checkpoint_service::schema_version },
        { QStringLiteral("continuation_revision"),
          static_cast<int>(state.continuation_revision) },
        { QStringLiteral("configuration"),
          gameplay_storage::encode_configuration(state.configuration) },
        { QStringLiteral("decks"), entries },
        { QStringLiteral("deck_to_slot"), mapping },
        { QStringLiteral("sequential_anchor"),
          state.sequential_anchor
              ? QJsonValue(number(state.sequential_anchor->value))
              : QJsonValue::Null },
        { QStringLiteral("random_dealing"),
          QJsonObject {
              { QStringLiteral("seed"), number(state.random_dealing.seed) },
              { QStringLiteral("next_step"),
                number(state.random_dealing.next_step) },
          } },
        { QStringLiteral("table_schedule"),
          QJsonObject {
              { QStringLiteral("targets"),
                targets(state.table_schedule.targets) },
              { QStringLiteral("next_target"),
                number(state.table_schedule.next_target) },
              { QStringLiteral("continuation"),
                recipe(state.table_schedule.continuation) },
          } },
        { QStringLiteral("batch"), batch(state.batch) },
        { QStringLiteral("last_batch_id"), number(state.last_batch_id) },
        { QStringLiteral("challenge"), statistics(state.challenge) },
        { QStringLiteral("timing"),
          QJsonObject {
              { QStringLiteral("play_time_ms"),
                number(state.timing.play_time_ms) },
              { QStringLiteral("quiz_time_ms"),
                number(state.timing.quiz_time_ms) },
          } },
        { QStringLiteral("remaining_lives"), state.remaining_lives },
        { QStringLiteral("interval_ms"), state.pickup_interval_ms },
        { QStringLiteral("pickup_elapsed_ms"),
          number(checkpoint.pickup_elapsed_ms) },
    };
    auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return bytes.size() <= gameplay_checkpoint_service::maximum_document_bytes
        ? std::make_optional(std::move(bytes))
        : std::nullopt;
}

std::optional<gameplay_checkpoint> decode_gameplay_checkpoint(
    const QByteArray& bytes, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources
) {
    if (!gameplay_storage::valid_limits(limits) || !resources.valid()
        || bytes.isEmpty()
        || bytes.size() > gameplay_checkpoint_service::maximum_document_bytes)
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(bytes);
    return document.isObject() ? read_root(document.object(), limits, resources)
                               : std::nullopt;
}
