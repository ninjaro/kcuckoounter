// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "table/gameplay_setup.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QLineEdit>
#include <QtTest/QtTest>

#include <numeric>

using namespace table_test;

void table_tests::checkpoint_restore_preflight_is_atomic() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture()));
    QVERIFY(view.start_gameplay_runtime(11));
    const auto saved = view.capture_gameplay_checkpoint().value();
    auto* owner = view.active_gameplay_session();
    QPointer<table_slot> first = view.slot_widgets.front();
    const auto before = gameplay::capture_checkpoint(*owner).value();
    QSignalSpy changed(&view, &table::gameplay_session_changed);
    for (int fault = 0; fault < 5; ++fault) {
        auto invalid = saved;
        switch (fault) {
        case 0:
            invalid.state.decks.clear();
            break;
        case 1:
            ++invalid.state.continuation_revision;
            break;
        case 2:
            invalid.state.deck_to_slot[1] = invalid.state.deck_to_slot[0];
            break;
        case 3:
            invalid.pickup_elapsed_ms = 300;
            break;
        case 4:
            invalid.state.pickup_interval_ms = -1;
            break;
        }
        QVERIFY(!view.restore_gameplay_checkpoint(invalid));
        QCOMPARE(view.active_gameplay_session(), owner);
        QCOMPARE(view.slot_widgets.front(), first.data());
        QCOMPARE(gameplay::capture_checkpoint(*owner).value(), before);
        QVERIFY(view.gameplay_timer.isActive());
        QVERIFY(view.gameplay_elapsed.isValid());
    }
    gameplay::generation_policy tight;
    tight.maximum_cards = 1;
    QVERIFY(!view.restore_gameplay_checkpoint(saved, tight));
    auto large = owned_clock_fixture(65);
    // This domain fixture has no table/Packing host to supply Start traversal.
    std::vector<std::size_t> traversal(65);
    std::iota(traversal.begin(), traversal.end(), std::size_t { 0 });
    QVERIFY(large->set_traversal(traversal));
    QVERIFY(large->start());
    const gameplay_checkpoint excessive {
        gameplay::capture_checkpoint(*large).value(), 0
    };
    QVERIFY(!view.restore_gameplay_checkpoint(excessive));
    view.resize(1, 88);
    QVERIFY(!view.restore_gameplay_checkpoint(saved));
    QCOMPARE(view.active_gameplay_session(), owner);
    QCOMPARE(view.slot_widgets.front(), first.data());
    QVERIFY(view.gameplay_timer.isActive());
    QCOMPARE(changed.count(), 0);
    table legacy;
    legacy.resize(900, 700);
    legacy.set_slot_count(2);
    legacy.start_quiz(0, false);
    const auto old = legacy.capture_session_state();
    QVERIFY(!legacy.restore_gameplay_checkpoint(saved));
    QVERIFY(!legacy.capture_gameplay_checkpoint());
    QCOMPARE(legacy.capture_session_state(), old);
}

void table_tests::checkpoint_restore_replaces_paused_views() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1, 4 }
    )));
    QVERIFY(view.start_gameplay_runtime(23));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    const auto stale = view.gameplay_quiz_prompt({ 0 }).value();
    QVERIFY(view.edit_gameplay_quiz(stale, INT64_MIN));
    QVERIFY(view.advance_gameplay_runtime(1250, answers));
    QVERIFY(view.pause_gameplay_runtime());
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    owned_input(view.slot_widgets[0])
        ->setText("-"); // transient draft is not model input
    const auto saved = view.capture_gameplay_checkpoint().value();
    auto* old_owner = view.active_gameplay_session();
    QPointer<table_slot> old_slot = view.slot_widgets[0];
    auto fields = std::make_unique<gameplay::setup_widget>(*old_owner);
    bool old_alive = false;
    bool attached_when_changed = false;
    QObject::connect(
        &view, &table::gameplay_session_about_to_change, &view, [&] {
            old_alive = view.active_gameplay_session() == old_owner;
            fields.reset();
        }
    );
    QObject::connect(&view, &table::gameplay_session_changed, &view, [&] {
        attached_when_changed = view.gameplay_runtime_attached
            && view.gameplay_owner->phase() == gameplay::session_phase::paused
            && !view.gameplay_timer.isActive()
            && !view.gameplay_elapsed.isValid()
            && view.slot_widgets[0]->is_quiz_prompt_active();
    });
    QSignalSpy legacy_scores(&view, &table::score_adjusted);
    QSignalSpy legacy_end(&view, &table::game_over);
    QVERIFY(view.restore_gameplay_checkpoint(saved));
    QVERIFY(old_alive && attached_when_changed);
    QVERIFY(!fields && old_slot.isNull());
    const auto prompt = view.gameplay_quiz_prompt({ 0 }).value();
    QVERIFY(prompt != stale);
    QCOMPARE(prompt.scene_revision, stale.scene_revision + 1);
    QCOMPARE(
        owned_input(view.slot_widgets[0])->text(), QString::number(INT64_MIN)
    );
    QVERIFY(!owned_input(view.slot_widgets[0])->isEnabled());
    QVERIFY(!view.check_gameplay_quiz(prompt));
    QVERIFY(view.capture_gameplay_checkpoint() == saved);
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QVERIFY(!view.check_gameplay_quiz(stale));
    QVERIFY(view.check_gameplay_quiz(prompt));
    QCOMPARE(legacy_scores.count(), 0);
    QCOMPARE(legacy_end.count(), 0);
}

