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
#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QTabWidget>
#include <QTimer>
#include <QtTest/QtTest>

#include <memory>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::gameplay_launcher_roster_preserves_exact_choices() {
    auto current = owned_table_fixture(4);
    auto settings = current->configuration();
    settings.sequential_count = 3;
    QVERIFY(current->configure(settings));
    QVERIFY(current->set_pick_interval_ms(650));
    const auto before = *current->deck({ 0 });
    auto grown = gameplay::resize_setup_roster(
        *current, 24, strategy_repository(), { "hi_lo", 1, false, false },
        { 4, 64 }
    );
    QVERIFY(grown);
    QCOMPARE(grown->size(), std::size_t { 24 });
    QCOMPARE(grown->configuration(), current->configuration());
    QCOMPARE(grown->pick_interval_ms(), 650);
    QCOMPARE(grown->deck({ 0 })->configuration, before.configuration);
    QVERIFY(grown->deck({ 0 })->show_count);
    QVERIFY(grown->deck({ 0 })->stream.cards.empty());
    QCOMPARE(*current->deck({ 0 }), before);
    const auto recommended = gameplay::recommended_configuration(
        strategy_repository(), { "hi_lo", 1, false, false }
    );
    QVERIFY(recommended);
    QCOMPARE(grown->deck({ 4 })->configuration, *recommended);
    auto shrunk = gameplay::resize_setup_roster(
        *grown, 2, strategy_repository(), { "missing", 1, false, false },
        { 1, 64 }
    );
    QVERIFY(shrunk); // no new template is needed when shrinking
    QCOMPARE(shrunk->configuration().sequential_count, std::size_t { 2 });
    QCOMPARE(shrunk->deck({ 0 })->configuration, before.configuration);
    QVERIFY(!gameplay::resize_setup_roster(
        *current, 0, strategy_repository(), {}, { 4, 64 }
    ));
    QVERIFY(!gameplay::resize_setup_roster(
        *current, 65, strategy_repository(), {}, { 4, 64 }
    ));
    QVERIFY(!gameplay::resize_setup_roster(
        *current, 5, strategy_repository(), { "missing", 1, false, false },
        { 4, 64 }
    ));
    QVERIFY(current->set_traversal(std::vector<std::size_t> { 0, 1, 2, 3 }));
    QVERIFY(current->start());
    QVERIFY(!gameplay::resize_setup_roster(
        *current, 2, strategy_repository(), {}, { 4, 64 }
    ));
    QVERIFY(current->pause());
    QVERIFY(!gameplay::resize_setup_roster(
        *current, 2, strategy_repository(), {}, { 4, 64 }
    ));
}

