// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/table_tests.hpp"

#include "settings/gameplay_preferences.hpp"
#include "settings/training_progress.hpp"
#include "shell/main_window.hpp"
#include "table/drill_fixture.hpp"
#include "table/gameplay_setup.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QtTest/QtTest>

#include <limits>

using namespace table_test;

void table_tests::drill_setup_loads_exact_choices_data() {
    QTest::addColumn<int>("slot_count");
    QTest::newRow("four") << 4;
    QTest::newRow("twenty-four") << 24;
    QTest::newRow("generation-budget") << 1;
}

void table_tests::drill_setup_loads_exact_choices() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop target drill integration");
#endif
    QFETCH(int, slot_count);
    launcher_preferences_guard preferences;
    drill_storage_guard catalogues;
    auto target = target_drill(static_cast<std::size_t>(slot_count));
    if (slot_count == 1)
        target.decks[0].configuration.deck_count
            = static_cast<std::size_t>(std::numeric_limits<int>::max());
    gameplay_drill_service service(catalogues.storage, drill_limits);
    QVERIFY(service.save({ target }));
    const auto saved
        = catalogues.storage.value(QStringLiteral("gameplay_drills/document"));
    const auto trainer = load_trainer_preferences();
    const auto progress = load_training_progress();
    main_window window;
    window.resize(1600, 1000);
    window.show();
    QCoreApplication::processEvents();
    window.findChild<QDialog*>()->reject();
    auto* view = window.findChild<table*>();
    QVERIFY(view);
    set_gameplay_motion_style(*view, 0);
    view->setFixedSize(1600, 1000);
    QCoreApplication::processEvents();
    window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))->trigger();
    auto* previous = view->active_gameplay_session();
    QVERIFY(previous);
    visit_drill_picker(window, [&](QDialog& picker) {
        answer_drill_question(picker, QMessageBox::No);
        picker.findChild<QPushButton*>(QStringLiteral("launch_drill"))->click();
    });
    QCOMPARE(view->active_gameplay_session(), previous);
    visit_drill_picker(window, [&](QDialog& picker) {
        answer_drill_question(picker, QMessageBox::Yes);
        picker.findChild<QPushButton*>(QStringLiteral("launch_drill"))->click();
    });
    auto* owner = view->active_gameplay_session();
    QVERIFY(owner && owner->phase() == gameplay::session_phase::setup);
    QCOMPARE(
        capture_gameplay_drill(*owner, target.name, drill_limits),
        std::make_optional(target)
    );
    QVERIFY(!owner->result() && !owner->current_quiz_batch());
    for (std::size_t id = 0; id < owner->size(); ++id)
        QVERIFY(owner->deck({ id })->stream.cards.empty());
    QVERIFY(!catalogues.storage.contains(
        QStringLiteral("gameplay_preferences/document")
    ));
    auto* setup
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"));
    QVERIFY(setup && setup->isVisible());
    auto* saved_button = setup->findChild<QPushButton*>(
        QStringLiteral("gameplay_saved_drills")
    );
    QVERIFY(saved_button);
    auto* slot = view->slot_widgets.front();
    slot->findChild<QPushButton*>(QStringLiteral("slot_details_button"))
        ->click();
    QVERIFY(view->has_open_gameplay_settings());
    visit_drill_picker(window, [&](QDialog& picker) {
        auto* save = picker.findChild<QPushButton*>(
            QStringLiteral("save_current_drill")
        );
        QVERIFY(!save->isEnabled());
        picker.findChild<QLineEdit*>(QStringLiteral("saved_drill_name"))
            ->setText(QStringLiteral("Pending draft"));
        save->click();
    });
    QCOMPARE(
        catalogues.storage.value(QStringLiteral("gameplay_drills/document")),
        saved
    );
    slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"))
        ->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)
        ->click();
    QVERIFY(!view->has_open_gameplay_settings());
    if (slot_count == 24) {
        view->setFixedSize(300, 240);
        QCoreApplication::processEvents();
        QVERIFY(owner->slot_constraints().recommended < owner->size());
        launcher_start_action(window)->trigger();
        QCOMPARE(owner->phase(), gameplay::session_phase::setup);
        QCOMPARE(owner->size(), std::size_t { 24 });
        QCOMPARE(owner->configuration().sequential_count, std::size_t { 23 });
        QVERIFY(!owner->configuration().allow_extra_slots);
        setup
            ->findChild<QCheckBox*>(
                QStringLiteral("gameplay_allow_extra_slots")
            )
            ->setChecked(true);
    }
    launcher_start_action(window)->trigger();
    QCOMPARE(
        owner->phase(),
        slot_count == 1 ? gameplay::session_phase::setup
                        : gameplay::session_phase::running
    );
    view->stop_gameplay_clock();
    QCOMPARE(load_trainer_preferences(), trainer);
    QCOMPARE(load_training_progress(), progress);
    QCOMPARE(
        catalogues.storage.value(QStringLiteral("gameplay_drills/document")),
        saved
    );
    if (slot_count == 1) {
        QVERIFY(!catalogues.storage.contains(
            QStringLiteral("gameplay_preferences/document")
        ));
        QVERIFY(
            !setup->findChild<QLabel*>(QStringLiteral("gameplay_setup_error"))
                 ->text()
                 .isEmpty()
        );
    } else {
        QVERIFY(catalogues.storage.contains(
            QStringLiteral("gameplay_preferences/document")
        ));
    }
}

