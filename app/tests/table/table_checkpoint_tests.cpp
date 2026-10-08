// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>

using namespace table_test;

void table_tests::checkpoint_restore_continues_runtime_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("scope");
    QTest::addColumn<bool>("freeze");
    for (int mode = 0; mode < 3; ++mode)
        for (int scope = 0; scope < 2; ++scope)
            for (const bool freeze : { false, true })
                QTest::newRow(qPrintable(
                    QStringLiteral("%1/%2/%3").arg(mode).arg(scope).arg(freeze)
                )) << mode
                   << scope << freeze;
}

void table_tests::checkpoint_restore_continues_runtime() {
    QFETCH(int, mode);
    QFETCH(int, scope);
    QFETCH(bool, freeze);
    table baseline;
    baseline.resize(1200, 900);
    auto owner = owned_clock_fixture(
        4, freeze, static_cast<gameplay::dealing_mode>(mode),
        static_cast<gameplay::quiz_scope>(scope), { 1, 4 }
    );
    QVERIFY(owner->update_slot_limits({ 4, 64 }));
    auto configuration = owner->configuration();
    configuration.allow_extra_slots = false;
    QVERIFY(owner->configure(configuration));
    QVERIFY(baseline.install_gameplay_session(std::move(owner)));
    QVERIFY(baseline.start_gameplay_runtime(UINT64_MAX));
    baseline.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(baseline.advance_gameplay_runtime(300, answers));
    baseline.publish_gameplay_runtime(answers);
    for (const auto id : baseline.gameplay_owner->unresolved_quizzes())
        QVERIFY(baseline.edit_gameplay_quiz(
            *baseline.gameplay_quiz_prompt(id), INT64_MIN
        ));
    QVERIFY(baseline.advance_gameplay_runtime(125, answers));
    QVERIFY(baseline.pause_gameplay_runtime());
    QVERIFY(baseline.swap_gameplay_decks({ 0 }, { 2 }));
    const auto saved = baseline.capture_gameplay_checkpoint();
    QVERIFY(saved);

    // Actual durable storage and a newly sized/oriented table, not a second
    // runtime model or a snapshot-only codec test.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto file = directory.filePath("checkpoint.ini");
    QSettings settings(file, QSettings::IniFormat);
    QVERIFY(gameplay_checkpoint_service(settings, { 1, 64 }).save(*saved));
    QSettings reopened(file, QSettings::IniFormat);
    const auto loaded = gameplay_checkpoint_service(reopened, { 1, 64 }).load();
    QVERIFY(loaded && *loaded == *saved);
    table recovered;
    recovered.resize(340, 880);
    recovered.set_card_orientation(card_orientation_mode::vertical);
    QVERIFY(recovered.restore_gameplay_checkpoint(*loaded));
    QCOMPARE(
        recovered.gameplay_owner->phase(), gameplay::session_phase::paused
    );
    QVERIFY(recovered.gameplay_runtime_attached);
    QVERIFY(!recovered.gameplay_owner->configuration().allow_extra_slots);
    QVERIFY(!recovered.gameplay_timer.isActive());
    QVERIFY(!recovered.gameplay_elapsed.isValid());
    QVERIFY(!recovered.start_gameplay_runtime(9));
    QVERIFY(!recovered.advance_gameplay_runtime(900000, answers));
    recovered.on_clock_tick(900000, 900000);
    recovered.on_gameplay_clock_tick();
    QVERIFY(recovered.capture_gameplay_checkpoint() == saved);
    const auto packed = recovered.pack_gameplay_slots(4).value();
    QVERIFY(
        std::ranges::equal(
            recovered.gameplay_owner->traversal(), packed.traversal, {},
            &gameplay::physical_slot_id::value
        )
    );
    for (std::size_t index = 0; index < 4; ++index) {
        QCOMPARE(
            recovered.slot_widgets[index]->geometry(),
            widget_rectangle(
                packed.rectangles[saved->state.deck_to_slot[index].value]
            )
        );
        const auto* motion = recovered.gameplay_layout->deck_motion({ index });
        QCOMPARE(motion->from.x, motion->to.x);
        QCOMPARE(motion->from.y, motion->to.y);
        QCOMPARE(motion->from.width, motion->to.width);
        QCOMPARE(motion->from.height, motion->to.height);
        QCOMPARE(motion->from.rotated, motion->to.rotated);
    }
    QCOMPARE(
        recovered.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
    );
    QVERIFY(!recovered.gameplay_motion_layer);
    // Both continuations use today's same Packing traversal; physical IDs and
    // Sequential anchor remain the saved ones, not creation-order positions.
    baseline.resize(recovered.size());
    baseline.set_card_orientation(card_orientation_mode::vertical);
    baseline.update_layout();
    QVERIFY(baseline.resume_gameplay_runtime());
    QVERIFY(recovered.resume_gameplay_runtime());
    baseline.stop_gameplay_clock();
    recovered.stop_gameplay_clock();
    for (int step = 0; step < 80; ++step) {
        QCOMPARE(
            recovered.gameplay_owner->phase(), baseline.gameplay_owner->phase()
        );
        if (baseline.gameplay_owner->phase()
            == gameplay::session_phase::finished)
            break;
        for (const auto id : baseline.gameplay_owner->unresolved_quizzes()) {
            const auto first = baseline.gameplay_quiz_prompt(id).value();
            const auto second = recovered.gameplay_quiz_prompt(id).value();
            const auto input
                = baseline.gameplay_owner->deck(id)->quiz->expected_count;
            QVERIFY(baseline.edit_gameplay_quiz(first, input));
            QVERIFY(recovered.edit_gameplay_quiz(second, input));
            QVERIFY(baseline.check_gameplay_quiz(first));
            QVERIFY(recovered.check_gameplay_quiz(second));
        }
        if (baseline.gameplay_owner->phase()
            != gameplay::session_phase::finished) {
            const std::uint64_t slice = step % 2 == 0 ? 133 : 167;
            std::vector<gameplay::quiz_answer> first_answers;
            std::vector<gameplay::quiz_answer> second_answers;
            QVERIFY(baseline.advance_gameplay_runtime(slice, first_answers));
            QVERIFY(recovered.advance_gameplay_runtime(slice, second_answers));
            baseline.publish_gameplay_runtime(first_answers);
            recovered.publish_gameplay_runtime(second_answers);
            QCOMPARE(first_answers, second_answers);
        }
        QCOMPARE(
            gameplay::capture_checkpoint(*recovered.gameplay_owner),
            gameplay::capture_checkpoint(*baseline.gameplay_owner)
        );
    }
    QCOMPARE(
        recovered.gameplay_owner->phase(), gameplay::session_phase::finished
    );
    QVERIFY(
        recovered.gameplay_owner->result() == baseline.gameplay_owner->result()
    );
    QVERIFY(!recovered.capture_gameplay_checkpoint());
}

