#include "settings/gameplay_codec.hpp"

#include "table/gameplay_session.hpp"

#include <initializer_list>
#include <limits>
#include <utility>

namespace gameplay_storage {
namespace {

    template <typename Enum>
    std::optional<Enum> read_enum(
        const QJsonValue& value,
        std::initializer_list<std::pair<QString, Enum>> choices
    ) {
        if (value.isString())
            for (const auto& [key, choice] : choices)
                if (value.toString() == key)
                    return choice;
        return std::nullopt;
    }

} // namespace

using namespace gameplay;

bool valid_limits(slot_limits limits) {
    return limits.recommended > 0 && limits.recommended <= limits.technical_cap
        && limits.technical_cap
        <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

std::optional<int>
read_integer(const QJsonValue& value, int minimum, int maximum) {
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (!(number >= minimum && number <= maximum))
        return std::nullopt;
    const int integer = static_cast<int>(number);
    return number == integer ? std::make_optional(integer) : std::nullopt;
}

QJsonObject encode_configuration(const session_configuration& config) {
    return {
        { QStringLiteral("failure"),
          config.failure == failure_policy::lives ? QStringLiteral("lives")
                                                  : QStringLiteral("block") },
        { QStringLiteral("lives"), config.initial_lives },
        { QStringLiteral("source"),
          config.source == quiz_source::physical_joker
              ? QStringLiteral("physical_joker")
              : QStringLiteral("virtual_interrupt") },
        { QStringLiteral("scope"),
          config.scope == quiz_scope::single ? QStringLiteral("single")
                                             : QStringLiteral("multi") },
        { QStringLiteral("global_pause"), config.global_quiz_pause },
        { QStringLiteral("skip"), config.allow_skip },
        { QStringLiteral("dealing"),
          config.dealing == dealing_mode::sequential
              ? QStringLiteral("sequential")
              : config.dealing == dealing_mode::random
              ? QStringLiteral("random")
              : QStringLiteral("simultaneous") },
        { QStringLiteral("sequential_count"),
          static_cast<int>(config.sequential_count) },
        { QStringLiteral("allow_extra_slots"), config.allow_extra_slots },
    };
}

std::optional<session_configuration> read_configuration(
    const QJsonObject& object, std::size_t count, slot_limits limits
) {
    if (!valid_limits(limits) || count == 0 || count > limits.technical_cap)
        return std::nullopt;
    const auto lives
        = read_integer(object.value(QStringLiteral("lives")), 1, 12);
    const auto n = read_integer(
        object.value(QStringLiteral("sequential_count")), 1,
        static_cast<int>(count)
    );
    const auto failure = read_enum<failure_policy>(
        object.value(QStringLiteral("failure")),
        { { QStringLiteral("lives"), failure_policy::lives },
          { QStringLiteral("block"), failure_policy::block } }
    );
    const auto source = read_enum<quiz_source>(
        object.value(QStringLiteral("source")),
        { { QStringLiteral("physical_joker"), quiz_source::physical_joker },
          { QStringLiteral("virtual_interrupt"),
            quiz_source::virtual_interrupt } }
    );
    const auto scope = read_enum<quiz_scope>(
        object.value(QStringLiteral("scope")),
        { { QStringLiteral("single"), quiz_scope::single },
          { QStringLiteral("multi"), quiz_scope::multi } }
    );
    const auto dealing = read_enum<dealing_mode>(
        object.value(QStringLiteral("dealing")),
        { { QStringLiteral("sequential"), dealing_mode::sequential },
          { QStringLiteral("random"), dealing_mode::random },
          { QStringLiteral("simultaneous"), dealing_mode::simultaneous } }
    );
    if (!lives || !n || !failure || !source || !scope || !dealing
        || !object.value(QStringLiteral("global_pause")).isBool()
        || !object.value(QStringLiteral("skip")).isBool()
        || !object.value(QStringLiteral("allow_extra_slots")).isBool())
        return std::nullopt;
    session_configuration result {
        *failure,
        *lives,
        *source,
        *scope,
        object.value(QStringLiteral("global_pause")).toBool(),
        object.value(QStringLiteral("skip")).toBool(),
        *dealing,
        static_cast<std::size_t>(*n),
        object.value(QStringLiteral("allow_extra_slots")).toBool(),
    };
    return valid_configuration(result, count, limits)
        ? std::make_optional(result)
        : std::nullopt;
}

} // namespace gameplay_storage
