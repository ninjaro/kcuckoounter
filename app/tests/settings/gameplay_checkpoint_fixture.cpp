#include "settings/gameplay_checkpoint_fixture.hpp"

#include <QJsonDocument>
#include <QtTest/QtTest>

#include <array>
#include <limits>
#include <utility>

namespace checkpoint_test {
using namespace gameplay;

namespace {

    session prepare(session_configuration settings, bool mixed) {
        std::vector<deck_configuration> configured {
            { "external_strategy", 1, false, false },
            { "hi_lo", 3, mixed, mixed },
            { "strategy_é", 5, mixed, true },
        };
        auto result = session::create(settings, configured, { 3, 64 }).value();
        generation_policy policy;
        policy.infinite_chunk_decks = 1;
        policy.minimum_quiz_gap = 1;
        policy.maximum_quiz_gap = 2;
        policy.quiz_page_targets = 3;
        std::vector<prepared_deck> streams;
        for (std::size_t i = 0; i < configured.size(); ++i) {
            prepared_deck stream;
            stream.rank_weights.fill(1);
            stream.initial_running_count = -7 + static_cast<std::int64_t>(i);
            const auto seed = UINT64_MAX - i;
            if (configured[i].infinite) {
                auto shoe = generate_infinite_shoe(
                                settings.source, seed, { i }, policy
                )
                                .value();
                stream.cards = std::move(shoe.cards);
                stream.infinite_cards = std::move(shoe.continuation);
            } else {
                stream.cards = { 0, 1, 2, 3, 4, 5, 6, 7 };
                if (settings.source == quiz_source::physical_joker)
                    stream.cards = { 0, 52, 1, 53, 2, 3, 4, 5 };
            }
            if (settings.source == quiz_source::virtual_interrupt
                && settings.scope == quiz_scope::single) {
                if (configured[i].infinite) {
                    const auto page = generate_quiz_page(
                                          { .seed = seed,
                                            .minimum_gap = 1,
                                            .maximum_gap = 2,
                                            .page_size = 3 },
                                          deck_id { i }, policy
                    )
                                          .value();
                    stream.virtual_quiz_targets = page.targets;
                    stream.quiz_continuation = page.continuation;
                } else {
                    stream.virtual_quiz_targets = { 1, 3, 5, 7 };
                }
            }
            streams.push_back(std::move(stream));
        }
        std::vector<std::uint64_t> targets;
        std::optional<quiz_schedule_continuation> continuation;
        if (settings.source == quiz_source::virtual_interrupt
            && settings.scope == quiz_scope::multi) {
            if (mixed) {
                const auto page = generate_quiz_page(
                                      { .seed = UINT64_MAX,
                                        .minimum_gap = 1,
                                        .maximum_gap = 2,
                                        .page_size = 3 },
                                      std::nullopt, policy
                )
                                      .value();
                targets = page.targets;
                continuation = page.continuation;
            } else {
                targets = { 1, 3, 5, 7 };
            }
        }
        if (!result.prepare_decks(streams, targets, continuation)
            || !result.set_traversal(std::array<std::size_t, 3> { 2, 0, 1 })
            || !result.set_show_count(deck_id { 2 }, true)
            || !result.start(UINT64_MAX))
            qFatal("Invalid checkpoint fixture");
        return result;
    }

} // namespace

session finite_session(session_configuration settings) {
    return prepare(settings, false);
}

session mixed_session(session_configuration settings) {
    return prepare(settings, true);
}

gameplay_checkpoint sample_checkpoint() {
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.scope = quiz_scope::multi;
    settings.dealing = dealing_mode::simultaneous;
    auto game = mixed_session(settings);
    for (std::size_t i = 0; !game.current_quiz_batch() && i < 3; ++i)
        (void)game.deal_step();
    if (!game.current_quiz_batch() || !game.pause())
        qFatal("Missing fixture quiz");
    return { capture_checkpoint(game).value(), 175 };
}

QByteArray document(const QJsonObject& root) {
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool continue_pair(session& first, session& second, std::size_t steps) {
    for (std::size_t tick = 0;
         tick < steps && first.phase() == session_phase::running; ++tick) {
        const auto a = first.advance_time(17 + tick);
        const auto b = second.advance_time(17 + tick);
        if (a.accepted != b.accepted || a.elapsed_ms != b.elapsed_ms
            || a.answers != b.answers)
            return false;
        const auto ca = first.deal_step();
        const auto cb = second.deal_step();
        if (ca.status != cb.status || ca.cards != cb.cards)
            return false;
        for (const auto id : first.unresolved_quizzes()) {
            const auto expected = first.deck(id)->quiz->expected_count;
            const auto input = (tick + id.value) % 3 == 1
                ? (expected == 0 ? 1 : 0)
                : expected;
            if (!first.edit_quiz_input(id, input)
                || !second.edit_quiz_input(id, input))
                return false;
            const bool skip = first.configuration().allow_skip
                && (tick + id.value) % 3 == 2;
            const auto qa = skip ? first.skip_quiz(id) : first.check_quiz(id);
            const auto qb = skip ? second.skip_quiz(id) : second.check_quiz(id);
            if (!qa || qa != qb)
                return false;
        }
        if (first.phase() != second.phase()
            || first.result() != second.result())
            return false;
        if (first.phase() == session_phase::running) {
            const auto state = capture_checkpoint(first);
            if (!state || state != capture_checkpoint(second))
                return false;
        }
    }
    return true;
}

} // namespace checkpoint_test
