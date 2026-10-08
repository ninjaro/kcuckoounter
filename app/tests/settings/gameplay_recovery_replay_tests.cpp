// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/gameplay_recovery_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"
#include "settings/gameplay_recovery_fixture.hpp"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace gameplay;
using namespace recovery_test;

void gameplay_recovery_tests::active_round_trip_data() {
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

void gameplay_recovery_tests::active_round_trip() {
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
    auto model = progress_test::ready_session(config, infinite);
    prime(model);
    QVERIFY(model.pause());
    const auto initial = snapshot(model);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto file = directory.filePath("settings.ini");
    QSettings settings(file, QSettings::IniFormat);
    gameplay_recovery_service service(settings, { 1, 64 });
    QString error("old error");
    QVERIFY(!service.load(&error) && error.isEmpty());
    QVERIFY(settings.allKeys().isEmpty());
    const auto sequence = service.begin(initial, &error);
    QVERIFY(sequence && *sequence == 1 && error.isEmpty());
    QVERIFY(!settings.contains(progress_key));
    QSettings reopened(file, QSettings::IniFormat);
    gameplay_recovery_service again(reopened, { 2, 64 });
    const auto recovered = again.load(&error);
    QVERIFY(recovered && error.isEmpty());
    const auto active = std::get<gameplay_active_record>(*recovered);
    QCOMPARE(active.sequence, *sequence);
    QVERIFY(active.checkpoint == initial);
    auto resumed = restored(active.checkpoint);
    QCOMPARE(resumed.phase(), session_phase::paused);
    QVERIFY(!resumed.advance_time(999'999).accepted);
    QVERIFY(!resumed.check_quiz(deck_id { 0 }));
    QVERIFY(snapshot(resumed) == initial);
    QVERIFY(model.resume());
    QVERIFY(resumed.resume());
    drive(model, 16);
    drive(resumed, 16);
    const auto middle = snapshot(model, 149);
    QVERIFY(snapshot(resumed, 149) == middle);
    QVERIFY(again.save(*sequence, middle, &error) && error.isEmpty());
    const auto before = reopened.value(storage_key);
    QVERIFY(again.save(*sequence, middle, &error) && error.isEmpty());
    QCOMPARE(reopened.value(storage_key), before);
    drive(model, 180);
    drive(resumed, 180);
    const auto record = finish(model, *sequence);
    const auto replayed = finish(resumed, *sequence);
    QVERIFY(record == replayed);
    QVERIFY(again.complete(record, &error) && error.isEmpty());
    QVERIFY(!again.load(&error) && error.isEmpty());
    const auto progress = gameplay_progress_service(reopened, { 3, 64 }).load();
    QVERIFY(progress && progress->completed_sessions == 1);
    QVERIFY(progress->recent_results.front() == record);
    QVERIFY(again.complete(record, &error) && error.isEmpty());
    settings.sync();
    QVERIFY(!settings.contains(storage_key));
}

void gameplay_recovery_tests::manual_finish_after_restore() {
    auto model = progress_test::ready_session({}, true);
    prime(model);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service service(settings, { 1, 64 });
    const auto sequence = service.begin(snapshot(model)).value();
    const auto active = std::get<gameplay_active_record>(*service.load());
    auto paused = restored(active.checkpoint);
    QVERIFY(!paused.unresolved_quizzes().empty());
    const auto record = finish(paused, sequence);
    QCOMPARE(record.result.reason, session_end_reason::manual_finish);
    QCOMPARE(record.result.active_decks, 3U);
    QVERIFY(service.complete(record));
    QVERIFY(
        gameplay_progress_service(settings, { 3, 64 })
            .load()
            ->recent_results.front()
        == record
    );
}

void gameplay_recovery_tests::natural_finish() {
    session_configuration config;
    config.dealing = dealing_mode::simultaneous;
    auto model = progress_test::ready_session(config);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service service(settings, { 3, 64 });
    QVERIFY(service.begin(snapshot(model)) == 1);
    drive(model, 60);
    QCOMPARE(model.phase(), session_phase::finished);
    const auto record = finish(model);
    QCOMPARE(record.result.reason, session_end_reason::no_live_decks);
    QVERIFY(service.complete(record));
    QCOMPARE(
        gameplay_progress_service(settings, { 3, 64 })
            .load()
            ->completed_sessions,
        1U
    );
}

void gameplay_recovery_tests::lives_batch_finish() {
    session_configuration config;
    config.initial_lives = 1;
    config.scope = quiz_scope::multi;
    config.dealing = dealing_mode::simultaneous;
    auto model = progress_test::ready_session(config);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service service(settings, { 3, 64 });
    QVERIFY(service.begin(snapshot(model)) == 1);
    prime(model);
    QVERIFY(model.check_quiz(deck_id { 0 }));
    QCOMPARE(model.remaining_lives(), 0);
    QVERIFY(service.save(1, snapshot(model)));
    const auto saved = std::get<gameplay_active_record>(*service.load());
    auto paused = restored(saved.checkpoint);
    QVERIFY(paused.resume());
    for (const auto id : paused.unresolved_quizzes())
        QVERIFY(paused.check_quiz(id));
    const auto record = finish(paused);
    QCOMPARE(record.result.reason, session_end_reason::lives_exhausted);
    QCOMPARE(record.result.challenge.errors, 2U);
    QCOMPARE(record.result.decks[1].statistics.errors, 1U);
    QVERIFY(service.complete(record));
    QVERIFY(
        gameplay_progress_service(settings, { 3, 64 })
            .load()
            ->recent_results.front()
        == record
    );
}

void gameplay_recovery_tests::mutable_state_and_stopped_decks() {
    session_configuration config;
    config.failure = failure_policy::block;
    config.global_quiz_pause = false;
    auto model = progress_test::ready_session(config, true);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(
        directory.filePath("settings.ini"), QSettings::IniFormat
    );
    gameplay_recovery_service service(settings, { 3, 64 });
    QVERIFY(service.begin(snapshot(model)) == 1);
    prime(model);
    QVERIFY(model.check_quiz(deck_id { 0 }));
    QCOMPARE(model.deck(deck_id { 0 })->status, deck_status::failed);
    QVERIFY(model.pause());
    QVERIFY(model.swap_decks(deck_id { 0 }, deck_id { 2 }));
    QVERIFY(model.set_show_count(deck_id { 1 }, true));
    QVERIFY(model.set_pick_interval_ms(400));
    const auto changed = snapshot(model, 399);
    QVERIFY(service.save(1, changed));
    auto invalid = changed;
    invalid.state.decks[0].status = deck_status::active;
    QVERIFY(encode_gameplay_checkpoint(invalid, { 3, 64 }));
    QString error;
    QVERIFY(!service.save(1, invalid, &error) && !error.isEmpty());
    QVERIFY(
        std::get<gameplay_active_record>(*service.load()).checkpoint == changed
    );
    const auto record = finish(model);
    QCOMPARE(record.result.failed_decks, 1U);
    QVERIFY(service.complete(record));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
