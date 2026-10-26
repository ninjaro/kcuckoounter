#include <shared_ui/qt/appearance_editor.hpp>
#include <shared_ui/qt/icons.hpp>
#include <shared_ui/qt/toolbar_presentation.hpp>
// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"
#include <shared_ui/qt/presentation.hpp>

#include "arch/str_label.hpp"
#include "settings/training_progress.hpp"
#include "shell/main_window.hpp"
#include "table/settings_template.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QAccessible>
#include <QComboBox>
#include <QDialog>
#include <QFocusEvent>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSlider>
#include <QStringList>
#include <QToolButton>
#include <QToolTip>
#include <QtTest/QtTest>

#include <array>

using namespace table_test;

namespace {

// Keep native docking state out of both the user's settings and later rows.
struct shell_state_guard {
    desktop_shell_state previous = load_desktop_shell_state();

    shell_state_guard() { save_desktop_shell_state({}); }

    ~shell_state_guard() { save_desktop_shell_state(previous); }
};

settings_template_widget* appearance_editor(main_window& window) {
    for (auto* editor : window.findChildren<settings_template_widget*>())
        if (editor->desktop_model())
            return editor;
    return nullptr;
}

} // namespace

void table_tests::toolbar_styles_preserve_owned_quiz_data() {
    QTest::addColumn<bool>("initial_compact");
    QTest::addColumn<bool>("global_pause");
    QTest::addColumn<bool>("multi");
    for (const bool compact : { false, true })
        for (const bool global : { false, true })
            for (const bool multi : { false, true })
                QTest::newRow(
                    qPrintable(QStringLiteral("compact=%1/global=%2/multi=%3")
                                   .arg(compact)
                                   .arg(global)
                                   .arg(multi))
                ) << compact
                  << global << multi;
}

