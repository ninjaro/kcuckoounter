// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "arch/str_label.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QtTest/QtTest>

#include <array>
#include <limits>

using namespace table_test;

void table_tests::corrections_neither_gate_nor_overwrite_controls_data() {
    QTest::addColumn<bool>("chips");
    QTest::addColumn<bool>("stamp");
    for (const bool chips : { false, true }) {
        for (const bool stamp : { false, true }) {
            QTest::newRow(qPrintable(
                QStringLiteral("chips=%1/stamp=%2").arg(chips).arg(stamp)
            )) << chips
               << stamp;
        }
    }
}

void table_tests::corrections_neither_gate_nor_overwrite_controls() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop activated feedback compatibility");
#endif
    QFETCH(bool, chips);
    QFETCH(bool, stamp);
    table view;
    view.resize(1800, 1200);
    const auto input_style
        = chips ? quiz_answer_style::chips : quiz_answer_style::numeric;
    view.set_quiz_presentation(
        input_style,
        stamp ? quiz_feedback_style::stamp : quiz_feedback_style::classic
    );
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, false, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1, 2, 4 }
    )));
    QSignalSpy legacy(&view, &table::score_adjusted);
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    QVERIFY(view.start_gameplay_runtime(35));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    auto& owner = *view.active_gameplay_session();
    auto* slot = view.slot_widgets[0];
    auto* input = owned_input(slot);
    enter_owned_count(input, QStringLiteral("9"));
    slot->findChild<QPushButton*>(QStringLiteral("quiz_skip_button"))->click();
    const auto expected = -(std::int64_t { 1 } << 40) + 1;
    auto* feedback
        = slot->findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    auto* heading
        = slot->findChild<QLabel*>(QStringLiteral("quiz_feedback_stamp"));
    auto* dismiss
        = slot->findChild<QPushButton*>(QStringLiteral("quiz_continue_button"));
    QVERIFY(feedback && heading && dismiss);
    QVERIFY(feedback->isVisible() && dismiss->isVisible());
    QCOMPARE(heading->isVisible(), stamp);
    QCOMPARE(feedback->textFormat(), Qt::PlainText);
    QVERIFY(feedback->text().contains(QString::number(expected)));
    QVERIFY(feedback->text().contains(QStringLiteral("9")));
    QCOMPARE(dismiss->text(), str_label("Dismiss"));
    QCOMPARE(owner.deck({ 0 })->latest_input, std::int64_t { 9 });
    QCOMPARE(owner.statistics().errors, std::uint64_t { 0 });
    QCOMPARE(owner.deck({ 0 })->statistics.errors, std::uint64_t { 0 });
    QCOMPARE(view.gameplay_corrections().size(), std::size_t { 1 });
    QCOMPARE(
        view.gameplay_corrections().front().outcome,
        gameplay::quiz_outcome::skipped
    );

    auto* peer_input = owned_input(view.slot_widgets[1]);
    enter_owned_count(peer_input, QStringLiteral("+12345"));
    peer_input->setSelection(1, 2);
    // Feedback treatment must not change any answer, timer or raster policy.
    const std::array records { *owner.deck({ 0 }), *owner.deck({ 1 }),
                               *owner.deck({ 2 }), *owner.deck({ 3 }) };
    const auto correction = view.gameplay_corrections();
    const auto timing = owner.timing();
    const auto statistics = owner.statistics();
    const auto peer_prompt = view.gameplay_quiz_prompt({ 1 });
    const auto peer_text = peer_input->text();
    const auto cursor = peer_input->cursorPosition();
    const auto selection = peer_input->selectedText();
    const auto text = feedback->text();
    const auto geometry = slot->geometry();
    const auto demand = slot->card_face_need_short_px();
    auto* cache = view.shared_raster_cache_service();
    const auto generation = view.next_shared_generation_id;
    for (const bool next_stamp : { !stamp, stamp }) {
        view.set_quiz_presentation(
            input_style,
            next_stamp ? quiz_feedback_style::stamp
                       : quiz_feedback_style::classic
        );
        QCOMPARE(heading->isVisible(), next_stamp);
        QCOMPARE(heading->text(), str_label("ACCUMULATED COUNT"));
        QCOMPARE(feedback->styleSheet().isEmpty(), !next_stamp);
        QCOMPARE(feedback->text(), text);
        QCOMPARE(view.gameplay_corrections(), correction);
        for (std::size_t id = 0; id < records.size(); ++id) {
            QCOMPARE(*owner.deck({ id }), records[id]);
        }
        QCOMPARE(owner.timing(), timing);
        QCOMPARE(owner.statistics(), statistics);
        QCOMPARE(view.gameplay_quiz_prompt({ 1 }), peer_prompt);
        QCOMPARE(peer_input->text(), peer_text);
        QCOMPARE(peer_input->cursorPosition(), cursor);
        QCOMPARE(peer_input->selectedText(), selection);
        QCOMPARE(slot->geometry(), geometry);
        QCOMPARE(slot->card_face_need_short_px(), demand);
        QCOMPARE(view.shared_raster_cache_service(), cache);
        QCOMPARE(view.next_shared_generation_id, generation);
    }

    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    // A new Single question need not wait for correction acknowledgement.
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 2 });
    QVERIFY(owner.timing().play_time_ms > timing.play_time_ms);
    QCOMPARE(input->text(), QStringLiteral("9"));
    auto* previous = slot->findChild<QLabel*>(
        QStringLiteral("gameplay_previous_correction")
    );
    QVERIFY(previous->isVisible());
    QCOMPARE(previous->text(), text);
    QCOMPARE(previous->textFormat(), Qt::PlainText);
    QVERIFY(!view.gameplay_status_text().contains(str_label("Quiz remaining")));
    const auto before = *owner.deck({ 0 });
    QVERIFY(
        QMetaObject::invokeMethod(
            slot, "on_quiz_continue_button_clicked", Qt::DirectConnection
        )
    );
    QCOMPARE(*owner.deck({ 0 }), before);
    QVERIFY(previous->isHidden());
    QVERIFY(view.gameplay_corrections().empty());
    QTest::keyClick(input, Qt::Key_Return); // wrong Training count
    QVERIFY(feedback->isVisible());
    QVERIFY(feedback->text().contains(QString::number(expected + 1)));
    QCOMPARE(owner.deck({ 0 })->statistics.errors, std::uint64_t { 1 });
    QCOMPARE(owner.statistics().errors, std::uint64_t { 0 });
    QCOMPARE(owner.deck({ 0 })->status, gameplay::deck_status::active);

    // A Challenge Block failure retains its correction in the same surface.
    enter_owned_count(peer_input, QStringLiteral("-7"));
    view.slot_widgets[1]
        ->findChild<QPushButton*>(QStringLiteral("quiz_answer_button"))
        ->click();
    QCOMPARE(owner.deck({ 1 })->status, gameplay::deck_status::failed);
    auto* failed_feedback = view.slot_widgets[1]->findChild<QLabel*>(
        QStringLiteral("quiz_feedback_label")
    );
    QVERIFY(failed_feedback->isVisible());
    QVERIFY(failed_feedback->text().contains(QStringLiteral("-7")));
    QVERIFY(failed_feedback->text().contains(QStringLiteral("101")));
    QCOMPARE(view.gameplay_corrections().size(), std::size_t { 2 });
    const auto errors = owner.statistics().errors;
    // A prepared count does not seed answer memory; make an explicit edit.
    enter_owned_count(owned_input(view.slot_widgets[2]), QStringLiteral("100"));
    view.slot_widgets[2]
        ->findChild<QPushButton*>(QStringLiteral("quiz_skip_button"))
        ->click();
    QCOMPARE(owner.statistics().errors, errors);
    QCOMPARE(owner.deck({ 2 })->statistics.errors, std::uint64_t { 0 });
    QCOMPARE(owner.deck({ 2 })->status, gameplay::deck_status::active);
    QCOMPARE(owner.deck({ 2 })->latest_input, std::int64_t { 100 });
    auto* skipped_feedback = view.slot_widgets[2]->findChild<QLabel*>(
        QStringLiteral("quiz_feedback_label")
    );
    QVERIFY(skipped_feedback->isVisible());
    QVERIFY(skipped_feedback->text().contains(QStringLiteral("100")));
    QVERIFY(skipped_feedback->text().contains(QStringLiteral("101")));
    QCOMPARE(view.gameplay_corrections().size(), std::size_t { 3 });
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QVERIFY(previous->isVisible());
    QVERIFY(owner.deck({ 0 })->quiz);
    enter_owned_count(
        input, QString::number(owner.deck({ 0 })->quiz->expected_count)
    );
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(previous->isHidden());
    QCOMPARE(view.gameplay_corrections().size(), std::size_t { 2 });
    QCOMPARE(
        view.gameplay_corrections().front().owner, gameplay::deck_id { 1 }
    );
    QCOMPARE(legacy.count(), 0);
    QCOMPARE(preparation.count(), 0);
}

