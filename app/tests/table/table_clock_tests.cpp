// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "card_helpers/card_sheet.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "settings/training_progress.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/slot_settings.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"

#include <QtTest/QtTest>

#include <array>
#include <limits>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::gameplay_clock_requires_explicit_bound_start() {
    table view;
    view.resize(1200, 900);
    QVERIFY(!view.start_gameplay_runtime(7));
    QVERIFY(!view.pause_gameplay_runtime());
    QVERIFY(!view.resume_gameplay_runtime());
    QVERIFY(!view.finish_gameplay_runtime());
    QVERIFY(!view.set_gameplay_pick_interval(100));
    QVERIFY(view.install_gameplay_session(owned_clock_fixture()));
    auto* owner = view.active_gameplay_session();
    QVERIFY(!view.gameplay_timer.isActive());
    QVERIFY(!view.gameplay_elapsed.isValid());
    view.on_gameplay_clock_tick();
    QCOMPARE(owner->timing().play_time_ms, std::uint64_t { 0 });
    auto config = owner->deck({ 0 })->configuration;
    config.deck_count = 2;
    QVERIFY(owner->configure_deck({ 0 }, config));
    QVERIFY(!view.start_gameplay_runtime(7));
    QVERIFY(!view.gameplay_timer.isActive());
    QVERIFY(view.install_gameplay_session(owned_clock_fixture()));
    owner = view.active_gameplay_session();
    QSignalSpy changed(&view, &table::gameplay_runtime_updated);
    QVERIFY(view.start_gameplay_runtime(0x123456789ULL));
    QCOMPARE(owner->random_dealing().seed, std::uint64_t { 0x123456789ULL });
    QVERIFY(view.gameplay_timer.isActive());
    QVERIFY(view.gameplay_elapsed.isValid());
    QCOMPARE(changed.count(), 1);
    QVERIFY(!view.start_gameplay_runtime(9));
    // A rejected install cannot stop the currently active owner/timer.
    QVERIFY(!view.install_gameplay_session(nullptr));
    QCOMPARE(view.active_gameplay_session(), owner);
    QVERIFY(view.gameplay_timer.isActive());
    const auto before = *owner->deck({ 0 });
    view.on_clock_tick(9000, 9000);
    QCOMPARE(*owner->deck({ 0 }), before);
    QCOMPARE(owner->timing().play_time_ms, std::uint64_t { 0 });
    QVERIFY(view.capture_drill_settings().isEmpty());
    QCOMPARE(view.capture_session_state(), table_session_state {});
}

void table_tests::gameplay_clock_routes_dealing_modes_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("scope");
    for (int mode = 0; mode < 3; ++mode) {
        for (int scope = 0; scope < 2; ++scope) {
            QTest::newRow(qPrintable(
                QStringLiteral("mode-%1-scope-%2").arg(mode).arg(scope)
            )) << mode
               << scope;
        }
    }
}

void table_tests::gameplay_clock_routes_dealing_modes() {
    QFETCH(int, mode);
    QFETCH(int, scope);
    table view;
    view.resize(1200, 900);
    const auto dealing = static_cast<gameplay::dealing_mode>(mode);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, false, dealing, static_cast<gameplay::quiz_scope>(scope)
    )));
    QVERIFY(view.start_gameplay_runtime(77));
    view.stop_gameplay_clock(); // deterministic kernel, not a second engine
    auto& owner = *view.active_gameplay_session();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(299, answers));
    for (std::size_t id = 0; id < owner.size(); ++id)
        QCOMPARE(owner.deck({ id })->next_card, std::size_t { 0 });
    QVERIFY(view.advance_gameplay_runtime(1, answers));
    view.publish_gameplay_runtime(answers);
    std::size_t total = 0;
    for (std::size_t id = 0; id < owner.size(); ++id) {
        const auto* record = owner.deck({ id });
        total += record->next_card;
        if (record->next_card != 0)
            QVERIFY(owned_card(view.slot_widgets[id])->has_cards());
    }
    QCOMPARE(
        total,
        dealing == gameplay::dealing_mode::simultaneous
            ? std::size_t { 4 }
            : (dealing == gameplay::dealing_mode::sequential
                   ? std::size_t { 2 }
                   : std::size_t { 1 })
    );
    QCOMPARE(owner.timing().play_time_ms, std::uint64_t { 300 });
    QCOMPARE(
        owner.random_dealing().next_step,
        dealing == gameplay::dealing_mode::random ? std::uint64_t { 1 }
                                                  : std::uint64_t { 0 }
    );
    QVERIFY(answers.empty());
}