void table_tests::toolbar_styles_preserve_owned_quiz() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop activated toolbar compatibility");
#endif
    QFETCH(bool, initial_compact);
    QFETCH(bool, global_pause);
    QFETCH(bool, multi);
    launcher_preferences_guard preferences;
    shell_state_guard shell;
    auto ui = load_desktop_ui_preferences();
    ui.toolbar_override = initial_compact ? desktop_toolbar_style::compact
                                          : desktop_toolbar_style::classic;
    QVERIFY(save_desktop_ui_preferences(ui));
    main_window window;
    window.resize(1800, 1200);
    window.show();
    QCoreApplication::processEvents();
    if (auto* setup = window.findChild<QDialog*>())
        setup->reject();
    auto* view = window.findChild<table*>();
    auto* toolbar
        = window.findChild<BaseToolBar*>(QStringLiteral("main_toolbar"));
    QVERIFY(view && toolbar);
    set_gameplay_motion_style(*view, 0);
    const auto actions = toolbar->actions();
    QCOMPARE(actions.size(), qsizetype { 4 });
    QList<QWidget*> buttons;
    for (auto* action : actions)
        buttons.append(toolbar->widgetForAction(action));
    auto* start = launcher_start_action(window);
    QCOMPARE(start, actions[1]);
    auto* finish = actions[2];
    auto* settings = actions[3];
    auto* start_button
        = qobject_cast<QToolButton*>(toolbar->widgetForAction(start));
    auto* finish_button
        = qobject_cast<QToolButton*>(toolbar->widgetForAction(finish));
    auto* settings_button
        = qobject_cast<QToolButton*>(toolbar->widgetForAction(settings));
    QVERIFY(start_button && finish_button && settings_button);
    const auto shortcuts = start->shortcuts();
    QVERIFY(shortcuts.contains(QKeySequence(Qt::Key_P)));
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(
        4, global_pause, gameplay::dealing_mode::simultaneous,
        multi ? gameplay::quiz_scope::multi : gameplay::quiz_scope::single,
        { 1, 4 }
    )));
    auto& owner = *view->active_gameplay_session();
    QCOMPARE(start->text(), str_label("Start"));
    QCOMPARE(start_button->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    QVERIFY(start->isEnabled() && !finish->isEnabled());
    const auto legacy_preferences = load_trainer_preferences();
    const auto progress = load_training_progress();
    auto* legacy_clock
        = window.findChild<BaseClock*>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(legacy_clock && !legacy_clock->is_active());
    QSignalSpy legacy_pause(view, &table::dialog_opened);
    QSignalSpy legacy_score(view, &table::score_adjusted);
    QSignalSpy preparation(view, &table::gameplay_preparation_required);
    QSignalSpy triggered(start, &QAction::triggered);
    QVERIFY(view->start_gameplay_runtime(71));
    view->stop_gameplay_clock();
    QCOMPARE(start->text(), str_label("Pause"));
    QCOMPARE(start_button->text(), start->text());
    QVERIFY(start->isEnabled() && finish->isEnabled());
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    view->publish_gameplay_runtime(answers);
    QVERIFY(owner.current_quiz_batch());
    QCOMPARE(
        owner.current_quiz_batch()->remaining_ms.has_value(), global_pause
    );
    QCOMPARE(start->text(), str_label("Pause"));
    QCOMPARE(start_button->text(), start->text());
    auto* slot = view->slot_widgets[2];
    auto* input = owned_input(slot);
    QVERIFY(input && input->isVisible() && input->isEnabled());
    enter_owned_count(input, QStringLiteral("+17"));
    input->setSelection(1, 2);
    const auto cursor = input->cursorPosition();
    const auto selection = input->selectedText();
    const auto prompt = view->gameplay_quiz_prompt({ 2 });
    QVERIFY(prompt);
    QTest::keyClick(start_button, Qt::Key_Space);
    QCOMPARE(triggered.count(), 1);
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    QCOMPARE(start->text(), str_label("Resume"));
    QVERIFY(!input->isEnabled());
    const std::array records { *owner.deck({ 0 }), *owner.deck({ 1 }),
                               *owner.deck({ 2 }), *owner.deck({ 3 }) };
    const auto batch = *owner.current_quiz_batch();
    const auto timing = owner.timing();
    const auto statistics = owner.statistics();
    const auto widgets = view->slot_widgets;
    std::array<QRect, 4> bounds;
    std::array<int, 4> demand;
    for (std::size_t id = 0; id < widgets.size(); ++id) {
        bounds[id] = widgets[id]->geometry();
        demand[id] = widgets[id]->card_face_need_short_px();
    }
    auto* cache = table::shared_image_service();
    const auto state = window.saveState();
    auto* speed
        = window.findChild<QSlider*>(QStringLiteral("pickup_interval_slider"));
    QVERIFY(speed && speed->isEnabled());
    const auto interval = speed->value();
    QTest::mouseClick(settings_button, Qt::LeftButton);
    auto* editor = appearance_editor(window);
    QVERIFY(editor);
    auto* combo = editor->desktop_model();
    QVERIFY(combo);
    // Native accessibility can omit dialog punctuation and mnemonics. Preserve
    // the finalized widgets' names rather than synthesizing a shared label.
    QStringList names;
    for (auto* action : actions) {
        auto* button = toolbar->widgetForAction(action);
        auto* accessible = QAccessible::queryAccessibleInterface(button);
        QVERIFY(accessible && !accessible->text(QAccessible::Name).isEmpty());
        names.append(accessible->text(QAccessible::Name));
    }
    QCOMPARE(names[1], str_label("Resume"));
    // The native appearance transaction changes only the toolbar treatment.
    for (const bool compact : { !initial_compact, initial_compact, true }) {
        combo->set_override("toolbar", compact ? "compact" : "classic");
        QVERIFY(editor->apply_theme_settings());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCOMPARE(toolbar->actions(), actions);
        QCOMPARE(start->shortcuts(), shortcuts);
        QCOMPARE(start->text(), str_label("Resume"));
        QCOMPARE(start_button->text(), start->text());
        QCOMPARE(owner.phase(), gameplay::session_phase::paused);
        for (auto* action : actions) {
            auto* button
                = qobject_cast<QToolButton*>(toolbar->widgetForAction(action));
            QVERIFY(button && button->focusPolicy() == Qt::StrongFocus);
            QCOMPARE(button, buttons.at(actions.indexOf(action)));
            QCOMPARE(button->defaultAction(), action);
            QCOMPARE(
                button->toolButtonStyle(),
                compact && action != start && !action->icon().isNull()
                    ? Qt::ToolButtonIconOnly
                    : Qt::ToolButtonTextBesideIcon
            );
            auto* accessible = QAccessible::queryAccessibleInterface(button);
            QVERIFY(
                accessible && !accessible->text(QAccessible::Name).isEmpty()
            );
            QCOMPARE(
                accessible->text(QAccessible::Name),
                names.at(actions.indexOf(action))
            );
            QVERIFY(!button->toolTip().isEmpty());
            bool in_menu = false;
            for (auto* menu : window.findChildren<QMenu*>())
                in_menu |= menu->actions().contains(action);
            QVERIFY(in_menu);
        }
        for (std::size_t id = 0; id < records.size(); ++id) {
            QCOMPARE(*owner.deck({ id }), records[id]);
            QCOMPARE(widgets[id]->geometry(), bounds[id]);
            QCOMPARE(widgets[id]->card_face_need_short_px(), demand[id]);
        }
        QCOMPARE(view->gameplay_quiz_prompt({ 2 }), prompt);
        QCOMPARE(owned_input(slot), input);
        QCOMPARE(input->text(), QStringLiteral("+17"));
        QCOMPARE(input->cursorPosition(), cursor);
        QCOMPARE(input->selectedText(), selection);
        QCOMPARE(*owner.current_quiz_batch(), batch);
        QCOMPARE(owner.timing(), timing);
        QCOMPARE(owner.statistics(), statistics);
        QCOMPARE(table::shared_image_service(), cache);
        QCOMPARE(speed->value(), interval);
        QVERIFY(speed->isEnabled());
        QCOMPARE(window.saveState(), state);
    }
    editor->window()->hide();
    auto* secondary
        = qobject_cast<QToolButton*>(toolbar->widgetForAction(actions[0]));
    QVERIFY(secondary);
    const auto size = secondary->size();
    QFocusEvent focus(QEvent::FocusIn, Qt::TabFocusReason);
    QApplication::sendEvent(secondary, &focus);
    QCOMPARE(QToolTip::text(), secondary->toolTip());
    QCOMPARE(secondary->size(), size);
    QFocusEvent unfocus(QEvent::FocusOut, Qt::TabFocusReason);
    QApplication::sendEvent(secondary, &unfocus);
    const auto icon = actions[0]->icon();
    actions[0]->setIcon(QIcon());
    QCOMPARE(secondary->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    actions[0]->setIcon(icon);
    QCOMPARE(secondary->toolButtonStyle(), Qt::ToolButtonIconOnly);

    // Space and P dispatch the same labelled native action after restyling.
    window.activateWindow();
    start_button->setFocus();
    QTest::qWait(1); // native activation, while the owned clock is paused
    QTest::keyClick(start_button, Qt::Key_Space);
    view->stop_gameplay_clock();
    QCOMPARE(owner.phase(), gameplay::session_phase::running);
    QCOMPARE(start->text(), str_label("Pause"));
    QTest::keyClick(&window, Qt::Key_P);
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    QTest::keyClick(&window, Qt::Key_P);
    view->stop_gameplay_clock();
    QCOMPARE(triggered.count(), 4);
    QCOMPARE(owner.phase(), gameplay::session_phase::running);
    QCOMPARE(*owner.current_quiz_batch(), batch);
    QCOMPARE(owner.timing(), timing);
    QCOMPARE(input->text(), QStringLiteral("+17"));
    QCOMPARE(input->cursorPosition(), cursor);
    QCOMPARE(input->selectedText(), selection);

    // Reusing native Settings while Running pauses only the owned runtime;
    // the already-created appearance view must not answer or reroll the quiz.
    QTest::mouseClick(settings_button, Qt::LeftButton);
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    QCOMPARE(start->text(), str_label("Resume"));
    QCOMPARE(appearance_editor(window), editor);
    QCOMPARE(*owner.current_quiz_batch(), batch);
    QCOMPARE(owner.timing(), timing);
    QCOMPARE(view->gameplay_quiz_prompt({ 2 }), prompt);
    QCOMPARE(input->text(), QStringLiteral("+17"));
    editor->window()->hide();
    QTest::keyClick(start_button, Qt::Key_Space);
    view->stop_gameplay_clock();
    QCOMPARE(owner.phase(), gameplay::session_phase::running);

    answer_launcher_finish(window, QMessageBox::No);
    QTest::mouseClick(finish_button, Qt::LeftButton);
    view->stop_gameplay_clock();
    QCOMPARE(owner.phase(), gameplay::session_phase::running);
    QCOMPARE(*owner.current_quiz_batch(), batch);
    QVERIFY(!owner.result());
    QTest::keyClick(start_button, Qt::Key_Space);
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    answer_launcher_finish(window, QMessageBox::No);
    QTest::mouseClick(finish_button, Qt::LeftButton);
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    QCOMPARE(*owner.current_quiz_batch(), batch);
    QCOMPARE(owner.timing(), timing);
    QTest::keyClick(start_button, Qt::Key_Space);
    view->stop_gameplay_clock();
    enter_owned_count(input, QString::number(records[2].quiz->expected_count));
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(!owner.deck({ 2 })->quiz);
    QCOMPARE(owner.deck({ 2 })->statistics.verified_cards, std::uint64_t { 1 });
    answer_launcher_finish(window, QMessageBox::Yes);
    QTest::mouseClick(finish_button, Qt::LeftButton);
    QCOMPARE(owner.phase(), gameplay::session_phase::finished);
    QVERIFY(owner.result());
    QVERIFY(!start->isEnabled() && !finish->isEnabled());
    QVERIFY(!start_button->isEnabled() && !finish_button->isEnabled());
    const auto result = *owner.result();
    const auto finished_timing = owner.timing();
    QTest::mouseClick(settings_button, Qt::LeftButton);
    QCOMPARE(appearance_editor(window), editor);
    for (const bool compact : { false, true }) {
        combo->set_override("toolbar", compact ? "compact" : "classic");
        QVERIFY(editor->apply_theme_settings());
        QCOMPARE(toolbar->actions(), actions);
        QCOMPARE(*owner.result(), result);
        QCOMPARE(owner.timing(), finished_timing);
        QVERIFY(!start->isEnabled() && !finish->isEnabled());
    }
    QCOMPARE(legacy_clock->elapsed_time_ms(), qint64 { 0 });
    QVERIFY(!legacy_clock->is_active());
    QCOMPARE(load_trainer_preferences(), legacy_preferences);
    QCOMPARE(load_training_progress(), progress);
    QCOMPARE(legacy_pause.count(), 0);
    QCOMPARE(legacy_score.count(), 0);
    QCOMPARE(preparation.count(), 0);
}

void table_tests::toolbar_styles_preserve_terminal_routes_data() {
    QTest::addColumn<bool>("initial_compact");
    QTest::addColumn<bool>("physical_jokers");
    for (const bool compact : { false, true })
        for (const bool physical : { false, true })
            QTest::newRow(
                qPrintable(QStringLiteral("compact=%1/physical=%2")
                               .arg(compact)
                               .arg(physical))
            ) << compact
              << physical;
}

void table_tests::toolbar_styles_preserve_terminal_routes() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop activated toolbar terminal routes");
#endif
    QFETCH(bool, initial_compact);
    QFETCH(bool, physical_jokers);
    launcher_preferences_guard preferences;
    shell_state_guard shell;
    auto ui = load_desktop_ui_preferences();
    ui.toolbar_override = initial_compact ? desktop_toolbar_style::compact
                                          : desktop_toolbar_style::classic;
    QVERIFY(save_desktop_ui_preferences(ui));
    main_window window;
    window.resize(1600, 1000);
    window.show();
    QCoreApplication::processEvents();
    if (auto* setup = window.findChild<QDialog*>())
        setup->reject();
    auto* toolbar
        = window.findChild<BaseToolBar*>(QStringLiteral("main_toolbar"));
    auto* view = window.findChild<table*>();
    QVERIFY(toolbar && view);
    set_gameplay_motion_style(*view, 0);
    const auto actions = toolbar->actions();
    QCOMPARE(actions.size(), qsizetype { 4 });
    auto* start = launcher_start_action(window);
    auto* start_button
        = qobject_cast<QToolButton*>(toolbar->widgetForAction(start));
    QVERIFY(start_button);
    auto fixture = owned_clock_fixture(1, true);
    auto configuration = fixture->configuration();
    configuration.source = physical_jokers
        ? gameplay::quiz_source::physical_joker
        : gameplay::quiz_source::virtual_interrupt;
    QVERIFY(fixture->configure(configuration));
    QVERIFY(view->install_gameplay_session(std::move(fixture)));
    auto& owner = *view->active_gameplay_session();
    const auto legacy_preferences = load_trainer_preferences();
    const auto progress = load_training_progress();
    QSignalSpy triggered(start, &QAction::triggered);
    // Native Start prepares the real finite shoe, unlike the deterministic
    // prepared quiz fixture in the companion test above.
    QTest::mouseClick(start_button, Qt::LeftButton);
    view->stop_gameplay_clock();
    QCOMPARE(triggered.count(), 1);
    QCOMPARE(owner.phase(), gameplay::session_phase::running);
    QCOMPARE(start->text(), str_label("Pause"));
    QVERIFY(actions[2]->isEnabled());
    std::vector<gameplay::quiz_answer> answers;
    for (int step = 0;
         step < 100 && owner.phase() != gameplay::session_phase::finished;
         ++step) {
        if (owner.current_quiz_batch()) {
            auto* skip = view->slot_widgets[0]->findChild<QPushButton*>(
                QStringLiteral("quiz_skip_button")
            );
            QVERIFY(skip && skip->isVisible() && skip->isEnabled());
            QTest::mouseClick(skip, Qt::LeftButton);
        } else {
            QVERIFY(view->advance_gameplay_runtime(
                static_cast<std::uint64_t>(owner.pick_interval_ms()), answers
            ));
            view->publish_gameplay_runtime(answers);
        }
    }
    QCOMPARE(owner.phase(), gameplay::session_phase::finished);
    QCOMPARE(owner.deck({ 0 })->status, gameplay::deck_status::completed);
    QCOMPARE(
        owner.deck({ 0 })->dealt_physical_cards, physical_jokers ? 54U : 52U
    );
    QVERIFY(owner.result() && !view->gameplay_timer.isActive());
    QVERIFY(!start->isEnabled() && !actions[2]->isEnabled());
    QVERIFY(!start_button->isEnabled());
    const auto result = *owner.result();
    const auto record = *owner.deck({ 0 });
    const auto timing = owner.timing();
    auto* settings_button
        = qobject_cast<QToolButton*>(toolbar->widgetForAction(actions[3]));
    QVERIFY(settings_button && settings_button->isEnabled());
    QTest::mouseClick(settings_button, Qt::LeftButton);
    auto* editor = appearance_editor(window);
    QVERIFY(editor);
    auto* combo = editor->desktop_model();
    editor->window()->hide();
    window.addToolBar(Qt::BottomToolBarArea, toolbar);
    toolbar->setMaximumWidth(170);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    auto* extension = toolbar->findChild<QToolButton*>(
        QStringLiteral("qt_toolbar_ext_button")
    );
    QVERIFY(extension && extension->isVisible());
    if (extension->menu()) {
        for (auto* action : actions)
            QVERIFY(
                toolbar->widgetForAction(action)->isVisible()
                || extension->menu()->actions().contains(action)
            );
    } else {
        QTest::mouseClick(extension, Qt::LeftButton);
        for (auto* action : actions)
            QTRY_VERIFY_WITH_TIMEOUT(
                toolbar->widgetForAction(action)->isVisible(), 1000
            );
        QTest::mouseClick(extension, Qt::LeftButton);
    }
    toolbar->hide();
    const auto state = window.saveState();
    for (const bool compact : { false, true }) {
        combo->set_override("toolbar", compact ? "compact" : "classic");
        QVERIFY(editor->apply_theme_settings());
        QCOMPARE(toolbar->actions(), actions);
        QVERIFY(toolbar->isHidden());
        QCOMPARE(window.toolBarArea(toolbar), Qt::BottomToolBarArea);
        QCOMPARE(window.saveState(), state);
        QCOMPARE(*owner.result(), result);
        QCOMPARE(*owner.deck({ 0 }), record);
        QCOMPARE(owner.timing(), timing);
        QVERIFY(!start_button->isEnabled());
    }
    QCOMPARE(load_trainer_preferences(), legacy_preferences);
    QCOMPARE(load_training_progress(), progress);
    // Return via the very same native New game action, without leaving a
    // borrowed global form or a started owned clock behind.
    toolbar->show();
    toolbar->setMaximumWidth(QWIDGETSIZE_MAX);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    auto* new_button
        = qobject_cast<QToolButton*>(toolbar->widgetForAction(actions[0]));
    QVERIFY(new_button && new_button->isVisible() && new_button->isEnabled());
    QTest::keyClick(new_button, Qt::Key_Space);
    QVERIFY(!view->active_gameplay_session());
    QVERIFY(!view->gameplay_timer.isActive());
    QVERIFY(
        !window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"))
    );
    QCOMPARE(toolbar->actions(), actions);
    QCOMPARE(start->text(), str_label("Start"));
    QVERIFY(!actions[2]->isEnabled());
    QVERIFY(window.findChild<QAction*>(QStringLiteral("game_saved_drills"))
                ->isEnabled());
}