void table_tests::
    gameplay_controls_compact_correction_stays_nonblocking_data() {
    corrections_neither_gate_nor_overwrite_controls_data();
}

void table_tests::gameplay_controls_compact_correction_stays_nonblocking() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop activated feedback compatibility");
#endif
    QFETCH(bool, chips);
    QFETCH(bool, stamp);
    table view;
    view.resize(180, 120);
    view.set_quiz_presentation(
        chips ? quiz_answer_style::chips : quiz_answer_style::numeric,
        stamp ? quiz_feedback_style::stamp : quiz_feedback_style::classic
    );
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        1, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1, 3 }
    )));
    QVERIFY(view.start_gameplay_runtime(40));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    auto* slot = view.slot_widgets[0];
    auto* opener = slot->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    QVERIFY(opener && opener->isVisible());
    opener->click();
    auto* host
        = slot->findChild<QDialog*>(QStringLiteral("slot_controls_dialog"));
    auto* input = owned_input(slot);
    QVERIFY(host && host->isVisible());
    enter_owned_count(input, QStringLiteral("9"));
    slot->findChild<QPushButton*>(QStringLiteral("quiz_skip_button"))->click();
    QVERIFY(host->isVisible());
    auto* feedback
        = slot->findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    auto* heading
        = slot->findChild<QLabel*>(QStringLiteral("quiz_feedback_stamp"));
    QVERIFY(feedback && feedback->isVisible() && heading);
    QCOMPARE(heading->isVisible(), stamp);
    const auto before = *view.active_gameplay_session()->deck({ 0 });
    const auto timing = view.active_gameplay_session()->timing();
    const auto correction = view.gameplay_corrections();
    for (const bool next_stamp : { !stamp, stamp }) {
        view.set_quiz_presentation(
            chips ? quiz_answer_style::chips : quiz_answer_style::numeric,
            next_stamp ? quiz_feedback_style::stamp
                       : quiz_feedback_style::classic
        );
        QCOMPARE(heading->isVisible(), next_stamp);
        QCOMPARE(
            slot->findChild<QDialog*>(QStringLiteral("slot_controls_dialog")),
            host
        );
        QCOMPARE(
            host->findChild<QLineEdit*>(QStringLiteral("gameplay_quiz_input")),
            input
        );
        QVERIFY(feedback->isVisible() && host->isVisible());
        QCOMPARE(*view.active_gameplay_session()->deck({ 0 }), before);
        QCOMPARE(view.active_gameplay_session()->timing(), timing);
        QCOMPARE(view.gameplay_corrections(), correction);
    }
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QVERIFY(host->isVisible());
    QCOMPARE(view.gameplay_owner->deck({ 0 })->next_card, std::size_t { 3 });
    QCOMPARE(input->text(), QStringLiteral("9"));
    QVERIFY(
        slot->findChild<QLabel*>(QStringLiteral("gameplay_previous_correction"))
            ->isVisible()
    );
    host->reject();
    QCOMPARE(
        view.gameplay_owner->deck({ 0 })->latest_input, std::int64_t { 9 }
    );
    QVERIFY(opener->isVisible());
    opener->click();
    const auto prompt = view.gameplay_quiz_prompt({ 0 });
    QVERIFY(prompt);
    enter_owned_count(
        input,
        QString::number(view.gameplay_owner->deck({ 0 })->quiz->expected_count)
    );
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(view.gameplay_corrections().empty());
    QVERIFY(!view.gameplay_quiz_prompt({ 0 }));
    QVERIFY(!opener->isVisible());
}

