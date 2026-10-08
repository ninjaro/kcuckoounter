// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/gameplay_progress_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"
#include "table/gameplay_result.hpp"

#include <QtTest/QtTest>
#include <limits>

using namespace gameplay;
using namespace progress_test;

void gameplay_progress_tests::result_round_trip_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<bool>("multi");
    QTest::addColumn<bool>("physical");
    QTest::addColumn<bool>("block");
    QTest::addColumn<bool>("infinite");
    QTest::addColumn<bool>("frozen");
    for (int mode = 0; mode < 3; ++mode)
        for (bool multi : { false, true })
            for (bool physical : { false, true })
                for (bool block : { false, true })
                    for (bool infinite : { false, true })
                        for (bool frozen : { false, true }) {
                            if (!block && !frozen)
                                continue;
                            const auto name
                                = QStringLiteral("%1-%2-%3-%4-%5-%6")
                                      .arg(mode)
                                      .arg(multi)
                                      .arg(physical)
                                      .arg(block)
                                      .arg(infinite)
                                      .arg(frozen);
                            QTest::newRow(name.toUtf8().constData())
                                << mode << multi << physical << block
                                << infinite << frozen;
                        }
}

void gameplay_progress_tests::result_round_trip() {
    QFETCH(int, mode);
    QFETCH(bool, multi);
    QFETCH(bool, physical);
    QFETCH(bool, block);
    QFETCH(bool, infinite);
    QFETCH(bool, frozen);
    session_configuration config;
    config.dealing = static_cast<dealing_mode>(mode);
    config.sequential_count = 2;
    config.scope = multi ? quiz_scope::multi : quiz_scope::single;
    config.source = physical ? quiz_source::physical_joker
                             : quiz_source::virtual_interrupt;
    config.failure = block ? failure_policy::block : failure_policy::lives;
    config.initial_lives = 12;
    config.global_quiz_pause = frozen;
    auto model = ready_session(config, infinite);
    for (int i = 0; i < 12; ++i) {
        QVERIFY(model.advance_time(17).accepted);
        const auto step = model.deal_step();
        QVERIFY(
            step.status == dealing_step_status::advanced
            || step.status == dealing_step_status::quiz_blocked
        );
        for (const auto id : model.unresolved_quizzes()) {
            QVERIFY(model.advance_time(9).accepted);
            if (id.value == 2) {
                QVERIFY(
                    model.edit_quiz_input(id, model.deck(id)->running_count)
                );
                QVERIFY(model.check_quiz(id));
            } else if (i % 2 == 0) {
                QVERIFY(model.skip_quiz(id));
            } else {
                QVERIFY(model.check_quiz(id));
            }
        }
    }
    QVERIFY(model.complete_deck(deck_id { 1 }));
    QVERIFY(model.pause());
    QVERIFY(model.finish());
    QVERIFY(model.result());
    const auto expected = progress_for(*model.result());
    QVERIFY(valid_result(expected.recent_results.front().result, { 1, 64 }));
    const auto bytes = encode_gameplay_progress(expected, { 1, 64 });
    QVERIFY(bytes);
    const auto restored = decode_gameplay_progress(*bytes, { 2, 64 });
    QVERIFY(restored && *restored == expected);
    QCOMPARE(restored->recent_results.front().result.decks[0].slot.value, 2U);
    QCOMPARE(
        restored->recent_results.front()
            .result.decks[1]
            .statistics.verified_cards,
        0U
    );
    QCOMPARE(restored->recent_results.front().result.configuration, config);
}

void gameplay_progress_tests::automatic_end_results() {
    session_configuration simultaneous;
    simultaneous.dealing = dealing_mode::simultaneous;
    auto model = ready_session(simultaneous);
    for (int i = 0; i < 60 && model.phase() == session_phase::running; ++i) {
        QVERIFY(model.advance_time(1).accepted);
        QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
        for (const auto id : model.unresolved_quizzes()) {
            QVERIFY(model.edit_quiz_input(id, model.deck(id)->running_count));
            QVERIFY(model.check_quiz(id));
        }
    }
    QCOMPARE(model.phase(), session_phase::finished);
    QCOMPARE(model.result()->reason, session_end_reason::no_live_decks);
    const auto natural = progress_for(*model.result());
    QVERIFY(
        decode_gameplay_progress(
            encode_gameplay_progress(natural, { 3, 64 }).value(), { 3, 64 }
        )
        == natural
    );

    session_configuration config;
    config.initial_lives = 1;
    config.scope = quiz_scope::multi;
    config.dealing = dealing_mode::simultaneous;
    auto lives = ready_session(config);
    QVERIFY(lives.deal_step().status == dealing_step_status::advanced);
    QVERIFY(lives.deal_step().status == dealing_step_status::advanced);
    for (const auto id : lives.unresolved_quizzes())
        QVERIFY(lives.check_quiz(id));
    QCOMPARE(lives.result()->reason, session_end_reason::lives_exhausted);
    QCOMPARE(lives.result()->challenge.errors, 2U);
    QCOMPARE(lives.result()->decks[1].statistics.errors, 1U);
    const auto ended = progress_for(*lives.result());
    QVERIFY(
        decode_gameplay_progress(
            encode_gameplay_progress(ended, { 3, 64 }).value(), { 3, 64 }
        )
        == ended
    );
}

