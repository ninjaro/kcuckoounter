#include "table/gameplay_generation.hpp"

#include <algorithm>
#include <limits>
#include <random>

namespace gameplay {
namespace {

    enum class random_domain : std::uint32_t {
        cards = 1,
        single_quizzes = 2,
        multi_quizzes = 3,
        infinite_cards = 4,
        infinite_single_quizzes = 5,
        infinite_multi_quizzes = 6
    };

    std::mt19937_64 engine_for(
        std::uint64_t seed, random_domain domain, std::optional<deck_id> owner,
        std::optional<std::uint64_t> ordinal = std::nullopt
    ) {
        const auto identity
            = owner ? static_cast<std::uint64_t>(owner->value) : 0;
        const std::array<std::uint32_t, 7> words {
            static_cast<std::uint32_t>(seed),
            static_cast<std::uint32_t>(seed >> 32),
            static_cast<std::uint32_t>(domain),
            static_cast<std::uint32_t>(identity),
            static_cast<std::uint32_t>(identity >> 32),
            static_cast<std::uint32_t>(ordinal.value_or(0)),
            static_cast<std::uint32_t>(ordinal.value_or(0) >> 32),
        };
        // Preserve the original five-word recipe for all finite outputs.
        std::seed_seq sequence(
            words.begin(), words.begin() + (ordinal ? 7 : 5)
        );
        return std::mt19937_64(sequence);
    }

    // Explicit rejection sampling fixes draw consumption/ordering rather than
    // depending on a standard library's distribution or shuffle algorithm.
    std::uint64_t bounded(std::mt19937_64& engine, std::uint64_t bound) {
        const auto threshold = (std::uint64_t { 0 } - bound) % bound;
        auto value = engine();
        while (value < threshold) {
            value = engine();
        }
        return value % bound;
    }

    std::optional<std::size_t> faces_for(quiz_source source) {
        if (source == quiz_source::physical_joker) {
            return 54;
        }
        if (source == quiz_source::virtual_interrupt) {
            return 52;
        }
        return std::nullopt;
    }

