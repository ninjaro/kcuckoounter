#include "settings/gameplay_drill.hpp"

#include "table/gameplay_strategy.hpp"

#include <algorithm>

bool supports_drill(
    const gameplay_drill& drill, const strategy_catalog& catalog,
    gameplay::slot_limits limits
) {
    return valid_gameplay_drill(drill, limits)
        && std::ranges::all_of(drill.decks, [&](const auto& deck) {
               return gameplay::resolve_strategy(catalog, deck.configuration)
                   .has_value();
           });
}

std::optional<gameplay::session> create_drill_session(
    const gameplay_drill& drill, const strategy_catalog& catalog,
    gameplay::slot_limits limits
) {
    if (!supports_drill(drill, catalog, limits))
        return std::nullopt;
    std::vector<gameplay::deck_configuration> decks;
    decks.reserve(drill.decks.size());
    for (const auto& deck : drill.decks)
        decks.push_back(deck.configuration);
    auto result = gameplay::session::create(
        drill.configuration, decks, { decks.size(), limits.technical_cap }
    );
    if (!result || !result->update_slot_limits(limits)
        || !result->set_pick_interval_ms(drill.pickup_interval_ms))
        return std::nullopt;
    for (std::size_t index = 0; index < drill.decks.size(); ++index)
        if (drill.decks[index].show_count
            && !result->set_show_count({ index }, true))
            return std::nullopt;
    return result;
}

std::optional<gameplay_drill> capture_gameplay_drill(
    const gameplay::session& game, const QString& name,
    gameplay::slot_limits limits
) {
    // Creation-ID order intentionally drops live Swap/mapping history. Never
    // traverse/copy cards, quiz payloads, scores or generation continuations.
    gameplay_drill result {
        name, game.configuration(), game.pick_interval_ms(), {}
    };
    if (game.size() == 0 || game.size() > limits.technical_cap)
        return std::nullopt;
    result.decks.reserve(game.size());
    for (std::size_t index = 0; index < game.size(); ++index) {
        const auto* deck = game.deck({ index });
        result.decks.push_back({ deck->configuration, deck->show_count });
    }
    return valid_gameplay_drill(result, limits) ? std::make_optional(result)
                                                : std::nullopt;
}