void table_tests::gameplay_clock_preserves_jitter_and_coalesces_stalls() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    QVERIFY(view.start_gameplay_runtime(1));
    view.stop_gameplay_clock();
    auto& owner = *view.active_gameplay_session();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(275, answers));
    QVERIFY(view.advance_gameplay_runtime(50, answers));
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 1 });
    QCOMPARE(view.gameplay_pick_elapsed_ms, std::uint64_t { 25 });
    QVERIFY(view.advance_gameplay_runtime(275, answers));
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 2 });
    QCOMPARE(owner.timing().play_time_ms, std::uint64_t { 600 });
    QVERIFY(view.advance_gameplay_runtime(5000, answers));
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 3 });
    QCOMPARE(owner.timing().play_time_ms, std::uint64_t { 5600 });
    QCOMPARE(
        owner.deck({ 0 })->statistics.dealing_time_ms, std::uint64_t { 5600 }
    );
    QCOMPARE(view.gameplay_pick_elapsed_ms, std::uint64_t { 0 });
    QVERIFY(view.advance_gameplay_runtime(200, answers));
    QVERIFY(view.pause_gameplay_runtime());
    QCOMPARE(view.gameplay_pick_elapsed_ms, std::uint64_t { 200 });
    QVERIFY(!view.advance_gameplay_runtime(9000, answers));
    QCOMPARE(owner.timing().play_time_ms, std::uint64_t { 5800 });
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QVERIFY(view.advance_gameplay_runtime(99, answers));
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 3 });
    QVERIFY(view.advance_gameplay_runtime(1, answers));
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 4 });
    QVERIFY(view.advance_gameplay_runtime(200, answers));
    QVERIFY(view.set_gameplay_pick_interval(100));
    QCOMPARE(view.gameplay_pick_elapsed_ms, std::uint64_t { 0 });
    QVERIFY(!view.set_gameplay_pick_interval(99));
    QVERIFY(!view.set_gameplay_pick_interval(1001));
    QVERIFY(view.advance_gameplay_runtime(99, answers));
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 4 });
    QVERIFY(view.advance_gameplay_runtime(1, answers));
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 5 });
}

void table_tests::gameplay_clock_samples_one_monotonic_timebase() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    QVERIFY(view.start_gameplay_runtime(15));
    view.gameplay_timer.stop(); // use the real stopwatch and actual tick path
    for (int sample = 0; sample < 8; ++sample) {
        QTest::qWait(1);
        view.on_gameplay_clock_tick();
        QCOMPARE(
            view.gameplay_owner->timing().play_time_ms,
            static_cast<std::uint64_t>(view.gameplay_sampled_ms)
        );
    }
    QVERIFY(view.gameplay_sampled_ms > 0);
    // Pause flushes the last sample before stopping; no background elapsed
    // time is replayed into the domain on Resume.
    QVERIFY(view.pause_gameplay_runtime());
    const auto paused = view.gameplay_owner->timing();
    QTest::qWait(2);
    view.on_gameplay_clock_tick();
    QCOMPARE(view.gameplay_owner->timing(), paused);
    QVERIFY(view.resume_gameplay_runtime());
    view.gameplay_timer.stop();
    view.on_gameplay_clock_tick();
    QCOMPARE(
        view.gameplay_owner->timing().play_time_ms - paused.play_time_ms,
        static_cast<std::uint64_t>(view.gameplay_sampled_ms)
    );
}

