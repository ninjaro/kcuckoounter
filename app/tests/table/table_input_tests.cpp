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

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QtTest/QtTest>

#include <array>
#include <limits>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::gameplay_controls_numeric_and_chips_data() {
    QTest::addColumn<bool>("chips");
    QTest::addColumn<bool>("stamp");
    QTest::addColumn<bool>("compact");
    QTest::addColumn<bool>("horizontal");
    for (const bool chips : { false, true })
        for (const bool stamp : { false, true })
            for (const bool compact : { false, true })
                for (const bool horizontal : { false, true })
                    QTest::newRow(
                        qPrintable(QStringLiteral("%1-%2-%3-%4")
                                       .arg(chips)
                                       .arg(stamp)
                                       .arg(compact)
                                       .arg(horizontal))
                    ) << chips
                      << stamp << compact << horizontal;
}

void table_tests::gameplay_controls_numeric_and_chips() {
    QFETCH(bool, chips);
    QFETCH(bool, stamp);
    QFETCH(bool, compact);
    QFETCH(bool, horizontal);
    table view;
    view.resize(compact ? QSize(180, 120) : QSize(1200, 900));
    view.set_card_orientation(
        horizontal ? card_orientation_mode::horizontal
                   : card_orientation_mode::vertical
    );
    view.set_frame_style(
        stamp ? slot_frame_style::thin : slot_frame_style::classic
    );
    view.set_quiz_presentation(
        chips ? quiz_answer_style::chips : quiz_answer_style::numeric,
        stamp ? quiz_feedback_style::stamp : quiz_feedback_style::classic
    );
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        1, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1, 3 }
    )));
    auto* slot = view.slot_widgets.front();
    auto* input = owned_input(slot);
    QVERIFY(input);
    QVERIFY(!input->isEnabled());
    QVERIFY(view.start_gameplay_runtime(31));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    if (compact) {
        auto* opener = slot->findChild<QToolButton*>(
            QStringLiteral("compact_slot_controls")
        );
        QVERIFY(opener && opener->isVisible());
        opener->click();
        auto* dialog
            = slot->findChild<QDialog*>(QStringLiteral("slot_controls_dialog"));
        QVERIFY(dialog && dialog->isVisible());
        QCOMPARE(
            dialog->findChild<QLineEdit*>(
                QStringLiteral("gameplay_quiz_input")
            ),
            input
        );
    }
    QVERIFY(input->isVisible());
    QVERIFY(input->isEnabled());
    QVERIFY(view.pause_gameplay_runtime());
    QVERIFY(!input->isEnabled());
    auto* opener = slot->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    if (opener->isVisible())
        opener->click();
    auto* details
        = slot->findChild<QPushButton*>(QStringLiteral("slot_details_button"));
    QVERIFY(details && details->isVisible() && details->isEnabled());
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QVERIFY(!slot->findChild<QSpinBox*>(QStringLiteral("quiz_spin_box"))
                 ->isEnabled());
    QVERIFY(
        slot->findChild<QSpinBox*>(QStringLiteral("quiz_spin_box"))->isHidden()
    );
    const auto expected
        = view.gameplay_owner->deck({ 0 })->quiz->expected_count;
    QVERIFY(expected < std::numeric_limits<int>::min());
    enter_owned_count(input, QString::number(expected));
    QCOMPARE(view.gameplay_owner->deck({ 0 })->latest_input, expected);
    if (chips) {
        auto* plus
            = slot->findChild<QPushButton*>(QStringLiteral("quiz_chip_1"));
        auto* minus
            = slot->findChild<QPushButton*>(QStringLiteral("quiz_chip_-1"));
        QVERIFY(plus->isVisible() && plus->isEnabled());
        QCOMPARE(plus->text(), QStringLiteral("+1"));
        QCOMPARE(minus->text(), QStringLiteral("−1"));
        QVERIFY(!plus->accessibleName().contains(QStringLiteral("%1")));
        QVERIFY(!plus->accessibleName().contains(
            QStringLiteral("I18N_ARGUMENT_MISSING")
        ));
        plus->click();
        QCOMPARE(view.gameplay_owner->deck({ 0 })->latest_input, expected + 1);
        minus->click();
        QCOMPARE(view.gameplay_owner->deck({ 0 })->latest_input, expected);
    }
    QSignalSpy legacy_score(&view, &table::score_adjusted);
    QSignalSpy resolved(&view, &table::gameplay_quiz_resolved);
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(resolved.count(), 1);
    QVERIFY(!view.gameplay_owner->deck({ 0 })->quiz);
    QCOMPARE(legacy_score.count(), 0);
    QCOMPARE(
        view.gameplay_owner->deck({ 0 })->statistics.errors, std::uint64_t { 0 }
    );
    QWidget other_window;
    other_window.show();
    other_window.activateWindow();
    QTest::qWait(1);
    QCOMPARE(QApplication::activeWindow(), &other_window);
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(input->text(), QString::number(expected));
    QCOMPARE(QApplication::activeWindow(), &other_window);
}