void table_tests::felt_shell_round_trips_one_bar() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop shell chrome");
#endif
    launcher_preferences_guard preferences;
    shell_state_guard shell;

    desktop_ui_preferences ui;
    ui.preset = desktop_ui_preset::felt;
    QVERIFY(save_desktop_ui_preferences(ui));
    main_window window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    // D-NEW-1: segmented choices are views of the unchanged combo models.
    auto* setup = window.findChild<QDialog*>(QStringLiteral("setup_dialog"));
    QVERIFY(setup);
    const auto tracks
        = setup->findChildren<QFrame*>(QStringLiteral("kit_segmented"));
    QCOMPARE(tracks.size(), qsizetype { 2 });
    auto* quiz_mode
        = tracks.constFirst()->parentWidget()->findChild<QComboBox*>();
    QVERIFY(quiz_mode);
    QVERIFY(tracks.constFirst()->isVisible());
    QVERIFY(!quiz_mode->isVisible());
    const auto options = tracks.constFirst()->findChildren<QPushButton*>();
    QCOMPARE(options.size(), qsizetype { quiz_mode->count() });
    const int initial_mode = quiz_mode->currentIndex();
    options.at(1 - initial_mode)->click();
    QCOMPARE(quiz_mode->currentIndex(), 1 - initial_mode);
    quiz_mode->setCurrentIndex(initial_mode);
    QVERIFY(options.at(initial_mode)->isChecked());
    setup->reject();

    // D-TOP-1: brand first; in-window menus join the same bar.
    auto* toolbar
        = window.findChild<BaseToolBar*>(QStringLiteral("main_toolbar"));
    QVERIFY(toolbar);
    const auto felt_actions = toolbar->actions();
    auto* brand = toolbar->widgetForAction(felt_actions.constFirst());
    QVERIFY(brand && brand->objectName() == QStringLiteral("kit_brand"));
    const bool in_window_menus = !window.menuBar()->isNativeMenuBar();