void table_tests::gameplay_clock_pause_preserves_batch_and_actions() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1, 4 }
    )));
    QVERIFY(view.start_gameplay_runtime(2));
    view.stop_gameplay_clock();
    auto& owner = *view.active_gameplay_session();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    std::array<table::gameplay_prompt_key, 4> prompts;
    for (std::size_t id = 0; id < prompts.size(); ++id)
        prompts[id] = *view.gameplay_quiz_prompt({ id });
    const auto expected = owner.deck({ 0 })->quiz->expected_count;
    QVERIFY(expected < std::numeric_limits<int>::min());
    QVERIFY(view.edit_gameplay_quiz(prompts[0], expected));
    QVERIFY(view.advance_gameplay_runtime(1250, answers));
    const auto frozen = *owner.current_quiz_batch();
    const auto record = *owner.deck({ 0 });
    QVERIFY(view.pause_gameplay_runtime());
    QVERIFY(!view.gameplay_timer.isActive());
    QVERIFY(!view.edit_gameplay_quiz(prompts[0], 2));
    QVERIFY(!view.check_gameplay_quiz(prompts[0]));
    QVERIFY(!view.skip_gameplay_quiz(prompts[0]));
    QVERIFY(!view.advance_gameplay_runtime(900000, answers));
    QCOMPARE(*owner.current_quiz_batch(), frozen);
    QCOMPARE(*owner.deck({ 0 }), record);
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QSignalSpy resolved(&view, &table::gameplay_quiz_resolved);
    QSignalSpy legacy_score(&view, &table::score_adjusted);
    QVERIFY(view.check_gameplay_quiz(prompts[0]));
    QCOMPARE(
        owner.current_quiz_batch()->remaining_ms,
        std::optional<std::uint64_t> { 78750 }
    );
    QVERIFY(!view.check_gameplay_quiz(prompts[0]));
    QVERIFY(view.edit_gameplay_quiz(prompts[1], -500));
    QVERIFY(view.check_gameplay_quiz(prompts[1]));
    QCOMPARE(owner.deck({ 1 })->status, gameplay::deck_status::failed);
    QVERIFY(view.skip_gameplay_quiz(prompts[2]));
    QCOMPARE(
        owner.current_quiz_batch()->remaining_ms,
        std::optional<std::uint64_t> { 93750 }
    );
    QVERIFY(view.edit_gameplay_quiz(prompts[3], 101));
    QVERIFY(view.check_gameplay_quiz(prompts[3]));
    QVERIFY(!owner.current_quiz_batch());
    QCOMPARE(resolved.count(), 4);
    const auto correction
        = qvariant_cast<gameplay::quiz_answer>(resolved[1][0]);
    QCOMPARE(correction.submitted_count, std::int64_t { -500 });
    QCOMPARE(correction.expected_count, std::int64_t { 101 });
    QCOMPARE(correction.outcome, gameplay::quiz_outcome::wrong);
    QCOMPARE(owner.statistics().verified_cards, std::uint64_t { 1 });
    QCOMPARE(owner.statistics().errors, std::uint64_t { 1 });
    QCOMPARE(owner.statistics().skips, std::uint64_t { 1 });
    QCOMPARE(owner.timing().play_time_ms, std::uint64_t { 1550 });
    QCOMPARE(owner.timing().quiz_time_ms, std::uint64_t { 1250 });
    QCOMPARE(legacy_score.count(), 0);
    QVERIFY(view.advance_gameplay_runtime(299, answers));
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 1 });
    QVERIFY(view.advance_gameplay_runtime(1, answers));
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 2 });
}

void table_tests::gameplay_clock_non_global_questions_keep_dealing() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, false, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1, 4 }, false
    )));
    QVERIFY(view.start_gameplay_runtime(3));
    view.stop_gameplay_clock();
    auto& owner = *view.active_gameplay_session();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    const auto prompt = *view.gameplay_quiz_prompt({ 0 });
    QVERIFY(!owner.current_quiz_batch()->remaining_ms);
    QVERIFY(!view.skip_gameplay_quiz(*view.gameplay_quiz_prompt({ 2 })));
    QVERIFY(view.advance_gameplay_runtime(1000, answers));
    QCOMPARE(owner.unresolved_quizzes().size(), std::size_t { 4 });
    QVERIFY(
        view.edit_gameplay_quiz(prompt, owner.deck({ 0 })->quiz->expected_count)
    );
    QVERIFY(view.check_gameplay_quiz(prompt));
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 2 });
    for (std::size_t id = 1; id < owner.size(); ++id) {
        QCOMPARE(owner.deck({ id })->next_card, std::size_t { 1 });
        QCOMPARE(
            owner.deck({ id })->statistics.answer_time_ms,
            std::uint64_t { 1300 }
        );
    }
    QCOMPARE(
        owner.deck({ 0 })->statistics.answer_time_ms, std::uint64_t { 1000 }
    );
    QCOMPARE(
        owner.deck({ 0 })->statistics.dealing_time_ms, std::uint64_t { 600 }
    );
    QCOMPARE(owner.timing().play_time_ms, std::uint64_t { 1600 });
    QCOMPARE(owner.timing().quiz_time_ms, std::uint64_t { 1300 });
    QVERIFY(answers.empty());
}

