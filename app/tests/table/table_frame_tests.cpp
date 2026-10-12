// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QtTest/QtTest>

#include <array>

using namespace table_test;

void table_tests::frame_styles_preserve_owned_runtime_data() {
    QTest::addColumn<bool>("thin");
    QTest::addColumn<bool>("horizontal");
    QTest::addColumn<bool>("compact");
    for (const bool thin : { false, true }) {
        for (const bool horizontal : { false, true }) {
            for (const bool compact : { false, true }) {
                QTest::newRow(qPrintable(
                    QStringLiteral("thin=%1/horizontal=%2/compact=%3")
                        .arg(thin)
                        .arg(horizontal)
                        .arg(compact)
                )) << thin
                   << horizontal << compact;
            }
        }
    }
}

void table_tests::frame_styles_preserve_owned_runtime() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop activated frame compatibility");
#endif
    QFETCH(bool, thin);
    QFETCH(bool, horizontal);
    QFETCH(bool, compact);
    table view;
    set_gameplay_motion_style(view, 0);
    view.resize(compact ? QSize(240, 180) : QSize(1800, 1200));
    const auto frame
        = thin ? slot_frame_style::thin : slot_frame_style::classic;
    view.set_frame_style(frame);
    view.set_card_orientation(
        horizontal ? card_orientation_mode::horizontal
                   : card_orientation_mode::vertical
    );
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture()));
    auto& owner = *view.active_gameplay_session();
    QCOMPARE(owner.size(), std::size_t { 4 });
    const auto widgets = view.slot_widgets;
    auto* cache = view.shared_raster_cache_service();
    QSignalSpy legacy_score(&view, &table::score_adjusted);
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    QSignalSpy resolved(&view, &table::gameplay_quiz_resolved);
    QVERIFY(view.start_gameplay_runtime(101));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QVERIFY(owner.deck({ 2 })->quiz);
    QVERIFY(owner.deck({ 3 })->quiz);
    const auto prompt = view.gameplay_quiz_prompt({ 2 });
    QVERIFY(prompt);

    // Native Check drives Block failure; a pending peer does not freeze
    // dealing.
    auto* failed_slot = widgets[3];
    auto* failed_opener = failed_slot->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    QVERIFY(failed_opener);
    if (failed_opener->isVisible()) {
        failed_opener->click();
    }
    auto* failed_input = owned_input(failed_slot);
    QVERIFY(
        failed_input && failed_input->isVisible() && failed_input->isEnabled()
    );
    enter_owned_count(
        failed_input,
        QString::number(owner.deck({ 3 })->quiz->expected_count + 1)
    );
    auto* check = failed_slot->findChild<QPushButton*>(
        QStringLiteral("quiz_answer_button")
    );
    QVERIFY(check && check->isVisible() && check->isEnabled());
    check->click();
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(owner.deck({ 3 })->status, gameplay::deck_status::failed);
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(owner.deck({ 1 })->status, gameplay::deck_status::completed);
    QCOMPARE(owner.deck({ 0 })->status, gameplay::deck_status::active);
    QCOMPARE(owner.deck({ 2 })->status, gameplay::deck_status::active);
    QVERIFY(owner.deck({ 2 })->quiz);
    QCOMPARE(owner.timing().play_time_ms, std::uint64_t { 600 });
    QVERIFY(owned_card(widgets[1])
                ->accessibleDescription()
                .startsWith(str_label("Completed deck.")));
    QVERIFY(owned_card(widgets[3])
                ->accessibleDescription()
                .startsWith(str_label("Failed deck. Card back.")));
    QCOMPARE(
        owned_card(widgets[2])->accessibleDescription(),
        str_label("Card hidden")
    );
    QVERIFY(owned_card(widgets[0])
                ->accessibleDescription()
                .contains(QString::number(owner.deck({ 0 })->running_count)));

    auto* input = owned_input(widgets[2]);
    auto* opener = widgets[2]->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    QVERIFY(opener);
    if (opener->isVisible()) {
        opener->click();
    }
    QVERIFY(input && input->isVisible() && input->isEnabled());
    enter_owned_count(input, QStringLiteral("+17"));
    input->setSelection(1, 2);
    const auto cursor = input->cursorPosition();
    const auto selection = input->selectedText();
    QVERIFY(view.pause_gameplay_runtime());
    QVERIFY(input && !input->isEnabled());
    const std::array records { *owner.deck({ 0 }), *owner.deck({ 1 }),
                               *owner.deck({ 2 }), *owner.deck({ 3 }) };
    const auto statistics = owner.statistics();
    const auto timing = owner.timing();
    const auto configuration = owner.configuration();
    std::array<QRect, 4> bounds;
    std::array<int, 4> demand;
    for (std::size_t index = 0; index < widgets.size(); ++index) {
        bounds[index] = widgets[index]->geometry();
        demand[index] = widgets[index]->card_face_need_short_px();
        auto* card = owned_card(widgets[index]);
        QVERIFY(card);
        QCOMPARE(card->geometry(), widgets[index]->rect());
        const auto original = card->grab().toImage();
        card->set_frame_style(
            thin ? slot_frame_style::classic : slot_frame_style::thin
        );
        QVERIFY(card->grab().toImage() != original);
        card->set_frame_style(frame);
        QCOMPARE(card->grab().toImage(), original);
    }
    const auto generation = view.next_shared_generation_id;
    for (const auto style :
         { slot_frame_style::classic, slot_frame_style::thin, frame }) {
        view.set_frame_style(style);
        for (std::size_t index = 0; index < widgets.size(); ++index) {
            QCOMPARE(widgets[index]->geometry(), bounds[index]);
            QCOMPARE(widgets[index]->card_face_need_short_px(), demand[index]);
            QCOMPARE(*owner.deck({ index }), records[index]);
            auto* card = owned_card(widgets[index]);
            const auto rendered = card->grab().toImage();
            card->set_frame_style(style);
            QCOMPARE(card->grab().toImage(), rendered);
        }
        QCOMPARE(view.next_shared_generation_id, generation);
        QCOMPARE(view.gameplay_quiz_prompt({ 2 }), prompt);
    }

    // Fixed-slot Swap and a repack retain the same deck views and question.
    set_gameplay_motion_style(view, 200);
    QVERIFY(view.swap_gameplay_decks({ 1 }, { 2 }));
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
    );
    view.gameplay_layout_animation.pause();
    view.gameplay_layout_animation.setCurrentTime(100);
    QVERIFY(!view.grab().isNull());
    view.gameplay_layout_animation.setCurrentTime(200);
    view.resize(compact ? QSize(1800, 1200) : QSize(1000, 1500));
    view.gameplay_layout_animation.pause();
    view.gameplay_layout_animation.setCurrentTime(200);
    QCOMPARE(
        owner.slot_for({ 1 }),
        std::optional { gameplay::physical_slot_id { 2 } }
    );
    QCOMPARE(
        owner.slot_for({ 2 }),
        std::optional { gameplay::physical_slot_id { 1 } }
    );
    QCOMPARE(view.gameplay_quiz_prompt({ 2 }), prompt);
    QCOMPARE(owned_input(widgets[2]), input);
    QCOMPARE(input->text(), QStringLiteral("+17"));
    QCOMPARE(input->cursorPosition(), cursor);
    QCOMPARE(input->selectedText(), selection);
    const auto packed = view.pack_gameplay_slots(owner.size());
    QVERIFY(packed);
    for (std::size_t index = 0; index < widgets.size(); ++index) {
        QCOMPARE(*owner.deck({ index }), records[index]);
        QCOMPARE(
            widgets[index]->gameplay_deck_id(),
            std::optional { gameplay::deck_id { index } }
        );
        QCOMPARE(
            widgets[index]->geometry(),
            widget_rectangle(
                packed->rectangles[owner.slot_for({ index })->value]
            )
        );
    }
    QCOMPARE(owner.statistics(), statistics);
    QCOMPARE(owner.timing(), timing);
    QCOMPARE(owner.configuration(), configuration);
    QCOMPARE(view.shared_raster_cache_service(), cache);
    QCOMPARE(preparation.count(), 0);
    QCOMPARE(legacy_score.count(), 0);

    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    if (opener->isVisible()) {
        opener->click();
    }
    QVERIFY(input->isVisible() && input->isEnabled());
    enter_owned_count(
        input, QString::number(owner.deck({ 2 })->quiz->expected_count)
    );
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(resolved.count(), 2);
    QVERIFY(!owner.deck({ 2 })->quiz);
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(*owner.deck({ 1 }), records[1]);
    QCOMPARE(*owner.deck({ 3 }), records[3]);
    QVERIFY(view.finish_gameplay_runtime());
    QCOMPARE(owner.phase(), gameplay::session_phase::finished);
    QVERIFY(!view.gameplay_timer.isActive());
    const auto result = owner.result();
    QVERIFY(result);
    view.set_frame_style(
        thin ? slot_frame_style::classic : slot_frame_style::thin
    );
    QCOMPARE(owner.result(), result);
    QCOMPARE(view.slot_widgets, widgets);
    QCOMPARE(legacy_score.count(), 0);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
