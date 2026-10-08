// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QtTest/QtTest>

#include <array>

using namespace table_test;

void table_tests::action_styles_preserve_owned_quiz_data() {
    QTest::addColumn<int>("style_id");
    QTest::addColumn<bool>("compact");
    QTest::addColumn<bool>("multi");
    for (const auto style :
         { slot_action_style::classic, slot_action_style::rail,
           slot_action_style::pills }) {
        for (const bool compact : { false, true }) {
            for (const bool multi : { false, true }) {
                QTest::newRow(
                    qPrintable(QStringLiteral("style=%1/compact=%2/multi=%3")
                                   .arg(static_cast<int>(style))
                                   .arg(compact)
                                   .arg(multi))
                ) << static_cast<int>(style)
                  << compact << multi;
            }
        }
    }
}

void table_tests::action_styles_preserve_owned_quiz() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop activated action compatibility");
#endif
    QFETCH(int, style_id);
    QFETCH(bool, compact);
    QFETCH(bool, multi);
    const auto style = static_cast<slot_action_style>(style_id);
    table view;
    set_gameplay_motion_style(view, 0);
    view.set_action_style(style);
    view.resize(compact ? QSize(180, 120) : QSize(1800, 1200));
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        multi ? gameplay::quiz_scope::multi : gameplay::quiz_scope::single,
        { 1, 4 }
    )));
    auto& owner = *view.active_gameplay_session();
    const auto widgets = view.slot_widgets;
    auto* setup_slot = widgets[0];
    auto* details = setup_slot->findChild<QPushButton*>(
        QStringLiteral("slot_details_button")
    );
    auto* setup_swap = setup_slot->findChild<QPushButton*>(
        QStringLiteral("slot_swap_button")
    );
    auto* setup_opener = setup_slot->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    QVERIFY(details && setup_swap && setup_opener);
    QVERIFY(details->isEnabled());
    QVERIFY(!setup_swap->isEnabled());
    const auto setup = *owner.deck({ 0 });
    QSignalSpy legacy_pause(&view, &table::dialog_opened);
    QSignalSpy legacy_score(&view, &table::score_adjusted);
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    if (setup_opener->isVisible()) {
        setup_opener->click();
    }
    QTest::keyClick(details, Qt::Key_Space);
    auto* panel = setup_slot->findChild<QFrame*>(
        QStringLiteral("slot_settings_editor")
    );
    QVERIFY(panel && panel->isVisible());
    auto* buttons = panel->findChild<QDialogButtonBox*>();
    QVERIFY(buttons);
    QTest::mouseClick(
        buttons->button(QDialogButtonBox::Cancel), Qt::LeftButton
    );
    QCOMPARE(*owner.deck({ 0 }), setup);
    QVERIFY(!panel->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QVERIFY(view.start_gameplay_runtime(51));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(owner.unresolved_quizzes().size(), std::size_t { 4 });
    QVERIFY(owner.current_quiz_batch()->remaining_ms);
    // A frozen quiz is still Running, not permission to show paused actions.
    QSignalSpy denied_swaps(setup_slot, &table_slot::swap_clicked);
    QVERIFY(!details->isEnabled() && !setup_swap->isEnabled());
    QVERIFY(
        QMetaObject::invokeMethod(
            setup_slot, "on_swap_button_clicked", Qt::DirectConnection
        )
    );
    QVERIFY(
        QMetaObject::invokeMethod(
            setup_slot, "on_settings_button_clicked", Qt::DirectConnection
        )
    );
    QCOMPARE(denied_swaps.count(), 0);
    QVERIFY(
        !setup_slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"))
    );

    auto* failed = widgets[3];
    auto* failed_opener = failed->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    QVERIFY(failed_opener);
    if (failed_opener->isVisible()) {
        failed_opener->click();
    }
    enter_owned_count(owned_input(failed), QStringLiteral("-7"));
    failed->findChild<QPushButton*>(QStringLiteral("quiz_answer_button"))
        ->click();
    QCOMPARE(owner.deck({ 3 })->status, gameplay::deck_status::failed);
    auto* source = widgets[2];
    auto* input = owned_input(source);
    auto* source_opener = source->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    QVERIFY(source_opener && input);
    if (source_opener->isVisible()) {
        source_opener->click();
    }
    QVERIFY(input->isVisible() && input->isEnabled());
    enter_owned_count(input, QStringLiteral("+17"));
    input->setSelection(1, 2);
    const auto cursor = input->cursorPosition();
    const auto selection = input->selectedText();
    const auto prompt = view.gameplay_quiz_prompt({ 2 });
    QVERIFY(prompt);
    QVERIFY(view.pause_gameplay_runtime());
    QVERIFY(!input->isEnabled());
    auto* swap
        = source->findChild<QPushButton*>(QStringLiteral("slot_swap_button"));
    auto* target_swap
        = failed->findChild<QPushButton*>(QStringLiteral("slot_swap_button"));
    QVERIFY(swap && swap->isVisible() && swap->isEnabled());
    QTest::keyClick(swap, Qt::Key_Space);
    QVERIFY(source->swap_selected() && swap->isChecked());
    const std::array records { *owner.deck({ 0 }), *owner.deck({ 1 }),
                               *owner.deck({ 2 }), *owner.deck({ 3 }) };
    const auto timing = owner.timing();
    const auto statistics = owner.statistics();
    const auto batch = *owner.current_quiz_batch();
    const auto corrections = view.gameplay_corrections();
    const auto source_position = owner.slot_for({ 2 });
    const auto target_position = owner.slot_for({ 3 });
    std::array<QRect, 4> bounds;
    std::array<int, 4> demand;
    for (std::size_t id = 0; id < widgets.size(); ++id) {
        bounds[id] = widgets[id]->geometry();
        demand[id] = widgets[id]->card_face_need_short_px();
    }
    auto* cache = view.shared_raster_cache_service();
    const auto generation = view.next_shared_generation_id;
    auto* host
        = source->findChild<QDialog*>(QStringLiteral("slot_controls_dialog"));
    for (const auto next_style :
         { slot_action_style::classic, slot_action_style::rail,
           slot_action_style::pills, style }) {
        view.set_action_style(next_style);
        QCOMPARE(
            source->findChild<QPushButton*>(QStringLiteral("slot_swap_button")),
            swap
        );
        QVERIFY(source->swap_selected() && swap->isChecked());
        if (host) {
            QCOMPARE(
                source->findChild<QDialog*>(
                    QStringLiteral("slot_controls_dialog")
                ),
                host
            );
            QCOMPARE(
                host->findChild<QLineEdit*>(
                    QStringLiteral("gameplay_quiz_input")
                ),
                input
            );
            QVERIFY(host->isVisible());
        }
        for (std::size_t id = 0; id < widgets.size(); ++id) {
            auto* slot = widgets[id];
            auto* info = slot->findChild<QPushButton*>(
                QStringLiteral("slot_details_button")
            );
            auto* move = slot->findChild<QPushButton*>(
                QStringLiteral("slot_swap_button")
            );
            QVERIFY(info && move && info->isEnabled() && move->isEnabled());
            QCOMPARE(
                info->text().isEmpty(), next_style == slot_action_style::rail
            );
            QCOMPARE(
                move->text().isEmpty(), next_style == slot_action_style::rail
            );
            QCOMPARE(info->accessibleName(), str_label("Details"));
            QCOMPARE(move->accessibleName(), str_label("Swap"));
            QVERIFY(!info->toolTip().isEmpty() && !move->toolTip().isEmpty());
            QVERIFY(!info->icon().isNull() && !move->icon().isNull());
            for (const auto* name :
                 { "slot_copy_button", "slot_copy_all_button" }) {
                auto* copy
                    = slot->findChild<QPushButton*>(QString::fromLatin1(name));
                QVERIFY(copy && copy->isHidden() && !copy->isEnabled());
            }
            QVERIFY(
                QMetaObject::invokeMethod(
                    slot, "on_copy_button_clicked", Qt::DirectConnection
                )
            );
            QVERIFY(
                QMetaObject::invokeMethod(
                    slot, "on_copy_all_button_clicked", Qt::DirectConnection
                )
            );
            QCOMPARE(*owner.deck({ id }), records[id]);
            QCOMPARE(slot->geometry(), bounds[id]);
            QCOMPARE(slot->card_face_need_short_px(), demand[id]);
        }
        QCOMPARE(owner.timing(), timing);
        QCOMPARE(owner.statistics(), statistics);
        QCOMPARE(*owner.current_quiz_batch(), batch);
        QCOMPARE(view.gameplay_corrections(), corrections);
        QCOMPARE(view.gameplay_quiz_prompt({ 2 }), prompt);
        QCOMPARE(input->text(), QStringLiteral("+17"));
        QCOMPARE(input->cursorPosition(), cursor);
        QCOMPARE(input->selectedText(), selection);
        QCOMPARE(view.shared_raster_cache_service(), cache);
        QCOMPARE(view.next_shared_generation_id, generation);
    }
    if (failed_opener->isVisible()) {
        failed_opener->click();
    }
    QVERIFY(target_swap->isVisible() && target_swap->isEnabled());
    QSignalSpy mapping(&view, &table::gameplay_layout_changed);
    QTest::mouseClick(target_swap, Qt::LeftButton);
    QCOMPARE(mapping.count(), 1);
    QCOMPARE(owner.slot_for({ 2 }), target_position);
    QCOMPARE(owner.slot_for({ 3 }), source_position);
    QVERIFY(!swap->isChecked() && !source->swap_selected());
    view.set_card_orientation(card_orientation_mode::horizontal);
    QCOMPARE(view.slot_widgets, widgets);
    for (std::size_t id = 0; id < records.size(); ++id) {
        QCOMPARE(*owner.deck({ id }), records[id]);
    }
    QCOMPARE(*owner.current_quiz_batch(), batch);
    QCOMPARE(owner.timing(), timing);
    QCOMPARE(owner.statistics(), statistics);
    QCOMPARE(view.gameplay_quiz_prompt({ 2 }), prompt);
    QCOMPARE(input->text(), QStringLiteral("+17"));
    QCOMPARE(input->cursorPosition(), cursor);
    QCOMPARE(input->selectedText(), selection);
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QVERIFY(!swap->isEnabled() && !details->isEnabled());
    if (source_opener->isVisible()) {
        source_opener->click();
    }
    enter_owned_count(
        input, QString::number(owner.deck({ 2 })->quiz->expected_count)
    );
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(!owner.deck({ 2 })->quiz);
    QCOMPARE(owner.deck({ 2 })->statistics.verified_cards, std::uint64_t { 1 });
    QCOMPARE(*owner.deck({ 3 }), records[3]);
    QVERIFY(view.pause_gameplay_runtime());
    if (setup_opener->isVisible()) {
        setup_opener->click();
    }
    QTest::keyClick(setup_swap, Qt::Key_Space);
    QVERIFY(setup_slot->swap_selected());
    QVERIFY(view.finish_gameplay_runtime());
    QVERIFY(!setup_swap->isChecked() && !setup_slot->swap_selected());
    QVERIFY(!setup_swap->isEnabled() && !details->isEnabled());
    QVERIFY(owner.result());
    const auto result = *owner.result();
    for (const auto next_style :
         { slot_action_style::rail, slot_action_style::pills, style }) {
        view.set_action_style(next_style);
        QVERIFY(
            QMetaObject::invokeMethod(
                setup_slot, "on_swap_button_clicked", Qt::DirectConnection
            )
        );
        QVERIFY(
            QMetaObject::invokeMethod(
                setup_slot, "on_settings_button_clicked", Qt::DirectConnection
            )
        );
        QVERIFY(!setup_slot->findChild<QFrame*>(
            QStringLiteral("slot_settings_editor")
        ));
        QCOMPARE(*owner.result(), result);
    }
    QCOMPARE(
        denied_swaps.count(), 1
    ); // only the final allowed paused selection
    QCOMPARE(legacy_pause.count(), 0);
    QCOMPARE(legacy_score.count(), 0);
    QCOMPARE(preparation.count(), 0);
}