void table_tests::gameplay_launcher_native_setup_and_roster() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop opt-in launcher");
#endif
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    auto* view = window.findChild<table*>();
    QVERIFY(view && !view->active_gameplay_session());
    auto* entry
        = window.findChild<QAction*>(QStringLiteral("game_new_gameplay"));
    auto* button
        = window.findChild<QPushButton*>(QStringLiteral("setup_new_gameplay"));
    QVERIFY(entry && entry->isEnabled() && button);
    bool in_menu = false;
    for (auto* menu : window.findChildren<QMenu*>())
        in_menu = in_menu || menu->actions().contains(entry);
    QVERIFY(in_menu); // both Qt and rebuilt KDE menus expose the same action
    button->click(); // discovery route in the existing setup dialog
    auto* owner = view->active_gameplay_session();
    QVERIFY(owner);
    QCOMPARE(owner->phase(), gameplay::session_phase::setup);
    QVERIFY(!view->gameplay_timer.isActive());
    auto* dialog
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"));
    auto* count
        = dialog->findChild<QSpinBox*>(QStringLiteral("gameplay_slot_count"));
    QPointer<gameplay::setup_widget> fields
        = dialog->findChild<gameplay::setup_widget*>();
    QVERIFY(dialog->isVisible() && !dialog->isModal() && fields && count);
    QVERIFY(!count->keyboardTracking());
    QCOMPARE(count->maximum(), 64);
    auto chosen = owner->deck({ 0 })->configuration;
    chosen.deck_count = 1;
    chosen.training = true;
    QVERIFY(owner->configure_deck({ 0 }, chosen));
    QVERIFY(owner->set_show_count({ 0 }, true));
    QVERIFY(owner->set_pick_interval_ms(650));
    fields->findChild<QSpinBox*>(QStringLiteral("gameplay_sequential_count"))
        ->setValue(3);
    QCOMPARE(owner->configuration().sequential_count, std::size_t { 3 });
    const auto legacy = load_trainer_preferences();
    count->setValue(24);
    QVERIFY(fields.isNull()); // borrowing view dies before its previous owner
    owner = view->active_gameplay_session();
    QCOMPARE(owner->size(), std::size_t { 24 });
    QCOMPARE(owner->deck({ 0 })->configuration, chosen);
    QVERIFY(owner->deck({ 0 })->show_count);
    QCOMPARE(owner->pick_interval_ms(), 650);
    QCOMPARE(owner->configuration().sequential_count, std::size_t { 3 });
    count->setValue(2);
    owner = view->active_gameplay_session();
    QCOMPARE(owner->size(), std::size_t { 2 });
    QCOMPARE(owner->configuration().sequential_count, std::size_t { 2 });
    QVERIFY(!dialog->findChild<QLabel*>(QStringLiteral("gameplay_setup_error"))
                 ->text()
                 .isEmpty());
    dialog->reject();
    QCOMPARE(
        owner->deck({ 0 })->configuration, chosen
    ); // Close is not rollback
    entry->trigger();
    QVERIFY(dialog->isVisible());
    QCOMPARE(view->active_gameplay_session(), owner);
    QCOMPARE(load_trainer_preferences(), legacy);
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("game_saved_drills"))
                 ->isEnabled());
    QVERIFY(launcher_start_action(window)->isEnabled());
}

void table_tests::gameplay_launcher_roster_rejection_keeps_borrowed_views() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop opt-in launcher");
#endif
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))->trigger();
    auto* view = window.findChild<table*>();
    auto* owner = view->active_gameplay_session();
    auto* dialog
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"));
    QPointer<gameplay::setup_widget> fields
        = dialog->findChild<gameplay::setup_widget*>();
    const auto before = *owner->deck({ 0 });
    const auto count_before = owner->size();
    view->resize(
        1, 1
    ); // cannot produce representable geometry for the candidate
    auto* count
        = dialog->findChild<QSpinBox*>(QStringLiteral("gameplay_slot_count"));
    count->setValue(64);
    QCOMPARE(view->active_gameplay_session(), owner);
    QVERIFY(fields);
    QCOMPARE(*owner->deck({ 0 }), before);
    QCOMPARE(count->value(), static_cast<int>(count_before));
    QVERIFY(!dialog->findChild<QLabel*>(QStringLiteral("gameplay_setup_error"))
                 ->text()
                 .isEmpty());
    QVERIFY(!view->gameplay_timer.isActive());
    auto settings = owner->configuration();
    settings.allow_extra_slots = true;
    QVERIFY(owner->configure(settings));
    launcher_start_action(window)->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::setup);
    QCOMPARE(*owner->deck({ 0 }), before);
    QVERIFY(!view->gameplay_timer.isActive());
}