#ifndef KC_KDE
    if (in_window_menus) {
        QVERIFY(window.menuBar()->isHidden());
        qsizetype menus = 0;
        for (auto* action : felt_actions)
            menus += action->menu() != nullptr ? 1 : 0;
        QCOMPARE(menus, qsizetype { 2 });
    }
#endif
    QCOMPARE(window.centralWidget()->layout()->contentsMargins(), QMargins());

    // D-HUD-1 paints instruments over the unchanged status text.
    auto* status = window.findChild<QLabel*>(QStringLiteral("session_status"));
    QVERIFY(status);
    QVERIFY(status->property("instrument_paint").toBool());
    QVERIFY(status->text().startsWith(str_label("Status: ")));
    auto* clock = window.findChild<QLabel*>(QStringLiteral("session_clock"));
    QVERIFY(clock);
    QCOMPARE(clock->font().pixelSize(), 17);
    QCOMPARE(
        clock->palette().color(QPalette::WindowText),
        shared_ui::qt::material_for(*clock, shared_ui::qt::material::felt)
            .accent
    );

    // Back to Classic: the native shell is exactly what it was.
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_settings_triggered", Qt::DirectConnection
        )
    );
    auto* editor = appearance_editor(window);
    QVERIFY(editor);
    editor->window()->hide();
    auto* preset = editor->desktop_model();
    QVERIFY(preset);
    preset->choose_preset("classic");
    QVERIFY(editor->apply_theme_settings());
    QCoreApplication::processEvents();
    QCOMPARE(toolbar->actions().size(), qsizetype { 4 });
    for (auto* action : toolbar->actions())
        QVERIFY(action->menu() == nullptr);
    if (in_window_menus)
        QVERIFY(!window.menuBar()->isHidden());
    QVERIFY(window.centralWidget()->layout()->contentsMargins() != QMargins());
    QVERIFY(tracks.constFirst()->isHidden());
    QVERIFY(!quiz_mode->isHidden());
    QVERIFY(!status->property("instrument_paint").toBool());
    QVERIFY(
        shared_ui::qt::presentation_for(window).value
        != shared_ui::qt::material::felt
    );
}

