#include <shared_ui/qt/appearance_editor.hpp>
#include <shared_ui/qt/presentation.hpp>
#include <shared_ui/qt/status_strip.hpp>
// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "card_helpers/card_sheet.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"

#include "shell/gameplay_reservation.hpp"
#include <QDialog>
#include <QFocusEvent>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QtTest/QtTest>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

namespace {
settings_template_widget* find_appearance_editor(main_window& window) {
    for (auto* editor : window.findChildren<settings_template_widget*>())
        if (editor->desktop_model())
            return editor;
    return nullptr;
}
}

void table_tests::desktop_toolbar_preserves_table_and_commands_data() {
    QTest::addColumn<int>("count");
    QTest::addColumn<bool>("initial_compact");
    for (const int count : { 1, 4, 16 }) {
        for (const bool compact : { false, true }) {
            const auto name = QStringLiteral("slots=%1/compact=%2")
                                  .arg(count)
                                  .arg(compact)
                                  .toLatin1();
            QTest::newRow(name.constData()) << count << compact;
        }
    }
}

void table_tests::desktop_toolbar_preserves_table_and_commands() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop native toolbar");
#endif
    QFETCH(int, count);
    QFETCH(bool, initial_compact);
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    QCoreApplication::processEvents();
    if (auto* setup
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog")))
        setup->accept();
    auto* view = window.findChild<table*>();
    auto* start = launcher_start_action(window);
    auto* shell_bar
        = window.findChild<QToolBar*>(QStringLiteral("main_toolbar"));
    QVERIFY(shell_bar && shell_bar->actions().size() >= 4);
    auto* finish = shell_bar->actions()[2];
    auto* fresh = window.findChild<QAction*>(QStringLiteral("game_new"));
    QVERIFY(start && finish && fresh);
    QVERIFY(view->install_gameplay_session(
        owned_clock_fixture(static_cast<std::size_t>(count))
    ));
    start->trigger();
    QVERIFY(view->has_gameplay_runtime());
    view->stop_gameplay_clock();
    start->trigger();
    const auto before = view->capture_gameplay_checkpoint();
    QVERIFY(before);
    auto* bar = window.findChild<QToolBar*>();
    QVERIFY(bar);
    const auto actions = bar->actions();
    shell_bar->actions()[3]->trigger();
    auto* editor = find_appearance_editor(window);
    QVERIFY(editor && editor->desktop_model());
    for (bool compact :
         { initial_compact, !initial_compact, initial_compact }) {
        QVERIFY(editor->desktop_model()->set_override(
            "toolbar", compact ? "compact" : "classic"
        ));
        QVERIFY(editor->apply_theme_settings());
        QCoreApplication::processEvents();
        QCOMPARE(bar->actions(), actions);
        QCOMPARE(view->capture_gameplay_checkpoint(), before);
        QCOMPARE(view->slot_widgets.size(), static_cast<std::size_t>(count));
        QVERIFY(start->isEnabled());
        QVERIFY(finish->isEnabled());
    }
    QVERIFY(view->finish_gameplay_runtime());
    QVERIFY(fresh->isEnabled());
    fresh->trigger();
    QCOMPARE(
        view->active_gameplay_session()->phase(), gameplay::session_phase::setup
    );
}

void table_tests::desktop_status_reports_existing_values() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop status surface");
#endif
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    QCoreApplication::processEvents();
    if (auto* setup
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog")))
        setup->accept();
    auto* view = window.findChild<table*>();
    auto* start = launcher_start_action(window);
    auto* shell_bar
        = window.findChild<QToolBar*>(QStringLiteral("main_toolbar"));
    QVERIFY(shell_bar && shell_bar->actions().size() >= 4);
    auto* finish = shell_bar->actions()[2];
    auto* fresh = window.findChild<QAction*>(QStringLiteral("game_new"));
    QVERIFY(start && finish && fresh);
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(1)));
    auto* strip = window.findChild<shared_ui::qt::status_strip*>();
    QVERIFY(strip);
    start->trigger();
    QVERIFY(view->has_gameplay_runtime());
    view->stop_gameplay_clock();
    QTRY_COMPARE(strip->status_fields().front().value, str_label("Running"));
    start->trigger();
    QCOMPARE(strip->status_fields().front().value, str_label("Paused"));
    QVERIFY(!strip->status_label()->text().contains(
        QStringLiteral("I18N_ARGUMENT_MISSING")
    ));
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_table_rasterization_busy_changed",
            Qt::DirectConnection, Q_ARG(bool, true)
        )
    );
    QVERIFY(strip->progress_bar()->isVisible());
    QVERIFY(
        strip->status_fields().front().value.contains(str_label("Processing"))
    );
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_table_rasterization_busy_changed",
            Qt::DirectConnection, Q_ARG(bool, false)
        )
    );
    strip->control_slider()->setValue(735);
    QCOMPARE(view->active_gameplay_session()->pick_interval_ms(), 735);
    QVERIFY(strip->readout_label()->text().contains(QStringLiteral("735")));
    QVERIFY(view->finish_gameplay_runtime());
    QVERIFY(fresh->isEnabled());
}

void table_tests::desktop_hud_fits_without_changing_policy_data() {
    QTest::addColumn<int>("count");
    QTest::addColumn<bool>("instruments");
    for (const int count : { 1, 4, 16 }) {
        for (const bool instruments : { false, true }) {
            const auto name = QStringLiteral("slots=%1/instruments=%2")
                                  .arg(count)
                                  .arg(instruments)
                                  .toLatin1();
            QTest::newRow(name.constData()) << count << instruments;
        }
    }
}

void table_tests::desktop_hud_fits_without_changing_policy() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop HUD");
#endif
    QFETCH(int, count);
    QFETCH(bool, instruments);
    launcher_preferences_guard preferences;
    main_window window;
    window.resize(1600, 1000);
    window.show();
    QCoreApplication::processEvents();
    if (auto* setup
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog")))
        setup->accept();
    auto* view = window.findChild<table*>();
    auto* start = launcher_start_action(window);
    auto* shell_bar
        = window.findChild<QToolBar*>(QStringLiteral("main_toolbar"));
    QVERIFY(shell_bar && shell_bar->actions().size() >= 4);
    auto* finish = shell_bar->actions()[2];
    auto* fresh = window.findChild<QAction*>(QStringLiteral("game_new"));
    QVERIFY(start && finish && fresh);
    QVERIFY(view->install_gameplay_session(
        owned_clock_fixture(static_cast<std::size_t>(count))
    ));
    shell_bar->actions()[3]->trigger();
    auto* editor = find_appearance_editor(window);
    QVERIFY(editor && editor->desktop_model());
    QVERIFY(editor->desktop_model()->set_override(
        "hud", instruments ? "instruments" : "classic"
    ));
    QVERIFY(editor->apply_theme_settings());
    start->trigger();
    QVERIFY(view->has_gameplay_runtime());
    view->stop_gameplay_clock();
    start->trigger();
    const auto before = view->capture_gameplay_checkpoint();
    auto* strip = window.findChild<shared_ui::qt::status_strip*>();
    QVERIFY(strip && before);
    for (int width : { 1100, 850, 640, 1100 }) {
        window.resize(width, 700);
        QCoreApplication::processEvents();
        const auto settled = window.size();
        for (int iteration = 0; iteration < 4; ++iteration)
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCOMPARE(window.size(), settled);
        QCOMPARE(view->capture_gameplay_checkpoint(), before);
        QVERIFY(
            strip->control_slider()->minimum()
            <= before->state.pickup_interval_ms
        );
    }
    QVERIFY(view->finish_gameplay_runtime());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