void table_tests::gameplay_launcher_start_rejects_drafts_and_budgets() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop opt-in launcher");
#endif
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))->trigger();
    auto* view = window.findChild<table*>();
    auto* owner = view->active_gameplay_session();
    auto* start = launcher_start_action(window);
    auto* dialog
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"));
    auto* count
        = dialog->findChild<QSpinBox*>(QStringLiteral("gameplay_slot_count"));
    count->setValue(1);
    owner = view->active_gameplay_session();
    auto* slot = view->slot_widgets.front();
    slot->findChild<QPushButton*>(QStringLiteral("slot_details_button"))
        ->click();
    QVERIFY(view->has_open_gameplay_settings());
    start->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::setup);
    QVERIFY(!view->gameplay_timer.isActive());
    auto* error
        = dialog->findChild<QLabel*>(QStringLiteral("gameplay_setup_error"));
    QVERIFY(error->text().contains(str_label("Apply or close")));
    auto* panel
        = slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    panel->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)
        ->click();
    QVERIFY(!view->has_open_gameplay_settings());
    auto choice = owner->deck({ 0 })->configuration;
    choice.deck_count = 100'000;
    QVERIFY(owner->configure_deck({ 0 }, choice));
    const auto before = *owner->deck({ 0 });
    start->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::setup);
    QCOMPARE(*owner->deck({ 0 }), before);
    QVERIFY(!view->gameplay_timer.isActive());
    QVERIFY(error->text().contains(str_label("Preparation failed")));
    choice.deck_count = 1;
    QVERIFY(owner->configure_deck({ 0 }, choice));
    start->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::running);
    QVERIFY(view->gameplay_timer.isActive());
    QVERIFY(!dialog->isVisible());
    QVERIFY(error->text().isEmpty());
    QCOMPARE(owner->deck({ 0 })->stream.cards.size(), std::size_t { 52 });
}

void table_tests::gameplay_launcher_geometry_requires_explicit_consent() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop opt-in launcher");
#endif
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))->trigger();
    auto* view = window.findChild<table*>();
    auto* dialog
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"));
    dialog->findChild<QSpinBox*>(QStringLiteral("gameplay_slot_count"))
        ->setValue(24);
    auto* owner = view->active_gameplay_session();
    const auto recommendation = owner->slot_constraints().recommended;
    view->setFixedSize(180, 120); // parent layout must not undo this demand
    QCoreApplication::processEvents();
    QVERIFY(owner->slot_constraints().recommended < recommendation);
    QCOMPARE(owner->size(), std::size_t { 24 });
    auto* start = launcher_start_action(window);
    start->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::setup);
    QVERIFY(!view->gameplay_timer.isActive());
    QVERIFY(owner->deck({ 0 })->stream.cards.empty());
    dialog->findChild<QCheckBox*>(QStringLiteral("gameplay_allow_extra_slots"))
        ->setChecked(true);
    QCOMPARE(owner->configuration().allow_extra_slots, true);
    view->set_card_orientation(card_orientation_mode::horizontal);
    QCoreApplication::processEvents();
    start->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::running);
    QCOMPARE(owner->size(), std::size_t { 24 });
}

void table_tests::gameplay_launcher_routes_runtime_and_finish_data() {
    QTest::addColumn<bool>("confirm");
    QTest::newRow("cancel-and-resume") << false;
    QTest::newRow("confirmed-finish") << true;
}

