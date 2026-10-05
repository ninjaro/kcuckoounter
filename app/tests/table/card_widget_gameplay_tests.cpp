// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/card_widget_tests.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "table/card_widget.hpp"
#include "table/gameplay_generation.hpp"

#include <QtTest/QtTest>
#include <array>
#include <limits>

#include "table/card_widget_fixture.hpp"

using namespace card_test;

void card_widget_tests::binding_reads_owned_decks_rejects_legacy_mutation() {
    auto owner = renderer_session();
    card_widget widget;
    const auto original = *owner.deck({ 0 });
    QVERIFY(!widget.bind_gameplay_deck(owner, { 99 }));
    QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
    QVERIFY(widget.picker.deck_order().empty());
    QVERIFY(widget.has_cards());
    QVERIFY(!widget.has_current_card());
    QCOMPARE(widget.accessibleDescription(), str_label("Card back"));
    card_session_state legacy;
    legacy.cards_per_deck = 52;
    legacy.decks_count = 1;
    for (int face = 0; face < 52; ++face)
        legacy.deck.push_back(face);
    widget.start_quiz(1, 4, true);
    widget.set_running(true);
    widget.advance_card();
    widget.set_infinity(true);
    widget.set_training_mode(true);
    widget.set_strategy_weights(QVector<int>(13, 1));
    widget.mark_deck_exhausted();
    widget.clear_quiz();
    QVERIFY(!widget.restore_session_state(legacy));
    QCOMPARE(widget.capture_session_state(), card_session_state {});
    QCOMPARE(*owner.deck({ 0 }), original);
    QVERIFY(widget.picker.deck_order().empty());
    QVERIFY(!widget.bind_gameplay_deck(owner, { 1 }));
    auto another = renderer_session();
    QVERIFY(!widget.bind_gameplay_deck(another, { 0 }));
    QVERIFY(owner.start());
    QCOMPARE(owner.deal_step().status, gameplay::dealing_step_status::advanced);
    widget.refresh_gameplay_deck();
    QCOMPARE(widget.display_card_index(), 0);
    QVERIFY(widget.has_current_card());
    QVERIFY(!widget.is_deck_exhausted());
    const auto dealt = *owner.deck({ 0 });
    widget.advance_card();
    widget.clear_quiz();
    QCOMPARE(*owner.deck({ 0 }), dealt);
    QCOMPARE(widget.display_card_index(), 0);
    widget.unbind_gameplay_deck();
    QVERIFY(!widget.has_cards());
    QVERIFY(!widget.has_current_card());
    QCOMPARE(widget.accessibleDescription(), str_label("Empty card slot"));
    QVERIFY(widget.restore_session_state(legacy));
    const auto saved = widget.capture_session_state();
    QVERIFY(!widget.bind_gameplay_deck(owner, { 0 }));
    QCOMPARE(widget.capture_session_state(), saved);
    widget.clear_quiz();
    QVERIFY(widget.bind_gameplay_deck(owner, { 1 }));
}

