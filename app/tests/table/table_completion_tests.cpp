// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_completion_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"
#include "settings/gameplay_recovery_fixture.hpp"
#include "shell/gameplay_reservation.hpp"
#include "table/table_fixture.hpp"

#include <QDateTime>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace gameplay;
using namespace recovery_test;
using namespace table_test;

std::unique_ptr<session> gameplay_completion_tests::lives_fixture() {
    auto owner = owned_clock_fixture(
        3, true, dealing_mode::simultaneous, quiz_scope::multi, { 1 }
    );
    auto config = owner->configuration();
    config.failure = failure_policy::lives;
    config.initial_lives = 1;
    if (!owner->configure(config))
        qFatal("Invalid terminal Lives fixture");
    return owner;
}

bool gameplay_completion_tests::tick(table& view, std::uint64_t elapsed_ms) {
    std::vector<quiz_answer> answers;
    const bool accepted = view.advance_gameplay_runtime(elapsed_ms, answers);
    view.publish_gameplay_runtime(answers);
    return accepted;
}

void gameplay_completion_tests::manual_retains_result_data() {
    QTest::addColumn<bool>("paused");
    QTest::newRow("running") << false;
    QTest::newRow("paused") << true;
}

void gameplay_completion_tests::manual_retains_result() {
    QFETCH(bool, paused);
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath("completion.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(3)));
    gameplay_reservation binding;
    QVERIFY(binding.start(view, recovery, 91));
    view.stop_gameplay_clock();
    QSignalSpy score(&view, &table::score_adjusted);
    QSignalSpy ended(&view, &table::game_over);
    QVERIFY(tick(view, 300));
    QVERIFY(tick(view, 300));
    if (paused)
        QVERIFY(view.pause_gameplay_runtime());
    const auto before = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(view.finish_gameplay_runtime());
    const auto after = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(
        binding.completion() && binding.owns(view) && !binding.acknowledged()
    );
    const auto exact = *binding.completion();
    QCOMPARE(exact.sequence, std::uint64_t { 1 });
    QVERIFY(
        exact.completed_at_utc_ms >= before
        && exact.completed_at_utc_ms <= after
    );
    QCOMPARE(exact.result.reason, session_end_reason::manual_finish);
    QVERIFY(exact.result == *view.active_gameplay_session()->result());
    QCOMPARE(exact.result.timing.play_time_ms, std::uint64_t { 600 });
    const auto active = storage.value(storage_key);
    QVERIFY(
        !storage.contains(progress_key)
    ); // Capture performs no settings I/O.
    view.publish_gameplay_runtime({});
    QVERIFY(!view.finish_gameplay_runtime());
    QVERIFY(binding.completion() == exact);
    QCOMPARE(storage.value(storage_key), active);
    QString error("old diagnostic");
    QVERIFY(binding.complete(recovery, &error) && error.isEmpty());
    QVERIFY(binding.acknowledged() && binding.completion() == exact);
    QVERIFY(!storage.contains(storage_key));
    const auto progress = gameplay_progress_service(storage, { 1, 64 }).load();
    QVERIFY(
        progress && progress->completed_sessions == 1
        && progress->recent_results.front() == exact
    );
    const auto bytes = storage.value(progress_key);
    QVERIFY(binding.complete(recovery, &error) && error.isEmpty());
    QCOMPARE(storage.value(progress_key), bytes);
    QCOMPARE(score.count(), 0);
    QCOMPARE(ended.count(), 0);
    QVERIFY(!binding.start(view, recovery, 92, &error) && !error.isEmpty());
}

void gameplay_completion_tests::natural_retains_result_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("scope");
    for (int mode = 0; mode < 3; ++mode)
        for (int scope = 0; scope < 2; ++scope)
            QTest::newRow(
                qPrintable(QStringLiteral("%1-%2").arg(mode).arg(scope))
            ) << mode
              << scope;
}

void gameplay_completion_tests::natural_retains_result() {
    QFETCH(int, mode);
    QFETCH(int, scope);
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath("completion.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        3, true, static_cast<dealing_mode>(mode), static_cast<quiz_scope>(scope)
    )));
    gameplay_reservation binding;
    QVERIFY(binding.start(view, recovery, 91));
    view.stop_gameplay_clock();
    auto* owner = view.active_gameplay_session();
    for (int step = 0; step < 64 && owner->phase() == session_phase::running;
         ++step) {
        for (const auto id : owner->unresolved_quizzes()) {
            const auto prompt = *view.gameplay_quiz_prompt(id);
            QVERIFY(
                view.edit_gameplay_quiz(prompt, owner->deck(id)->running_count)
            );
            QVERIFY(view.check_gameplay_quiz(prompt));
        }
        if (owner->phase() == session_phase::running)
            QVERIFY(tick(view, 300));
    }
    QCOMPARE(owner->phase(), session_phase::finished);
    QVERIFY(
        binding.completion() && binding.completion()->result == *owner->result()
    );
    QCOMPARE(
        binding.completion()->result.reason, session_end_reason::no_live_decks
    );
    QCOMPARE(binding.completion()->result.completed_decks, std::size_t { 3 });
    const auto exact = *binding.completion();
    QVERIFY(binding.complete(recovery));
    const auto progress = gameplay_progress_service(storage, { 1, 64 }).load();
    QVERIFY(
        progress && progress->completed_sessions == 1
        && progress->recent_results.front() == exact
    );
    view.publish_gameplay_runtime({});
    QVERIFY(binding.completion() == exact && binding.acknowledged());
}

