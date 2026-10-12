#include "settings/gameplay_codec.hpp"

#include <limits>

namespace gameplay_storage {

QString number(std::uint64_t value) {
    return QString::number(static_cast<qulonglong>(value));
}

QString signed_number(std::int64_t value) {
    return QString::number(static_cast<qlonglong>(value));
}

std::optional<std::uint64_t> read_unsigned(const QJsonValue& value) {
    if (!value.isString())
        return std::nullopt;
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 20)
        return std::nullopt;
    bool ok = false;
    const auto result = static_cast<std::uint64_t>(text.toULongLong(&ok, 10));
    return ok && number(result) == text ? std::make_optional(result)
                                        : std::nullopt;
}

std::optional<std::int64_t> read_signed(const QJsonValue& value) {
    if (!value.isString())
        return std::nullopt;
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 20)
        return std::nullopt;
    bool ok = false;
    const auto result = static_cast<std::int64_t>(text.toLongLong(&ok, 10));
    return ok && signed_number(result) == text ? std::make_optional(result)
                                               : std::nullopt;
}

std::optional<std::size_t> read_size(const QJsonValue& value) {
    const auto result = read_unsigned(value);
    return result && *result <= std::numeric_limits<std::size_t>::max()
        ? std::make_optional(static_cast<std::size_t>(*result))
        : std::nullopt;
}

bool valid_slug(const std::string& slug) {
    if (slug.empty() || slug.size() > 512)
        return false;
    const auto text = QString::fromStdString(slug);
    if (text.size() > 128 || text != text.trimmed()
        || text.toStdString() != slug)
        return false;
    for (const auto character : text)
        if (character.category() == QChar::Other_Control)
            return false;
    return true;
}

QJsonValue recipe(
    const std::optional<gameplay::quiz_schedule_continuation>& continuation
) {
    if (!continuation)
        return QJsonValue::Null;
    const auto& value = *continuation;
    return QJsonObject {
        { QStringLiteral("seed"), number(value.seed) },
        { QStringLiteral("next_page"), number(value.next_page) },
        { QStringLiteral("after_target"), number(value.after_target) },
        { QStringLiteral("minimum_gap"), number(value.minimum_gap) },
        { QStringLiteral("maximum_gap"), number(value.maximum_gap) },
        { QStringLiteral("page_size"), number(value.page_size) },
    };
}

bool read_recipe(
    const QJsonValue& value,
    std::optional<gameplay::quiz_schedule_continuation>& output
) {
    if (value.isNull()) {
        output.reset();
        return true;
    }
    if (!value.isObject())
        return false;
    const auto object = value.toObject();
    const auto seed = read_unsigned(object.value(QStringLiteral("seed")));
    const auto page = read_unsigned(object.value(QStringLiteral("next_page")));
    const auto after
        = read_unsigned(object.value(QStringLiteral("after_target")));
    const auto minimum
        = read_unsigned(object.value(QStringLiteral("minimum_gap")));
    const auto maximum
        = read_unsigned(object.value(QStringLiteral("maximum_gap")));
    const auto size = read_size(object.value(QStringLiteral("page_size")));
    if (object.size() != 6 || !seed || !page || !after || !minimum || !maximum
        || !size || *minimum > std::numeric_limits<std::uint32_t>::max()
        || *maximum > std::numeric_limits<std::uint32_t>::max())
        return false;
    output = gameplay::quiz_schedule_continuation {
        *seed,
        *page,
        *after,
        static_cast<std::uint32_t>(*minimum),
        static_cast<std::uint32_t>(*maximum),
        *size
    };
    return true;
}

QJsonObject statistics(const gameplay::session_statistics& value) {
    return {
        { QStringLiteral("dealt_normal_cards"),
          number(value.dealt_normal_cards) },
        { QStringLiteral("verified_cards"), number(value.verified_cards) },
        { QStringLiteral("errors"), number(value.errors) },
        { QStringLiteral("skips"), number(value.skips) },
    };
}

std::optional<gameplay::session_statistics>
read_statistics(const QJsonValue& value) {
    if (!value.isObject())
        return std::nullopt;
    const auto object = value.toObject();
    const auto dealt
        = read_unsigned(object.value(QStringLiteral("dealt_normal_cards")));
    const auto verified
        = read_unsigned(object.value(QStringLiteral("verified_cards")));
    const auto errors = read_unsigned(object.value(QStringLiteral("errors")));
    const auto skips = read_unsigned(object.value(QStringLiteral("skips")));
    if (object.size() != 4 || !dealt || !verified || !errors || !skips)
        return std::nullopt;
    return gameplay::session_statistics { *dealt, *verified, *errors, *skips };
}

} // namespace gameplay_storage