void card_widget_tests::count_visibility_uses_training_choice_and_int64() {
    auto owner = renderer_session();
    card_widget widget;
    supply_renderer_faces(widget);
    QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
    QVERIFY(owner.start());
    QCOMPARE(owner.deal_step().status, gameplay::dealing_step_status::advanced);
    widget.refresh_gameplay_deck();
    const auto count = owner.deck({ 0 })->running_count;
    QVERIFY(count < std::numeric_limits<int>::min());
    QVERIFY(widget.gameplay_extra_lines().isEmpty());
    QVERIFY(!widget.accessibleDescription().contains(QString::number(count)));
    const auto hidden = render_card(widget);
    QVERIFY(owner.pause());
    QVERIFY(owner.set_show_count({ 0 }, true));
    QVERIFY(owner.resume());
    widget.refresh_gameplay_deck();
    QCOMPARE(
        widget.gameplay_extra_lines(), QStringList { QString::number(count) }
    );
    QVERIFY(widget.accessibleDescription().contains(QString::number(count)));
    QVERIFY(render_card(widget) != hidden);
    QVERIFY(widget.picker.deck_order().empty());
    QCOMPARE(owner.deck({ 0 })->running_count, count);
    QVERIFY(!widget.rasterizing);
    QVERIFY(owner.pause());
    QVERIFY(owner.set_show_count({ 0 }, false));
    QVERIFY(owner.resume());
    widget.refresh_gameplay_deck();
    QCOMPARE(render_card(widget), hidden);
    QVERIFY(widget.gameplay_extra_lines().isEmpty());
    card_widget challenge;
    supply_renderer_faces(challenge);
    QVERIFY(challenge.bind_gameplay_deck(owner, { 1 }));
    challenge.set_training_mode(
        true
    ); // legacy setter cannot expose target count
    QVERIFY(challenge.gameplay_extra_lines().isEmpty());
    QVERIFY(!owner.set_show_count({ 1 }, true));
    QCOMPARE(
        card_widget::weight_text_for_value(
            std::numeric_limits<std::int64_t>::max()
        ),
        QStringLiteral("+")
            + QString::number(std::numeric_limits<std::int64_t>::max())
    );
    QCOMPARE(
        card_widget::weight_text_for_value(
            std::numeric_limits<std::int64_t>::min()
        ),
        QString::number(std::numeric_limits<std::int64_t>::min())
    );
}

void card_widget_tests::quiz_and_pause_keep_faces_hidden() {
    auto owner = renderer_session();
    auto prepared = owner.deck({ 0 })->stream;
    prepared.virtual_quiz_targets = { 1 };
    QVERIFY(owner.prepare_deck({ 0 }, prepared));
    card_widget widget;
    supply_renderer_faces(widget);
    QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
    QVERIFY(owner.start());
    QCOMPARE(owner.deal_step().status, gameplay::dealing_step_status::advanced);
    QVERIFY(owner.deck({ 0 })->quiz);
    widget.refresh_gameplay_deck();
    QVERIFY(widget.display_hidden());
    QCOMPARE(widget.accessibleDescription(), str_label("Card hidden"));
    QVERIFY(widget.gameplay_extra_lines().isEmpty());
    const auto face = widget.display_card_index();
    const auto payload = *owner.deck({ 0 });
    QVERIFY(owner.pause());
    widget.refresh_gameplay_deck();
    QVERIFY(widget.display_hidden());
    QCOMPARE(*owner.deck({ 0 }), payload);
    QVERIFY(owner.set_show_count({ 0 }, true));
    widget.refresh_gameplay_deck();
    QVERIFY(
        widget.accessibleDescription().startsWith(str_label("Card hidden"))
    );
    QVERIFY(
        !widget.accessibleDescription().contains(card_label_from_index(face))
    );
    QVERIFY(widget.accessibleDescription().contains(
        QString::number(payload.running_count)
    ));
    QVERIFY(owner.resume());
    QVERIFY(owner.edit_quiz_input({ 0 }, payload.quiz->expected_count));
    QVERIFY(owner.check_quiz({ 0 }));
    widget.refresh_gameplay_deck();
    QVERIFY(!widget.display_hidden());
    QVERIFY(
        widget.accessibleDescription().contains(card_label_from_index(face))
    );
    QVERIFY(owner.pause());
    widget.refresh_gameplay_deck();
    QVERIFY(widget.accessibleDescription().startsWith(str_label("Card back")));
    widget.set_hide_cards(true);
    QCOMPARE(
        widget.accessibleDescription().split(QLatin1Char('\n')).first(),
        str_label("Card hidden")
    );
    widget.set_hide_cards(false);
    QCOMPARE(
        widget.accessibleDescription().split(QLatin1Char('\n')).first(),
        str_label("Card back")
    );
}

