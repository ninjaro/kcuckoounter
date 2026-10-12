#ifndef KCUCKOOUNTER_SETTINGS_GAMEPLAY_PROGRESS_CODEC_HPP
#define KCUCKOOUNTER_SETTINGS_GAMEPLAY_PROGRESS_CODEC_HPP

#include "settings/gameplay_progress.hpp"

#include <QJsonObject>

// Implementation-only archived result fields and admission checks.
namespace progress_storage {

[[nodiscard]] bool accumulate(
    gameplay::session_statistics& counters, gameplay::session_timing& timing,
    const gameplay::session_statistics& next,
    const gameplay::session_timing& elapsed
);
[[nodiscard]] bool
valid(const gameplay_progress& progress, gameplay::slot_limits limits);
[[nodiscard]] bool valid_record(
    const gameplay_result_record& record, gameplay::slot_limits limits
);
[[nodiscard]] QJsonObject result(const gameplay::session_result& value);
[[nodiscard]] std::optional<gameplay::session_result>
read_result(const QJsonValue& value, gameplay::slot_limits limits);
[[nodiscard]] QJsonObject deck(const gameplay::deck_result& value);
[[nodiscard]] std::optional<gameplay::deck_result>
read_deck(const QJsonValue& value, std::size_t& targets);
[[nodiscard]] QJsonObject
schedule(const gameplay::quiz_schedule_snapshot& value);
[[nodiscard]] std::optional<gameplay::quiz_schedule_snapshot>
read_schedule(const QJsonValue& value, std::size_t& targets);

} // namespace progress_storage

#endif // KCUCKOOUNTER_SETTINGS_GAMEPLAY_PROGRESS_CODEC_HPP
