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
#include <QToolButton>
#include <QtTest/QtTest>

#include <memory>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::gameplay_table_owns_roster_without_legacy_state() {
    table view;
    view.resize(1200, 900);
    auto owner = owned_table_fixture(24);
    auto* identity = owner.get();
    QSignalSpy changed(&view, &table::gameplay_session_changed);
    QVERIFY(view.install_gameplay_session(std::move(owner)));
    QCOMPARE(view.active_gameplay_session(), identity);
    QCOMPARE(view.slot_widgets.size(), std::size_t { 24 });
    QCOMPARE(changed.size(), 1);
    QVERIFY(view.gameplay_owner->start());
    QCOMPARE(
        view.gameplay_owner->deal_step().status,
        gameplay::dealing_step_status::advanced
    );
    view.refresh_gameplay_session();
    const auto before = *identity->deck({ 0 });
    QSignalSpy score(&view, &table::score_adjusted);
    QSignalSpy ended(&view, &table::game_over);
    view.set_slot_count(1);
    view.start_quiz(1, false);
    view.set_paused(true);
    view.set_pick_interval(100);
    view.set_dealing_mode(2);
    view.set_allow_skipping(false);
    view.on_clock_tick(9000, 9000);
    view.clear_quiz();
    QCOMPARE(view.slot_widgets.size(), std::size_t { 24 });
    QCOMPARE(identity->phase(), gameplay::session_phase::running);
    QCOMPARE(*identity->deck({ 0 }), before);
    QCOMPARE(view.capture_session_state(), table_session_state {});
    QVERIFY(view.capture_drill_settings().isEmpty());
    for (std::size_t index = 0; index < view.slot_widgets.size(); ++index) {
        auto* slot = view.slot_widgets[index];
        QCOMPARE(
            slot->gameplay_deck_id(),
            std::optional { gameplay::deck_id { index } }
        );
        QCOMPARE(
            owned_card(slot)->capture_session_state(), card_session_state {}
        );
        slot->start_quiz(0);
        slot->set_paused(false);
        slot->advance_card();
        slot->clear_quiz();
        QVERIFY(!slot->restore_session_state({}));
        slot->apply_drill_settings({ 3, true, QStringLiteral("hi_lo"), false });
        QVERIFY(
            QMetaObject::invokeMethod(
                slot, "on_quiz_answer_button_clicked", Qt::DirectConnection
            )
        );
        QVERIFY(
            QMetaObject::invokeMethod(
                slot, "on_quiz_skip_button_clicked", Qt::DirectConnection
            )
        );
        QVERIFY(
            QMetaObject::invokeMethod(
                slot, "on_settings_button_clicked", Qt::DirectConnection
            )
        );
        QVERIFY(
            QMetaObject::invokeMethod(
                slot, "on_copy_all_button_clicked", Qt::DirectConnection
            )
        );
        slot->resize(45, 65);
        auto* compact = slot->findChild<QToolButton*>(
            QStringLiteral("compact_slot_controls")
        );
        if (compact)
            QVERIFY(compact->isHidden());
        QVERIFY(
            slot->findChild<QWidget*>(QStringLiteral("quiz_spin_box"))
                ->isEnabled()
            == false
        );
    }
    QCOMPARE(*identity->deck({ 0 }), before);
    QCOMPARE(score.size(), 0);
    QCOMPARE(ended.size(), 0);
    QVERIFY(view.findChildren<QDialog*>().isEmpty());
}

void table_tests::
    gameplay_table_installation_preflight_preserves_current_scene() {
    table view;
    view.resize(900, 700);
    view.set_slot_count(2);
    view.start_quiz(0, false);
    const auto before = view.capture_session_state();
    QVERIFY(!view.install_gameplay_session(owned_table_fixture()));
    QCOMPARE(view.capture_session_state(), before);
    QVERIFY(!view.active_gameplay_session());
    view.clear_quiz();
    QVERIFY(view.install_gameplay_session(owned_table_fixture()));
    auto* identity = view.active_gameplay_session();
    QPointer<table_slot> first = view.slot_widgets.front();
    const auto bounds = first->geometry();
    QSignalSpy changed(&view, &table::gameplay_session_changed);
    QVERIFY(!view.install_gameplay_session(nullptr));
    QVERIFY(!view.install_gameplay_session(owned_table_fixture(65)));
    QCOMPARE(view.active_gameplay_session(), identity);
    QCOMPARE(view.slot_widgets.front(), first.data());
    QCOMPARE(first->geometry(), bounds);
    QVERIFY(!view.restore_session_state(before));
    training_drill drill;
    drill.name = QStringLiteral("Legacy fixture");
    drill.slot_settings = { { 1, false, QStringLiteral("hi_lo"), false } };
    QVERIFY(!view.configure_drill(drill));
    QCOMPARE(changed.size(), 0);
    view.resize(
        1, 88
    ); // Packing's subpixel slots cannot form usable QWidget geometry.
    QVERIFY(!view.install_gameplay_session(owned_table_fixture(64)));
    QCOMPARE(view.active_gameplay_session(), identity);
    QCOMPARE(view.slot_widgets.front(), first.data());
}

