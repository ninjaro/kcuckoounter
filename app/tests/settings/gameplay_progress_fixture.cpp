#include "settings/gameplay_progress_fixture.hpp"

#include "table/gameplay_generation.hpp"

#include <QJsonDocument>
#include <array>
#include <utility>

namespace progress_test {

gameplay::session
ready_session(gameplay::session_configuration settings, bool infinite) {
    using namespace gameplay;
    std::vector<deck_configuration> decks { { "hi_lo", 1, false, false },
                                            { "training", 1, false, true },
                                            { "infinite", 1, infinite,
                                              false } };
    auto model = session::create(settings, decks, { 3, 64 }).value();
    generation_policy policy;
    policy.minimum_quiz_gap = 2;
    policy.maximum_quiz_gap = 2;
    policy.quiz_page_targets = 3;
    policy.infinite_chunk_decks = 1;
    std::vector<prepared_deck> streams;
    for (std::size_t i = 0; i < decks.size(); ++i) {
        prepared_deck prepared;
        prepared.rank_weights.fill(1);
        prepared.initial_running_count = 7;
        const bool physical = settings.source == quiz_source::physical_joker;
        prepared.cards.assign(physical ? 54 : 52, 0);
        if (physical)
            prepared.cards[1] = 52;
        if (decks[i].infinite)
            prepared.infinite_cards
                = infinite_card_buffer { 99, 2, 1, prepared.cards };
        if (!physical && settings.scope == quiz_scope::single) {
            if (decks[i].infinite) {
                auto page = generate_quiz_page(
                                { .seed = 77,
                                  .minimum_gap = 2,
                                  .maximum_gap = 2,
                                  .page_size = 3 },
                                deck_id { i }, policy
                )
                                .value();
                prepared.virtual_quiz_targets = std::move(page.targets);
                prepared.quiz_continuation = page.continuation;
            } else {
                prepared.virtual_quiz_targets = { 2, 4 };
            }
        }
        streams.push_back(std::move(prepared));
    }
    std::vector<std::uint64_t> targets;
    std::optional<quiz_schedule_continuation> continuation;
    if (settings.source == quiz_source::virtual_interrupt
        && settings.scope == quiz_scope::multi) {
        if (infinite) {
            auto page = generate_quiz_page(
                            { .seed = 12,
                              .minimum_gap = 2,
                              .maximum_gap = 2,
                              .page_size = 3 },
                            std::nullopt, policy
            )
                            .value();
            targets = std::move(page.targets);
            continuation = page.continuation;
        } else {
            targets = { 2, 4 };
        }
    }
    if (!model.prepare_decks(streams, targets, continuation)
        || !model.set_traversal(std::array<std::size_t, 3> { 2, 0, 1 })
        || !model.start() || !model.pause()
        || !model.swap_decks(deck_id { 0 }, deck_id { 2 }) || !model.resume())
        qFatal("Invalid result fixture");
    return model;
}

gameplay::session_result sample_result() {
    auto model = ready_session();
    if (!model.advance_time(17).accepted || !model.finish())
        qFatal("Could not finish result fixture");
    return *model.result();
}

gameplay_progress progress_for(const gameplay::session_result& result) {
    return {
        1, result.challenge, result.timing, { { 1, 1'700'000'000'000, result } }
    };
}

QByteArray document(const QJsonObject& root) {
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace progress_test
