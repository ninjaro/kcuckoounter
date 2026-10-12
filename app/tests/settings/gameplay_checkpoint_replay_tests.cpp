// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/gameplay_checkpoint_tests.hpp"

#include "settings/gameplay_checkpoint_fixture.hpp"

#include <QJsonDocument>
#include <QtTest/QtTest>

#include <array>
#include <limits>

using namespace gameplay;
using namespace checkpoint_test;

void gameplay_checkpoint_tests::finite_round_trip_data() {
    QTest::addColumn<int>("failure");
    QTest::addColumn<int>("source");
    QTest::addColumn<int>("scope");
    QTest::addColumn<int>("dealing");
    QTest::addColumn<bool>("pause");
    for (const int failure : { 0, 1 })
        for (const int source : { 0, 1 })
            for (const int scope : { 0, 1 })
                for (const int dealing : { 0, 1, 2 })
                    for (const bool pause : { false, true }) {
                        if (failure == 0 && !pause)
                            continue;
                        QTest::newRow(
                            qPrintable(QStringLiteral("%1/%2/%3/%4/%5")
                                           .arg(failure)
                                           .arg(source)
                                           .arg(scope)
                                           .arg(dealing)
                                           .arg(pause))
                        ) << failure
                          << source << scope << dealing << pause;
                    }
}

void gameplay_checkpoint_tests::finite_round_trip() {
    QFETCH(int, failure);
    QFETCH(int, source);
    QFETCH(int, scope);
    QFETCH(int, dealing);
    QFETCH(bool, pause);
    session_configuration settings { static_cast<failure_policy>(failure),
                                     12,
                                     static_cast<quiz_source>(source),
                                     static_cast<quiz_scope>(scope),
                                     pause,
                                     dealing != 1,
                                     static_cast<dealing_mode>(dealing),
                                     2,
                                     false };
    auto model = finite_session(settings);
    (void)model.advance_time(123);
    (void)model.deal_step();
    (void)model.deal_step();
    for (const auto id : model.unresolved_quizzes())
        QVERIFY(
            model.edit_quiz_input(id, id.value == 2 ? INT64_MAX : INT64_MIN)
        );
    QVERIFY(model.pause());
    QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 2 }));
    gameplay_checkpoint expected { capture_checkpoint(model).value(), 299 };
    const auto bytes = encode_gameplay_checkpoint(expected, { 1, 64 });
    QVERIFY(bytes);
    const auto decoded = decode_gameplay_checkpoint(*bytes, { 64, 64 });
    QVERIFY(decoded && *decoded == expected);
    QCOMPARE(*encode_gameplay_checkpoint(*decoded, { 3, 64 }), *bytes);
    auto restored = restore_checkpoint(
        decoded->state, { 1, 64 }, std::array<std::size_t, 3> { 2, 0, 1 }
    );
    QVERIFY(restored && restored->resume() && model.resume());
    QVERIFY(continue_pair(model, *restored, 80));
    QCOMPARE(restored->phase(), session_phase::finished);
    QVERIFY(restored->result() == model.result());
    const auto root = QJsonDocument::fromJson(*bytes).object();
    QCOMPARE(root.size(), 15);
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 2);
    QVERIFY(!root.contains(QStringLiteral("geometry")));
    QVERIFY(!root.contains(QStringLiteral("was_running")));
    QVERIFY(!root.contains(QStringLiteral("score_correct")));
}

void gameplay_checkpoint_tests::mixed_round_trip_data() {
    QTest::addColumn<int>("source");
    QTest::addColumn<int>("scope");
    QTest::addColumn<int>("dealing");
    for (const int source : { 0, 1 })
        for (const int scope : { 0, 1 })
            for (const int dealing : { 0, 1, 2 })
                QTest::newRow(
                    qPrintable(QStringLiteral("%1/%2/%3")
                                   .arg(source)
                                   .arg(scope)
                                   .arg(dealing))
                ) << source
                  << scope << dealing;
}