void table_tests::gameplay_controls_invalid_drafts_and_signed_boundaries() {
    table view;
    view.resize(1200, 900);
    view.set_quiz_presentation(
        quiz_answer_style::chips, quiz_feedback_style::classic
    );
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        1, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1 }
    )));
    QVERIFY(view.start_gameplay_runtime(32));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    auto* slot = view.slot_widgets.front();
    auto* input = owned_input(slot);
    auto* check
        = slot->findChild<QPushButton*>(QStringLiteral("quiz_answer_button"));
    enter_owned_count(input, QStringLiteral("9223372036854775807"));
    QCOMPARE(
        view.gameplay_owner->deck({ 0 })->latest_input,
        std::numeric_limits<std::int64_t>::max()
    );
    for (const int step : { 1, 2 }) {
        auto* chip = slot->findChild<QPushButton*>(
            QStringLiteral("quiz_chip_%1").arg(step)
        );
        QVERIFY(!chip->isEnabled());
        chip->click();
    }
    enter_owned_count(input, QStringLiteral("-9223372036854775808"));
    QCOMPARE(
        view.gameplay_owner->deck({ 0 })->latest_input,
        std::numeric_limits<std::int64_t>::min()
    );
    for (const int step : { -2, -1 })
        QVERIFY(!slot->findChild<QPushButton*>(
                         QStringLiteral("quiz_chip_%1").arg(step)
        )
                     ->isEnabled());
    const auto last_valid = view.gameplay_owner->deck({ 0 })->latest_input;
    enter_owned_count(input, QStringLiteral("-9223372036854775809"));
    QCOMPARE(input->text(), QStringLiteral("-9223372036854775809"));
    QVERIFY(!check->isEnabled());
    QVERIFY(slot->findChild<QLabel*>(QStringLiteral("gameplay_quiz_validation"))
                ->isVisible());
    QCOMPARE(
        view.gameplay_owner->deck({ 0 })->latest_input,
        QStringLiteral("-922337203685477580").toLongLong()
    );
    // Invalid final character has no integer meaning: retain the last valid
    // prefix edit, not a clipped/saturated value or the earlier submitted
    // input.
    const auto before = *view.gameplay_owner->deck({ 0 });
    check->click();
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(*view.gameplay_owner->deck({ 0 }), before);
    enter_owned_count(input, QStringLiteral("-"));
    QVERIFY(!check->isEnabled());
    view.refresh_gameplay_session();
    QCOMPARE(input->text(), QStringLiteral("-"));
    QCOMPARE(*view.gameplay_owner->deck({ 0 }), before);
    enter_owned_count(input, QString::number(last_valid));
    QVERIFY(check->isEnabled());
    QCOMPARE(view.gameplay_owner->deck({ 0 })->latest_input, last_valid);
}

