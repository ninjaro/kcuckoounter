#include "settings/gameplay_checkpoint_codec.hpp"

#include "settings/gameplay_codec.hpp"

#include <limits>
#include <utility>

namespace checkpoint_storage {

using namespace gameplay_storage;

namespace {

    QJsonArray cards(const std::vector<std::uint8_t>& values) {
        QJsonArray result;
        for (const auto value : values)
            result.append(value);
        return result;
    }

    std::optional<std::vector<std::uint8_t>>
    read_cards(const QJsonValue& value, budget& capacity) {
        if (!value.isArray())
            return std::nullopt;
        const auto array = value.toArray();
        const auto size = static_cast<std::size_t>(array.size());
        if (size == 0 || size > capacity.cards)
            return std::nullopt;
        capacity.cards -= size;
        std::vector<std::uint8_t> result;
        result.reserve(size);
        for (const auto& entry : array) {
            const auto face = gameplay_storage::read_integer(entry, 0, 53);
            if (!face)
                return std::nullopt;
            result.push_back(static_cast<std::uint8_t>(*face));
        }
        return result;
    }

    QJsonValue
    infinite(const std::optional<gameplay::infinite_card_buffer>& buffer) {
        if (!buffer)
            return QJsonValue::Null;
        return QJsonObject {
            { QStringLiteral("seed"), number(buffer->seed) },
            { QStringLiteral("chunk"), number(buffer->chunk) },
            { QStringLiteral("decks_per_chunk"),
              number(buffer->decks_per_chunk) },
            { QStringLiteral("lookahead"), cards(buffer->lookahead) },
        };
    }

    bool read_infinite(
        const QJsonValue& value, budget& capacity,
        std::optional<gameplay::infinite_card_buffer>& output
    ) {
        if (value.isNull()) {
            output.reset();
            return true;
        }
        if (!value.isObject())
            return false;
        const auto object = value.toObject();
        const auto seed = read_unsigned(object.value(QStringLiteral("seed")));
        const auto chunk = read_unsigned(object.value(QStringLiteral("chunk")));
        const auto count
            = read_size(object.value(QStringLiteral("decks_per_chunk")));
        auto lookahead
            = read_cards(object.value(QStringLiteral("lookahead")), capacity);
        if (object.size() != 4 || !seed || !chunk || !count || !lookahead)
            return false;
        output = gameplay::infinite_card_buffer { *seed, *chunk, *count,
                                                  std::move(*lookahead) };
        return true;
    }

} // namespace

QJsonObject stream(const gameplay::prepared_deck& value) {
    QJsonArray weights;
    for (const auto weight : value.rank_weights)
        weights.append(weight);
    return {
        { QStringLiteral("cards"), cards(value.cards) },
        { QStringLiteral("rank_weights"), weights },
        { QStringLiteral("initial_count"),
          signed_number(value.initial_running_count) },
        { QStringLiteral("targets"), targets(value.virtual_quiz_targets) },
        { QStringLiteral("infinite"), infinite(value.infinite_cards) },
        { QStringLiteral("continuation"), recipe(value.quiz_continuation) },
    };
}

std::optional<gameplay::prepared_deck>
read_stream(const QJsonValue& value, budget& capacity) {
    if (!value.isObject())
        return std::nullopt;
    const auto object = value.toObject();
    const auto weights_value = object.value(QStringLiteral("rank_weights"));
    const auto initial
        = read_signed(object.value(QStringLiteral("initial_count")));
    auto physical = read_cards(object.value(QStringLiteral("cards")), capacity);
    auto schedule
        = read_targets(object.value(QStringLiteral("targets")), capacity);
    if (object.size() != 6 || !weights_value.isArray()
        || weights_value.toArray().size() != 13 || !initial || !physical
        || !schedule)
        return std::nullopt;
    gameplay::prepared_deck result;
    const auto weights = weights_value.toArray();
    for (std::size_t i = 0; i < result.rank_weights.size(); ++i) {
        const auto weight = gameplay_storage::read_integer(
            weights.at(static_cast<qsizetype>(i)),
            std::numeric_limits<int>::min(), std::numeric_limits<int>::max()
        );
        if (!weight)
            return std::nullopt;
        result.rank_weights[i] = *weight;
    }
    if (!read_infinite(
            object.value(QStringLiteral("infinite")), capacity,
            result.infinite_cards
        )
        || !read_recipe(
            object.value(QStringLiteral("continuation")),
            result.quiz_continuation
        ))
        return std::nullopt;
    result.cards = std::move(*physical);
    result.initial_running_count = *initial;
    result.virtual_quiz_targets = std::move(*schedule);
    return result;
}

} // namespace checkpoint_storage