    std::vector<std::uint8_t> shuffled_cards(
        std::size_t decks, std::size_t faces, std::mt19937_64 engine
    ) {
        std::vector<std::uint8_t> cards;
        cards.reserve(decks * faces);
        for (std::size_t deck = 0; deck < decks; ++deck) {
            for (std::size_t face = 0; face < faces; ++face) {
                cards.push_back(static_cast<std::uint8_t>(face));
            }
        }
        for (auto remaining = cards.size(); remaining > 1; --remaining) {
            const auto index = static_cast<std::size_t>(
                bounded(engine, static_cast<std::uint64_t>(remaining))
            );
            std::swap(cards[remaining - 1], cards[index]);
        }
        return cards;
    }

} // namespace

bool generation_policy::valid() const {
    return minimum_quiz_gap > 0 && maximum_quiz_gap >= minimum_quiz_gap
        && maximum_cards > 0 && maximum_targets > 0 && infinite_chunk_decks > 0
        && quiz_page_targets > 0;
}

std::optional<std::size_t> normal_card_rank(std::uint8_t face) {
    if (face >= 52) {
        return std::nullopt;
    }
    return face % 13;
}

std::optional<std::vector<std::uint8_t>> generate_shoe(
    quiz_source source, std::size_t deck_count, std::uint64_t seed,
    deck_id owner, const generation_policy& policy
) {
    const auto faces = faces_for(source);
    if (!policy.valid() || !faces) {
        return std::nullopt;
    }
    const auto face_count = *faces;
    if (deck_count == 0 || deck_count > policy.maximum_cards / face_count) {
        return std::nullopt;
    }
    return shuffled_cards(
        deck_count, face_count, engine_for(seed, random_domain::cards, owner)
    );
}

std::optional<std::vector<std::uint64_t>> generate_quiz_targets(
    std::uint64_t exclusive_horizon, std::uint64_t seed,
    std::optional<deck_id> owner, const generation_policy& policy
) {
    if (!policy.valid() || exclusive_horizon == 0
        || exclusive_horizon > policy.maximum_cards) {
        return std::nullopt;
    }
    const auto maximum_count
        = (exclusive_horizon - 1) / policy.minimum_quiz_gap;
    if (maximum_count > policy.maximum_targets) {
        return std::nullopt;
    }
    std::vector<std::uint64_t> targets;
    targets.reserve(static_cast<std::size_t>(maximum_count));
    auto engine = engine_for(
        seed,
        owner ? random_domain::single_quizzes : random_domain::multi_quizzes,
        owner
    );
    const auto range = std::uint64_t { policy.maximum_quiz_gap }
        - policy.minimum_quiz_gap + 1;
    std::uint64_t position = 0;
    while (true) {
        const auto gap = policy.minimum_quiz_gap + bounded(engine, range);
        // Comparing before addition avoids wrap and excludes the shoe end.
        if (gap >= exclusive_horizon - position) {
            break;
        }
        position += gap;
        targets.push_back(position);
    }
    return targets;
}

std::optional<generated_infinite_shoe> generate_infinite_shoe(
    quiz_source source, std::uint64_t seed, deck_id owner,
    const generation_policy& policy
) {
    const auto faces = faces_for(source);
    if (!policy.valid() || !faces
        || policy.infinite_chunk_decks > policy.maximum_cards / 2 / *faces) {
        return std::nullopt;
    }
    return generated_infinite_shoe {
        .cards = shuffled_cards(
            policy.infinite_chunk_decks, *faces,
            engine_for(seed, random_domain::infinite_cards, owner, 0)
        ),
        .continuation = {
            .seed = seed,
            .chunk = 0,
            .decks_per_chunk = policy.infinite_chunk_decks,
            .lookahead = shuffled_cards(
                policy.infinite_chunk_decks, *faces,
                engine_for(seed, random_domain::infinite_cards, owner, 1)
            ),
        },
    };
}

bool rollover_infinite_shoe(
    deck_state& deck, quiz_source source, const generation_policy& policy
) {
    const auto faces = faces_for(source);
    if (!policy.valid() || !faces || !deck.configuration.infinite
        || deck.status != deck_status::active || !deck.stream.infinite_cards
        || deck.next_card != deck.stream.cards.size()) {
        return false;
    }
    auto& continuation = *deck.stream.infinite_cards;
    if (continuation.decks_per_chunk == 0
        || continuation.decks_per_chunk > policy.maximum_cards / 2 / *faces
        || continuation.chunk > std::numeric_limits<std::uint64_t>::max() - 2) {
        return false;
    }
    const auto count = continuation.decks_per_chunk * *faces;
    if (deck.stream.cards.size() != count
        || continuation.lookahead.size() != count) {
        return false;
    }
    auto lookahead = shuffled_cards(
        continuation.decks_per_chunk, *faces,
        engine_for(
            continuation.seed, random_domain::infinite_cards, deck.id,
            continuation.chunk + 2
        )
    );
    // Moves are non-allocating; all preparation succeeded before mutation.
    deck.stream.cards = std::move(continuation.lookahead);
    continuation.lookahead = std::move(lookahead);
    ++continuation.chunk;
    deck.next_card = 0;
    return true;
}

std::optional<generated_quiz_page> generate_quiz_page(
    const quiz_schedule_continuation& continuation,
    std::optional<deck_id> owner, const generation_policy& policy
) {
    const auto limit = std::numeric_limits<std::uint64_t>::max();
    if (!policy.valid() || continuation.minimum_gap == 0
        || continuation.maximum_gap < continuation.minimum_gap
        || continuation.page_size == 0
        || continuation.page_size > policy.maximum_targets
        || continuation.next_page == limit
        || continuation.page_size
            > (limit - continuation.after_target) / continuation.maximum_gap) {
        return std::nullopt;
    }
    generated_quiz_page page { .targets = {}, .continuation = continuation };
    page.targets.reserve(continuation.page_size);
    auto engine = engine_for(
        continuation.seed,
        owner ? random_domain::infinite_single_quizzes
              : random_domain::infinite_multi_quizzes,
        owner, continuation.next_page
    );
    const auto range = std::uint64_t { continuation.maximum_gap }
        - continuation.minimum_gap + 1;
    for (std::size_t i = 0; i < continuation.page_size; ++i) {
        page.continuation.after_target
            += continuation.minimum_gap + bounded(engine, range);
        page.targets.push_back(page.continuation.after_target);
    }
    ++page.continuation.next_page;
    return page;
}

} // namespace gameplay
