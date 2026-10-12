// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "table/card_widget.hpp"
#include "table/gameplay_setup.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QtTest/QtTest>

#include <array>

using namespace table_test;

namespace {

// Exercise the real Details route, including its compact native host.
QFrame* open_settings(table_slot* slot) {
    auto* opener = slot->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    if (opener && opener->isVisible())
        QTest::mouseClick(opener, Qt::LeftButton);
    auto* details
        = slot->findChild<QPushButton*>(QStringLiteral("slot_details_button"));
    if (!details || !details->isVisible() || !details->isEnabled())
        return nullptr;
    QTest::keyClick(details, Qt::Key_Space);
    return slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
}

void show_controls(table_slot* slot) {
    auto* opener = slot->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    if (opener && opener->isVisible())
        QTest::mouseClick(opener, Qt::LeftButton);
}

} // namespace

void table_tests::settings_hosts_preserve_frozen_quiz_data() {
    QTest::addColumn<int>("style_id");
    QTest::addColumn<bool>("compact");
    QTest::addColumn<bool>("multi");
    for (const auto style :
         { slot_settings_style::classic, slot_settings_style::card,
           slot_settings_style::drawer, slot_settings_style::sill }) {
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

void table_tests::settings_hosts_preserve_frozen_quiz() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop activated settings compatibility");
#endif
    QFETCH(int, style_id);
    QFETCH(bool, compact);
    QFETCH(bool, multi);
    const auto style = static_cast<slot_settings_style>(style_id);
    table view;
    set_gameplay_motion_style(view, 0);
    view.set_settings_style(style);
    view.resize(compact ? QSize(180, 120) : QSize(2400, 1800));
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        multi ? gameplay::quiz_scope::multi : gameplay::quiz_scope::single,
        { 1, 4 }
    )));
    QSignalSpy legacy_pause(&view, &table::dialog_opened);
    QSignalSpy legacy_score(&view, &table::score_adjusted);
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    QVERIFY(view.start_gameplay_runtime(61));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    auto& owner = *view.active_gameplay_session();
    QVERIFY(owner.current_quiz_batch()->remaining_ms);
    const auto widgets = view.slot_widgets;
    auto* training = widgets[0];
    auto* peer = widgets[2];
    // Automatic quiz freeze is not manual Pause, even with a countdown.
    QVERIFY(
        QMetaObject::invokeMethod(
            training, "on_settings_button_clicked", Qt::DirectConnection
        )
    );
    QVERIFY(
        !training->findChild<QFrame*>(QStringLiteral("slot_settings_editor"))
    );
    show_controls(training);
    auto* input = owned_input(training);
    QVERIFY(input && input->isVisible() && input->isEnabled());
    enter_owned_count(input, QStringLiteral("-27"));
    input->setSelection(1, 2);
    const auto cursor = input->cursorPosition();
    const auto selection = input->selectedText();
    show_controls(peer);
    auto* peer_input = owned_input(peer);
    QVERIFY(peer_input && peer_input->isVisible() && peer_input->isEnabled());
    enter_owned_count(peer_input, QStringLiteral("+17"));
    peer_input->setSelection(1, 2);
    const auto peer_cursor = peer_input->cursorPosition();
    const auto peer_selection = peer_input->selectedText();
    QVERIFY(view.advance_gameplay_runtime(137, answers));
    view.publish_gameplay_runtime(answers);
    QVERIFY(answers.empty());
    QVERIFY(view.pause_gameplay_runtime());
    QVERIFY(!input->isEnabled() && !peer_input->isEnabled());
    const std::array records { *owner.deck({ 0 }), *owner.deck({ 1 }),
                               *owner.deck({ 2 }), *owner.deck({ 3 }) };
    const auto timing = owner.timing();
    const auto statistics = owner.statistics();
    const auto batch = *owner.current_quiz_batch();
    const auto prompt = view.gameplay_quiz_prompt({ 0 });
    const auto peer_prompt = view.gameplay_quiz_prompt({ 2 });
    QVERIFY(prompt && peer_prompt);
    std::array<QRect, 4> bounds;
    std::array<int, 4> demand;
    for (std::size_t id = 0; id < widgets.size(); ++id) {
        bounds[id] = widgets[id]->geometry();
        demand[id] = widgets[id]->card_face_need_short_px();
    }
    auto* cache = view.shared_raster_cache_service();
    const auto generation = view.next_shared_generation_id;
    auto* panel = open_settings(training);
    QVERIFY(panel && panel->isVisible());
    auto* draft = panel->findChild<gameplay::deck_setup_widget*>();
    auto* show = panel->findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QVERIFY(draft && draft->pending_changes_current());
    QVERIFY(
        show && show->isVisible() && show->isEnabled() && show->isChecked()
    );
    QCOMPARE(qobject_cast<QDialog*>(panel->parentWidget()) != nullptr, compact);
    // Opening Details moves the same disabled quiz input out of compact
    // hosting.
    auto* controls
        = training->findChild<QDialog*>(QStringLiteral("slot_controls_dialog"));
    QVERIFY(!controls || !controls->isVisible());
    for (const auto* name :
         { "gameplay_deck_strategy", "gameplay_deck_count",
           "gameplay_deck_infinite", "gameplay_deck_training" }) {
        auto* field = draft->findChild<QWidget*>(QString::fromLatin1(name));
        QVERIFY(field && !field->isEnabled());
    }
    QTest::keyClick(show, Qt::Key_Space);
    QVERIFY(!show->isChecked());
    for (const auto next_style :
         { slot_settings_style::classic, slot_settings_style::card,
           slot_settings_style::drawer, slot_settings_style::sill, style }) {
        view.set_settings_style(next_style);
        view.refresh_gameplay_session();
        QVERIFY(!view.advance_gameplay_runtime(9000, answers));
        QCOMPARE(panel->findChild<gameplay::deck_setup_widget*>(), draft);
        QVERIFY(panel->isVisible() && draft->pending_changes_current());
        QVERIFY(!show->isChecked());
        for (std::size_t id = 0; id < records.size(); ++id) {
            QCOMPARE(*owner.deck({ id }), records[id]);
            QCOMPARE(widgets[id]->geometry(), bounds[id]);
            QCOMPARE(widgets[id]->card_face_need_short_px(), demand[id]);
        }
        QCOMPARE(owner.timing(), timing);
        QCOMPARE(owner.statistics(), statistics);
        QCOMPARE(*owner.current_quiz_batch(), batch);
        QCOMPARE(view.gameplay_quiz_prompt({ 0 }), prompt);
        QCOMPARE(view.gameplay_quiz_prompt({ 2 }), peer_prompt);
        QCOMPARE(owned_input(training), input);
        QCOMPARE(input->text(), QStringLiteral("-27"));
        QCOMPARE(input->cursorPosition(), cursor);
        QCOMPARE(input->selectedText(), selection);
        QCOMPARE(peer_input->text(), QStringLiteral("+17"));
        QCOMPARE(peer_input->cursorPosition(), peer_cursor);
        QCOMPARE(peer_input->selectedText(), peer_selection);
        QCOMPARE(view.shared_raster_cache_service(), cache);
        QCOMPARE(view.next_shared_generation_id, generation);
    }
    QTest::mouseClick(
        panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel),
        Qt::LeftButton
    );
    QCOMPARE(*owner.deck({ 0 }), records[0]);
    QVERIFY(!panel->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // A pending Challenge question can show read-only Details during Pause;
    // accepting this unchanged form neither answers nor regenerates its deck.
    panel = open_settings(peer);
    QVERIFY(panel && panel->isVisible());
    for (const auto* name :
         { "gameplay_deck_strategy", "gameplay_deck_count",
           "gameplay_deck_infinite", "gameplay_deck_training" }) {
        auto* field = panel->findChild<QWidget*>(QString::fromLatin1(name));
        QVERIFY(field && !field->isEnabled());
    }
    auto* peer_show = panel->findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QVERIFY(peer_show && peer_show->isHidden() && !peer_show->isEnabled());
    QTest::mouseClick(
        panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),
        Qt::LeftButton
    );
    QVERIFY(!panel->isVisible());
    QCOMPARE(*owner.deck({ 2 }), records[2]);
    QCOMPARE(view.gameplay_quiz_prompt({ 2 }), peer_prompt);
    QCOMPARE(peer_input->text(), QStringLiteral("+17"));
    QCOMPARE(peer_input->cursorPosition(), peer_cursor);
    QCOMPARE(peer_input->selectedText(), peer_selection);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    panel = open_settings(training);
    QVERIFY(panel && panel->isVisible());
    draft = panel->findChild<gameplay::deck_setup_widget*>();
    show = panel->findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QTest::keyClick(show, Qt::Key_Space);
    const auto training_position = owner.slot_for({ 0 });
    const auto peer_position = owner.slot_for({ 2 });
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    view.resize(900, 1500);
    QCoreApplication::processEvents();
    QCOMPARE(owner.slot_for({ 0 }), peer_position);
    QCOMPARE(owner.slot_for({ 2 }), training_position);
    QCOMPARE(panel->findChild<gameplay::deck_setup_widget*>(), draft);
    QVERIFY(draft->pending_changes_current() && !show->isChecked());
    // Resize can legitimately start an adaptive raster generation. The
    // settings commit must preserve its final geometry/demand, not freeze
    // the asynchronous cache at the pre-resize size.
    for (std::size_t id = 0; id < widgets.size(); ++id) {
        bounds[id] = widgets[id]->geometry();
        demand[id] = widgets[id]->card_face_need_short_px();
    }
    QTest::mouseClick(
        panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),
        Qt::LeftButton
    );
    QVERIFY(!panel->isVisible());
    auto hidden_hint = records[0];
    hidden_hint.show_count = false;
    QCOMPARE(*owner.deck({ 0 }), hidden_hint);
    for (std::size_t id = 1; id < records.size(); ++id)
        QCOMPARE(*owner.deck({ id }), records[id]);
    QCOMPARE(owner.timing(), timing);
    QCOMPARE(owner.statistics(), statistics);
    QCOMPARE(*owner.current_quiz_batch(), batch);
    QCOMPARE(view.gameplay_quiz_prompt({ 0 }), prompt);
    QCOMPARE(view.gameplay_quiz_prompt({ 2 }), peer_prompt);
    QCOMPARE(input->text(), QStringLiteral("-27"));
    QCOMPARE(input->cursorPosition(), cursor);
    QCOMPARE(input->selectedText(), selection);
    QCOMPARE(peer_input->text(), QStringLiteral("+17"));
    QCOMPARE(peer_input->cursorPosition(), peer_cursor);
    QCOMPARE(peer_input->selectedText(), peer_selection);
    for (std::size_t id = 0; id < widgets.size(); ++id) {
        QCOMPARE(widgets[id]->geometry(), bounds[id]);
        QCOMPARE(widgets[id]->card_face_need_short_px(), demand[id]);
    }
    QCOMPARE(view.shared_raster_cache_service(), cache);
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QVERIFY(view.advance_gameplay_runtime(101, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(
        *owner.current_quiz_batch()->remaining_ms, *batch.remaining_ms - 101
    );
    show_controls(peer);
    enter_owned_count(
        peer_input, QString::number(records[2].quiz->expected_count)
    );
    QTest::keyClick(peer_input, Qt::Key_Return);
    QVERIFY(!owner.deck({ 2 })->quiz);
    QCOMPARE(owner.deck({ 2 })->statistics.verified_cards, std::uint64_t { 1 });
    show_controls(training);
    enter_owned_count(input, QString::number(records[0].quiz->expected_count));
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(!owner.deck({ 0 })->quiz);
    QVERIFY(!owned_card(training)->accessibleDescription().contains(
        QString::number(records[0].quiz->expected_count)
    ));
    QVERIFY(view.finish_gameplay_runtime());
    QCOMPARE(preparation.count(), 0);
    QCOMPARE(legacy_pause.count(), 0);
    QCOMPARE(legacy_score.count(), 0);
}

void table_tests::settings_hosts_discard_runtime_drafts_data() {
    QTest::addColumn<int>("style_id");
    QTest::addColumn<bool>("compact");
    for (const auto style :
         { slot_settings_style::classic, slot_settings_style::card,
           slot_settings_style::drawer, slot_settings_style::sill }) {
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

void table_tests::settings_hosts_discard_runtime_drafts() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop activated settings lifetime");
#endif
    QFETCH(int, style_id);
    QFETCH(bool, compact);
    table view;
    set_gameplay_motion_style(view, 0);
    view.set_settings_style(static_cast<slot_settings_style>(style_id));
    view.resize(compact ? QSize(180, 120) : QSize(2400, 1800));
    view.show();
    auto fixture = owned_clock_fixture(2);
    std::vector<gameplay::prepared_deck> prepared(2);
    for (auto& deck : prepared) {
        deck.cards = { 0, 1, 2, 3, 4, 5, 6, 7 };
        deck.rank_weights.fill(1);
    }
    prepared[0].virtual_quiz_targets = { 1, 4 };
    QVERIFY(fixture->prepare_decks(prepared));
    QVERIFY(view.install_gameplay_session(std::move(fixture)));
    auto& owner = *view.active_gameplay_session();
    QSignalSpy legacy_pause(&view, &table::dialog_opened);
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    QVERIFY(view.start_gameplay_runtime(63));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QVERIFY(!owner.current_quiz_batch()->remaining_ms);
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(owner.deck({ 1 })->next_card, std::size_t { 2 });
    auto* slot = view.slot_widgets[0];
    show_controls(slot);
    auto* input = owned_input(slot);
    QVERIFY(input && input->isVisible() && input->isEnabled());
    enter_owned_count(input, QStringLiteral("+17"));
    input->setSelection(1, 2);
    const auto cursor = input->cursorPosition();
    const auto selection = input->selectedText();
    const auto prompt = view.gameplay_quiz_prompt({ 0 });
    QVERIFY(view.pause_gameplay_runtime());
    const auto before = *owner.deck({ 0 });
    const auto peer = *owner.deck({ 1 });
    const auto batch = *owner.current_quiz_batch();
    const auto timing = owner.timing();
    auto* panel = open_settings(slot);
    QVERIFY(panel && panel->isVisible());
    QPointer<gameplay::deck_setup_widget> draft
        = panel->findChild<gameplay::deck_setup_widget*>();
    QVERIFY(draft);
    auto* show = draft->findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QVERIFY(show && show->isChecked());
    QTest::keyClick(show, Qt::Key_Space);
    slot->resize(80, 60); // promote the same paused draft, never a second form
    auto* host = qobject_cast<QDialog*>(panel->parentWidget());
    QVERIFY(host && host->isVisible());
    QCOMPARE(panel->findChild<gameplay::deck_setup_widget*>(), draft.data());
    QVERIFY(!show->isChecked() && draft->pending_changes_current());
    QTest::keyClick(host, Qt::Key_Escape);
    QVERIFY(!panel->isVisible());
    QCOMPARE(*owner.deck({ 0 }), before);
    QCOMPARE(*owner.deck({ 1 }), peer);
    QCOMPARE(*owner.current_quiz_batch(), batch);
    QCOMPARE(owner.timing(), timing);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(draft.isNull());

    panel = open_settings(slot);
    QVERIFY(panel && panel->isVisible());
    draft = panel->findChild<gameplay::deck_setup_widget*>();
    show = draft->findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QTest::keyClick(show, Qt::Key_Space);
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QVERIFY(!panel->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(draft.isNull());
    QCOMPARE(*owner.deck({ 0 }), before);
    QCOMPARE(view.gameplay_quiz_prompt({ 0 }), prompt);
    QCOMPARE(input->text(), QStringLiteral("+17"));
    QCOMPARE(input->cursorPosition(), cursor);
    QCOMPARE(input->selectedText(), selection);
    QCOMPARE(owner.timing(), timing);
    show_controls(slot);
    auto* skip
        = slot->findChild<QPushButton*>(QStringLiteral("quiz_skip_button"));
    QVERIFY(skip && skip->isVisible() && skip->isEnabled());
    QTest::mouseClick(skip, Qt::LeftButton);
    QVERIFY(!owner.deck({ 0 })->quiz);
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 2 });
    QCOMPARE(owner.deck({ 1 })->next_card, std::size_t { 3 });
    QCOMPARE(owner.timing().play_time_ms, timing.play_time_ms + 300);
    QVERIFY(owner.deck({ 0 })->show_count);

    QVERIFY(view.pause_gameplay_runtime());
    panel = open_settings(slot);
    QVERIFY(panel && panel->isVisible());
    draft = panel->findChild<gameplay::deck_setup_widget*>();
    show = draft->findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QTest::keyClick(show, Qt::Key_Space);
    QVERIFY(view.finish_gameplay_runtime());
    QVERIFY(!panel->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(draft.isNull());
    QVERIFY(owner.deck({ 0 })->show_count && owner.result());
    const auto result = *owner.result();
    const auto finished_timing = owner.timing();
    view.set_settings_style(slot_settings_style::sill);
    QVERIFY(
        QMetaObject::invokeMethod(
            slot, "on_settings_button_clicked", Qt::DirectConnection
        )
    );
    QVERIFY(!slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor")));
    QCOMPARE(*owner.result(), result);
    QCOMPARE(owner.timing(), finished_timing);

    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        1, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1 }
    )));
    QVERIFY(view.start_gameplay_runtime(65));
    view.stop_gameplay_clock();
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QVERIFY(view.pause_gameplay_runtime());
    QPointer<table_slot> retired = view.slot_widgets[0];
    QPointer<QLineEdit> retired_input = owned_input(retired);
    panel = open_settings(retired);
    QVERIFY(panel && panel->isVisible());
    draft = panel->findChild<gameplay::deck_setup_widget*>();
    QPointer<QWidget> retired_host = panel->parentWidget();
    show = draft->findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QTest::keyClick(show, Qt::Key_Space);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(2)));
    QVERIFY(retired.isNull() && retired_input.isNull() && draft.isNull());
    QVERIFY(retired_host.isNull());
    QVERIFY(!view.gameplay_timer.isActive());
    QCOMPARE(
        view.active_gameplay_session()->phase(), gameplay::session_phase::setup
    );
    QVERIFY(view.active_gameplay_session()->deck({ 0 })->show_count);
    QCOMPARE(
        view.active_gameplay_session()->deck({ 0 })->latest_input,
        std::int64_t { 0 }
    );
    QCOMPARE(preparation.count(), 0);
    QCOMPARE(legacy_pause.count(), 0);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