void table_tests::feedback_timeout_preserves_finished_result_data() {
    QTest::addColumn<bool>("chips");
    QTest::addColumn<bool>("stamp");
    QTest::addColumn<bool>("compact");
    for (const bool chips : { false, true }) {
        for (const bool stamp : { false, true }) {
            for (const bool compact : { false, true }) {
                QTest::newRow(
                    qPrintable(QStringLiteral("chips=%1/stamp=%2/compact=%3")
                                   .arg(chips)
                                   .arg(stamp)
                                   .arg(compact))
                ) << chips
                  << stamp << compact;
            }
        }
    }
}

void table_tests::feedback_timeout_preserves_finished_result() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop activated feedback compatibility");
#endif
    QFETCH(bool, chips);
    QFETCH(bool, stamp);
    QFETCH(bool, compact);
    table view;
    view.resize(compact ? QSize(180, 120) : QSize(1800, 1200));
    const auto input_style
        = chips ? quiz_answer_style::chips : quiz_answer_style::numeric;
    view.set_quiz_presentation(
        input_style,
        stamp ? quiz_feedback_style::stamp : quiz_feedback_style::classic
    );
    view.show();
    auto fixture = owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1, 4 }
    );
    auto settings = fixture->configuration();
    settings.failure = gameplay::failure_policy::lives;
    settings.initial_lives = 2;
    QVERIFY(fixture->configure(settings));
    std::vector<gameplay::prepared_deck> streams;
    for (std::size_t id = 0; id < fixture->size(); ++id) {
        streams.push_back(fixture->deck({ id })->stream);
    }
    streams[0].initial_running_count = std::numeric_limits<std::int64_t>::min();
    streams[0].rank_weights.fill(0);
    QVERIFY(
        fixture->prepare_decks(streams, std::array<std::uint64_t, 2> { 1, 4 })
    );
    QVERIFY(view.install_gameplay_session(std::move(fixture)));
    QSignalSpy legacy(&view, &table::score_adjusted);
    QSignalSpy resolved(&view, &table::gameplay_quiz_resolved);
    QVERIFY(view.start_gameplay_runtime(41));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    auto& owner = *view.active_gameplay_session();
    const std::array entered {
        QString::number(std::numeric_limits<std::int64_t>::max()),
        QStringLiteral("-7"), QStringLiteral("101"), QStringLiteral("-")
    };
    for (std::size_t id = 0; id < view.slot_widgets.size(); ++id) {
        auto* slot = view.slot_widgets[id];
        auto* opener = slot->findChild<QToolButton*>(
            QStringLiteral("compact_slot_controls")
        );
        QVERIFY(opener);
        if (compact) {
            QVERIFY(opener->isVisible());
            opener->click();
        }
        auto* input = owned_input(slot);
        QVERIFY(input->isVisible() && input->isEnabled());
        enter_owned_count(input, entered[id]);
        if (compact) {
            slot->findChild<QDialog*>(QStringLiteral("slot_controls_dialog"))
                ->reject();
        }
    }
    // Timeout snapshots valid input memory, never coercing the final '-' draft.
    const std::array records { *owner.deck({ 0 }), *owner.deck({ 1 }),
                               *owner.deck({ 2 }), *owner.deck({ 3 }) };
    QVERIFY(view.pause_gameplay_runtime());
    const auto frozen = *owner.current_quiz_batch();
    QVERIFY(!view.advance_gameplay_runtime(900000, answers));
    QCOMPARE(*owner.current_quiz_batch(), frozen);
    QVERIFY(answers.empty());
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QVERIFY(view.advance_gameplay_runtime(60000, answers));
    QCOMPARE(answers.size(), std::size_t { 4 });
    view.publish_gameplay_runtime(answers);
    QCOMPARE(resolved.count(), 4);
    QCOMPARE(owner.phase(), gameplay::session_phase::finished);
    QCOMPARE(owner.statistics().errors, std::uint64_t { 2 });
    QCOMPARE(owner.statistics().skips, std::uint64_t { 0 });
    QCOMPARE(owner.remaining_lives(), 0);
    QCOMPARE(owner.deck({ 0 })->statistics.errors, std::uint64_t { 1 });
    QCOMPARE(owner.deck({ 2 })->statistics.verified_cards, std::uint64_t { 1 });
    QVERIFY(!view.gameplay_timer.isActive());
    QVERIFY(owner.result());
    const auto result = *owner.result();
    const auto timing = owner.timing();
    QCOMPARE(view.gameplay_corrections().size(), std::size_t { 3 });
    const auto generation = view.next_shared_generation_id;
    auto* cache = view.shared_raster_cache_service();
    for (const auto& answer : answers) {
        const auto id = answer.owner.value;
        QVERIFY(answer.timed_out);
        QCOMPARE(answer.submitted_count, records[id].latest_input);
        QCOMPARE(answer.expected_count, records[id].quiz->expected_count);
        auto* slot = view.slot_widgets[id];
        auto* opener = slot->findChild<QToolButton*>(
            QStringLiteral("compact_slot_controls")
        );
        if (answer.outcome == gameplay::quiz_outcome::correct) {
            QCOMPARE(answer.owner, gameplay::deck_id { 2 });
            QVERIFY(!opener->isVisible());
            continue;
        }
        QCOMPARE(answer.outcome, gameplay::quiz_outcome::wrong);
        if (compact) {
            QVERIFY(opener->isVisible());
            opener->click();
        }
        auto* feedback
            = slot->findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
        auto* heading
            = slot->findChild<QLabel*>(QStringLiteral("quiz_feedback_stamp"));
        auto* dismiss = slot->findChild<QPushButton*>(
            QStringLiteral("quiz_continue_button")
        );
        QVERIFY(feedback && feedback->isVisible() && heading);
        QVERIFY(dismiss && dismiss->isVisible() && dismiss->isEnabled());
        QCOMPARE(feedback->textFormat(), Qt::PlainText);
        QVERIFY(
            feedback->text().contains(QString::number(answer.submitted_count))
        );
        QVERIFY(
            feedback->text().contains(QString::number(answer.expected_count))
        );
        const auto before = *owner.deck(answer.owner);
        const auto correction = view.gameplay_corrections();
        for (const bool next_stamp : { !stamp, stamp }) {
            view.set_quiz_presentation(
                input_style,
                next_stamp ? quiz_feedback_style::stamp
                           : quiz_feedback_style::classic
            );
            QCOMPARE(heading->isVisible(), next_stamp);
            QCOMPARE(feedback->styleSheet().isEmpty(), !next_stamp);
            QVERIFY(feedback->isVisible());
            QCOMPARE(view.gameplay_corrections(), correction);
            QCOMPARE(*owner.deck(answer.owner), before);
            QCOMPARE(*owner.result(), result);
            QCOMPARE(owner.timing(), timing);
            QCOMPARE(view.shared_raster_cache_service(), cache);
            QCOMPARE(view.next_shared_generation_id, generation);
        }
        dismiss->click(); // native dismissal cannot mutate terminal accounting
        QCOMPARE(*owner.deck(answer.owner), before);
        QCOMPARE(*owner.result(), result);
        QCOMPARE(owner.timing(), timing);
        QCOMPARE(view.gameplay_corrections().size(), correction.size() - 1);
        QVERIFY(!view.gameplay_quiz_prompt(answer.owner));
        QVERIFY(!opener->isVisible());
    }
    QVERIFY(view.gameplay_corrections().empty());
    QCOMPARE(legacy.count(), 0);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
