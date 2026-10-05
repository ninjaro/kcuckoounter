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

#include <QtTest/QtTest>

#ifdef KC_KDE
#include <KActionCollection>
#endif

void table_tests::session_restore_preflight_is_transactional() {
    table table_widget;
    table_widget.set_slot_count(1);
    table_widget.start_quiz(0, false);
    const table_session_state before = table_widget.capture_session_state();
    QVERIFY(before.quiz_running);
    QCOMPARE(before.slot_states.size(), 1);

    table_session_state invalid = before;
    table_slot_session_state invalid_later_slot = before.slot_states.first();
    invalid_later_slot.strategy_slug = QStringLiteral("missing_strategy");
    invalid_later_slot.strategy_id = 999999;
    invalid.slot_states.append(invalid_later_slot);

    QVERIFY(!table_widget.restore_session_state(invalid));
    QCOMPARE(table_widget.capture_session_state(), before);
}

void table_tests::session_capture_restore_recovers_paused_quiz_state() {
    table source;
    source.set_slot_count(1);
    source.set_pick_interval(300);
    source.start_quiz(0, false);
    source.on_clock_tick(9000, 9000);

    const table_session_state captured = source.capture_session_state();
    QVERIFY(captured.quiz_running);
    QVERIFY(!captured.quiz_paused);
    QCOMPARE(captured.slot_states.size(), 1);
    QVERIFY(captured.slot_states.first().quiz_prompt_active);
    QVERIFY(captured.slot_states.first().card.deck_position > 0);

    table restored;
    restored.set_slot_count(2);
    QVERIFY(restored.restore_session_state(captured));

    table_session_state expected = captured;
    expected.quiz_paused = true;
    for (table_slot_session_state& slot_state : expected.slot_states) {
        slot_state.paused = true;
    }
    QCOMPARE(restored.capture_session_state(), expected);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