void table_tests::gameplay_launcher_routes_runtime_and_finish() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop opt-in launcher");
#endif
    QFETCH(bool, confirm);
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))->trigger();
    auto* view = window.findChild<table*>();
    auto* dialog
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"));
    dialog->findChild<QSpinBox*>(QStringLiteral("gameplay_slot_count"))
        ->setValue(2);
    auto* owner = view->active_gameplay_session();
    auto choice = owner->deck({ 1 })->configuration;
    choice.infinite = true;
    QVERIFY(owner->configure_deck({ 1 }, choice));
    auto* fields = dialog->findChild<gameplay::setup_widget*>();
    fields->findChild<QComboBox*>(QStringLiteral("gameplay_dealing_mode"))
        ->setCurrentIndex(2);
    auto* start = launcher_start_action(window);
    const auto legacy = load_trainer_preferences();
    const auto progress = load_training_progress();
    auto* clock
        = window.findChild<BaseClock*>(QString(), Qt::FindDirectChildrenOnly);
    QSignalSpy legacy_score(view, &table::score_adjusted);
    start->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::running);
    QVERIFY(view->gameplay_timer.isActive());
    QVERIFY(owner->deck({ 1 })->stream.infinite_cards);
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))
                 ->isEnabled());
    QVERIFY(clock->elapsed_time_ms() == 0);
    view->stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    for (int step = 0; step < 40 && !owner->current_quiz_batch(); ++step)
        QVERIFY(view->advance_gameplay_runtime(
            static_cast<std::uint64_t>(owner->pick_interval_ms()), answers
        ));
    view->publish_gameplay_runtime(answers);
    QVERIFY(owner->current_quiz_batch());
    const auto batch = *owner->current_quiz_batch();
    start->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::paused);
    QCOMPARE(*owner->current_quiz_batch(), batch);
    start->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::running);
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_settings_triggered", Qt::DirectConnection
        )
    );
    QCOMPARE(owner->phase(), gameplay::session_phase::paused);
    QCOMPARE(*owner->current_quiz_batch(), batch);
    // Opening ordinary appearance settings pauses only the owned runtime.
    for (auto* settings : window.findChildren<QDialog*>())
        if (settings->findChild<QTabWidget*>())
            settings->reject();
    start->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::running);
    answer_launcher_finish(
        window, confirm ? QMessageBox::Yes : QMessageBox::No
    );
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_finish_triggered", Qt::DirectConnection
        )
    );
    QCOMPARE(
        owner->phase(),
        confirm ? gameplay::session_phase::finished
                : gameplay::session_phase::running
    );
    QCOMPARE(owner->result().has_value(), confirm);
    QCOMPARE(view->gameplay_timer.isActive(), !confirm);
    QCOMPARE(start->isEnabled(), !confirm);
    QCOMPARE(
        window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))
            ->isEnabled(),
        confirm
    );
    if (confirm) {
        const auto result = *owner->result();
        QVERIFY(
            QMetaObject::invokeMethod(
                &window, "on_finish_triggered", Qt::DirectConnection
            )
        );
        QCOMPARE(*owner->result(), result);
        // Explicit fresh replacement reuses choices, never progress or shoes.
        window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))
            ->trigger();
        owner = view->active_gameplay_session();
        QCOMPARE(owner->phase(), gameplay::session_phase::setup);
        QCOMPARE(owner->deck({ 1 })->configuration, choice);
        QVERIFY(owner->deck({ 1 })->stream.cards.empty());
        QVERIFY(!owner->result());
    }
    QCOMPARE(load_trainer_preferences(), legacy);
    QCOMPARE(load_training_progress(), progress);
    QCOMPARE(legacy_score.count(), 0);
    QCOMPARE(view->capture_session_state(), table_session_state {});
    QVERIFY(view->capture_drill_settings().isEmpty());
}

void table_tests::gameplay_launcher_finish_replacement_is_not_finished() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop opt-in launcher");
#endif
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))->trigger();
    auto* view = window.findChild<table*>();
    launcher_start_action(window)->trigger();
    bool replaced = false;
    QTimer::singleShot(0, &window, [&] {
        replaced = view->install_gameplay_session(owned_clock_fixture(1));
        if (auto* question = window.findChild<QMessageBox*>())
            question->button(QMessageBox::Yes)->click();
    });
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_finish_triggered", Qt::DirectConnection
        )
    );
    QVERIFY(replaced);
    QCOMPARE(
        view->active_gameplay_session()->phase(), gameplay::session_phase::setup
    );
    QVERIFY(!view->gameplay_timer.isActive());
    QVERIFY(!view->active_gameplay_session()->result());
}

void table_tests::launcher_natural_end_and_legacy_return_data() {
    QTest::addColumn<bool>("physical_jokers");
    QTest::newRow("virtual-final-count") << false;
    QTest::newRow("physical-joker-final-count") << true;
}

