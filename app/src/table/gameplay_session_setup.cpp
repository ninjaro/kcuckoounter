#include "table/gameplay_session.hpp"

#include "table/gameplay_cadence.hpp"
#include "table/gameplay_generation.hpp"

#include <algorithm>
#include <utility>

namespace gameplay {

namespace session_setup {
    bool valid_slot_limits(slot_limits limits, std::size_t count) {
        return limits.recommended > 0
            && limits.technical_cap >= limits.recommended && count > 0
            && count <= limits.technical_cap;
    }

    bool valid(
        const session_configuration& settings, std::size_t count,
        slot_limits limits
    ) {
        if (!valid_slot_limits(limits, count)
            || (!settings.allow_extra_slots && count > limits.recommended)) {
            return false;
        }
        if (settings.failure != failure_policy::lives
            && settings.failure != failure_policy::block) {
            return false;
        }
        if (settings.source != quiz_source::physical_joker
            && settings.source != quiz_source::virtual_interrupt) {
            return false;
        }
        if (settings.scope != quiz_scope::single
            && settings.scope != quiz_scope::multi) {
            return false;
        }
        if (settings.dealing != dealing_mode::sequential
            && settings.dealing != dealing_mode::random
            && settings.dealing != dealing_mode::simultaneous) {
            return false;
        }
        return settings.initial_lives >= 1 && settings.initial_lives <= 12
            && (settings.failure != failure_policy::lives
                || settings.global_quiz_pause)
            && settings.sequential_count >= 1
            && settings.sequential_count <= count;
    }

    bool valid(const deck_configuration& settings) {
        // Catalogue resolution and formula validation belong to G3. No
        // arbitrary minimum/ideal deck-count recommendation becomes a gameplay
        // restriction.
        return !settings.strategy_slug.empty() && settings.deck_count > 0;
    }

    bool valid_targets(
        std::span<const std::uint64_t> targets,
        std::optional<std::uint64_t> exclusive_horizon
    ) {
        std::uint64_t previous = 0;
        for (const auto target : targets) {
            if (target <= previous
                || (exclusive_horizon && target >= *exclusive_horizon)) {
                return false;
            }
            previous = target;
        }
        return true;
    }

    bool valid_page(
        std::span<const std::uint64_t> targets,
        const std::optional<quiz_schedule_continuation>& continuation
    ) {
        return continuation && !targets.empty() && continuation->minimum_gap > 0
            && continuation->maximum_gap >= continuation->minimum_gap
            && continuation->page_size == targets.size()
            && continuation->next_page > 0
            && continuation->after_target == targets.back()
            && valid_targets(targets, std::nullopt);
    }

