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
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QtTest/QtTest>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::drill_configuration_preflight_and_fresh_shoes() {
    const auto& catalog = strategy_repository();
    QVERIFY(catalog.strategies.size() >= 2);
    table view;
    view.set_slot_count(2);
    view.set_frame_style(slot_frame_style::thin);
    auto* slot = view.findChild<table_slot*>();
    auto* fields = slot->findChild<slot_settings*>();
    QVERIFY(fields);
    fields->show_card_indexing()->setChecked(true);
    fields->show_strategy_name()->setChecked(true);
    view.start_quiz(0, true);
    const auto before = view.capture_session_state();
    const auto prefs = load_trainer_preferences();
    training_drill drill;
    drill.name = QStringLiteral("Two different shoes");
    drill.quiz_type = 1;
    drill.wait_for_answers = true;
    drill.allow_skipping = false;
    drill.dealing_mode = 2;
    drill.pickup_interval_ms = 570;
    drill.slot_settings = { { 1, false, catalog.strategies[0].slug, false },
                            { 8, true, catalog.strategies[1].slug, true } };
    auto invalid = drill;
    invalid.slot_settings.last().strategy_slug
        = QStringLiteral("missing-strategy");
    QVERIFY(!view.configure_drill(invalid));
    QCOMPARE(view.capture_session_state(), before);
    invalid = drill;
    invalid.slot_settings.last().deck_count = 17;
    QVERIFY(!view.configure_drill(invalid));
    QCOMPARE(view.capture_session_state(), before);
    QVERIFY(view.configure_drill(drill));
    QCOMPARE(view.capture_drill_settings(), drill.slot_settings);
    QCOMPARE(
        load_trainer_preferences(), prefs
    ); // no default-strategy side effect
    const auto ready = view.capture_session_state();
    QVERIFY(!ready.quiz_running);
    QVERIFY(ready.slot_states.first().show_card_indexing);
    QVERIFY(ready.slot_states.first().show_strategy_name);
    view.start_quiz(drill.quiz_type, drill.wait_for_answers);
    const auto started = view.capture_session_state();
    QVERIFY(started.quiz_running && started.quiz_paused);
    QVERIFY(!started.allow_skipping);
    QCOMPARE(started.dealing_mode, 2);
    for (qsizetype i = 0; i < started.slot_states.size(); ++i) {
        const auto& state = started.slot_states[i];
        QCOMPARE(state.card.decks_count, drill.slot_settings[i].deck_count);
        QCOMPARE(
            state.card.infinity_enabled, drill.slot_settings[i].infinity_enabled
        );
        QCOMPARE(state.training_mode, drill.slot_settings[i].training_mode);
        QCOMPARE(state.strategy_slug, drill.slot_settings[i].strategy_slug);
        QVERIFY(!state.quiz_prompt_active && !state.quiz_feedback_active);
        QCOMPARE(state.quiz_input_value, 0);
    }
}