void table_tests::checkpoint_capture_settles_active_clock() {
    table view;
    view.resize(900, 700);
    QVERIFY(!view.capture_gameplay_checkpoint());
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    QVERIFY(!view.capture_gameplay_checkpoint());
    QVERIFY(view.start_gameplay_runtime(11));
    QVERIFY(view.set_gameplay_pick_interval(1000));
    const auto before_speed = view.gameplay_owner->timing().play_time_ms;
    view.gameplay_timer.stop(); // real stopwatch, without queued ticks
    QTest::qWait(3);
    const auto captured = view.capture_gameplay_checkpoint();
    QVERIFY(captured);
    QVERIFY(captured->state.timing.play_time_ms >= 3);
    QCOMPARE(
        captured->state.timing.play_time_ms,
        static_cast<std::uint64_t>(view.gameplay_sampled_ms)
    );
    QCOMPARE(
        captured->pickup_elapsed_ms,
        captured->state.timing.play_time_ms - before_speed
    );
    QCOMPARE(view.gameplay_owner->phase(), gameplay::session_phase::running);
    QVERIFY(view.pause_gameplay_runtime());
    const auto paused = view.capture_gameplay_checkpoint();
    QTest::qWait(2);
    QVERIFY(view.capture_gameplay_checkpoint() == paused);
    QVERIFY(view.finish_gameplay_runtime());
    QVERIFY(!view.capture_gameplay_checkpoint());
}

void table_tests::checkpoint_capture_projects_timeout() {
    table view;
    view.resize(1200, 900);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        2, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1, 4 }
    )));
    QVERIFY(view.start_gameplay_runtime(9));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    const auto stale = view.gameplay_quiz_prompt({ 1 }).value();
    const auto remaining
        = view.gameplay_owner->current_quiz_batch()->remaining_ms.value();
    QVERIFY(view.advance_gameplay_runtime(remaining - 1, answers));
    view.gameplay_elapsed.start();
    QSignalSpy resolved(&view, &table::gameplay_quiz_resolved);
    QTest::qWait(2);
    const auto saved = view.capture_gameplay_checkpoint();
    QVERIFY(saved);
    QCOMPARE(resolved.count(), 2);
    QCOMPARE(saved->state.challenge.errors, std::uint64_t { 1 });
    QVERIFY(!saved->state.batch);
    QVERIFY(!saved->state.decks[1].quiz);
    QCOMPARE(saved->state.decks[1].status, gameplay::deck_status::failed);
    QVERIFY(!view.check_gameplay_quiz(stale));
}

void table_tests::checkpoint_capture_rejects_replaced_scene() {
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    QVERIFY(view.start_gameplay_runtime(11));
    view.stop_gameplay_clock();
    const auto connection
        = QObject::connect(&view, &table::gameplay_runtime_updated, &view, [&] {
              QVERIFY(view.install_gameplay_session(owned_clock_fixture(2)));
          });
    QVERIFY(!view.capture_gameplay_checkpoint());
    QObject::disconnect(connection);
    QCOMPARE(view.gameplay_owner->size(), std::size_t { 2 });
    QCOMPARE(view.gameplay_owner->phase(), gameplay::session_phase::setup);
    QVERIFY(!view.gameplay_runtime_attached);
    auto* dying = new table;
    dying->resize(900, 700);
    QVERIFY(dying->install_gameplay_session(owned_clock_fixture(1)));
    QVERIFY(dying->start_gameplay_runtime(12));
    QPointer<table> guard(dying);
    QObject::connect(dying, &table::gameplay_runtime_updated, dying, [dying] {
        delete dying;
    });
    QVERIFY(!dying->capture_gameplay_checkpoint());
    QVERIFY(!guard);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