void gameplay_progress_tests::exact_integer_round_trip() {
    auto result = sample_result();
    auto& deck = result.decks[0];
    deck.initial_running_count = std::numeric_limits<std::int64_t>::min();
    deck.final_running_count = std::numeric_limits<std::int64_t>::max();
    deck.rank_weights[0] = std::numeric_limits<int>::min();
    deck.rank_weights[1] = std::numeric_limits<int>::max();
    deck.configuration.infinite = true;
    deck.infinite_position
        = infinite_stream_position { std::numeric_limits<std::uint64_t>::max(),
                                     std::numeric_limits<std::uint64_t>::max(),
                                     1 };
    deck.schedule.continuation = quiz_schedule_continuation {
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint64_t>::max(),
        4,
        1,
        std::numeric_limits<std::uint32_t>::max(),
        2
    };
    deck.dealt_physical_cards = std::numeric_limits<std::uint64_t>::max();
    deck.statistics.dealt_normal_cards = deck.dealt_physical_cards;
    deck.statistics.verified_cards = deck.dealt_physical_cards - 1;
    deck.unverified_normal_cards = 1;
    result.challenge = { deck.statistics.dealt_normal_cards,
                         deck.statistics.verified_cards, 0, 0 };
    result.random_dealing = { std::numeric_limits<std::uint64_t>::max(),
                              std::numeric_limits<std::uint64_t>::max() };
    result.timing = { std::numeric_limits<std::uint64_t>::max(),
                      std::numeric_limits<std::uint64_t>::max() - 1 };
    auto expected = progress_for(result);
    expected.recent_results.front().completed_at_utc_ms
        = std::numeric_limits<std::int64_t>::max();
    const auto bytes = encode_gameplay_progress(expected, { 3, 64 });
    QVERIFY(bytes);
    QVERIFY(decode_gameplay_progress(*bytes, { 3, 64 }) == expected);
}

void gameplay_progress_tests::result_invariants() {
    const auto good = sample_result();
    const auto reject = [](const session_result& value) {
        return !valid_result(value, { 3, 64 })
            && !encode_gameplay_progress(progress_for(value), { 3, 64 });
    };
    auto bad = good;
    bad.decks[0].slot = bad.decks[1].slot;
    QVERIFY(reject(bad));
    bad = good;
    bad.decks[1].id.value = 0;
    QVERIFY(reject(bad));
    bad = good;
    bad.challenge.errors = 1;
    QVERIFY(reject(bad));
    bad = good;
    bad.remaining_lives = 2;
    QVERIFY(reject(bad));
    bad = good;
    bad.decks[1].statistics.verified_cards = 1;
    QVERIFY(reject(bad));
    bad = good;
    bad.decks[0].unverified_normal_cards = 1;
    QVERIFY(reject(bad));
    bad = good;
    bad.decks[0].statistics.answer_time_ms = 1;
    QVERIFY(reject(bad));
    bad = good;
    bad.decks[0].statistics.dealing_time_ms = 18;
    QVERIFY(reject(bad));
    bad = good;
    bad.active_decks = 2;
    QVERIFY(reject(bad));
    bad = good;
    bad.reason = session_end_reason::no_live_decks;
    QVERIFY(reject(bad));
    bad = good;
    bad.reason = session_end_reason::lives_exhausted;
    QVERIFY(reject(bad));
    bad = good;
    bad.decks[0].status = deck_status::failed;
    QVERIFY(reject(bad));
    bad = good;
    bad.decks[0].schedule.next_target = 3;
    QVERIFY(reject(bad));
    bad = good;
    bad.decks[0].schedule.targets = { 2, 2 };
    QVERIFY(reject(bad));
    bad = good;
    bad.table_schedule.targets = { 1 };
    QVERIFY(reject(bad));
    bad = good;
    bad.decks[0].infinite_position = infinite_stream_position {};
    QVERIFY(reject(bad));
    QVERIFY(!valid_result(good, { 1, 2 }));
    QVERIFY(!valid_result(good, { 3, 64 }, 3));
    QVERIFY(valid_result(good, { 1, 64 }, 6));
    bad = good;
    for (const auto i : { 0U, 2U }) {
        bad.decks[i].dealt_physical_cards
            = std::numeric_limits<std::uint64_t>::max();
        bad.decks[i].statistics.dealt_normal_cards
            = std::numeric_limits<std::uint64_t>::max();
    }
    QVERIFY(reject(bad));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