void card_widget_tests::terminal_states_are_distinct_geometry_is_unchanged() {
    auto owner = renderer_session(false);
    auto short_shoe = owner.deck({ 1 })->stream;
    short_shoe.cards = { 12, 11 };
    short_shoe.initial_running_count = std::int64_t { 1 } << 40;
    QVERIFY(owner.prepare_deck({ 1 }, short_shoe));
    card_widget failed;
    card_widget completed;
    for (auto* widget : { &failed, &completed }) {
        supply_renderer_faces(*widget);
        widget->set_show_card_indexing(true);
    }
    QVERIFY(failed.bind_gameplay_deck(owner, { 0 }));
    QVERIFY(completed.bind_gameplay_deck(owner, { 1 }));
    QVERIFY(owner.start());
    QCOMPARE(owner.deal_step().status, gameplay::dealing_step_status::advanced);
    failed.refresh_gameplay_deck();
    completed.refresh_gameplay_deck();
    const auto target = failed.card_face_target_size();
    const auto rectangle = failed.geometry();
    const std::vector<gameplay::physical_slot_id> traversal(
        owner.traversal().begin(), owner.traversal().end()
    );
    // A stale host prompt-hide flag must not suppress terminal back/final face.
    failed.set_hide_cards(true);
    completed.set_hide_cards(true);
    QVERIFY(owner.fail_deck({ 0 }));
    QCOMPARE(owner.deal_step().status, gameplay::dealing_step_status::advanced);
    QCOMPARE(owner.phase(), gameplay::session_phase::finished);
    failed.refresh_gameplay_deck();
    completed.refresh_gameplay_deck();
    QVERIFY(failed.is_deck_exhausted());
    QVERIFY(completed.is_deck_exhausted());
    QVERIFY(failed.gameplay_extra_lines().isEmpty());
    QCOMPARE(
        failed.accessibleDescription(), str_label("Failed deck. Card back.")
    );
    QVERIFY(!failed.display_running());
    QVERIFY(completed.display_running());
    QVERIFY(!failed.display_hidden());
    QVERIFY(!completed.display_hidden());
    QVERIFY(completed.accessibleDescription().startsWith(
        str_label("Completed deck.")
    ));
    QVERIFY(completed.gameplay_extra_lines()
                .join(QLatin1Char('\n'))
                .contains(QString::number(short_shoe.initial_running_count)));
    QCOMPARE(completed.current_index_text(), QStringLiteral("2/2"));
    for (const auto frame :
         { slot_frame_style::classic, slot_frame_style::thin }) {
        failed.set_frame_style(frame);
        completed.set_frame_style(frame);
        QVERIFY(render_card(failed) != render_card(completed));
    }
    QCOMPARE(failed.card_face_target_size(), target);
    QCOMPARE(failed.geometry(), rectangle);
    QVERIFY(std::ranges::equal(owner.traversal(), traversal));
    QVERIFY(owner.slot_for({ 0 }) == gameplay::physical_slot_id { 0 });
    QVERIFY(owner.slot_for({ 1 }) == gameplay::physical_slot_id { 1 });
    QCOMPARE(owner.statistics().verified_cards, 0U);
    QCOMPARE(owner.statistics().errors, 0U); // no artificial final Check
}