void table_tests::clock_prompt_keys_guard_batches_and_scenes() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, false, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1, 2, 4 }
    )));
    QVERIFY(view.start_gameplay_runtime(13));
    view.stop_gameplay_clock();
    auto& owner = *view.active_gameplay_session();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    const auto old_prompt = *view.gameplay_quiz_prompt({ 0 });
    QVERIFY(view.edit_gameplay_quiz(
        old_prompt, owner.deck({ 0 })->quiz->expected_count
    ));
    QVERIFY(view.check_gameplay_quiz(old_prompt));
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    const auto next_prompt = *view.gameplay_quiz_prompt({ 0 });
    QCOMPARE(next_prompt.batch_id, old_prompt.batch_id);
    QVERIFY(next_prompt.physical_cards != old_prompt.physical_cards);
    const auto record = *owner.deck({ 0 });
    QVERIFY(!view.edit_gameplay_quiz(old_prompt, 15));
    QVERIFY(!view.check_gameplay_quiz(old_prompt));
    QVERIFY(!view.skip_gameplay_quiz(old_prompt));
    QCOMPARE(*owner.deck({ 0 }), record);

    // A geometry/mapping edit does not create a new question or invalidate
    // its owner-bound observation key.
    QVERIFY(view.pause_gameplay_runtime());
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 3 }));
    QCOMPARE(view.gameplay_quiz_prompt({ 0 }), std::optional { next_prompt });
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QVERIFY(view.edit_gameplay_quiz(
        next_prompt, owner.deck({ 0 })->quiz->expected_count
    ));
    QVERIFY(view.check_gameplay_quiz(next_prompt));

    // A new owner may reproduce the same deck/batch/reveal numbers. The GUI
    // scene epoch still rejects the old handle rather than answering it.
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, false, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1 }
    )));
    QVERIFY(view.start_gameplay_runtime(14));
    view.stop_gameplay_clock();
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    const auto fresh = *view.gameplay_quiz_prompt({ 0 });
    QCOMPARE(fresh.batch_id, old_prompt.batch_id);
    QCOMPARE(fresh.physical_cards, old_prompt.physical_cards);
    QVERIFY(fresh.scene_revision != old_prompt.scene_revision);
    QVERIFY(!view.edit_gameplay_quiz(old_prompt, 15));
    QVERIFY(!view.check_gameplay_quiz(old_prompt));
    QVERIFY(!view.skip_gameplay_quiz(old_prompt));
    QCOMPARE(view.gameplay_owner->deck({ 0 })->quiz->input, std::int64_t { 0 });
    QVERIFY(view.edit_gameplay_quiz(
        fresh, view.gameplay_owner->deck({ 0 })->quiz->expected_count
    ));
    QVERIFY(view.check_gameplay_quiz(fresh));
}

void table_tests::clock_timeout_clips_and_rejects_stale_batches() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1, 2, 4 }
    )));
    QVERIFY(view.start_gameplay_runtime(4));
    view.stop_gameplay_clock();
    auto& owner = *view.active_gameplay_session();
    std::vector<gameplay::quiz_answer> answers;
    // A delayed delivery shows one new question now, not a retroactive quiz
    // already timed out before its view could have existed.
    QVERIFY(view.advance_gameplay_runtime(900000, answers));
    QVERIFY(answers.empty());
    QCOMPARE(owner.timing().play_time_ms, std::uint64_t { 900000 });
    QCOMPARE(owner.timing().quiz_time_ms, std::uint64_t { 0 });
    QCOMPARE(
        owner.current_quiz_batch()->remaining_ms,
        std::optional<std::uint64_t> { 60000 }
    );
    const auto expired_prompt = *view.gameplay_quiz_prompt({ 0 });
    // A later stalled delivery spends only the frozen deadline; no surplus
    // can deal unseen cards or open the next batch after timeout.
    QVERIFY(view.advance_gameplay_runtime(900000, answers));
    QCOMPARE(answers.size(), std::size_t { 4 });
    QCOMPARE(owner.timing().play_time_ms, std::uint64_t { 960000 });
    QCOMPARE(owner.timing().quiz_time_ms, std::uint64_t { 60000 });
    const auto expired = answers.front().batch_id;
    QSignalSpy resolved(&view, &table::gameplay_quiz_resolved);
    view.publish_gameplay_runtime(answers);
    QCOMPARE(resolved.count(), 4);
    for (const auto& answer : answers) {
        QVERIFY(answer.timed_out);
        QCOMPARE(answer.submitted_count, std::int64_t { 0 });
        QCOMPARE(answer.outcome, gameplay::quiz_outcome::wrong);
    }
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 1 });
    QVERIFY(!owner.current_quiz_batch());
    answers.clear();
    QVERIFY(view.advance_gameplay_runtime(0, answers));
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 1 });
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    const auto replacement = *owner.deck({ 0 })->quiz;
    QVERIFY(replacement.batch_id != expired);
    QCOMPARE(replacement.input, std::int64_t { 0 });
    QVERIFY(!view.edit_gameplay_quiz(expired_prompt, 99));
    QVERIFY(!view.check_gameplay_quiz(expired_prompt));
    QVERIFY(!view.skip_gameplay_quiz(expired_prompt));
    QCOMPARE(*owner.deck({ 0 })->quiz, replacement);
    QCOMPARE(
        owner.current_quiz_batch()->remaining_ms,
        std::optional<std::uint64_t> { 60000 }
    );
    QCOMPARE(resolved.count(), 4);
}

