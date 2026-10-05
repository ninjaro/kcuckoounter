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
#include <QMenu>
#include <QTableWidget>
#include <QtTest/QtTest>

#include <array>
#include <limits>
#include <memory>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::review_keeps_one_record_per_deck() {
    table view;
    view.resize(1200, 900);
    QVERIFY(!view.has_gameplay_corrections());
    QVERIFY(view.gameplay_corrections().empty());
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, false, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1, 3 }
    )));
    QVERIFY(view.start_gameplay_runtime(101));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    QSignalSpy changed(&view, &table::gameplay_corrections_changed);
    QVERIFY(view.skip_gameplay_quiz(*view.gameplay_quiz_prompt({ 0 })));
    QVERIFY(
        view.check_gameplay_quiz(*view.gameplay_quiz_prompt({ 1 }))
    ); // Block failure
    QVERIFY(view.skip_gameplay_quiz(*view.gameplay_quiz_prompt({ 3 })));
    QCOMPARE(changed.count(), 3);
    auto records = view.gameplay_corrections();
    QCOMPARE(records.size(), std::size_t { 3 });
    QCOMPARE(records[0].owner, gameplay::deck_id { 0 });
    QCOMPARE(records[1].outcome, gameplay::quiz_outcome::wrong);
    QCOMPARE(records[2].owner, gameplay::deck_id { 3 });
    QCOMPARE(
        view.gameplay_owner->deck({ 1 })->status, gameplay::deck_status::failed
    );
    const auto statistics = view.gameplay_owner->statistics();
    const auto timing = view.gameplay_owner->timing();
    for (int repeat = 0; repeat < 12; ++repeat) {
        QCOMPARE(view.gameplay_corrections(), records);
        view.publish_gameplay_runtime({});
    }
    QCOMPARE(changed.count(), 3); // publication without answers is not polling
    QCOMPARE(view.gameplay_owner->statistics(), statistics);
    QCOMPARE(view.gameplay_owner->timing(), timing);
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    const auto prompt = view.gameplay_quiz_prompt({ 0 });
    QVERIFY(prompt);
    QCOMPARE(view.gameplay_corrections(), records); // re-asking is not clearing
    const auto expected
        = view.gameplay_owner->deck({ 0 })->quiz->expected_count;
    QVERIFY(view.edit_gameplay_quiz(*prompt, expected));
    QVERIFY(view.check_gameplay_quiz(*prompt));
    QCOMPARE(view.gameplay_corrections().size(), std::size_t { 2 });
    QCOMPARE(
        view.gameplay_corrections().front().owner, gameplay::deck_id { 1 }
    );
    QVERIFY(
        QMetaObject::invokeMethod(
            view.slot_widgets[3], "on_quiz_continue_button_clicked",
            Qt::DirectConnection
        )
    );
    QCOMPARE(view.gameplay_corrections().size(), std::size_t { 1 });
    const auto notified = changed.count();
    QVERIFY(
        QMetaObject::invokeMethod(
            view.slot_widgets[3], "on_quiz_continue_button_clicked",
            Qt::DirectConnection
        )
    );
    QCOMPARE(
        changed.count(), notified
    ); // no-op Dismiss does not rebuild a review
    view.clear_gameplay_session();
    QVERIFY(!view.has_gameplay_corrections());
    QVERIFY(view.gameplay_corrections().empty());
}

void table_tests::gameplay_review_native_timeout_data() {
    QTest::addColumn<int>("count");
    QTest::addColumn<bool>("maximum_count");
    for (const int count : { 24, 64 })
        for (const bool maximum : { false, true })
            QTest::newRow(qPrintable(
                QStringLiteral("slots-%1-max-%2").arg(count).arg(maximum)
            )) << count
               << maximum;
}

