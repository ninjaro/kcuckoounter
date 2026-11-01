#ifndef KCUCKOOUNTER_TABLE_TOUCH_TEXT_HPP
#define KCUCKOOUNTER_TABLE_TOUCH_TEXT_HPP

#include "table/gameplay_session.hpp"

#include <QString>
#include <QStringList>

struct strategy_data;

// Wording shared by the touch table and screens. Presentation only: every
// value is read from the session or its result, never recomputed.
namespace touch_text {

// Catalogue name without its parenthetical and trailing "Count", as the
// touch kit prints it in deck legends ("KO (Knock-Out) Count" → "KO").
[[nodiscard]] QString short_strategy(const QString& name);
[[nodiscard]] const strategy_data* strategy(const std::string& slug);
[[nodiscard]] QString strategy_name(const std::string& slug);
// Largest absolute tag of a point count; -1 for ratio counts above level 4.
[[nodiscard]] int strategy_level(const strategy_data& strategy);

// "DECK 1 · HI-LO" for the deck legend.
[[nodiscard]] QString
deck_legend(gameplay::deck_id id, const gameplay::deck_configuration& deck);
// "6-deck shoe · ends when dealt" or "Endless stream".
[[nodiscard]] QString shoe_summary(const gameplay::deck_configuration& deck);
// "Sequential 1/M", "Random", "Together"…
[[nodiscard]] QString
dealing_summary(const gameplay::session_configuration& configuration);
// Setup chips: Lives, scope, dealing, source and pause.
[[nodiscard]] QStringList setup_chips(
    const gameplay::session_configuration& configuration, std::size_t decks
);
// "04:12" (or "1:04:12") from milliseconds.
[[nodiscard]] QString clock(std::uint64_t milliseconds);
// Countdown seconds, rounded up: "00:47".
[[nodiscard]] QString countdown(std::uint64_t milliseconds);
// Pickup interval as "0.8 s".
[[nodiscard]] QString interval(int milliseconds);

} // namespace touch_text

#endif // KCUCKOOUNTER_TABLE_TOUCH_TEXT_HPP