void table_tests::gameplay_clock_settles_deadline_before_answer() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        1, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1, 3 }
    )));
    QVERIFY(view.start_gameplay_runtime(5));
    view.stop_gameplay_clock();
    auto& owner = *view.active_gameplay_session();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    const auto question = *owner.deck({ 0 })->quiz;
    const auto prompt = *view.gameplay_quiz_prompt({ 0 });
    QVERIFY(view.edit_gameplay_quiz(prompt, question.expected_count));
    QVERIFY(view.advance_gameplay_runtime(59999, answers));
    // Only this deadline-order proof needs real monotonic sampling (2ms),
    // never a minute-long countdown or second elapsed-time implementation.
    view.gameplay_elapsed.start();
    QTest::qWait(2);
    QSignalSpy resolved(&view, &table::gameplay_quiz_resolved);
    QVERIFY(!view.check_gameplay_quiz(prompt));
    QCOMPARE(resolved.count(), 1);
    const auto result = qvariant_cast<gameplay::quiz_answer>(resolved[0][0]);
    QVERIFY(result.timed_out);
    QCOMPARE(result.outcome, gameplay::quiz_outcome::correct);
    QCOMPARE(result.submitted_count, question.expected_count);
    QCOMPARE(owner.timing().play_time_ms, std::uint64_t { 60300 });
    QCOMPARE(owner.deck({ 0 })->statistics.errors, std::uint64_t { 0 });
    QVERIFY(!owner.deck({ 0 })->quiz);
}