void table_tests::launcher_natural_end_and_legacy_return() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop opt-in launcher");
#endif
    QFETCH(bool, physical_jokers);
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))->trigger();
    auto* view = window.findChild<table*>();
    auto* dialog
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"));
    dialog->findChild<QSpinBox*>(QStringLiteral("gameplay_slot_count"))
        ->setValue(1);
    auto* owner = view->active_gameplay_session();
    auto configuration = owner->configuration();
    configuration.source = physical_jokers
        ? gameplay::quiz_source::physical_joker
        : gameplay::quiz_source::virtual_interrupt;
    QVERIFY(owner->configure(configuration));
    auto choice = owner->deck({ 0 })->configuration;
    choice.deck_count = 1;
    choice.training = true;
    QVERIFY(owner->configure_deck({ 0 }, choice));
    QVERIFY(owner->set_show_count({ 0 }, true));
    launcher_start_action(window)->trigger();
    view->stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    for (int step = 0;
         step < 60 && owner->phase() != gameplay::session_phase::finished;
         ++step) {
        if (owner->current_quiz_batch()) {
            const auto prompt = view->gameplay_quiz_prompt({ 0 });
            QVERIFY(prompt);
            QVERIFY(view->skip_gameplay_quiz(*prompt));
        } else {
            QVERIFY(view->advance_gameplay_runtime(
                static_cast<std::uint64_t>(owner->pick_interval_ms()), answers
            ));
            view->publish_gameplay_runtime(answers);
        }
    }
    QCOMPARE(owner->phase(), gameplay::session_phase::finished);
    QVERIFY(owner->result());
    const auto* deck = owner->deck({ 0 });
    QCOMPARE(deck->status, gameplay::deck_status::completed);
    QCOMPARE(deck->dealt_physical_cards, physical_jokers ? 54U : 52U);
    QVERIFY(
        !owner->current_quiz_batch()
    ); // informational end, not another quiz
    QVERIFY(!view->gameplay_quiz_prompt({ 0 }));
    QCOMPARE(owner->statistics().verified_cards, std::uint64_t { 0 });
    QCOMPARE(owner->statistics().errors, std::uint64_t { 0 });
    const auto* card = owned_card(view->slot_widgets.front());
    QVERIFY(
        card->accessibleDescription().contains(str_label("Completed deck."))
    );
    QVERIFY(card->accessibleDescription().contains(
        QString::number(deck->running_count)
    ));
    QVERIFY(!card->accessibleDescription().contains(
        QStringLiteral("I18N_ARGUMENT_MISSING")
    ));
    QVERIFY(!launcher_start_action(window)->isEnabled());
    QPointer<gameplay::setup_widget> fields
        = dialog->findChild<gameplay::setup_widget*>();
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_new_game_triggered", Qt::DirectConnection
        )
    );
    QVERIFY(fields.isNull());
    QVERIFY(!view->active_gameplay_session());
    QVERIFY(
        !window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"))
    );
    QVERIFY(window.findChild<QAction*>(QStringLiteral("game_saved_drills"))
                ->isEnabled());
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_continue_button_clicked", Qt::DirectConnection
        )
    );
    auto* start = launcher_start_action(window);
    QVERIFY(start->isEnabled());
    start->trigger();
    QVERIFY(!view->active_gameplay_session());
    QVERIFY(view->quiz_running);
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))
                 ->isEnabled());
    auto* clock
        = window.findChild<BaseClock*>(QString(), Qt::FindDirectChildrenOnly);
    clock->pause();
}

void table_tests::gameplay_launcher_teardown_retires_global_view() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop opt-in launcher");
#endif
    launcher_preferences_guard preferences;
    auto window = std::make_unique<main_window>();
    window->resize(1600, 1000);
    window->show();
    window->findChild<QAction*>(QStringLiteral("game_new_gameplay"))->trigger();
    QPointer<gameplay::setup_widget> fields
        = window->findChild<gameplay::setup_widget*>();
    QPointer<QDialog> dialog
        = window->findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"));
    QVERIFY(fields && dialog);
    dialog->findChild<QSpinBox*>(QStringLiteral("gameplay_slot_count"))
        ->findChild<QLineEdit*>()
        ->setText(QStringLiteral("1"));
    launcher_start_action(*window)->trigger();
    QVERIFY(fields.isNull()); // Start commits pending count before generating
    auto* view = window->findChild<table*>();
    QCOMPARE(view->active_gameplay_session()->size(), std::size_t { 1 });
    QCOMPARE(
        view->active_gameplay_session()->phase(),
        gameplay::session_phase::running
    );
    fields = dialog->findChild<gameplay::setup_widget*>();
    window.reset();
    QVERIFY(fields.isNull());
    QVERIFY(dialog.isNull());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
