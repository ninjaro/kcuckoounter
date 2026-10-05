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
#include <QToolButton>
#include <QToolTip>
#include <QtTest/QtTest>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

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

    struct restore_preferences {
        desktop_ui_preferences ui = load_desktop_ui_preferences();
        trainer_preferences trainer = load_trainer_preferences();
        desktop_shell_state shell = load_desktop_shell_state();

        ~restore_preferences() {
            static_cast<void>(save_desktop_ui_preferences(ui));
            save_trainer_preferences(trainer);
            save_desktop_shell_state(shell);
        }
    } guard;

    auto trainer = guard.trainer;
    trainer.slot_count = count;
    trainer.quiz_type = 0;
    trainer.wait_for_answers = false;
    trainer.pickup_interval_ms = 1000;
    save_trainer_preferences(trainer);
    save_desktop_shell_state({});
    desktop_ui_preferences ui;
    ui.toolbar_override = initial_compact ? desktop_toolbar_style::compact
                                          : desktop_toolbar_style::classic;
    QVERIFY(save_desktop_ui_preferences(ui));

    main_window window;
    window.resize(1100, 720);
    window.show();
    QCoreApplication::processEvents(); // Deliver the initial setup dialog.
    auto* setup = window.findChild<QDialog*>();
    QVERIFY(setup);
    setup->reject();
    auto* toolbar
        = window.findChild<BaseToolBar*>(QStringLiteral("main_toolbar"));
    auto* view = window.findChild<table*>();
    auto* slider = window.findChild<QSlider*>();
    QVERIFY(toolbar && view && slider);
    QVERIFY(window.menuBar()->isVisible());
    QVERIFY(!window.windowFlags().testFlag(Qt::FramelessWindowHint));
    const auto actions = toolbar->actions();
    QVERIFY(actions.size() >= 4);
    auto* start = actions[1];
    auto* finish = actions[2];
    auto* settings = actions[3];
    auto* start_button
        = qobject_cast<QToolButton*>(toolbar->widgetForAction(start));
    QVERIFY(start_button);
    QCOMPARE(start->text(), str_label("Start"));
    QCOMPARE(start_button->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    QVERIFY(!finish->isEnabled());
    const auto shortcuts = start->shortcuts();
    QVERIFY(!shortcuts.isEmpty());
    for (auto* action : actions) {
        auto* button
            = qobject_cast<QToolButton*>(toolbar->widgetForAction(action));
        QVERIFY(button && button->focusPolicy() != Qt::NoFocus);
        QCOMPARE(button->defaultAction(), action);
        QCOMPARE(
            button->toolButtonStyle(),
            initial_compact && action != start && !action->icon().isNull()
                ? Qt::ToolButtonIconOnly
                : Qt::ToolButtonTextBesideIcon
        );
        bool in_menu = false;
        for (auto* menu : window.findChildren<QMenu*>())
            in_menu |= menu->actions().contains(action);
        QVERIFY(in_menu);
    }

    QSignalSpy started(start, &QAction::triggered);
    QTest::mouseClick(start_button, Qt::LeftButton);
    QCOMPARE(started.count(), 1);
    QCOMPARE(start->text(), str_label("Pause"));
    QVERIFY(finish->isEnabled());
    // The existing Settings action pauses once; applying a style never resumes.
    settings->trigger();
    QCOMPARE(start->text(), str_label("Resume"));
    settings_template_widget* editor = nullptr;
    for (auto* candidate : window.findChildren<settings_template_widget*>()) {
        if (candidate->findChild<QComboBox*>(
                QStringLiteral("desktop_ui_toolbar")
            ))
            editor = candidate;
    }
    QVERIFY(editor);
    auto* combo
        = editor->findChild<QComboBox*>(QStringLiteral("desktop_ui_toolbar"));
    const auto state = view->capture_session_state();
    QVERIFY(state.quiz_running && state.quiz_paused);
    const auto interval = slider->value();
    const auto editable = slider->isEnabled();
    const auto window_state
        = window.saveState(desktop_shell_state::qt_main_window_state_version);
    const auto slot_widgets = view->findChildren<table_slot*>();
    QCOMPARE(slot_widgets.size(), count);
    QList<QRect> bounds;
    QList<int> raster;
    for (auto* slot : slot_widgets) {
        bounds.append(slot->geometry());
        raster.append(slot->card_face_need_short_px());
    }
    for (const bool compact : { true, false, true }) {
        combo->setCurrentIndex(compact ? 2 : 1);
        QVERIFY(editor->apply_theme_settings());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCOMPARE(toolbar->actions(), actions);
        QCOMPARE(start->shortcuts(), shortcuts);
        QCOMPARE(start_button->defaultAction(), start);
        QCOMPARE(start_button->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
        QCOMPARE(view->capture_session_state(), state);
        QCOMPARE(slider->value(), interval);
        QCOMPARE(slider->isEnabled(), editable);
        QCOMPARE(
            window.saveState(desktop_shell_state::qt_main_window_state_version),
            window_state
        );
        for (qsizetype i = 0; i < slot_widgets.size(); ++i) {
            QCOMPARE(slot_widgets[i]->geometry(), bounds[i]);
            QCOMPARE(slot_widgets[i]->card_face_need_short_px(), raster[i]);
        }
    }
    editor->window()->hide();
    // Descriptions supplement native accessible action names without widening.
    auto* secondary
        = qobject_cast<QToolButton*>(toolbar->widgetForAction(actions[0]));
    QVERIFY(secondary);
    const auto secondary_size = secondary->size();
    QFocusEvent focused(QEvent::FocusIn, Qt::TabFocusReason);
    QApplication::sendEvent(secondary, &focused);
    QCOMPARE(QToolTip::text(), secondary->toolTip());
    QCOMPARE(secondary->size(), secondary_size);
    QFocusEvent unfocused(QEvent::FocusOut, Qt::TabFocusReason);
    QApplication::sendEvent(secondary, &unfocused);
    const auto icon = actions[0]->icon();
    actions[0]->setIcon(QIcon());
    QCOMPARE(secondary->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    actions[0]->setIcon(icon);
    QCOMPARE(secondary->toolButtonStyle(), Qt::ToolButtonIconOnly);

    // Keyboard dispatch goes through the very same action after restyling.
    window.activateWindow();
    start_button->setFocus();
    QTest::qWait(1);
    QTest::keyClick(start_button, Qt::Key_Space);
    QCOMPARE(started.count(), 2);
    QCOMPARE(start->text(), str_label("Pause"));
    QTest::keyClick(start_button, Qt::Key_Space);
    QCOMPARE(started.count(), 3);
    QCOMPARE(start->text(), str_label("Resume"));
    QTest::keyClick(&window, Qt::Key_P);
    QCOMPARE(started.count(), 4);
    QCOMPARE(start->text(), str_label("Pause"));
    QTest::keyClick(&window, Qt::Key_P);
    QCOMPARE(started.count(), 5);
    QCOMPARE(start->text(), str_label("Resume"));

    // The native toolbar, not a new overlay host, still owns overflow and
    // docks.
    window.addToolBar(Qt::BottomToolBarArea, toolbar);
    toolbar->setMaximumWidth(170);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    auto* extension = toolbar->findChild<QToolButton*>(
        QStringLiteral("qt_toolbar_ext_button")
    );
    QVERIFY(extension && extension->isVisible());
    if (extension->menu()) {
        for (auto* action : actions) {
            auto* button = toolbar->widgetForAction(action);
            QVERIFY(
                button->isVisible()
                || extension->menu()->actions().contains(action)
            );
        }
    } else {
        // Docked Qt toolbars may expand into rows instead of opening a menu.
        QTest::mouseClick(extension, Qt::LeftButton);
        for (int i = 0; i < 4; ++i)
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        for (auto* action : actions)
            QTRY_VERIFY_WITH_TIMEOUT(
                toolbar->widgetForAction(action)->isVisible(), 1000
            );
        QTest::mouseClick(extension, Qt::LeftButton);
    }
    combo->setCurrentIndex(1);
    QVERIFY(editor->apply_theme_settings());
    QCOMPARE(window.toolBarArea(toolbar), Qt::BottomToolBarArea);
    toolbar->hide();
    combo->setCurrentIndex(2);
    QVERIFY(editor->apply_theme_settings());
    QVERIFY(
        toolbar->isHidden()
    ); // Appearance must not override native visibility.
    QCOMPARE(toolbar->actions(), actions);
    if (count == 1) {
        // Finish retains the confirmation/result workflow, with no answered
        // questions here (so no highscore/name dialog or new progress record).
        int dialogs_seen = 0;
        QTimer::singleShot(0, &window, [&] {
            auto* question
                = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (question && question->button(QMessageBox::Yes)) {
                ++dialogs_seen;
                question->button(QMessageBox::Yes)->click();
            }
            QTimer::singleShot(0, &window, [&] {
                auto* result = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget()
                );
                if (result) {
                    ++dialogs_seen;
                    result->accept();
                }
            });
        });
        finish->trigger();
        QCOMPARE(dialogs_seen, 2);
        QVERIFY(!finish->isEnabled());
        QCOMPARE(start->text(), str_label("Start"));
        QVERIFY(!view->capture_session_state().quiz_running);
        QVERIFY(setup->isVisible());
        setup->reject();
        save_desktop_shell_state(
            { window.saveGeometry(),
              window.saveState(
                  desktop_shell_state::qt_main_window_state_version
              ) }
        );
        main_window restored;
        restored.show();
        auto* restored_toolbar
            = restored.findChild<BaseToolBar*>(QStringLiteral("main_toolbar"));
        QVERIFY(restored_toolbar && restored_toolbar->isHidden());
        QCOMPARE(restored.toolBarArea(restored_toolbar), Qt::BottomToolBarArea);
        restored_toolbar->show();
        auto* button = qobject_cast<QToolButton*>(
            restored_toolbar->widgetForAction(restored_toolbar->actions()[0])
        );
        QVERIFY(button);
        QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
    }
}

void table_tests::desktop_status_reports_existing_values() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop status surface");
#endif
    struct restore_preferences {
        trainer_preferences trainer = load_trainer_preferences();
        desktop_shell_state shell = load_desktop_shell_state();

        ~restore_preferences() {
            save_trainer_preferences(trainer);
            save_desktop_shell_state(shell);
        }
    } guard;

    auto trainer = guard.trainer;
    trainer.slot_count = 1;
    trainer.wait_for_answers = false;
    trainer.quiz_type = 0;
    trainer.pickup_interval_ms = 300;
    save_trainer_preferences(trainer);
    main_window window;
    window.show();
    QCoreApplication::processEvents();
    auto* setup = window.findChild<QDialog*>();
    QVERIFY(setup);
    setup->reject();
    auto* status = window.findChild<QLabel*>(QStringLiteral("session_status"));
    auto* readout
        = window.findChild<QLabel*>(QStringLiteral("pickup_interval_readout"));
    auto* slider
        = window.findChild<QSlider*>(QStringLiteral("pickup_interval_slider"));
    auto* progress
        = window.findChild<QProgressBar*>(QStringLiteral("raster_progress"));
    auto* clock
        = window.findChild<BaseClock*>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(status && readout && slider && progress && clock);
    QVERIFY(status->text().contains(str_label("Ready")));
    QVERIFY(!status->text().contains(QStringLiteral("I18N_ARGUMENT_MISSING")));
    QVERIFY(readout->text().contains(QStringLiteral("300")));
    QVERIFY(!readout->text().contains(QStringLiteral("I18N_ARGUMENT_MISSING")));
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_start_pause_triggered", Qt::DirectConnection
        )
    );
    QVERIFY(status->text().contains(str_label("Running")));
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_start_pause_triggered", Qt::DirectConnection
        )
    );
    QVERIFY(status->text().contains(str_label("Paused")));
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_table_score_adjusted", Qt::DirectConnection,
            Q_ARG(int, 2), Q_ARG(int, 3)
        )
    );
    clock->set_elapsed_time_ms(65000);
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_clock_ticked", Qt::DirectConnection,
            Q_ARG(qint64, 65000), Q_ARG(qint64, 0)
        )
    );
    QVERIFY(status->text().contains(QStringLiteral("2/3")));
    QVERIFY(status->text().contains(QStringLiteral("01:05")));
    QVERIFY(!status->text().contains(QStringLiteral("I18N_ARGUMENT_MISSING")));
    QVERIFY(!status->text().contains(QStringLiteral("%1")));
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_table_rasterization_busy_changed",
            Qt::DirectConnection, Q_ARG(bool, true)
        )
    );
    QVERIFY(status->text().contains(str_label("Processing")));
    QVERIFY(progress->isVisible());
    slider->setValue(735);
    QVERIFY(readout->text().contains(QStringLiteral("735")));
    QVERIFY(!readout->text().contains(QStringLiteral("I18N_ARGUMENT_MISSING")));
    QVERIFY(!readout->text().contains(QStringLiteral("%1")));
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_table_rasterization_busy_changed",
            Qt::DirectConnection, Q_ARG(bool, false)
        )
    );
    QVERIFY(!status->text().contains(str_label("Processing")));
    QVERIFY(progress->isHidden());
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

    struct restore_preferences {
        desktop_ui_preferences ui = load_desktop_ui_preferences();
        trainer_preferences trainer = load_trainer_preferences();
        desktop_shell_state shell = load_desktop_shell_state();

        ~restore_preferences() {
            static_cast<void>(save_desktop_ui_preferences(ui));
            save_trainer_preferences(trainer);
            save_desktop_shell_state(shell);
        }
    } guard;

    auto trainer = guard.trainer;
    trainer.slot_count = count;
    trainer.quiz_type = 0;
    trainer.wait_for_answers = false;
    trainer.pickup_interval_ms = 300;
    save_trainer_preferences(trainer);
    save_desktop_shell_state({});
    desktop_ui_preferences ui;
    ui.hud_override = instruments ? desktop_hud_style::instruments
                                  : desktop_hud_style::classic;
    QVERIFY(save_desktop_ui_preferences(ui));
    main_window window;
    window.resize(1100, 700);
    window.show();
    QCoreApplication::processEvents();
    auto* setup = window.findChild<QDialog*>();
    QVERIFY(setup);
    setup->reject();
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_start_pause_triggered", Qt::DirectConnection
        )
    );
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_settings_triggered", Qt::DirectConnection
        )
    );
    auto* surface
        = window.findChild<QFrame*>(QStringLiteral("desktop_status_surface"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("session_status"));
    auto* readout
        = window.findChild<QLabel*>(QStringLiteral("pickup_interval_readout"));
    auto* slider
        = window.findChild<QSlider*>(QStringLiteral("pickup_interval_slider"));
    auto* progress
        = window.findChild<QProgressBar*>(QStringLiteral("raster_progress"));
    auto* view = window.findChild<table*>();
    QVERIFY(surface && status && readout && slider && progress && view);
    QList<QWidget*> fields { status, readout, slider, progress };
#ifdef KC_KDE
    auto* clock = window.findChild<QLabel*>(QStringLiteral("session_clock"));
    QVERIFY(clock);
    fields.append(clock);
#endif
    QCOMPARE(
        surface->frameShape(),
        instruments ? QFrame::StyledPanel : QFrame::NoFrame
    );
    QCOMPARE(
        slider->tickPosition(),
        instruments ? QSlider::TicksBelow : QSlider::NoTicks
    );
    settings_template_widget* editor = nullptr;
    for (auto* candidate : window.findChildren<settings_template_widget*>())
        if (candidate->findChild<QComboBox*>(QStringLiteral("desktop_ui_hud")))
            editor = candidate;
    QVERIFY(editor);
    editor->window()->hide();
    const auto state = view->capture_session_state();
    QVERIFY(state.quiz_paused && state.quiz_running);
    auto* choice
        = editor->findChild<QComboBox*>(QStringLiteral("desktop_ui_hud"));
    auto* speed_choice = editor->findChild<QComboBox*>(
        QStringLiteral("desktop_ui_speed_readout")
    );
    const auto settle = [] {
        for (int i = 0; i < 12; ++i)
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    };
    for (const bool show_speed : { true, false }) {
        // Presentation must not grant/revoke permission to edit speed.
        slider->setEnabled(show_speed);
        speed_choice->setCurrentIndex(show_speed ? 1 : 2);
        for (const int style_index : { 1, 2 }) {
            choice->setCurrentIndex(style_index);
            QVERIFY(editor->apply_theme_settings());
            QCOMPARE(slider->isEnabled(), show_speed);
            QCOMPARE(readout->isHidden(), !show_speed);
            QCOMPARE(slider->value(), 300);
            QCOMPARE(view->capture_session_state(), state);
            QVERIFY(
                QMetaObject::invokeMethod(
                    &window, "on_table_rasterization_busy_changed",
                    Qt::DirectConnection, Q_ARG(bool, true)
                )
            );
            for (const QSize viewport :
                 { QSize(1100, 700), QSize(640, 480), QSize(480, 320),
                   QSize(320, 480), QSize(1100, 700) }) {
                window.resize(viewport);
                settle();
                QCOMPARE(window.size(), viewport);
                for (QWidget* field : fields) {
                    if (field->isHidden())
                        continue;
                    QVERIFY(field->isVisible());
                    QVERIFY2(
                        surface->rect().contains(QRect(
                            field->mapTo(surface, QPoint()), field->size()
                        )),
                        qPrintable(field->objectName())
                    );
                    if (auto* label = qobject_cast<QLabel*>(field))
                        QVERIFY2(
                            label->height()
                                >= label->heightForWidth(label->width()),
                            qPrintable(label->objectName())
                        );
                }
                QVERIFY(slider->width() >= slider->minimumWidth());
                QList<QRect> bounds;
                for (auto* slot : view->findChildren<table_slot*>()) {
                    QVERIFY(slot->isVisible());
                    QVERIFY(view->rect().contains(slot->geometry()));
                    QCOMPARE(
                        slot->findChild<card_widget*>()->geometry(),
                        slot->rect()
                    );
                    for (const auto& previous : bounds)
                        QVERIFY(!previous.intersects(slot->geometry()));
                    bounds.append(slot->geometry());
                }
                QCOMPARE(bounds.size(), count);
                QCOMPARE(view->capture_session_state(), state);
                const auto stable = surface->geometry();
                // A parent can measure prospective widths before resizing us.
                // Those queries must not rearrange the live HUD.
                const auto status_bounds = status->geometry();
                QVERIFY(
                    surface->heightForWidth(300)
                    >= surface->heightForWidth(1200)
                );
                settle();
                QCOMPARE(surface->geometry(), stable);
                QCOMPARE(status->geometry(), status_bounds);
            }
        }
    }
    slider->setEnabled(true);
    window.activateWindow();
    slider->setFocus();
    QTest::keyClick(slider, Qt::Key_Right);
    QCOMPARE(slider->value(), 301);
    slider->setValue(300);
    // Longer localized content must wrap, not clip or widen the application.
    status->setText(status->text() + QStringLiteral(" / ") + status->text());
    readout->setText(
        QStringLiteral("Long localized pickup interval description: 300 ms")
    );
    readout->show();
    window.resize(480, 320);
    settle();
    QCOMPARE(window.size(), QSize(480, 320));
    QVERIFY(status->height() >= status->heightForWidth(status->width()));
    QVERIFY(readout->height() >= readout->heightForWidth(readout->width()));
#ifdef KC_KDE
    auto* visibility
        = window.actionCollection()->action(QStringLiteral("view_statusbar"));
    QVERIFY(visibility);
    QVERIFY(visibility->isChecked());
    visibility->trigger();
    QVERIFY(window.statusBar()->isHidden());
    choice->setCurrentIndex(1);
    QVERIFY(editor->apply_theme_settings());
    QVERIFY(window.statusBar()->isHidden());
    visibility->trigger();
    QVERIFY(window.statusBar()->isVisible());
    settle();
    QVERIFY(surface->isVisible());
#endif
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