void table_tests::gameplay_clock_rejections_pause_and_diagnose() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    auto prepared = view.gameplay_owner->deck({ 0 })->stream;
    prepared.initial_running_count = std::numeric_limits<std::int64_t>::max();
    QVERIFY(view.gameplay_owner->prepare_deck({ 0 }, prepared));
    QVERIFY(view.start_gameplay_runtime(6));
    view.stop_gameplay_clock();
    QSignalSpy failed(&view, &table::gameplay_runtime_failed);
    QSignalSpy legacy_ended(&view, &table::game_over);
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(!view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(view.gameplay_owner->phase(), gameplay::session_phase::paused);
    QCOMPARE(view.gameplay_owner->deck({ 0 })->next_card, std::size_t { 0 });
    QCOMPARE(failed.count(), 1);
    QVERIFY(!failed[0][0].toString().isEmpty());
    QVERIFY(!view.gameplay_timer.isActive());
    QVERIFY(!view.gameplay_elapsed.isValid());
    view.publish_gameplay_runtime({});
    QCOMPARE(failed.count(), 1); // not a repeated error every UI refresh
    QCOMPARE(legacy_ended.count(), 0);

    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    QVERIFY(view.start_gameplay_runtime(7));
    view.stop_gameplay_clock();
    QVERIFY(view.advance_gameplay_runtime(
        std::numeric_limits<std::uint64_t>::max(), answers
    ));
    QCOMPARE(
        view.gameplay_owner->timing().play_time_ms,
        std::numeric_limits<std::uint64_t>::max()
    );
    const auto record = *view.gameplay_owner->deck({ 0 });
    QVERIFY(!view.advance_gameplay_runtime(1, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(*view.gameplay_owner->deck({ 0 }), record);
    QCOMPARE(view.gameplay_owner->phase(), gameplay::session_phase::paused);
    QCOMPARE(failed.count(), 2);
    QVERIFY(view.finish_gameplay_runtime());
    QCOMPARE(
        view.gameplay_owner->result()->timing.play_time_ms,
        std::numeric_limits<std::uint64_t>::max()
    );
}

void table_tests::gameplay_clock_replacement_retires_timer_and_notifications() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1 }
    )));
    QVERIFY(view.start_gameplay_runtime(8));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    QVERIFY(view.advance_gameplay_runtime(60000, answers));
    QCOMPARE(answers.size(), std::size_t { 4 });
    QPointer<table_slot> old_slot = view.slot_widgets.front();
    QSignalSpy updated(&view, &table::gameplay_runtime_updated);
    int notifications = 0;
    bool replaced = false;
    connect(&view, &table::gameplay_quiz_resolved, &view, [&](const auto&) {
        ++notifications;
        replaced = view.install_gameplay_session(owned_clock_fixture(2));
    });
    view.publish_gameplay_runtime(answers);
    QVERIFY(replaced);
    QVERIFY(old_slot.isNull());
    QCOMPARE(notifications, 1); // old batch cannot leak into the new scene
    QCOMPARE(updated.count(), 0);
    QVERIFY(!view.gameplay_runtime_attached);
    QVERIFY(!view.gameplay_timer.isActive());
    QCOMPARE(view.gameplay_owner->phase(), gameplay::session_phase::setup);
    QVERIFY(view.start_gameplay_runtime(9));
    QVERIFY(view.gameplay_timer.isActive());
    view.clear_gameplay_session();
    QVERIFY(!view.gameplay_runtime_attached);
    QVERIFY(!view.gameplay_timer.isActive());
    QVERIFY(!view.gameplay_elapsed.isValid());
    view.on_gameplay_clock_tick();
    view.set_slot_count(1);
    view.start_quiz(0, false);
    view.on_clock_tick(300, 300);
    QVERIFY(view.capture_session_state().quiz_running);
    QVERIFY(view.slot_widgets.front()->has_cards());
}

void table_tests::gameplay_clock_finish_stops_without_replaying_results() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    QVERIFY(view.start_gameplay_runtime(10));
    view.stop_gameplay_clock();
    auto& owner = *view.active_gameplay_session();
    std::vector<gameplay::quiz_answer> answers;
    for (int step = 0; step < 8; ++step)
        QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(owner.phase(), gameplay::session_phase::finished);
    QCOMPARE(
        owner.result()->reason, gameplay::session_end_reason::no_live_decks
    );
    QCOMPARE(owner.result()->timing.play_time_ms, std::uint64_t { 2400 });
    QVERIFY(!view.gameplay_timer.isActive());
    const auto result = *owner.result();
    QVERIFY(!view.finish_gameplay_runtime());
    QVERIFY(!view.resume_gameplay_runtime());
    QVERIFY(!view.pause_gameplay_runtime());
    QVERIFY(!view.start_gameplay_runtime(11));
    QVERIFY(!view.set_gameplay_pick_interval(100));
    QVERIFY(!view.advance_gameplay_runtime(9000, answers));
    QCOMPARE(*owner.result(), result);

    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        1, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1 }
    )));
    QVERIFY(view.start_gameplay_runtime(12));
    view.stop_gameplay_clock();
    QVERIFY(view.advance_gameplay_runtime(1000, answers));
    const auto pending = *view.gameplay_owner->deck({ 0 })->quiz;
    QVERIFY(view.finish_gameplay_runtime());
    QCOMPARE(
        view.gameplay_owner->result()->reason,
        gameplay::session_end_reason::manual_finish
    );
    QCOMPARE(
        view.gameplay_owner->result()->timing.play_time_ms,
        std::uint64_t { 1000 }
    );
    QCOMPARE(*view.gameplay_owner->deck({ 0 })->quiz, pending);
    QCOMPARE(
        view.gameplay_owner->deck({ 0 })->statistics.skips, std::uint64_t { 0 }
    );
    QVERIFY(!view.gameplay_timer.isActive());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