void table_tests::gameplay_controls_pause_skip_and_timeout() {
    table view;
    view.resize(1800, 1200);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1, 4 }, false
    )));
    QVERIFY(view.start_gameplay_runtime(33));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    enter_owned_count(
        owned_input(view.slot_widgets[0]),
        QString::number(view.gameplay_owner->deck({ 0 })->quiz->expected_count)
    );
    enter_owned_count(owned_input(view.slot_widgets[1]), QStringLiteral("44"));
    enter_owned_count(owned_input(view.slot_widgets[2]), QStringLiteral("101"));
    enter_owned_count(owned_input(view.slot_widgets[3]), QStringLiteral("-"));
    const auto frozen = *view.gameplay_owner->current_quiz_batch();
    const auto records = std::array { *view.gameplay_owner->deck({ 0 }),
                                      *view.gameplay_owner->deck({ 1 }),
                                      *view.gameplay_owner->deck({ 2 }),
                                      *view.gameplay_owner->deck({ 3 }) };
    QVERIFY(view.pause_gameplay_runtime());
    for (std::size_t id = 0; id < view.slot_widgets.size(); ++id) {
        auto* slot = view.slot_widgets[id];
        QVERIFY(!owned_input(slot)->isEnabled());
        QVERIFY(
            !slot->findChild<QPushButton*>(QStringLiteral("quiz_answer_button"))
                 ->isEnabled()
        );
        auto* skip
            = slot->findChild<QPushButton*>(QStringLiteral("quiz_skip_button"));
        QVERIFY(skip->isHidden());
        skip->click();
        QCOMPARE(*view.gameplay_owner->deck({ id }), records[id]);
    }
    QCOMPARE(*view.gameplay_owner->current_quiz_batch(), frozen);
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QCOMPARE(owned_input(view.slot_widgets[3])->text(), QStringLiteral("-"));
    QSignalSpy resolved(&view, &table::gameplay_quiz_resolved);
    QVERIFY(view.advance_gameplay_runtime(900000, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(resolved.count(), 4);
    for (const auto& answer : answers) {
        QVERIFY(answer.timed_out);
        QVERIFY(answer.outcome != gameplay::quiz_outcome::skipped);
        QCOMPARE(
            answer.submitted_count, records[answer.owner.value].latest_input
        );
    }
    QCOMPARE(
        view.gameplay_owner->deck({ 2 })->statistics.verified_cards,
        std::uint64_t { 1 }
    );
    QCOMPARE(view.gameplay_owner->statistics().skips, std::uint64_t { 0 });
    QVERIFY(!view.gameplay_status_text().contains(str_label("Quiz remaining")));
}

void table_tests::controls_follow_packing_focus_and_swap_data() {
    QTest::addColumn<bool>("multi");
    QTest::addColumn<bool>("compact");
    QTest::newRow("parallel-single") << false << false;
    QTest::newRow("multi") << true << false;
    QTest::newRow("compact-parallel-single") << false << true;
    QTest::newRow("compact-multi") << true << true;
}

void table_tests::controls_follow_packing_focus_and_swap() {
    QFETCH(bool, multi);
    QFETCH(bool, compact);
    table view;
    view.resize(compact ? QSize(320, 240) : QSize(1800, 1200));
    view.show();
    view.activateWindow();
    QTest::qWait(1);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        multi ? gameplay::quiz_scope::multi : gameplay::quiz_scope::single,
        { 1, 4 }
    )));
    QVERIFY(view.start_gameplay_runtime(34));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    const auto order = view.gameplay_owner->unresolved_quizzes();
    QCOMPARE(order.size(), std::size_t { 4 });
    auto input = [&](gameplay::deck_id id) {
        return owned_input(view.slot_widgets[id.value]);
    };
    const auto focus_target = [&](gameplay::deck_id id) -> QWidget* {
        auto* opener = view.slot_widgets[id.value]->findChild<QToolButton*>(
            QStringLiteral("compact_slot_controls")
        );
        return compact ? static_cast<QWidget*>(opener) : input(id);
    };
    const auto open_prompt = [&](gameplay::deck_id id) {
        if (compact) {
            auto* opener = qobject_cast<QToolButton*>(focus_target(id));
            opener->click();
            input(id)->window()->activateWindow();
            QTest::qWait(1);
        }
    };
    for (const auto id : order) {
        QVERIFY(focus_target(id)->isVisible());
        QVERIFY(input(id)->isEnabled());
    }
    QCOMPARE(QApplication::focusWidget(), focus_target(order[0]));
    QTest::keyClick(focus_target(order[0]), Qt::Key_Tab);
    QCOMPARE(QApplication::focusWidget(), focus_target(order[1]));
    QTest::keyClick(focus_target(order[1]), Qt::Key_Backtab);
    QCOMPARE(QApplication::focusWidget(), focus_target(order[0]));
    open_prompt(order[0]);
    enter_owned_count(
        input(order[0]),
        QString::number(
            view.gameplay_owner->deck(order[0])->quiz->expected_count
        )
    );
    QTest::keyClick(input(order[0]), Qt::Key_Return);
    QTest::qWait(1);
    QCOMPARE(QApplication::focusWidget(), focus_target(order[1]));
    open_prompt(order[1]);
    auto* skip = view.slot_widgets[order[1].value]->findChild<QPushButton*>(
        QStringLiteral("quiz_skip_button")
    );
    skip->click();
    QTest::qWait(1);
    QCOMPARE(QApplication::focusWidget(), focus_target(order[2]));
    const auto pending = *view.gameplay_owner->deck(order[2]);
    const auto key = *view.gameplay_quiz_prompt(order[2]);
    QVERIFY(view.pause_gameplay_runtime());
    QVERIFY(view.swap_gameplay_decks(order[2], order[0]));
    view.set_card_orientation(card_orientation_mode::horizontal);
    QCOMPARE(*view.gameplay_owner->deck(order[2]), pending);
    QCOMPARE(view.gameplay_quiz_prompt(order[2]), std::optional { key });
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QVERIFY(view.focus_gameplay_quiz());
    const auto changed_order = view.gameplay_owner->unresolved_quizzes();
    QCOMPARE(QApplication::focusWidget(), focus_target(changed_order.front()));
    QTest::keyClick(focus_target(changed_order.front()), Qt::Key_Tab);
    QCOMPARE(QApplication::focusWidget(), focus_target(changed_order.back()));
}