void table_tests::saved_drill_picker_preserves_and_launches_sessions() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop drill picker");
#endif
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );

    struct restore_settings {
        QSettings& settings;
        QVariant drills
            = settings.value(QStringLiteral("training_drills/document"));
        trainer_preferences trainer = load_trainer_preferences();
        desktop_shell_state shell = load_desktop_shell_state();

        ~restore_settings() {
            if (drills.isValid())
                settings.setValue(
                    QStringLiteral("training_drills/document"), drills
                );
            else
                settings.remove(QStringLiteral("training_drills/document"));
            save_trainer_preferences(trainer);
            save_desktop_shell_state(shell);
        }
    } guard { settings };

    settings.remove(QStringLiteral("training_drills/document"));
    auto trainer = guard.trainer;
    trainer.slot_count = 2;
    trainer.quiz_type = 0;
    trainer.wait_for_answers = false;
    trainer.pickup_interval_ms = 735;
    save_trainer_preferences(trainer);
    save_desktop_shell_state({});
    const auto appearance = load_desktop_ui_preferences();
    main_window window;
    window.resize(640, 480);
    window.show();
    QCoreApplication::processEvents();
    auto* setup = window.findChild<QDialog*>();
    QVERIFY(setup);
    setup->reject();
    auto* view = window.findChild<table*>();
    QVERIFY(view);
    auto* route
        = window.findChild<QAction*>(QStringLiteral("game_saved_drills"));
    QVERIFY(route);
    QVERIFY(!window.findChild<BaseToolBar*>(QStringLiteral("main_toolbar"))
                 ->actions()
                 .contains(route));
    bool in_menu = false;
    for (auto* menu : window.findChildren<QMenu*>())
        in_menu = in_menu || menu->actions().contains(route);
    QVERIFY(in_menu);
    QVERIFY(
        window.findChild<QPushButton*>(QStringLiteral("setup_saved_drills"))
    );
    training_drill_service service(settings);
    const auto visit = [&](auto inspect) {
        bool visited = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(
                QStringLiteral("saved_drills_dialog")
            );
            QVERIFY(dialog);
            struct reject_on_exit {
                QDialog* dialog;
                ~reject_on_exit() { dialog->reject(); }
            } closer { dialog };
            visited = true;
            inspect(dialog);
        });
        route->trigger();
        QVERIFY(visited);
    };
    const auto before = view->capture_session_state();
    visit([&](QDialog* dialog) {
        auto* list = dialog->findChild<QListWidget*>(
            QStringLiteral("saved_drills_list")
        );
        auto* name
            = dialog->findChild<QLineEdit*>(QStringLiteral("saved_drill_name"));
        auto* save = dialog->findChild<QPushButton*>(
            QStringLiteral("save_current_drill")
        );
        auto* launch
            = dialog->findChild<QPushButton*>(QStringLiteral("launch_drill"));
        QVERIFY(list && name && save && launch);
        QCOMPARE(list->count(), 0);
        QVERIFY(!launch->isEnabled());
        name->setText(QStringLiteral("My mixed drill"));
        save->setFocus();
        QTest::keyClick(save, Qt::Key_Space);
        QCOMPARE(list->count(), 1);
        QVERIFY(launch->isEnabled());
        QCOMPARE(view->capture_session_state(), before);
        save->click(); // duplicate must not overwrite
        QCOMPARE(list->count(), 1);
        QVERIFY(!dialog
                     ->findChild<QLabel*>(QStringLiteral("saved_drill_message"))
                     ->text()
                     .isEmpty());
        name->setText(QStringLiteral("Renamed drill"));
        dialog->findChild<QPushButton*>(QStringLiteral("rename_drill"))
            ->click();
        QCOMPARE(list->currentItem()->text(), QStringLiteral("Renamed drill"));
        dialog->resize(360, 420);
        QCoreApplication::processEvents();
        QCOMPARE(dialog->width(), 360);
        QVERIFY(dialog->rect().contains(
            QRect(launch->mapTo(dialog, QPoint()), launch->size())
        ));
    });
    QCOMPARE(view->capture_session_state(), before);
    QVERIFY(service.load());
    QCOMPARE(
        service.load()->first().slot_settings, view->capture_drill_settings()
    );
    QCOMPARE(service.load()->first().pickup_interval_ms, 735);
    auto drill = service.load()->first();
    visit([&](QDialog* dialog) {
        auto external = drill;
        external.name = QStringLiteral("Changed outside the picker");
        QVERIFY(service.save({ external }));
        dialog->findChild<QLineEdit*>(QStringLiteral("saved_drill_name"))
            ->setText(QStringLiteral("Do not overwrite"));
        dialog->findChild<QPushButton*>(QStringLiteral("save_current_drill"))
            ->click();
        QCOMPARE(*service.load(), QVector<training_drill> { external });
        QCOMPARE(
            dialog->findChild<QListWidget*>(QStringLiteral("saved_drills_list"))
                ->count(),
            1
        );
        QVERIFY(!dialog
                     ->findChild<QLabel*>(QStringLiteral("saved_drill_message"))
                     ->text()
                     .isEmpty());
    });
    drill.wait_for_answers = true;
    drill.quiz_type = 1;
    drill.allow_skipping = false;
    drill.dealing_mode = 2;
    drill.pickup_interval_ms = 410;
    drill.slot_settings
        = { { 1, false, strategy_repository().strategies.first().slug,
              false } };
    QVERIFY(service.save({ drill }));
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_start_pause_triggered", Qt::DirectConnection
        )
    );
    // Opening/closing preserves the running session, rather than finishing it.
    const auto running = view->capture_session_state();
    QVERIFY(running.quiz_running && !running.quiz_paused);
    visit([&](QDialog* dialog) {
        auto* launch
            = dialog->findChild<QPushButton*>(QStringLiteral("launch_drill"));
        QTimer::singleShot(0, dialog, [] {
            auto* question
                = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(question);
            question->button(QMessageBox::No)->click();
        });
        launch->click();
        QVERIFY(dialog->isVisible());
        QCOMPARE(view->capture_drill_settings().size(), 2);
    });
    QCOMPARE(view->capture_session_state(), running);
    visit([&](QDialog* dialog) {
        QTimer::singleShot(0, dialog, [] {
            auto* question
                = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(question);
            question->button(QMessageBox::Yes)->click();
        });
        auto* launch
            = dialog->findChild<QPushButton*>(QStringLiteral("launch_drill"));
        launch->setFocus();
        QTest::keyClick(launch, Qt::Key_Space);
        QVERIFY(!dialog->isVisible());
    });
    const auto fresh = view->capture_session_state();
    QVERIFY(fresh.quiz_running && fresh.quiz_paused);
    QCOMPARE(view->capture_drill_settings(), drill.slot_settings);
    QCOMPARE(fresh.dealing_mode, 2);
    QVERIFY(!fresh.allow_skipping);
    QCOMPARE(
        window.findChild<QSlider*>(QStringLiteral("pickup_interval_slider"))
            ->value(),
        410
    );
    QCOMPARE(load_desktop_ui_preferences(), appearance);
    auto missing = drill;
    missing.slot_settings.first().strategy_slug
        = QStringLiteral("not-in-catalogue");
    QVERIFY(service.save({ missing }));
    visit([&](QDialog* dialog) {
        QVERIFY(!dialog->findChild<QPushButton*>(QStringLiteral("launch_drill"))
                     ->isEnabled());
        QVERIFY(
            dialog
                ->findChild<QPlainTextEdit*>(
                    QStringLiteral("saved_drill_details")
                )
                ->toPlainText()
                .contains(str_label("Cannot launch:"), Qt::CaseInsensitive)
        );
        QTimer::singleShot(0, dialog, [] {
            auto* question
                = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(question);
            question->button(QMessageBox::Yes)->click();
        });
        dialog->findChild<QPushButton*>(QStringLiteral("delete_drill"))
            ->click();
        QCOMPARE(
            dialog->findChild<QListWidget*>(QStringLiteral("saved_drills_list"))
                ->count(),
            0
        );
    });
    QCOMPARE(view->capture_session_state(), fresh);
    QVERIFY(service.load()->isEmpty());
    settings.setValue(
        QStringLiteral("training_drills/document"), QByteArray("invalid")
    );
    visit([&](QDialog* dialog) {
        QVERIFY(
            !dialog
                 ->findChild<QPushButton*>(QStringLiteral("save_current_drill"))
                 ->isEnabled()
        );
        QVERIFY(!dialog->findChild<QPushButton*>(QStringLiteral("launch_drill"))
                     ->isEnabled());
    });
    QCOMPARE(
        settings.value(QStringLiteral("training_drills/document"))
            .toByteArray(),
        QByteArray("invalid")
    );
    QCOMPARE(view->capture_session_state(), fresh);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