void table_tests::gameplay_table_replacement_destroys_views_before_owner() {
    table view;
    view.resize(1000, 700);
    QVERIFY(view.install_gameplay_session(owned_table_fixture()));
    auto* first_owner = view.active_gameplay_session();
    QPointer<table_slot> old_slot = view.slot_widgets.front();
    QPointer<card_widget> old_card = owned_card(old_slot);
    auto settings = std::make_unique<gameplay::deck_setup_widget>(
        *first_owner, gameplay::deck_id { 0 }, strategy_repository()
    );
    bool owner_alive_when_closed = false;
    QObject::connect(
        &view, &table::gameplay_session_about_to_change, &view, [&] {
            owner_alive_when_closed
                = view.active_gameplay_session() == first_owner;
            settings.reset();
        }
    );
    auto replacement = owned_table_fixture(17);
    auto* next_identity = replacement.get();
    QVERIFY(view.install_gameplay_session(std::move(replacement)));
    QVERIFY(owner_alive_when_closed);
    QVERIFY(!settings);
    QVERIFY(old_slot.isNull());
    QVERIFY(old_card.isNull());
    QCOMPARE(view.active_gameplay_session(), next_identity);
    QCOMPARE(view.slot_widgets.size(), std::size_t { 17 });
    QPointer<table_slot> last_slot = view.slot_widgets.back();
    view.clear_gameplay_session();
    QVERIFY(last_slot.isNull());
    QVERIFY(!view.active_gameplay_session());
    view.set_slot_count(2);
    view.start_quiz(0, false);
    QVERIFY(view.capture_session_state().quiz_running);
    QVERIFY(!view.slot_widgets.front()->gameplay_deck_id());
}