void table_tests::checkpoint_restore_keeps_pickup_remainder_data() {
    QTest::addColumn<int>("interval");
    QTest::addColumn<int>("count");
    for (const int interval : { 100, 300, 1000 })
        QTest::newRow(qPrintable(QString::number(interval))) << interval << 1;
    QTest::newRow("24-decks") << 300 << 24;
}

void table_tests::checkpoint_restore_keeps_pickup_remainder() {
    QFETCH(int, interval);
    QFETCH(int, count);
    table source;
    source.resize(900, 700);
    auto owner = owned_clock_fixture(static_cast<std::size_t>(count));
    QVERIFY(owner->set_pick_interval_ms(interval));
    QVERIFY(source.install_gameplay_session(std::move(owner)));
    QVERIFY(source.start_gameplay_runtime(19));
    source.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(source.advance_gameplay_runtime(
        static_cast<std::uint64_t>(interval - 1), answers
    ));
    QVERIFY(source.pause_gameplay_runtime());
    const auto saved = source.capture_gameplay_checkpoint().value();
    QCOMPARE(saved.pickup_elapsed_ms, static_cast<std::uint64_t>(interval - 1));
    table recovered;
    recovered.resize(600, 880);
    QVERIFY(recovered.restore_gameplay_checkpoint(saved));
    QCOMPARE(recovered.gameplay_owner->size(), static_cast<std::size_t>(count));
    QTest::qWait(2);
    recovered.on_gameplay_clock_tick();
    QVERIFY(recovered.capture_gameplay_checkpoint() == saved);
    QCOMPARE(recovered.pick_interval(), interval);
    QVERIFY(recovered.resume_gameplay_runtime());
    recovered.stop_gameplay_clock();
    QVERIFY(recovered.advance_gameplay_runtime(0, answers));
    QCOMPARE(
        recovered.gameplay_owner->deck({ 0 })->next_card, std::size_t { 0 }
    );
    QVERIFY(recovered.advance_gameplay_runtime(1, answers));
    QCOMPARE(
        recovered.gameplay_owner->deck({ 0 })->next_card, std::size_t { 1 }
    );
    QCOMPARE(
        recovered.gameplay_owner->deck({ static_cast<std::size_t>(count - 1) })
            ->next_card,
        std::size_t { 1 }
    );
    QCOMPARE(
        recovered.gameplay_owner->timing().play_time_ms,
        static_cast<std::uint64_t>(interval)
    );
    QCOMPARE(recovered.gameplay_pick_elapsed_ms, std::uint64_t { 0 });
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
