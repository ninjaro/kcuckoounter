#include "settings/gameplay_checkpoint_codec.hpp"

namespace checkpoint_storage {

using namespace gameplay_storage;

QJsonArray targets(const std::vector<std::uint64_t>& values) {
    QJsonArray result;
    for (const auto value : values)
        result.append(number(value));
    return result;
}

std::optional<std::vector<std::uint64_t>>
read_targets(const QJsonValue& value, budget& capacity) {
    if (!value.isArray())
        return std::nullopt;
    const auto array = value.toArray();
    const auto size = static_cast<std::size_t>(array.size());
    if (size > capacity.targets)
        return std::nullopt;
    capacity.targets -= size;
    std::vector<std::uint64_t> result;
    result.reserve(size);
    for (const auto& entry : array) {
        const auto target = read_unsigned(entry);
        if (!target)
            return std::nullopt;
        result.push_back(*target);
    }
    return result;
}

} // namespace checkpoint_storage