void table_tests::owned_table_projects_mixed_decks_and_swap() {
    table view;
    view.resize(1200, 900);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture()));
    auto& game = *view.active_gameplay_session();
    QVERIFY(game.start());
    QCOMPARE(game.deal_step().status, gameplay::dealing_step_status::advanced);
    QVERIFY(game.edit_quiz_input({ 2 }, 9));
    QVERIFY(
        game.check_quiz({ 3 })
    ); // wrong challenge -> failed, real answer path
    QCOMPARE(game.deal_step().status, gameplay::dealing_step_status::advanced);
    view.refresh_gameplay_session();
    const auto deck_views = view.slot_widgets;
    const auto training = *game.deck({ 0 });
    const auto completed = *game.deck({ 1 });
    const auto pending = *game.deck({ 2 });
    const auto failed = *game.deck({ 3 });
    QCOMPARE(completed.status, gameplay::deck_status::completed);
    QCOMPARE(failed.status, gameplay::deck_status::failed);
    QVERIFY(deck_views[2]->is_quiz_prompt_active());
    QVERIFY(owned_card(deck_views[0])
                ->accessibleDescription()
                .contains(QString::number(training.running_count)));
    QVERIFY(owned_card(deck_views[1])
                ->accessibleDescription()
                .startsWith(str_label("Completed deck.")));
    QVERIFY(owned_card(deck_views[1])
                ->accessibleDescription()
                .contains(QString::number(completed.running_count)));
    QCOMPARE(
        owned_card(deck_views[2])->accessibleDescription(),
        str_label("Card hidden")
    );
    QCOMPARE(
        owned_card(deck_views[3])->accessibleDescription(),
        str_label("Failed deck. Card back.")
    );
    const auto physical_zero = deck_views[0]->geometry();
    const auto physical_two = deck_views[2]->geometry();
    QVERIFY(!view.swap_gameplay_decks({ 0 }, { 2 }));
    QVERIFY(game.pause());
    view.refresh_gameplay_session();
    QVERIFY(
        QMetaObject::invokeMethod(
            &view, "on_slot_swap", Qt::DirectConnection,
            Q_ARG(table_slot*, deck_views[0])
        )
    );
    view.refresh_gameplay_session(); // same-state refresh preserves selection
    QVERIFY(deck_views[0]->swap_selected());
    QVERIFY(
        QMetaObject::invokeMethod(
            &view, "on_slot_swap", Qt::DirectConnection,
            Q_ARG(table_slot*, deck_views[2])
        )
    );
    QCOMPARE(
        view.slot_widgets, deck_views
    ); // deck storage is never widget-swap order
    QCOMPARE(deck_views[0]->geometry(), physical_two);
    QCOMPARE(deck_views[2]->geometry(), physical_zero);
    QCOMPARE(*game.deck({ 0 }), training);
    QCOMPARE(*game.deck({ 1 }), completed);
    QCOMPARE(*game.deck({ 2 }), pending);
    QCOMPARE(*game.deck({ 3 }), failed);
    for (const auto orientation : { card_orientation_mode::vertical,
                                    card_orientation_mode::horizontal }) {
        view.set_card_orientation(orientation);
        view.resize(700, 1100);
        QCoreApplication::processEvents();
        QTRY_COMPARE(
            view.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
        );
        const auto [long_side, short_side] = card_sheet_ratio();
        const auto expected = packing::pack_equal_rectangles(
            { .container = { static_cast<double>(view.width()),
                             static_cast<double>(view.height()) },
              .item = { static_cast<double>(long_side),
                        static_cast<double>(short_side) },
              .count = game.size(),
              .orientation = orientation == card_orientation_mode::vertical
                  ? packing::orientation_constraint::vertical_only
                  : packing::orientation_constraint::horizontal_only }
        );
        QVERIFY(
            std::ranges::equal(
                game.traversal(), expected.traversal, {},
                [](auto id) { return id.value; }
            )
        );
        for (std::size_t index = 0; index < game.size(); ++index) {
            QCOMPARE(
                deck_views[index]->geometry(),
                widget_rectangle(
                    expected.rectangles[game.slot_for({ index })->value]
                )
            );
            QCOMPARE(
                deck_views[index]->gameplay_deck_id(),
                std::optional { gameplay::deck_id { index } }
            );
        }
    }
    QCOMPARE(*game.deck({ 2 }), pending);
    QVERIFY(
        view.swap_gameplay_decks({ 1 }, { 3 })
    ); // stopped owners also remain mapped
    QCOMPARE(*game.deck({ 1 }), completed);
    QCOMPARE(*game.deck({ 3 }), failed);
    QVERIFY(game.resume());
    QVERIFY(game.skip_quiz({ 2 }));
    view.refresh_gameplay_session();
    QVERIFY(!deck_views[2]->is_quiz_prompt_active());
    QVERIFY(owned_card(deck_views[2])
                ->accessibleDescription()
                .contains(str_label("Current card:")));
    QVERIFY(!owned_card(deck_views[2])
                 ->accessibleDescription()
                 .contains(QString::number(pending.running_count)));
}

void table_tests::gameplay_table_terminal_resize_preserves_results() {
    table view;
    view.resize(1200, 700);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture()));
    auto& game = *view.active_gameplay_session();
    QVERIFY(game.start());
    QCOMPARE(game.deal_step().status, gameplay::dealing_step_status::advanced);
    QVERIFY(game.check_quiz({ 3 }));
    QCOMPARE(game.deal_step().status, gameplay::dealing_step_status::advanced);
    QVERIFY(game.finish());
    view.refresh_gameplay_session();
    const auto result = game.result();
    const auto finished = *game.deck({ 1 });
    const std::vector<gameplay::physical_slot_id> order(
        game.traversal().begin(), game.traversal().end()
    );
    auto* slot = view.slot_widgets[1];
    const auto before = slot->geometry();
    view.resize(600, 1000);
    QCoreApplication::processEvents();
    QTRY_COMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
    );
    QVERIFY(slot->geometry() != before);
    QCOMPARE(game.result(), result);
    QCOMPARE(*game.deck({ 1 }), finished);
    QVERIFY(std::ranges::equal(game.traversal(), order));
    QVERIFY(owned_card(slot)->accessibleDescription().startsWith(
        str_label("Completed deck.")
    ));
    QVERIFY(!view.swap_gameplay_decks({ 0 }, { 1 }));
    auto finished_owner = std::make_unique<gameplay::session>(game);
    QVERIFY(view.install_gameplay_session(std::move(finished_owner)));
    QCOMPARE(view.active_gameplay_session()->result(), result);
}