void table_tests::gameplay_review_native_timeout() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop correction review");
#endif
    QFETCH(int, count);
    QFETCH(bool, maximum_count);
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    auto* view = window.findChild<table*>();
    auto* action
        = window.findChild<QAction*>(QStringLiteral("game_review_corrections"));
    QVERIFY(action && !action->isEnabled());
    bool in_menu = false;
    for (auto* menu : window.findChildren<QMenu*>())
        in_menu = in_menu || menu->actions().contains(action);
    QVERIFY(in_menu);
    auto fixture = owned_clock_fixture(
        static_cast<std::size_t>(count), true,
        gameplay::dealing_mode::simultaneous, gameplay::quiz_scope::multi, { 1 }
    );
    auto settings = fixture->configuration();
    settings.failure = gameplay::failure_policy::lives;
    QVERIFY(fixture->configure(settings));
    auto prepared = fixture->deck({ 0 })->stream;
    prepared.rank_weights.fill(0);
    prepared.initial_running_count = maximum_count
        ? std::numeric_limits<std::int64_t>::max()
        : std::numeric_limits<std::int64_t>::min();
    std::vector<gameplay::prepared_deck> streams;
    for (std::size_t id = 0; id < fixture->size(); ++id)
        streams.push_back(id == 0 ? prepared : fixture->deck({ id })->stream);
    QVERIFY(
        fixture->prepare_decks(streams, std::array<std::uint64_t, 1> { 1 })
    );
    QVERIFY(view->install_gameplay_session(std::move(fixture)));
    QVERIFY(view->start_gameplay_runtime(102));
    view->stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    view->publish_gameplay_runtime(answers);
    const auto entered = maximum_count
        ? std::numeric_limits<std::int64_t>::min()
        : std::numeric_limits<std::int64_t>::max();
    QVERIFY(
        view->edit_gameplay_quiz(*view->gameplay_quiz_prompt({ 0 }), entered)
    );
    QSignalSpy changed(view, &table::gameplay_corrections_changed);
    QVERIFY(view->advance_gameplay_runtime(60'000, answers));
    QCOMPARE(answers.size(), static_cast<std::size_t>(count));
    view->publish_gameplay_runtime(answers);
    QCOMPARE(
        changed.count(), 1
    ); // one view update for the whole atomic timeout
    auto* owner = view->active_gameplay_session();
    QCOMPARE(owner->phase(), gameplay::session_phase::finished);
    QCOMPARE(owner->statistics().errors, static_cast<std::uint64_t>(count - 1));
    QCOMPARE(owner->remaining_lives(), 0);
    QVERIFY(!view->gameplay_timer.isActive());
    QVERIFY(action->isEnabled());
    QVERIFY(
        !window.findChild<QDialog*>(QStringLiteral("gameplay_review_dialog"))
    ); // never auto-opens
    const auto result = *owner->result();
    action->trigger();
    auto* dialog
        = window.findChild<QDialog*>(QStringLiteral("gameplay_review_dialog"));
    auto* rows = dialog->findChild<QTableWidget*>(
        QStringLiteral("gameplay_review_rows")
    );
    QVERIFY(dialog->isVisible() && !dialog->isModal());
    QCOMPARE(rows->rowCount(), count);
    QCOMPARE(rows->editTriggers(), QAbstractItemView::NoEditTriggers);
    QCOMPARE(rows->item(0, 2)->text(), str_label("Training"));
    QCOMPARE(rows->item(0, 3)->text(), str_label("Wrong (timeout)"));
    QCOMPARE(rows->item(0, 4)->text(), QString::number(entered));
    QCOMPARE(
        rows->item(0, 5)->text(),
        QString::number(prepared.initial_running_count)
    );
    QVERIFY(!rows->item(0, 4)->flags().testFlag(Qt::ItemIsEditable));
    rows->setCurrentCell(count - 1, 0);
    const auto selected = rows->item(rows->currentRow(), 0)->data(Qt::UserRole);
    QVERIFY(
        QMetaObject::invokeMethod(
            view->slot_widgets[0], "on_quiz_continue_button_clicked",
            Qt::DirectConnection
        )
    );
    QCOMPARE(rows->rowCount(), count - 1);
    QCOMPARE(rows->item(rows->currentRow(), 0)->data(Qt::UserRole), selected);
    QCOMPARE(
        *owner->result(), result
    ); // review/Dismiss cannot change terminal accounting
    dialog->resize(360, 300);
    QTest::keyClick(rows, Qt::Key_Up);
    QVERIFY(rows->currentRow() >= 0);
    dialog->reject();
    action->trigger();
    QCOMPARE(
        window.findChild<QDialog*>(QStringLiteral("gameplay_review_dialog")),
        dialog
    );
    QCOMPARE(rows->rowCount(), count - 1);
    QCOMPARE(*owner->result(), result);
}

void table_tests::gameplay_review_native_live_mapping_and_clear() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop correction review");
#endif
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    auto* view = window.findChild<table*>();
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(
        4, false, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1, 3 }
    )));
    view->setFixedSize(180, 120); // compact feedback/input hosts
    QVERIFY(view->start_gameplay_runtime(103));
    view->stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    view->publish_gameplay_runtime(answers);
    QVERIFY(view->skip_gameplay_quiz(*view->gameplay_quiz_prompt({ 0 })));
    QVERIFY(view->skip_gameplay_quiz(*view->gameplay_quiz_prompt({ 2 })));
    const auto timing = view->gameplay_owner->timing();
    const auto statistics = view->gameplay_owner->statistics();
    auto* action
        = window.findChild<QAction*>(QStringLiteral("game_review_corrections"));
    action->trigger();
    auto* dialog
        = window.findChild<QDialog*>(QStringLiteral("gameplay_review_dialog"));
    auto* rows = dialog->findChild<QTableWidget*>(
        QStringLiteral("gameplay_review_rows")
    );
    QCOMPARE(rows->rowCount(), 2);
    QCOMPARE(view->gameplay_owner->phase(), gameplay::session_phase::running);
    QCOMPARE(view->gameplay_owner->timing(), timing);
    QCOMPARE(view->gameplay_owner->statistics(), statistics);
    QCOMPARE(rows->item(0, 3)->text(), str_label("Skipped"));
    QVERIFY(view->pause_gameplay_runtime());
    rows->setCurrentCell(1, 0);
    QSignalSpy layout_changed(view, &table::gameplay_layout_changed);
    QVERIFY(view->swap_gameplay_decks({ 0 }, { 2 }));
    QCOMPARE(layout_changed.count(), 1);
    QCOMPARE(rows->item(0, 1)->text(), QStringLiteral("3"));
    QCOMPARE(rows->item(1, 1)->text(), QStringLiteral("1"));
    QCOMPARE(rows->currentRow(), 1);
    QSignalSpy data_changed(rows->model(), &QAbstractItemModel::dataChanged);
    for (int repeat = 0; repeat < 12; ++repeat)
        view->publish_gameplay_runtime({});
    QCOMPARE(data_changed.count(), 0);
    dialog->reject();
    const auto paused_timing = view->gameplay_owner->timing();
    QVERIFY(
        QMetaObject::invokeMethod(
            view->slot_widgets[0], "on_quiz_continue_button_clicked",
            Qt::DirectConnection
        )
    );
    QCOMPARE(data_changed.count(), 0); // hidden view is not rebuilt
    action->trigger();
    QCOMPARE(rows->rowCount(), 1);
    QCOMPARE(
        rows->item(0, 0)->data(Qt::UserRole).toULongLong(), qulonglong { 2 }
    );
    QCOMPARE(view->gameplay_owner->phase(), gameplay::session_phase::paused);
    QCOMPARE(view->gameplay_owner->timing(), paused_timing);
    dialog->activateWindow();
    QCoreApplication::processEvents();
    QCOMPARE(QApplication::activeWindow(), dialog);
    QVERIFY(view->resume_gameplay_runtime());
    view->stop_gameplay_clock();
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    view->publish_gameplay_runtime(answers);
    const auto prompt = view->gameplay_quiz_prompt({ 2 });
    QVERIFY(prompt);
    QCOMPARE(
        QApplication::activeWindow(), dialog
    ); // new prompts do not steal review activation
    QVERIFY(view->edit_gameplay_quiz(
        *prompt, view->gameplay_owner->deck({ 2 })->quiz->expected_count
    ));
    QVERIFY(view->check_gameplay_quiz(*prompt));
    QCOMPARE(rows->rowCount(), 0);
    QVERIFY(!action->isEnabled());
    QVERIFY(dialog->isVisible());
    QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("gameplay_review_empty"))
                ->isVisible());
}