void table_tests::corrections_neither_gate_nor_overwrite_controls() {
    table view;
    view.resize(1800, 1200);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, false, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1, 2, 4 }
    )));
    QVERIFY(view.start_gameplay_runtime(35));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    auto* slot = view.slot_widgets[0];
    enter_owned_count(owned_input(slot), QStringLiteral("9"));
    slot->findChild<QPushButton*>(QStringLiteral("quiz_skip_button"))->click();
    const auto expected = -(std::int64_t { 1 } << 40) + 1;
    auto* feedback
        = slot->findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    QVERIFY(feedback->isVisible());
    QVERIFY(feedback->text().contains(QString::number(expected)));
    QCOMPARE(
        view.gameplay_owner->deck({ 0 })->latest_input, std::int64_t { 9 }
    );
    const auto clock = view.gameplay_owner->timing();
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    // The repeated Single question belongs to the same still-open batch.
    QCOMPARE(view.gameplay_owner->deck({ 0 })->next_card, std::size_t { 2 });
    QVERIFY(view.gameplay_owner->timing().play_time_ms > clock.play_time_ms);
    QCOMPARE(owned_input(slot)->text(), QStringLiteral("9"));
    auto* previous = slot->findChild<QLabel*>(
        QStringLiteral("gameplay_previous_correction")
    );
    QVERIFY(previous->isVisible());
    QVERIFY(previous->text().contains(QString::number(expected)));
    QVERIFY(!view.gameplay_status_text().contains(str_label("Quiz remaining")));
    const auto before = *view.gameplay_owner->deck({ 0 });
    // Dismiss is presentation only, never implicit Check/Skip/Continue dealing.
    QVERIFY(
        QMetaObject::invokeMethod(
            slot, "on_quiz_continue_button_clicked", Qt::DirectConnection
        )
    );
    QCOMPARE(*view.gameplay_owner->deck({ 0 }), before);
    QVERIFY(previous->isHidden());
    QSignalSpy legacy(&view, &table::score_adjusted);
    QTest::keyClick(owned_input(slot), Qt::Key_Return); // wrong Training count
    QVERIFY(feedback->text().contains(QString::number(expected + 1)));
    QCOMPARE(
        view.gameplay_owner->deck({ 0 })->statistics.errors, std::uint64_t { 1 }
    );
    QCOMPARE(view.gameplay_owner->statistics().errors, std::uint64_t { 0 });
    QCOMPARE(legacy.count(), 0);
}