void table_tests::gameplay_table_refreshes_owned_check_and_timeout() {
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_table_fixture(4, true)));
    auto& game = *view.active_gameplay_session();
    QVERIFY(game.start());
    QCOMPARE(game.deal_step().status, gameplay::dealing_step_status::advanced);
    view.refresh_gameplay_session();
    auto* correct = view.slot_widgets[2];
    auto* timed_out = view.slot_widgets[3];
    QVERIFY(correct->is_quiz_prompt_active());
    QVERIFY(timed_out->is_quiz_prompt_active());
    QVERIFY(
        game.edit_quiz_input({ 2 }, game.deck({ 2 })->quiz->expected_count)
    );
    QVERIFY(game.check_quiz({ 2 }));
    view.refresh_gameplay_session();
    QVERIFY(!correct->is_quiz_prompt_active());
    QVERIFY(owned_card(correct)->accessibleDescription().contains(
        card_label_from_index(0)
    ));
    QVERIFY(timed_out->is_quiz_prompt_active());
    QVERIFY(game.pause());
    const auto batch = game.current_quiz_batch();
    view.on_clock_tick(
        90000, 90000
    ); // legacy clock cannot consume the new batch
    view.refresh_gameplay_session();
    QCOMPARE(game.current_quiz_batch(), batch);
    QVERIFY(game.resume());
    const auto expired = game.advance_time(*batch->remaining_ms);
    QCOMPARE(expired.answers.size(), std::size_t { 1 });
    view.refresh_gameplay_session();
    QVERIFY(!timed_out->is_quiz_prompt_active());
    QCOMPARE(game.deck({ 3 })->status, gameplay::deck_status::failed);
    QCOMPARE(
        owned_card(timed_out)->accessibleDescription(),
        str_label("Failed deck. Card back.")
    );
    QCOMPARE(game.statistics().verified_cards, 1U);
    QCOMPARE(game.statistics().errors, 1U);
}

void table_tests::gameplay_table_reuses_one_bounded_raster_owner() {
    table view;
    view.resize(1100, 800);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture()));
    view.prepare_cards_for_start();
    const auto all_ready = [&view] {
        return !view.is_rasterization_busy()
            && std::ranges::all_of(view.slot_widgets, [](const auto* slot) {
                   return slot->has_shared_card_faces();
               });
    };
    QTRY_VERIFY_WITH_TIMEOUT(all_ready(), 10000);
    QCOMPARE(view.findChildren<raster_cache*>().size(), 1);
    QVERIFY(view.rasterizing_slots.isEmpty());
    const auto displayed = view.displayed_shared_faces_key;
    QVERIFY(displayed);
    QVERIFY(displayed->render_scope.startsWith(QStringLiteral("all_faces#g")));
    QVERIFY(!displayed->render_scope.contains(
        QStringLiteral("I18N_ARGUMENT_MISSING")
    ));
    auto& game = *view.active_gameplay_session();
    QVERIFY(game.start());
    QCOMPARE(game.deal_step().status, gameplay::dealing_step_status::advanced);
    const auto before = *game.deck({ 0 });
    for (int repeat = 0; repeat < 3; ++repeat)
        view.refresh_gameplay_session();
    QCOMPARE(view.displayed_shared_faces_key, displayed);
    QVERIFY(!view.shared_faces_watcher.isRunning());
    QCOMPARE(*game.deck({ 0 }), before);
    for (const auto count : { std::size_t { 24 }, std::size_t { 2 } }) {
        QVERIFY(view.install_gameplay_session(owned_table_fixture(count)));
        view.prepare_cards_for_start();
        QTRY_VERIFY_WITH_TIMEOUT(all_ready(), 10000);
        QVERIFY(view.rasterizing_slots.isEmpty());
        QCOMPARE(view.shared_raster_cache_service()->ready_entry_count(), 1);
        QVERIFY(view.retained_shared_faces_keys.size() <= 2);
    }
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
