#include "settings/gameplay_checkpoint_codec.hpp"

#include <utility>

namespace checkpoint_storage {

using namespace gameplay_storage;

namespace {

    QJsonValue quiz(const std::optional<gameplay::pending_quiz>& question) {
        if (!question)
            return QJsonValue::Null;
        return QJsonObject {
            { QStringLiteral("batch_id"), number(question->batch_id) },
            { QStringLiteral("expected_count"),
              signed_number(question->expected_count) },
            { QStringLiteral("input"), signed_number(question->input) },
        };
    }

    bool read_quiz(
        const QJsonValue& value, std::optional<gameplay::pending_quiz>& output
    ) {
        if (value.isNull()) {
            output.reset();
            return true;
        }
        if (!value.isObject())
            return false;
        const auto object = value.toObject();
        const auto batch
            = read_unsigned(object.value(QStringLiteral("batch_id")));
        const auto expected
            = read_signed(object.value(QStringLiteral("expected_count")));
        const auto input = read_signed(object.value(QStringLiteral("input")));
        if (object.size() != 3 || !batch || !expected || !input)
            return false;
        output = gameplay::pending_quiz { *batch, *expected, *input };
        return true;
    }

} // namespace

QJsonObject deck(const gameplay::deck_state& value) {
    const auto& config = value.configuration;
    const auto& stats = value.statistics;
    auto counts = statistics(
        { stats.dealt_normal_cards, stats.verified_cards, stats.errors,
          stats.skips }
    );
    counts.insert(
        QStringLiteral("dealing_time_ms"), number(stats.dealing_time_ms)
    );
    counts.insert(
        QStringLiteral("answer_time_ms"), number(stats.answer_time_ms)
    );
    return {
        { QStringLiteral("id"), number(value.id.value) },
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
        { QStringLiteral("stream"), stream(value.stream) },
        { QStringLiteral("next_card"), number(value.next_card) },
        { QStringLiteral("dealt_physical_cards"),
          number(value.dealt_physical_cards) },
        { QStringLiteral("running_count"), signed_number(value.running_count) },
        { QStringLiteral("latest_input"), signed_number(value.latest_input) },
        { QStringLiteral("show_count"), value.show_count },
        { QStringLiteral("unverified_normal_cards"),
          number(value.unverified_normal_cards) },
        { QStringLiteral("next_quiz_target"), number(value.next_quiz_target) },
        { QStringLiteral("quiz"), quiz(value.quiz) },
        { QStringLiteral("statistics"), counts },
    };
}

std::optional<gameplay::deck_state>
read_deck(const QJsonValue& value, budget& capacity) {
    if (!value.isObject())
        return std::nullopt;
    const auto object = value.toObject();
    const auto config_value = object.value(QStringLiteral("configuration"));
    const auto stats_value = object.value(QStringLiteral("statistics"));
    if (object.size() != 13 || !config_value.isObject()
        || !stats_value.isObject()
        || !object.value(QStringLiteral("status")).isString()
        || !object.value(QStringLiteral("show_count")).isBool())
        return std::nullopt;
    const auto config = config_value.toObject();
    const auto slug_value = config.value(QStringLiteral("strategy"));
    const auto count = read_size(config.value(QStringLiteral("count")));
    if (config.size() != 4 || !slug_value.isString() || !count
        || !config.value(QStringLiteral("infinite")).isBool()
        || !config.value(QStringLiteral("training")).isBool())
        return std::nullopt;
    const auto text = slug_value.toString();
    if (text.size() > 128)
        return std::nullopt;
    const auto slug = text.toStdString();
    if (!valid_slug(slug))
        return std::nullopt;
    const auto id = read_size(object.value(QStringLiteral("id")));
    const auto next = read_size(object.value(QStringLiteral("next_card")));
    const auto physical
        = read_unsigned(object.value(QStringLiteral("dealt_physical_cards")));
    const auto running
        = read_signed(object.value(QStringLiteral("running_count")));
    const auto input
        = read_signed(object.value(QStringLiteral("latest_input")));
    const auto unverified = read_unsigned(
        object.value(QStringLiteral("unverified_normal_cards"))
    );
    const auto target
        = read_size(object.value(QStringLiteral("next_quiz_target")));
    auto stats = stats_value.toObject();
    const auto dealing_time
        = read_unsigned(stats.take(QStringLiteral("dealing_time_ms")));
    const auto answer_time
        = read_unsigned(stats.take(QStringLiteral("answer_time_ms")));
    const auto counters = read_statistics(stats);
    if (!id || !next || !physical || !running || !input || !unverified
        || !target || !dealing_time || !answer_time || !counters)
        return std::nullopt;
    const auto status = object.value(QStringLiteral("status")).toString();
    gameplay::deck_status lifecycle;
    if (status == QStringLiteral("active"))
        lifecycle = gameplay::deck_status::active;
    else if (status == QStringLiteral("completed"))
        lifecycle = gameplay::deck_status::completed;
    else if (status == QStringLiteral("failed"))
        lifecycle = gameplay::deck_status::failed;
    else
        return std::nullopt;
    auto prepared
        = read_stream(object.value(QStringLiteral("stream")), capacity);
    if (!prepared)
        return std::nullopt;
    gameplay::deck_state result {
        .id = { *id },
        .configuration
        = { slug, *count, config.value(QStringLiteral("infinite")).toBool(),
            config.value(QStringLiteral("training")).toBool() },
        .status = lifecycle,
        .stream = std::move(*prepared),
        .next_card = *next,
        .dealt_physical_cards = *physical,
        .running_count = *running,
        .latest_input = *input,
        .show_count = object.value(QStringLiteral("show_count")).toBool(),
        .unverified_normal_cards = *unverified,
        .next_quiz_target = *target,
        .quiz = std::nullopt,
        .statistics
        = { counters->dealt_normal_cards, counters->verified_cards,
            counters->errors, counters->skips, *dealing_time, *answer_time },
    };
    if (!read_quiz(object.value(QStringLiteral("quiz")), result.quiz))
        return std::nullopt;
    return result;
}

} // namespace checkpoint_storage