void table_tests::native_toolbar_follows_felt_commands_and_late_actions() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop native toolbar presentation");
#endif
    for (const bool compact : { false, true }) {
        launcher_preferences_guard preferences;
        shell_state_guard shell;
        desktop_ui_preferences ui;
        ui.preset = desktop_ui_preset::felt;
        ui.toolbar_override = compact ? desktop_toolbar_style::compact
                                      : desktop_toolbar_style::classic;
        QVERIFY(save_desktop_ui_preferences(ui));
        main_window window;
        window.resize(1400, 900);
        window.show();
        QCoreApplication::processEvents();
        for (auto* dialog : window.findChildren<QDialog*>())
            if (dialog->isVisible())
                dialog->reject();
        auto* view = window.findChild<table*>();
        auto* toolbar
            = window.findChild<BaseToolBar*>(QStringLiteral("main_toolbar"));
        auto* start = launcher_start_action(window);
        QVERIFY(view && toolbar && start);
        auto* button
            = qobject_cast<QToolButton*>(toolbar->widgetForAction(start));
        QVERIFY(button);
        const auto shortcuts = start->shortcuts();
        const auto actions = toolbar->actions();
        QVERIFY(view->install_gameplay_session(owned_clock_fixture(
            1, true, gameplay::dealing_mode::simultaneous,
            gameplay::quiz_scope::single, { 1 }
        )));
        QVERIFY(view->start_gameplay_runtime(61));
        view->stop_gameplay_clock();
        const auto ink = shared_ui::qt::material_for(*toolbar).panel;
        QCOMPARE(start->text(), str_label("Pause"));
        QCOMPARE(
            button->icon().pixmap(16, 16).toImage(),
            shared_ui::qt::transport_icon(true, ink).pixmap(16, 16).toImage()
        );
        QVERIFY(view->pause_gameplay_runtime());
        QCOMPARE(start->text(), str_label("Resume"));
        QCOMPARE(
            button->icon().pixmap(16, 16).toImage(),
            shared_ui::qt::transport_icon(false, ink).pixmap(16, 16).toImage()
        );
        QVERIFY(view->resume_gameplay_runtime());
        view->stop_gameplay_clock();
        QCOMPARE(start->text(), str_label("Pause"));
        QCOMPARE(
            button->icon().pixmap(16, 16).toImage(),
            shared_ui::qt::transport_icon(true, ink).pixmap(16, 16).toImage()
        );
        QCOMPARE(toolbar->widgetForAction(start), button);
        QCOMPARE(start->shortcuts(), shortcuts);
        QCOMPARE(toolbar->actions(), actions);
        QCOMPARE(
            button->objectName(), QStringLiteral("game_start_pause_button")
        );

        QAction extra(
            window.style()->standardIcon(QStyle::SP_DialogOpenButton),
            str_label("Extra"), &window
        );
        extra.setToolTip(str_label("Additional host action"));
        toolbar->addAction(&extra);
        QCoreApplication::processEvents();
        auto* late
            = qobject_cast<QToolButton*>(toolbar->widgetForAction(&extra));
        QVERIFY(late && late->focusPolicy() == Qt::StrongFocus);
        QCOMPARE(
            late->toolButtonStyle(),
            compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextOnly
        );
        extra.setIcon({});
        QCOMPARE(late->toolButtonStyle(), Qt::ToolButtonTextOnly);
        extra.setText(str_label("Longer host action"));
        QCOMPARE(late->text(), extra.iconText());
        extra.setEnabled(false);
        QVERIFY(!late->isEnabled());
        toolbar->removeAction(&extra);
        QCoreApplication::processEvents();
        QCOMPARE(toolbar->actions(), actions);
    }
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