    bool valid(
        const prepared_deck& prepared, const session_configuration& settings,
        const deck_configuration& configuration
    ) {
        const std::size_t card_limit
            = settings.source == quiz_source::physical_joker ? 54 : 52;
        const auto valid_cards = [card_limit](const auto& cards) {
            return !cards.empty()
                && std::all_of(
                    cards.begin(), cards.end(),
                    [card_limit](auto card) { return card < card_limit; }
                );
        };
        if (!valid_cards(prepared.cards)
            || configuration.infinite != prepared.infinite_cards.has_value()) {
            return false;
        }
        if (prepared.infinite_cards) {
            const auto& buffer = *prepared.infinite_cards;
            // Division avoids multiplying untrusted chunk geometry.
            if (buffer.decks_per_chunk == 0
                || prepared.cards.size() % card_limit != 0
                || prepared.cards.size() / card_limit != buffer.decks_per_chunk
                || buffer.lookahead.size() != prepared.cards.size()
                || !valid_cards(buffer.lookahead)) {
                return false;
            }
        }
        if (settings.source != quiz_source::virtual_interrupt
            || settings.scope != quiz_scope::single) {
            return prepared.virtual_quiz_targets.empty()
                && !prepared.quiz_continuation;
        }
        if (configuration.infinite) {
            return valid_page(
                prepared.virtual_quiz_targets, prepared.quiz_continuation
            );
        }
        return !prepared.quiz_continuation
            && valid_targets(
                prepared.virtual_quiz_targets, prepared.cards.size()
            );
    }

} // namespace session_setup

using namespace session_setup;

std::optional<session> session::create(
    const session_configuration& configuration,
    const std::vector<deck_configuration>& decks, slot_limits limits
) {
    if (!valid(configuration, decks.size(), limits)
        || !std::all_of(decks.begin(), decks.end(), [](const auto& deck) {
               return valid(deck);
           })) {
        return std::nullopt;
    }
    return session(configuration, decks, limits);
}

session::session(
    const session_configuration& configuration,
    const std::vector<deck_configuration>& initial_decks, slot_limits bounds
)
    : settings(configuration)
    , limits(bounds)
    , lives(configuration.initial_lives) {
    decks.reserve(initial_decks.size());
    deck_to_slot.reserve(initial_decks.size());
    slot_to_deck.reserve(initial_decks.size());
    for (std::size_t i = 0; i < initial_decks.size(); ++i) {
        deck_state state {};
        state.id = deck_id { i };
        state.configuration = initial_decks[i];
        decks.push_back(std::move(state));
        deck_to_slot.push_back(physical_slot_id { i });
        slot_to_deck.push_back(deck_id { i });
    }
}

const session_configuration& session::configuration() const { return settings; }

slot_limits session::slot_constraints() const { return limits; }

bool session::update_slot_limits(slot_limits bounds) {
    if (current_phase != session_phase::setup
        || !valid_slot_limits(bounds, decks.size())) {
        return false;
    }
    limits = bounds;
    return true;
}

bool session::configure(const session_configuration& configuration) {
    if (current_phase != session_phase::setup
        || !valid(configuration, decks.size(), limits)) {
        return false;
    }
    // Source/scope determine both physical cards and schedule ownership.
    if (configuration.source != settings.source
        || configuration.scope != settings.scope) {
        for (auto& deck : decks) {
            deck.stream = {};
            deck.running_count = 0;
        }
        multi_quiz_targets.clear();
        multi_quiz_continuation.reset();
        multi_schedule_prepared = false;
    }
    settings = configuration;
    lives = settings.initial_lives;
    return true;
}

bool session::configure_deck(
    deck_id id, const deck_configuration& configuration
) {
    if (current_phase != session_phase::setup || !deck(id)
        || !valid(configuration)) {
        return false;
    }
    auto& state = decks[id.value];
    if (state.configuration != configuration) {
        state.stream = {};
        state.running_count = 0;
        multi_quiz_targets.clear();
        multi_quiz_continuation.reset();
        multi_schedule_prepared = false;
    }
    state.configuration = configuration;
    if (!configuration.training) {
        state.show_count = false;
    }
    return true;
}

bool session::prepare_deck(deck_id id, const prepared_deck& prepared) {
    if (current_phase != session_phase::setup || !deck(id)
        || !valid(prepared, settings, decks[id.value].configuration)) {
        return false;
    }
    // Infinite preparation has multiple allocating buffers. Copy everything
    // before replacing the existing payload, as in whole-roster preparation.
    auto stream = prepared;
    decks[id.value].stream = std::move(stream);
    decks[id.value].running_count = prepared.initial_running_count;
    multi_quiz_targets.clear();
    multi_quiz_continuation.reset();
    multi_schedule_prepared = false;
    return true;
}

bool session::prepare_decks(
    std::span<const prepared_deck> prepared,
    std::span<const std::uint64_t> table_targets,
    std::optional<quiz_schedule_continuation> continuation
) {
    if (current_phase != session_phase::setup
        || prepared.size() != decks.size()) {
        return false;
    }
    std::size_t horizon = 0;
    bool infinite = false;
    for (std::size_t i = 0; i < prepared.size(); ++i) {
        const auto& stream = prepared[i];
        if (!valid(stream, settings, decks[i].configuration)) {
            return false;
        }
        horizon = std::max(horizon, stream.cards.size());
        infinite = infinite || decks[i].configuration.infinite;
    }
    const bool virtual_multi = settings.source == quiz_source::virtual_interrupt
        && settings.scope == quiz_scope::multi;
    if (!virtual_multi && (!table_targets.empty() || continuation)) {
        return false;
    }
    if (virtual_multi
        && (infinite ? !valid_page(table_targets, continuation)
                     : (continuation.has_value()
                        || !valid_targets(table_targets, horizon)))) {
        return false;
    }
    // Finish every potentially allocating copy before mutating the session.
    std::vector<prepared_deck> streams(prepared.begin(), prepared.end());
    std::vector<std::uint64_t> targets(
        table_targets.begin(), table_targets.end()
    );
    for (std::size_t i = 0; i < decks.size(); ++i) {
        decks[i].stream = std::move(streams[i]);
        decks[i].running_count = decks[i].stream.initial_running_count;
    }
    multi_quiz_targets = std::move(targets);
    multi_quiz_continuation = continuation;
    multi_schedule_prepared = virtual_multi;
    return true;
}

std::span<const std::uint64_t> session::table_quiz_targets() const {
    return multi_quiz_targets;
}

const std::optional<quiz_schedule_continuation>&
session::table_quiz_continuation() const {
    return multi_quiz_continuation;
}

bool session::multi_quiz_target_reached(std::size_t target_index) const {
    if (current_phase != session_phase::running
        || settings.source != quiz_source::virtual_interrupt
        || settings.scope != quiz_scope::multi || !multi_schedule_prepared
        || target_index >= multi_quiz_targets.size()) {
        return false;
    }
    return gameplay::multi_quiz_target_reached(
        decks, settings.dealing, multi_quiz_targets[target_index]
    );
}

bool session::start(std::uint64_t dealing_seed) {
    if (current_phase != session_phase::setup
        || !valid(settings, decks.size(), limits)
        || physical_order.size() != decks.size()
        || (settings.source == quiz_source::virtual_interrupt
            && settings.scope == quiz_scope::multi && !multi_schedule_prepared)
        || std::any_of(decks.begin(), decks.end(), [](const auto& deck) {
               return deck.stream.cards.empty();
           })) {
        return false;
    }
    random_dealing_state = { .seed = dealing_seed, .next_step = 0 };
    current_phase = session_phase::running;
    return true;
}

} // namespace gameplay