void card_widget_tests::stack_refresh_and_swap_preserve_deck_binding() {
    auto owner = renderer_session();
    card_widget widget;
    supply_renderer_faces(widget);
    QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
    QVERIFY(owner.start());
    for (int index = 0; index < 8; ++index) {
        QCOMPARE(
            owner.deal_step().status, gameplay::dealing_step_status::advanced
        );
        widget.refresh_gameplay_deck();
    }
    QCOMPARE(widget.discard_history.size(), std::size_t { 5 });
    const auto rotation = widget.card_rotation_deg;
    const auto offset = widget.card_offset;
    const auto history = widget.discard_history;
    const auto first = *owner.deck({ 0 });
    const auto second = *owner.deck({ 1 });
    const auto face = widget.display_card_index();
    for (int repeat = 0; repeat < 3; ++repeat) {
        widget.refresh_gameplay_deck();
        QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
        widget.set_frame_style(slot_frame_style::thin);
        widget.resize(360, 260);
        widget.set_slot_rotated(false);
    }
    QCOMPARE(widget.card_rotation_deg, rotation);
    QCOMPARE(widget.card_offset, offset);
    QCOMPARE(widget.discard_history.size(), history.size());
    for (std::size_t index = 0; index < history.size(); ++index) {
        QCOMPARE(
            widget.discard_history[index].rotation_deg,
            history[index].rotation_deg
        );
        QCOMPARE(widget.discard_history[index].offset, history[index].offset);
    }
    QVERIFY(owner.pause());
    QVERIFY(owner.swap_decks({ 0 }, { 1 }));
    widget.refresh_gameplay_deck();
    QCOMPARE(widget.display_card_index(), face);
    QCOMPARE(widget.gameplay_id, (gameplay::deck_id { 0 }));
    QCOMPARE(*owner.deck({ 0 }), first);
    QCOMPARE(*owner.deck({ 1 }), second);
    QCOMPARE(widget.discard_history.size(), history.size());
    QVERIFY(widget.picker.deck_order().empty());
    QVERIFY(!widget.rasterizing);
}

void card_widget_tests::rollover_reads_current_buffer_and_cumulative_index() {
    auto settings = gameplay::session_configuration {};
    auto owner = gameplay::session::create(
                     settings, { { "test", 1, true, true } }, { 1, 64 }
    )
                     .value();
    gameplay::generation_policy policy;
    policy.infinite_chunk_decks = 1;
    policy.minimum_quiz_gap = 1000;
    policy.maximum_quiz_gap = 1000;
    auto shoe = gameplay::generate_infinite_shoe(
        settings.source, 4321, { 0 }, policy
    );
    auto targets = gameplay::generate_quiz_page(
        { .seed = 9876, .minimum_gap = 1000, .maximum_gap = 1000 },
        gameplay::deck_id { 0 }, policy
    );
    QVERIFY(shoe && targets);
    gameplay::prepared_deck prepared { .cards = shoe->cards,
                                       .rank_weights = {},
                                       .initial_running_count = 0,
                                       .virtual_quiz_targets = targets->targets,
                                       .infinite_cards = shoe->continuation,
                                       .quiz_continuation
                                       = targets->continuation };
    QVERIFY(owner.prepare_deck({ 0 }, prepared));
    QVERIFY(owner.set_traversal(std::array<std::size_t, 1> { 0 }));
    card_widget widget;
    supply_renderer_faces(widget);
    widget.set_show_card_indexing(true);
    QVERIFY(widget.bind_gameplay_deck(owner, { 0 }));
    QVERIFY(owner.start());
    for (std::uint64_t exposure = 1; exposure <= 53; ++exposure) {
        const auto dealt = owner.deal_step();
        QCOMPARE(dealt.status, gameplay::dealing_step_status::advanced);
        QCOMPARE(dealt.cards.size(), std::size_t { 1 });
        widget.refresh_gameplay_deck();
        QCOMPARE(
            widget.display_card_index(),
            static_cast<int>(dealt.cards.front().face)
        );
        QCOMPARE(
            widget.current_index_text(),
            QString::number(static_cast<qulonglong>(exposure))
        );
        QVERIFY(widget.picker.deck_order().empty());
    }
    QCOMPARE(owner.deck({ 0 })->stream.infinite_cards->chunk, 1U);
    QCOMPARE(owner.deck({ 0 })->next_card, std::size_t { 1 });
    QCOMPARE(widget.discard_history.size(), std::size_t { 5 });
    QVERIFY(widget.gameplay_extra_lines().isEmpty());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