void table_tests::gameplay_controls_compact_correction_stays_nonblocking() {
    table view;
    view.resize(180, 120);
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
    slot->findChild<QToolButton*>(QStringLiteral("compact_slot_controls"))
        ->click();
    auto* host
        = slot->findChild<QDialog*>(QStringLiteral("slot_controls_dialog"));
    enter_owned_count(owned_input(slot), QStringLiteral("9"));
    slot->findChild<QPushButton*>(QStringLiteral("quiz_skip_button"))->click();
    QVERIFY(host->isVisible());
    QVERIFY(slot->findChild<QLabel*>(QStringLiteral("quiz_feedback_label"))
                ->isVisible());
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QVERIFY(host->isVisible());
    QCOMPARE(view.gameplay_owner->deck({ 0 })->next_card, std::size_t { 3 });
    QCOMPARE(owned_input(slot)->text(), QStringLiteral("9"));
    QVERIFY(
        slot->findChild<QLabel*>(QStringLiteral("gameplay_previous_correction"))
            ->isVisible()
    );
    host->reject();
    QCOMPARE(
        view.gameplay_owner->deck({ 0 })->latest_input, std::int64_t { 9 }
    );
}

void table_tests::gameplay_controls_compact_host_and_replacement() {
    table view;
    view.resize(180, 120);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        1, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1 }
    )));
    QVERIFY(view.start_gameplay_runtime(36));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    auto* slot = view.slot_widgets[0];
    auto* opener = slot->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    QVERIFY(opener->isVisible());
    opener->click();
    QPointer<QDialog> host
        = slot->findChild<QDialog*>(QStringLiteral("slot_controls_dialog"));
    QPointer<QLineEdit> input = owned_input(slot);
    QVERIFY(host && host->isVisible());
    enter_owned_count(input, QStringLiteral("17"));
    view.resize(1600, 1000);
    QCOMPARE(
        host->findChild<QLineEdit*>(QStringLiteral("gameplay_quiz_input")),
        input.data()
    );
    QVERIFY(view.pause_gameplay_runtime());
    QVERIFY(!input->isEnabled());
    host->reject();
    QVERIFY(!host->isVisible());
    QVERIFY(input);
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    bool replaced = false;
    connect(&view, &table::gameplay_quiz_resolved, &view, [&](const auto&) {
        replaced = view.install_gameplay_session(owned_clock_fixture(1));
    });
    QPointer<table_slot> old_slot = slot;
    QVERIFY(
        QMetaObject::invokeMethod(
            slot, "on_quiz_answer_button_clicked", Qt::DirectConnection
        )
    );
    QVERIFY(replaced);
    QVERIFY(old_slot.isNull());
    QVERIFY(input.isNull());
    QVERIFY(host.isNull());
    QCOMPARE(view.gameplay_owner->phase(), gameplay::session_phase::setup);
    QVERIFY(!view.gameplay_timer.isActive());
}

void table_tests::control_refresh_keeps_caret_and_raster_owner() {
    table view;
    view.resize(1200, 900);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        1, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1 }
    )));
    QVERIFY(view.start_gameplay_runtime(37));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    auto* input = owned_input(view.slot_widgets.front());
    enter_owned_count(input, QStringLiteral("+12345"));
    input->setSelection(2, 2);
    const auto cursor = input->cursorPosition();
    const auto selected = input->selectedText();
    auto* cache = view.shared_raster_cache_service();
    const auto generation = view.next_shared_generation_id;
    for (int repeat = 0; repeat < 12; ++repeat) {
        QVERIFY(view.advance_gameplay_runtime(50, answers));
        view.publish_gameplay_runtime(answers);
    }
    QCOMPARE(input->text(), QStringLiteral("+12345"));
    QCOMPARE(input->cursorPosition(), cursor);
    QCOMPARE(input->selectedText(), selected);
    QCOMPARE(
        view.gameplay_owner->deck({ 0 })->latest_input, std::int64_t { 12345 }
    );
    QCOMPARE(view.shared_raster_cache_service(), cache);
    QCOMPARE(view.next_shared_generation_id, generation);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
