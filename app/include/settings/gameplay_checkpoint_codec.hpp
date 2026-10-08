#ifndef KCUCKOOUNTER_SETTINGS_GAMEPLAY_CHECKPOINT_CODEC_HPP
#define KCUCKOOUNTER_SETTINGS_GAMEPLAY_CHECKPOINT_CODEC_HPP

#include "settings/gameplay_checkpoint.hpp"
#include "settings/gameplay_codec.hpp"

#include <QJsonArray>
#include <QJsonObject>

// Implementation-only bounded field codecs, not an additional storage service.
namespace checkpoint_storage {

struct budget {
    std::size_t cards;
    std::size_t targets;
};

[[nodiscard]] QJsonArray targets(const std::vector<std::uint64_t>& values);
[[nodiscard]] std::optional<std::vector<std::uint64_t>>
read_targets(const QJsonValue& value, budget& capacity);
[[nodiscard]] QJsonObject stream(const gameplay::prepared_deck& value);
[[nodiscard]] std::optional<gameplay::prepared_deck>
read_stream(const QJsonValue& value, budget& capacity);
[[nodiscard]] QJsonObject deck(const gameplay::deck_state& value);
[[nodiscard]] std::optional<gameplay::deck_state>
read_deck(const QJsonValue& value, budget& capacity);

} // namespace checkpoint_storage

#endif // KCUCKOOUNTER_SETTINGS_GAMEPLAY_CHECKPOINT_CODEC_HPP