void table_tests::action_styles_preserve_terminal_swaps_data() {
    QTest::addColumn<int>("style_id");
    QTest::addColumn<bool>("compact");
    for (const auto style :
         { slot_action_style::classic, slot_action_style::rail,
           slot_action_style::pills }) {
        for (const bool compact : { false, true }) {
            QTest::newRow(
                qPrintable(QStringLiteral("style=%1/compact=%2")
                               .arg(static_cast<int>(style))
                               .arg(compact))
            ) << static_cast<int>(style)
              << compact;
        }
    }
}

void table_tests::action_styles_preserve_terminal_swaps() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop activated action compatibility");
#endif
    QFETCH(int, style_id);
    QFETCH(bool, compact);
    const auto style = static_cast<slot_action_style>(style_id);
    table view;
    set_gameplay_motion_style(view, 0);
    view.set_action_style(style);
    view.resize(compact ? QSize(180, 120) : QSize(1800, 1200));
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture()));
    auto& owner = *view.active_gameplay_session();
    const auto widgets = view.slot_widgets;
    QVERIFY(view.start_gameplay_runtime(52));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    auto* failed = widgets[3];
    auto* failed_opener = failed->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    QVERIFY(failed_opener);
    if (failed_opener->isVisible()) {
        failed_opener->click();
    }
    enter_owned_count(owned_input(failed), QStringLiteral("-7"));
    failed->findChild<QPushButton*>(QStringLiteral("quiz_answer_button"))
        ->click();
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(owner.deck({ 1 })->status, gameplay::deck_status::completed);
    QCOMPARE(owner.deck({ 3 })->status, gameplay::deck_status::failed);
    QCOMPARE(owner.deck({ 0 })->status, gameplay::deck_status::active);
    QVERIFY(owner.deck({ 2 })->quiz);
    QVERIFY(view.pause_gameplay_runtime());
    const std::array records { *owner.deck({ 0 }), *owner.deck({ 1 }),
                               *owner.deck({ 2 }), *owner.deck({ 3 }) };
    const auto timing = owner.timing();
    const auto statistics = owner.statistics();
    const auto prompt = view.gameplay_quiz_prompt({ 2 });
    auto* completed = widgets[1];
    auto* source_opener = completed->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    auto* swap = completed->findChild<QPushButton*>(
        QStringLiteral("slot_swap_button")
    );
    auto* target_swap
        = failed->findChild<QPushButton*>(QStringLiteral("slot_swap_button"));
    QVERIFY(source_opener && swap && target_swap);
    if (source_opener->isVisible()) {
        source_opener->click();
    }
    QVERIFY(swap->isVisible() && swap->isEnabled());
    const auto source_position = owner.slot_for({ 1 });
    const auto target_position = owner.slot_for({ 3 });
    QSignalSpy mapping(&view, &table::gameplay_layout_changed);
    QTest::keyClick(swap, Qt::Key_Space);
    QVERIFY(completed->swap_selected() && swap->isChecked());
    view.set_action_style(slot_action_style::pills);
    view.set_action_style(slot_action_style::rail);
    view.set_action_style(style);
    QVERIFY(completed->swap_selected() && swap->isChecked());
    QTest::keyClick(swap, Qt::Key_Space); // cancel, never Swap with itself
    QVERIFY(!completed->swap_selected() && !swap->isChecked());
    QCOMPARE(mapping.count(), 0);
    QCOMPARE(owner.slot_for({ 1 }), source_position);
    QTest::keyClick(swap, Qt::Key_Space);
    if (failed_opener->isVisible()) {
        failed_opener->click();
    }
    QVERIFY(target_swap->isVisible() && target_swap->isEnabled());
    QTest::mouseClick(target_swap, Qt::LeftButton);
    QCOMPARE(mapping.count(), 1);
    QCOMPARE(owner.slot_for({ 1 }), target_position);
    QCOMPARE(owner.slot_for({ 3 }), source_position);
    QCOMPARE(view.slot_widgets, widgets);
    for (std::size_t id = 0; id < records.size(); ++id) {
        QCOMPARE(*owner.deck({ id }), records[id]);
    }
    QCOMPARE(owner.timing(), timing);
    QCOMPARE(owner.statistics(), statistics);
    QCOMPARE(view.gameplay_quiz_prompt({ 2 }), prompt);
    QVERIFY(owned_card(completed)->accessibleDescription().startsWith(
        str_label("Completed deck.")
    ));
    QVERIFY(owned_card(failed)->accessibleDescription().startsWith(
        str_label("Failed deck. Card back.")
    ));

    if (source_opener->isVisible()) {
        source_opener->click();
    }
    QTest::keyClick(swap, Qt::Key_Space);
    QVERIFY(completed->swap_selected());
    QPointer<table_slot> old_slot = completed;
    QPointer<QPushButton> old_swap = swap;
    QPointer<QDialog> old_host = completed->findChild<QDialog*>(
        QStringLiteral("slot_controls_dialog")
    );
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(2)));
    QVERIFY(old_slot.isNull() && old_swap.isNull() && old_host.isNull());
    QVERIFY(!view.swap_source_slot && !view.copy_source_slot);
    for (auto* slot : view.slot_widgets) {
        auto* move
            = slot->findChild<QPushButton*>(QStringLiteral("slot_swap_button"));
        QVERIFY(move && !move->isEnabled() && !move->isChecked());
        QCOMPARE(move->text().isEmpty(), style == slot_action_style::rail);
        QVERIFY(!slot->swap_selected());
    }
    view.clear_gameplay_session();
    view.set_slot_count(2);
    auto* legacy_source = view.slot_widgets[0];
    auto* legacy_target = view.slot_widgets[1];
    auto* copy = legacy_source->findChild<QPushButton*>(
        QStringLiteral("slot_copy_button")
    );
    auto* target_copy = legacy_target->findChild<QPushButton*>(
        QStringLiteral("slot_copy_button")
    );
    QVERIFY(copy && target_copy && copy->isEnabled() && !copy->isHidden());
    QCOMPARE(copy->text().isEmpty(), style == slot_action_style::rail);
    copy->click();
    QVERIFY(copy->isChecked());
    QCOMPARE(copy->accessibleName(), str_label("Cancel"));
    QCOMPARE(target_copy->accessibleName(), str_label("Set"));
    target_copy->click();
    QVERIFY(!copy->isChecked());
    QCOMPARE(copy->accessibleName(), str_label("Copy"));
    QVERIFY(!view.copy_source_slot && !view.swap_source_slot);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
