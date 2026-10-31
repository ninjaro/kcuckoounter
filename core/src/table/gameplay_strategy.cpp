#include "table/gameplay_strategy.hpp"

#include "settings/strategy_data.hpp"

#include <algorithm>

namespace gameplay {
namespace {

    const strategy_data*
    find_strategy(const strategy_catalog& catalog, const std::string& slug) {
        if (!catalog.is_valid()) {
            return nullptr;
        }
        const auto requested = QString::fromStdString(slug);
        const auto found = std::find_if(
            catalog.strategies.begin(), catalog.strategies.end(),
            [&requested](const strategy_data& strategy) {
                return strategy.slug == requested;
            }
        );
        return found == catalog.strategies.end() ? nullptr : &*found;
    }

} // namespace

std::optional<strategy_parameters> resolve_strategy(
    const strategy_catalog& catalog, const deck_configuration& configuration
) {
    const auto* strategy = find_strategy(catalog, configuration.strategy_slug);
    if (!strategy || strategy->weights.size() != 13
        || strategy->min_decks < 1) {
        return std::nullopt;
    }
    const auto count
        = strategy->initial_running_count_for(configuration.deck_count);
    if (!count) {
        return std::nullopt;
    }
    strategy_parameters result {};
    std::copy(
        strategy->weights.begin(), strategy->weights.end(),
        result.rank_weights.begin()
    );
    result.initial_running_count = *count;
    result.recommended_decks = static_cast<std::size_t>(strategy->min_decks);
    return result;
}

std::optional<deck_configuration> recommended_configuration(
    const strategy_catalog& catalog, const deck_configuration& configuration
) {
    const auto* strategy = find_strategy(catalog, configuration.strategy_slug);
    if (!strategy || strategy->min_decks < 1) {
        return std::nullopt;
    }
    auto result = configuration;
    result.deck_count = static_cast<std::size_t>(strategy->min_decks);
    return result;
}

std::optional<session> create_fresh_session(
    const strategy_catalog& catalog, const session_configuration& configuration,
    std::span<const deck_configuration> templates, slot_limits bounds
) {
    if (!catalog.is_valid() || templates.empty()
        || templates.size() > bounds.technical_cap)
        return std::nullopt;
    std::vector<deck_configuration> decks;
    decks.reserve(templates.size());
    for (const auto& chosen : templates) {
        const auto recommended = recommended_configuration(catalog, chosen);
        if (!recommended || !resolve_strategy(catalog, *recommended))
            return std::nullopt;
        decks.push_back(*recommended);
    }
    return session::create(configuration, std::move(decks), bounds);
}

std::optional<session> resize_setup_roster(
    const session& current, std::size_t count, const strategy_catalog& catalog,
    const deck_configuration& new_deck_template, slot_limits bounds
) {
    if (current.phase() != session_phase::setup || count == 0
        || count > bounds.technical_cap)
        return std::nullopt;
    const auto added = recommended_configuration(catalog, new_deck_template);
    if (count > current.size() && !added)
        return std::nullopt;
    std::vector<deck_configuration> configurations;
    configurations.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        configurations.push_back(
            index < current.size() ? current.deck({ index })->configuration
                                   : *added
        );
    auto settings = current.configuration();
    settings.sequential_count = std::min(settings.sequential_count, count);
    auto replacement = session::create(settings, configurations, bounds);
    if (!replacement
        || !replacement->set_pick_interval_ms(current.pick_interval_ms()))
        return std::nullopt;
    for (std::size_t index = 0; index < std::min(count, current.size());
         ++index)
        if (current.deck({ index })->show_count
            && !replacement->set_show_count({ index }, true))
            return std::nullopt;
    return replacement;
}

bool prepare_strategy_deck(
    session& game, deck_id id, const strategy_catalog& catalog,
    std::span<const std::uint8_t> cards
) {
    const auto* deck = game.deck(id);
    if (!deck || game.phase() != session_phase::setup || cards.empty()
        || deck->configuration.infinite) {
        return false;
    }
    const auto parameters = resolve_strategy(catalog, deck->configuration);
    if (!parameters) {
        return false;
    }
    prepared_deck prepared {
        .cards = { cards.begin(), cards.end() },
        .rank_weights = parameters->rank_weights,
        .initial_running_count = parameters->initial_running_count,
    };
    return game.prepare_deck(id, prepared);
}

bool prepare_finite_session(
    session& game, const strategy_catalog& catalog, std::uint64_t seed,
    const generation_policy& policy
) {
    for (std::size_t i = 0; i < game.size(); ++i) {
        if (game.deck(deck_id { i })->configuration.infinite) {
            return false;
        }
    }
    return prepare_generated_session(game, catalog, seed, policy);
}

bool prepare_generated_session(
    session& game, const strategy_catalog& catalog, std::uint64_t seed,
    const generation_policy& policy
) {
    if (game.phase() != session_phase::setup) {
        return false;
    }
    const auto& settings = game.configuration();
    const std::size_t face_count
        = settings.source == quiz_source::physical_joker ? 54 : 52;
    std::size_t total_cards = 0;
    std::size_t total_target_bound = 0;
    std::size_t horizon = 0;
    bool infinite = false;
    // Preflight the full roster before allocating any shoe or schedule.
    if (!policy.valid()) {
        return false;
    }
    std::vector<strategy_parameters> parameters;
    parameters.reserve(game.size());
    for (std::size_t i = 0; i < game.size(); ++i) {
        const auto& configuration = game.deck(deck_id { i })->configuration;
        const auto strategy = resolve_strategy(catalog, configuration);
        const auto multiplier = configuration.infinite ? 2u : 1u;
        const auto deck_count = configuration.infinite
            ? policy.infinite_chunk_decks
            : configuration.deck_count;
        if (!strategy
            || deck_count > (policy.maximum_cards - total_cards) / multiplier
                    / face_count) {
            return false;
        }
        const auto count = deck_count * face_count * multiplier;
        total_cards += count;
        horizon = std::max(horizon, count);
        infinite = infinite || configuration.infinite;
        if (settings.source == quiz_source::virtual_interrupt
            && settings.scope == quiz_scope::single) {
            const auto target_bound = configuration.infinite
                ? policy.quiz_page_targets
                : (count - 1) / policy.minimum_quiz_gap;
            if (target_bound > policy.maximum_targets - total_target_bound) {
                return false;
            }
            total_target_bound += target_bound;
        }
        parameters.push_back(*strategy);
    }
    if (settings.source == quiz_source::virtual_interrupt
        && settings.scope == quiz_scope::multi
        && (infinite ? policy.quiz_page_targets
                     : (horizon - 1) / policy.minimum_quiz_gap)
            > policy.maximum_targets) {
        return false;
    }
    const quiz_schedule_continuation initial_page {
        .seed = seed,
        .next_page = 0,
        .after_target = 0,
        .minimum_gap = policy.minimum_quiz_gap,
        .maximum_gap = policy.maximum_quiz_gap,
        .page_size = policy.quiz_page_targets,
    };
    std::vector<prepared_deck> prepared;
    prepared.reserve(game.size());
    for (std::size_t i = 0; i < game.size(); ++i) {
        const deck_id id { i };
        prepared_deck stream {
            .cards = {},
            .rank_weights = parameters[i].rank_weights,
            .initial_running_count = parameters[i].initial_running_count,
            .virtual_quiz_targets = {},
        };
        const auto& configuration = game.deck(id)->configuration;
        if (configuration.infinite) {
            auto shoe
                = generate_infinite_shoe(settings.source, seed, id, policy);
            if (!shoe) {
                return false;
            }
            stream.cards = std::move(shoe->cards);
            stream.infinite_cards = std::move(shoe->continuation);
        } else {
            auto cards = generate_shoe(
                settings.source, configuration.deck_count, seed, id, policy
            );
            if (!cards) {
                return false;
            }
            stream.cards = std::move(*cards);
        }
        if (settings.source == quiz_source::virtual_interrupt
            && settings.scope == quiz_scope::single) {
            if (configuration.infinite) {
                auto page = generate_quiz_page(initial_page, id, policy);
                if (!page) {
                    return false;
                }
                stream.virtual_quiz_targets = std::move(page->targets);
                stream.quiz_continuation = page->continuation;
            } else {
                auto targets = generate_quiz_targets(
                    stream.cards.size(), seed, id, policy
                );
                if (!targets) {
                    return false;
                }
                stream.virtual_quiz_targets = std::move(*targets);
            }
        }
        prepared.push_back(std::move(stream));
    }
    std::vector<std::uint64_t> table_targets;
    std::optional<quiz_schedule_continuation> continuation;
    if (settings.source == quiz_source::virtual_interrupt
        && settings.scope == quiz_scope::multi) {
        if (infinite) {
            auto page = generate_quiz_page(initial_page, std::nullopt, policy);
            if (!page) {
                return false;
            }
            table_targets = std::move(page->targets);
            continuation = page->continuation;
        } else {
            auto targets
                = generate_quiz_targets(horizon, seed, std::nullopt, policy);
            if (!targets) {
                return false;
            }
            table_targets = std::move(*targets);
        }
    }
    return game.prepare_decks(prepared, table_targets, continuation);
}

} // namespace gameplay
