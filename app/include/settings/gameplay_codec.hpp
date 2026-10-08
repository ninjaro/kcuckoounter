#ifndef KCUCKOOUNTER_SETTINGS_GAMEPLAY_CODEC_HPP
#define KCUCKOOUNTER_SETTINGS_GAMEPLAY_CODEC_HPP

#include "table/gameplay_types.hpp"

#include <QJsonObject>
#include <optional>

// Implementation-only JSON fields shared by gameplay storage formats.
namespace gameplay_storage {

[[nodiscard]] QString number(std::uint64_t value);
[[nodiscard]] QString signed_number(std::int64_t value);
[[nodiscard]] std::optional<std::uint64_t>
read_unsigned(const QJsonValue& value);
[[nodiscard]] std::optional<std::int64_t> read_signed(const QJsonValue& value);
[[nodiscard]] std::optional<std::size_t> read_size(const QJsonValue& value);
[[nodiscard]] bool valid_slug(const std::string& slug);
[[nodiscard]] QJsonValue
recipe(const std::optional<gameplay::quiz_schedule_continuation>& continuation);
[[nodiscard]] bool read_recipe(
    const QJsonValue& value,
    std::optional<gameplay::quiz_schedule_continuation>& output
);
[[nodiscard]] QJsonObject statistics(const gameplay::session_statistics& value);
[[nodiscard]] std::optional<gameplay::session_statistics>
read_statistics(const QJsonValue& value);

[[nodiscard]] bool valid_limits(gameplay::slot_limits limits);
[[nodiscard]] std::optional<int>
read_integer(const QJsonValue& value, int minimum, int maximum);
[[nodiscard]] QJsonObject
encode_configuration(const gameplay::session_configuration& configuration);
[[nodiscard]] std::optional<gameplay::session_configuration> read_configuration(
    const QJsonObject& object, std::size_t count, gameplay::slot_limits limits
);

} // namespace gameplay_storage

#endif // KCUCKOOUNTER_SETTINGS_GAMEPLAY_CODEC_HPP