void table_tests::gameplay_review_native_replacement_and_legacy() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop correction review");
#endif
    launcher_preferences_guard preferences;
    auto window = std::make_unique<main_window>();
    window->resize(1600, 1000);
    window->show();
    auto* view = window->findChild<table*>();
    auto* action = window->findChild<QAction*>(
        QStringLiteral("game_review_corrections")
    );
    const auto legacy = load_trainer_preferences();
    const auto progress = load_training_progress();
    QVERIFY(!action->isEnabled());
    QVERIFY(
        QMetaObject::invokeMethod(
            window.get(), "on_gameplay_review_triggered", Qt::DirectConnection
        )
    );
    QVERIFY(
        !window->findChild<QDialog*>(QStringLiteral("gameplay_review_dialog"))
    );
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(
        2, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1 }
    )));
    QVERIFY(view->start_gameplay_runtime(104));
    view->stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    view->publish_gameplay_runtime(answers);
    QVERIFY(view->skip_gameplay_quiz(*view->gameplay_quiz_prompt({ 0 })));
    action->trigger();
    QPointer<QDialog> dialog
        = window->findChild<QDialog*>(QStringLiteral("gameplay_review_dialog"));
    QVERIFY(dialog && dialog->isVisible());
    bool replaced = false;
    const auto connection
        = connect(view, &table::gameplay_corrections_changed, view, [&] {
              replaced = view->install_gameplay_session(owned_clock_fixture(1));
          });
    QVERIFY(view->skip_gameplay_quiz(*view->gameplay_quiz_prompt({ 1 })));
    disconnect(connection);
    QVERIFY(replaced && dialog.isNull());
    QVERIFY(!action->isEnabled());
    QVERIFY(view->gameplay_corrections().empty());
    QCOMPARE(view->gameplay_owner->phase(), gameplay::session_phase::setup);
    QCOMPARE(load_trainer_preferences(), legacy);
    QCOMPARE(load_training_progress(), progress);
    QVERIFY(
        QMetaObject::invokeMethod(
            window.get(), "on_new_game_triggered", Qt::DirectConnection
        )
    );
    QVERIFY(!view->active_gameplay_session());
    QVERIFY(!action->isEnabled());
    QVERIFY(view->gameplay_corrections().empty());
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(
        1, false, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::single, { 1 }
    )));
    QVERIFY(view->start_gameplay_runtime(105));
    view->stop_gameplay_clock();
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    view->publish_gameplay_runtime(answers);
    const auto prompt = view->gameplay_quiz_prompt({ 0 });
    QVERIFY(prompt);
    QVERIFY(view->skip_gameplay_quiz(*prompt));
    action->trigger();
    dialog
        = window->findChild<QDialog*>(QStringLiteral("gameplay_review_dialog"));
    QVERIFY(dialog && dialog->isVisible());
    window.reset();
    QVERIFY(dialog.isNull()); // shell teardown also retires the retained view
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