void gameplay_completion_tests::lives_waits_for_all_answers_data() {
    QTest::addColumn<bool>("timeout");
    QTest::newRow("checks") << false;
    QTest::newRow("timeout") << true;
}

void gameplay_completion_tests::lives_waits_for_all_answers() {
    QFETCH(bool, timeout);
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath("completion.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(lives_fixture()));
    gameplay_reservation binding;
    QVERIFY(binding.start(view, recovery, 91));
    view.stop_gameplay_clock();
    QVERIFY(tick(view, 300));
    auto* owner = view.active_gameplay_session();
    const auto pending = owner->unresolved_quizzes();
    QCOMPARE(pending.size(), std::size_t { 3 });
    for (const auto id : pending)
        QVERIFY(view.edit_gameplay_quiz(
            *view.gameplay_quiz_prompt(id), owner->deck(id)->running_count - 1
        ));
    QSignalSpy score(&view, &table::score_adjusted);
    QSignalSpy ended(&view, &table::game_over);
    bool retained_before_answers = false;
    QObject::connect(&view, &table::gameplay_quiz_resolved, &view, [&] {
        if (owner->phase() == session_phase::finished)
            retained_before_answers = binding.completion()
                && binding.completion()->result == *owner->result();
    });
    if (timeout) {
        QVERIFY(tick(view, session::quiz_initial_ms));
    } else {
        for (std::size_t index = 0; index < pending.size(); ++index) {
            QVERIFY(view.check_gameplay_quiz(
                *view.gameplay_quiz_prompt(pending[index])
            ));
            if (index + 1 < pending.size())
                QVERIFY(
                    !binding.completion()
                ); // Zero lives is not a partial-batch finish.
        }
    }
    QVERIFY(binding.completion() && retained_before_answers);
    const auto exact = *binding.completion();
    QCOMPARE(exact.result.reason, session_end_reason::lives_exhausted);
    QCOMPARE(exact.result.challenge.errors, std::uint64_t { 2 });
    QCOMPARE(exact.result.remaining_lives, 0);
    QCOMPARE(exact.result.failed_decks, std::size_t { 0 });
    QCOMPARE(
        exact.result.active_decks, std::size_t { 3 }
    ); // Lives is not Block.
    QVERIFY(binding.complete(recovery));
    const auto progress = gameplay_progress_service(storage, { 1, 64 }).load();
    QVERIFY(progress && progress->recent_results.front() == exact);
    QCOMPARE(score.count(), 0);
    QCOMPARE(ended.count(), 0);
}

void gameplay_completion_tests::restored_paused_owner_completes() {
    auto model = progress_test::ready_session({}, true);
    prime(model);
    QVERIFY(model.pause());
    auto checkpoint = snapshot(model, 149);
    QTemporaryDir directory;
    const auto file = directory.filePath("completion.ini");
    {
        QSettings storage(file, QSettings::IniFormat);
        gameplay_recovery_service recovery(storage, { 1, 64 });
        QVERIFY(recovery.begin(checkpoint) == 1);
        table active;
        active.resize(1000, 800);
        gameplay_reservation first;
        QCOMPARE(
            first.recover(active, recovery),
            gameplay_reservation::recovery_outcome::paused
        );
        QVERIFY(active.resume_gameplay_runtime());
        active.stop_gameplay_clock();
        QVERIFY(tick(active, 17));
        QVERIFY(active.pause_gameplay_runtime());
        const auto captured = active.capture_gameplay_checkpoint();
        QVERIFY(captured);
        checkpoint = *captured;
        QVERIFY(recovery.save(*first.sequence(), checkpoint));
    }
    model = restored(checkpoint);
    QSettings reopened(file, QSettings::IniFormat);
    gameplay_recovery_service recovery(reopened, { 1, 64 });
    table view;
    view.resize(450, 850);
    gameplay_reservation binding;
    QCOMPARE(
        binding.recover(view, recovery),
        gameplay_reservation::recovery_outcome::paused
    );
    QTest::qWait(5);
    QVERIFY(view.capture_gameplay_checkpoint() == checkpoint);
    QVERIFY(!binding.completion());
    QVERIFY(view.finish_gameplay_runtime());
    QVERIFY(binding.completion());
    const auto exact = *binding.completion();
    QVERIFY(exact.result == finish(model).result);
    QVERIFY(binding.complete(recovery));
    const auto progress = gameplay_progress_service(reopened, { 1, 64 }).load();
    QVERIFY(progress && progress->recent_results.front() == exact);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