void gameplay_checkpoint_tests::mixed_round_trip() {
    QFETCH(int, source);
    QFETCH(int, scope);
    QFETCH(int, dealing);
    session_configuration settings;
    settings.failure = failure_policy::block;
    settings.source = static_cast<quiz_source>(source);
    settings.scope = static_cast<quiz_scope>(scope);
    settings.dealing = static_cast<dealing_mode>(dealing);
    settings.global_quiz_pause = false;
    settings.sequential_count = 2;
    auto model = mixed_session(settings);
    auto baseline = model;
    QVERIFY(continue_pair(model, baseline, 200));
    QVERIFY(model.deck(deck_id { 1 })->stream.infinite_cards->chunk > 0);
    QVERIFY(model.pause());
    QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 2 }));
    const gameplay_checkpoint expected { capture_checkpoint(model).value(), 7 };
    const auto bytes = encode_gameplay_checkpoint(expected, { 3, 64 });
    QVERIFY(bytes);
    generation_policy changed;
    changed.infinite_chunk_decks = 7;
    const auto decoded = decode_gameplay_checkpoint(*bytes, { 1, 64 }, changed);
    QVERIFY(decoded && *decoded == expected);
    auto restored = restore_checkpoint(
        decoded->state, { 1, 64 }, std::array<std::size_t, 3> { 2, 0, 1 },
        changed
    );
    QVERIFY(restored && restored->resume() && model.resume());
    QVERIFY(continue_pair(model, *restored, 200));
    QVERIFY(restored->finish() && model.finish());
    QVERIFY(restored->result() == model.result());
}

void gameplay_checkpoint_tests::full_width_values_round_trip() {
    auto expected = sample_checkpoint();
    auto& state = expected.state;
    state.random_dealing = { UINT64_MAX, UINT64_MAX };
    state.batch->id = UINT64_MAX;
    state.last_batch_id = UINT64_MAX;
    state.timing = { UINT64_MAX, UINT64_MAX };
    for (auto& deck : state.decks) {
        deck.stream.rank_weights.fill(0);
        const auto count = deck.id.value == 0 ? INT64_MIN : INT64_MAX;
        deck.stream.initial_running_count = count;
        deck.running_count = count;
        deck.latest_input = count == INT64_MIN ? INT64_MAX : INT64_MIN;
        deck.quiz->batch_id = UINT64_MAX;
        deck.quiz->expected_count = count;
        deck.quiz->input = deck.latest_input;
        deck.configuration.deck_count = std::numeric_limits<std::size_t>::max();
        if (deck.stream.infinite_cards) {
            deck.stream.infinite_cards->seed = UINT64_MAX;
            deck.stream.infinite_cards->chunk = UINT64_MAX;
        }
    }
    state.decks[0].statistics.errors = UINT64_MAX;
    state.challenge.errors = UINT64_MAX;
    state.decks[2].stream.rank_weights[11] = std::numeric_limits<int>::min();
    state.decks[2].stream.rank_weights[12] = std::numeric_limits<int>::max();
    state.table_schedule.targets
        = { UINT64_MAX - 2, UINT64_MAX - 1, UINT64_MAX };
    state.table_schedule.next_target = 0;
    auto& recipe = *state.table_schedule.continuation;
    recipe.seed = UINT64_MAX;
    recipe.next_page = UINT64_MAX;
    recipe.after_target = UINT64_MAX;
    recipe.minimum_gap = UINT32_MAX;
    recipe.maximum_gap = UINT32_MAX;
    const auto bytes = encode_gameplay_checkpoint(expected, { 3, 64 });
    QVERIFY(bytes);
    QVERIFY(bytes->contains("\"18446744073709551615\""));
    QVERIFY(bytes->contains("\"-9223372036854775808\""));
    const auto decoded = decode_gameplay_checkpoint(*bytes, { 3, 64 });
    QVERIFY(decoded && *decoded == expected);
    QCOMPARE(*encode_gameplay_checkpoint(*decoded, { 3, 64 }), *bytes);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
