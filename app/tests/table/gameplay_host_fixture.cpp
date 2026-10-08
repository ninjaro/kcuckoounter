#include "table/gameplay_host_tests.hpp"

#include "shell/main_window.hpp"
#include "table/table_fixture.hpp"

#include <QDialog>
#include <QtTest/QtTest>

using namespace table_test;

void gameplay_host_tests::initTestCase() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP(
        "Desktop identified launcher; Android lifecycle activation is separate"
    );
#endif
}

void gameplay_host_tests::launch(main_window& window) {
    window.resize(1600, 1000);
    window.show();
    auto* entry
        = window.findChild<QAction*>(QStringLiteral("game_new_gameplay"));
    QVERIFY(entry);
    entry->trigger(); // intentionally precedes the queued startup callback
    auto* dialog
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"));
    QVERIFY(dialog);
    dialog->findChild<QSpinBox*>(QStringLiteral("gameplay_slot_count"))
        ->setValue(1);
    auto* view = window.findChild<table*>();
    QVERIFY(view);
    set_gameplay_motion_style(*view, 0);
    auto* owner = view->active_gameplay_session();
    QVERIFY(owner);
    auto deck = owner->deck({ 0 })->configuration;
    deck.deck_count = 1;
    deck.training = true;
    QVERIFY(owner->configure_deck({ 0 }, deck));
    launcher_start_action(window)->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::running);
    view->stop_gameplay_clock();
}

bool gameplay_host_tests::tick(table& view, std::uint64_t elapsed) {
    std::vector<gameplay::quiz_answer> answers;
    const bool accepted = view.advance_gameplay_runtime(elapsed, answers);
    view.publish_gameplay_runtime(answers);
    return accepted;
}