void table_tests::drill_picker_preserves_live_quiz() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop target drill integration");
#endif
    launcher_preferences_guard preferences;
    drill_storage_guard catalogues;
    QVERIFY(
        training_drill_service(catalogues.storage).save({ legacy_drill() })
    );
    main_window window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();
    window.findChild<QDialog*>()->reject();
    auto* view = window.findChild<table*>();
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(
        4, false, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1, 4 }
    )));
    auto& owner = *view->active_gameplay_session();
    QVERIFY(view->start_gameplay_runtime(87));
    QVERIFY(view->pause_gameplay_runtime());
    QVERIFY(view->swap_gameplay_decks({ 0 }, { 3 }));
    QVERIFY(view->resume_gameplay_runtime());
    view->stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    view->publish_gameplay_runtime(answers);
    auto* input = owned_input(view->slot_widgets[2]);
    QVERIFY(input);
    enter_owned_count(input, QStringLiteral("+17"));
    input->setSelection(1, 2);
    const auto text = input->text();
    const auto cursor = input->cursorPosition();
    const auto selected = input->selectedText();
    const auto batch = owner.current_quiz_batch();
    const auto statistics = owner.statistics();
    const auto timing = owner.timing();
    std::vector<gameplay::deck_state> decks;
    for (std::size_t id = 0; id < owner.size(); ++id)
        decks.push_back(*owner.deck({ id }));
    const auto legacy_bytes
        = catalogues.storage.value(QStringLiteral("training_drills/document"));
    visit_drill_picker(window, [&](QDialog& picker) {
        QCOMPARE(owner.phase(), gameplay::session_phase::paused);
        QVERIFY(!picker.findChild<QPushButton*>(QStringLiteral("launch_drill"))
                     ->isEnabled());
        picker.findChild<QLineEdit*>(QStringLiteral("saved_drill_name"))
            ->setText(QStringLiteral("Live configuration"));
        picker.findChild<QPushButton*>(QStringLiteral("save_current_drill"))
            ->click();
        QVERIFY(!picker.findChild<QPushButton*>(QStringLiteral("launch_drill"))
                     ->isEnabled());
    });
    view->stop_gameplay_clock();
    QCOMPARE(owner.phase(), gameplay::session_phase::running);
    QCOMPARE(owner.current_quiz_batch(), batch);
    QCOMPARE(owner.statistics(), statistics);
    QCOMPARE(owner.timing(), timing);
    for (std::size_t id = 0; id < owner.size(); ++id)
        QCOMPARE(*owner.deck({ id }), decks[id]);
    QCOMPARE(input->text(), text);
    QCOMPARE(input->cursorPosition(), cursor);
    QCOMPARE(input->selectedText(), selected);
    const auto saved
        = gameplay_drill_service(catalogues.storage, drill_limits).load();
    QVERIFY(saved && saved->size() == 1);
    QCOMPARE(
        saved->first(),
        *capture_gameplay_drill(
            owner, QStringLiteral("Live configuration"), drill_limits
        )
    );
    QCOMPARE(
        catalogues.storage.value(QStringLiteral("training_drills/document")),
        legacy_bytes
    );
    QVERIFY(view->pause_gameplay_runtime());
    visit_drill_picker(window, [&](QDialog&) { });
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    QVERIFY(owner.finish());
    view->refresh_gameplay_session();
    visit_drill_picker(window, [&](QDialog& picker) {
        picker.findChild<QListWidget*>(QStringLiteral("saved_drills_list"))
            ->setCurrentRow(0);
        answer_drill_question(picker, QMessageBox::Yes);
        picker.findChild<QPushButton*>(QStringLiteral("launch_drill"))->click();
    });
    QVERIFY(!view->active_gameplay_session());
    QCOMPARE(view->capture_drill_settings(), legacy_drill().slot_settings);
    QVERIFY(view->capture_session_state().quiz_running);
}

void table_tests::drill_picker_scene_replacement() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop target drill integration");
#endif
    launcher_preferences_guard preferences;
    drill_storage_guard catalogues;
    QVERIFY(
        training_drill_service(catalogues.storage).save({ legacy_drill() })
    );
    main_window window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();
    window.findChild<QDialog*>()->reject();
    auto* view = window.findChild<table*>();
    QVERIFY(view->install_gameplay_session(owned_clock_fixture()));
    visit_drill_picker(window, [&](QDialog& picker) {
        QTimer::singleShot(0, &picker, [&] {
            QVERIFY(picker.findChild<QDialog*>(
                QStringLiteral("drill_conversion_dialog")
            ));
            QVERIFY(view->install_gameplay_session(owned_clock_fixture(1)));
        });
        picker.findChild<QPushButton*>(QStringLiteral("convert_drill"))
            ->click();
    });
    QVERIFY(view->active_gameplay_session());
    QCOMPARE(view->active_gameplay_session()->size(), std::size_t { 1 });
    QCOMPARE(
        view->active_gameplay_session()->phase(), gameplay::session_phase::setup
    );
    QVERIFY(
        !catalogues.storage.contains(QStringLiteral("gameplay_drills/document"))
    );
    QVERIFY(!window.findChild<QDialog*>(QStringLiteral("saved_drills_dialog")));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
