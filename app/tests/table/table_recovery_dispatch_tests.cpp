// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_restore_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"
#include "settings/gameplay_recovery_fixture.hpp"
#include "shell/gameplay_reservation.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QApplication>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace gameplay;
using namespace recovery_test;
using outcome = gameplay_reservation::recovery_outcome;

void gameplay_restore_tests::active_restores_identity_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<bool>("multi");
    QTest::addColumn<bool>("infinite");
    for (int mode = 0; mode < 3; ++mode)
        for (bool multi : { false, true })
            for (bool infinite : { false, true })
                QTest::newRow(
                    qPrintable(QStringLiteral("%1-%2-%3")
                                   .arg(mode)
                                   .arg(multi)
                                   .arg(infinite))
                ) << mode
                  << multi << infinite;
}

void gameplay_restore_tests::active_restores_identity() {
    QFETCH(int, mode);
    QFETCH(bool, multi);
    QFETCH(bool, infinite);
    session_configuration config;
    config.dealing = static_cast<dealing_mode>(mode);
    config.sequential_count = 2;
    config.scope = multi ? quiz_scope::multi : quiz_scope::single;
    config.source = mode == 1 ? quiz_source::physical_joker
                              : quiz_source::virtual_interrupt;
    config.initial_lives = 12;
    auto model = progress_test::ready_session(config, infinite);
    if (infinite) {
        drive(model, 180);
        // Generated physical Jokers need not occur within prime's short
        // synthetic-shoe prefix. Seek a pending question within two chunks.
        for (int step = 0; step < 108 && model.unresolved_quizzes().empty();
             ++step) {
            QVERIFY(model.advance_time(17).accepted);
            QCOMPARE(model.deal_step().status, dealing_step_status::advanced);
        }
        QVERIFY(!model.unresolved_quizzes().empty());
    }
    prime(model);
    QVERIFY(model.pause());
    QVERIFY(model.set_pick_interval_ms(650));
    const auto saved = snapshot(model, 649);
    QTemporaryDir directory;
    const auto file = directory.filePath("recovery.ini");
    {
        QSettings storage(file, QSettings::IniFormat);
        gameplay_recovery_service recovery(storage, { 1, 64 });
        QVERIFY(recovery.begin(saved) == 1);
    }
    QSettings reopened(file, QSettings::IniFormat);
    gameplay_recovery_service recovery(reopened, { 1, 64 });
    const auto bytes = reopened.value(storage_key);
    table view;
    view.resize(450, 850);
    view.set_card_orientation(card_orientation_mode::vertical);
    gameplay_reservation binding;
    bool bound_at_publication = false;
    QObject::connect(&view, &table::gameplay_session_changed, &view, [&] {
        bound_at_publication = binding.sequence() == 1 && binding.owns(view)
            && view.gameplay_owner->phase() == session_phase::paused
            && view.gameplay_pick_elapsed_ms == saved.pickup_elapsed_ms
            && !view.gameplay_timer.isActive()
            && !view.gameplay_elapsed.isValid();
    });
    QSignalSpy legacy_score(&view, &table::score_adjusted);
    QSignalSpy legacy_end(&view, &table::game_over);
    QString error("old diagnostic");
    QCOMPARE(binding.recover(view, recovery, &error), outcome::paused);
    QVERIFY(error.isEmpty() && bound_at_publication);
    QVERIFY(view.capture_gameplay_checkpoint() == saved);
    QTest::qWait(2);
    QVERIFY(view.capture_gameplay_checkpoint() == saved);
    QVERIFY(!view.start_gameplay_runtime(9));
    QCOMPARE(reopened.value(storage_key), bytes);
    QVERIFY(!reopened.contains(progress_key));
    table reference;
    reference.resize(450, 850);
    reference.set_card_orientation(card_orientation_mode::vertical);
    QVERIFY(reference.restore_gameplay_checkpoint(saved));
    QVERIFY(reference.resume_gameplay_runtime());
    QVERIFY(view.resume_gameplay_runtime());
    reference.stop_gameplay_clock();
    view.stop_gameplay_clock();
    for (int step = 0; step < 8; ++step) {
        for (const auto id : reference.gameplay_owner->unresolved_quizzes()) {
            const auto input
                = reference.gameplay_owner->deck(id)->running_count;
            QVERIFY(reference.edit_gameplay_quiz(
                *reference.gameplay_quiz_prompt(id), input
            ));
            QVERIFY(
                view.edit_gameplay_quiz(*view.gameplay_quiz_prompt(id), input)
            );
            QVERIFY(reference.check_gameplay_quiz(
                *reference.gameplay_quiz_prompt(id)
            ));
            QVERIFY(view.check_gameplay_quiz(*view.gameplay_quiz_prompt(id)));
        }
        std::vector<quiz_answer> a, b;
        QVERIFY(reference.advance_gameplay_runtime(650, a));
        QVERIFY(view.advance_gameplay_runtime(650, b));
        QVERIFY(a == b);
        QVERIFY(
            gameplay::capture_checkpoint(*reference.gameplay_owner)
            == gameplay::capture_checkpoint(*view.gameplay_owner)
        );
        QCOMPARE(
            view.gameplay_pick_elapsed_ms, reference.gameplay_pick_elapsed_ms
        );
    }
    QCOMPARE(legacy_score.count(), 0);
    QCOMPARE(legacy_end.count(), 0);
    QVERIFY(binding.owns(view));
}

void gameplay_restore_tests::absent_keeps_scene() {
    QTemporaryDir directory;
    QSettings storage(directory.filePath("recovery.ini"), QSettings::IniFormat);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(table_test::owned_clock_fixture(1)));
    auto* owner = view.gameplay_owner.get();
    const auto scene = view.gameplay_scene();
    gameplay_reservation binding;
    QString error("old diagnostic");
    QSignalSpy changed(&view, &table::gameplay_session_changed);
    QCOMPARE(binding.recover(view, recovery, &error), outcome::absent);
    QVERIFY(error.isEmpty() && !binding.sequence());
    QCOMPARE(view.gameplay_owner.get(), owner);
    QCOMPARE(view.gameplay_scene(), scene);
    QCOMPARE(changed.count(), 0);
    QVERIFY(storage.allKeys().isEmpty());
}

void gameplay_restore_tests::protected_records_keep_scene_data() {
    QTest::addColumn<int>("fault");
    QTest::newRow("standalone-v2") << 0;
    QTest::newRow("standalone-damaged") << 1;
    QTest::newRow("future-recovery") << 2;
    QTest::newRow("wrong-value-type") << 3;
    QTest::newRow("future-progress") << 4;
    QTest::newRow("admitted-active") << 5;
    QTest::newRow("gapped-pending") << 6;
    QTest::newRow("over-capacity") << 7;
}

void gameplay_restore_tests::protected_records_keep_scene() {
    QFETCH(int, fault);
    auto model = progress_test::ready_session();
    const auto saved = snapshot(model);
    QTemporaryDir directory;
    QSettings storage(directory.filePath("recovery.ini"), QSettings::IniFormat);
    storage.setValue(
        storage_key,
        *encode_gameplay_recovery(
            gameplay_active_record { 1, saved }, { 1, 64 }
        )
    );
    if (fault == 0)
        storage.setValue(
            checkpoint_key, *encode_gameplay_checkpoint(saved, { 1, 64 })
        );
    else if (fault == 1)
        storage.setValue(checkpoint_key, QByteArray("broken"));
    else if (fault == 2)
        storage.setValue(storage_key, QByteArray("{\"version\":900}"));
    else if (fault == 3)
        storage.setValue(storage_key, QStringLiteral("wrong type"));
    else if (fault == 4)
        storage.setValue(progress_key, QByteArray("{\"version\":900}"));
    else if (fault == 5)
        QVERIFY(
            gameplay_progress_service(storage, { 1, 64 }).record(finish(model))
        );
    else if (fault == 6)
        storage.setValue(
            storage_key, *encode_gameplay_recovery(finish(model, 2), { 1, 64 })
        );
    storage.sync();
    const auto before = storage.value(storage_key);
    const auto other = storage.value(checkpoint_key);
    const auto progress = storage.value(progress_key);
    gameplay_recovery_service recovery(
        storage, fault == 7 ? slot_limits { 1, 2 } : slot_limits { 1, 64 }
    );
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(table_test::owned_clock_fixture(1)));
    auto* owner = view.gameplay_owner.get();
    QSignalSpy changed(&view, &table::gameplay_session_changed);
    gameplay_reservation binding;
    QString error;
    QCOMPARE(binding.recover(view, recovery, &error), outcome::failed);
    QVERIFY(!error.isEmpty() && !binding.sequence());
    QCOMPARE(view.gameplay_owner.get(), owner);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(storage.value(storage_key), before);
    QCOMPARE(storage.value(checkpoint_key), other);
    QCOMPARE(storage.value(progress_key), progress);
}

void gameplay_restore_tests::active_preflight_keeps_scene_data() {
    QTest::addColumn<int>("fault");
    QTest::newRow("geometry") << 0;
    QTest::newRow("generation-budget") << 1;
    QTest::newRow("populated-legacy") << 2;
    QTest::newRow("live-target") << 3;
}

void gameplay_restore_tests::active_preflight_keeps_scene() {
    QFETCH(int, fault);
    auto model = progress_test::ready_session();
    QTemporaryDir directory;
    QSettings storage(directory.filePath("recovery.ini"), QSettings::IniFormat);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    QVERIFY(recovery.begin(snapshot(model)) == 1);
    const auto bytes = storage.value(storage_key);
    table view;
    view.resize(900, 700);
    if (fault == 2) {
        view.set_slot_count(1);
        view.start_quiz(0, false);
        view.set_paused(true);
    } else {
        QVERIFY(
            view.install_gameplay_session(table_test::owned_clock_fixture(1))
        );
        if (fault == 3) {
            QVERIFY(view.start_gameplay_runtime(7));
            QVERIFY(view.pause_gameplay_runtime());
        }
    }
    if (fault == 0)
        view.resize(1, 1);
    auto* owner = view.gameplay_owner.get();
    const auto legacy = view.capture_session_state();
    const auto scene = view.gameplay_scene();
    gameplay::generation_policy resources;
    if (fault == 1)
        resources.maximum_cards = 1;
    gameplay_reservation binding;
    QString error;
    QCOMPARE(
        binding.recover(view, recovery, &error, resources), outcome::failed
    );
    QVERIFY(!error.isEmpty() && !binding.sequence());
    QCOMPARE(view.gameplay_owner.get(), owner);
    QCOMPARE(view.capture_session_state(), legacy);
    QCOMPARE(view.gameplay_scene(), scene);
    QCOMPARE(storage.value(storage_key), bytes);
}

void gameplay_restore_tests::active_binding_rejects_reuse() {
    QTemporaryDir directory;
    QSettings storage(directory.filePath("recovery.ini"), QSettings::IniFormat);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    auto first = progress_test::ready_session();
    QVERIFY(recovery.begin(snapshot(first)) == 1);
    QVERIFY(recovery.complete(finish(first)));
    auto second = progress_test::ready_session({}, true);
    QVERIFY(recovery.begin(snapshot(second)) == 2);
    const auto bytes = storage.value(storage_key);
    table view;
    view.resize(900, 700);
    gameplay_reservation binding;
    QCOMPARE(binding.recover(view, recovery), outcome::paused);
    QVERIFY(binding.sequence() == 2 && binding.owns(view));
    auto* owner = view.gameplay_owner.get();
    QString error;
    QCOMPARE(binding.recover(view, recovery, &error), outcome::failed);
    QVERIFY(!error.isEmpty());
    QVERIFY(!binding.start(view, recovery, 9, &error));
    QCOMPARE(view.gameplay_owner.get(), owner);
    QCOMPARE(storage.value(storage_key), bytes);
    QVERIFY(binding.sequence() == 2 && binding.owns(view));
}

void gameplay_restore_tests::active_commit_blocks_reentry() {
    QTemporaryDir directory;
    QSettings storage(directory.filePath("recovery.ini"), QSettings::IniFormat);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    auto model = progress_test::ready_session();
    const auto saved = snapshot(model);
    QVERIFY(recovery.begin(saved) == 1);
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(table_test::owned_clock_fixture(1)));
    const auto scene = view.gameplay_scene();
    gameplay_reservation binding;
    bool protected_scene = false;
    const auto connection = QObject::connect(
        &view, &table::gameplay_session_about_to_change, &view, [&] {
            QString error;
            const bool restored = view.restore_gameplay_checkpoint(saved);
            const bool installed = view.install_gameplay_session(
                table_test::owned_clock_fixture(2)
            );
            const bool started = view.start_gameplay_runtime(9);
            const auto nested = binding.recover(view, recovery, &error);
            const bool reserved = binding.start(view, recovery, 10, &error);
            view.clear_gameplay_session();
            protected_scene = !restored && !installed && !started && !reserved
                && nested == outcome::failed && !error.isEmpty()
                && view.gameplay_scene() == scene;
        }
    );
    QCOMPARE(binding.recover(view, recovery), outcome::paused);
    QVERIFY(protected_scene && binding.owns(view));
    QObject::disconnect(connection);
}

void gameplay_restore_tests::active_publication_retirement_data() {
    QTest::addColumn<int>("point");
    QTest::newRow("before-commit-destruction") << 0;
    QTest::newRow("changed-replacement") << 1;
    QTest::newRow("changed-clear") << 2;
    QTest::newRow("changed-destruction") << 3;
    QTest::newRow("layout-destruction") << 4;
}

void gameplay_restore_tests::active_publication_retirement() {
    QFETCH(int, point);
    QTemporaryDir directory;
    QSettings storage(directory.filePath("recovery.ini"), QSettings::IniFormat);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    auto model = progress_test::ready_session();
    QVERIFY(recovery.begin(snapshot(model)) == 1);
    const auto bytes = storage.value(storage_key);
    auto* view = new table;
    QPointer<table> guard(view);
    view->resize(900, 700);
    gameplay_reservation binding;
    QVector<QPointer<table_slot>> staged;
    bool retired = false;
    bool notified = false;
    const auto notify = point == 0 ? &table::gameplay_session_about_to_change
        : point == 4               ? &table::gameplay_layout_changed
                                   : &table::gameplay_session_changed;
    const auto connection = QObject::connect(view, notify, view, [&] {
        if (notified)
            return;
        notified = true;
        if (point == 0)
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* slot = qobject_cast<table_slot*>(widget))
                    staged.append(slot);
        if (point == 1)
            retired = view->install_gameplay_session(
                table_test::owned_clock_fixture(1)
            );
        else if (point == 2) {
            view->clear_gameplay_session();
            retired = !view->active_gameplay_session();
        } else {
            delete view;
            retired = true;
        }
    });
    QString error;
    QCOMPARE(binding.recover(*view, recovery, &error), outcome::failed);
    QVERIFY(retired && !error.isEmpty());
    QCOMPARE(binding.sequence().has_value(), point != 0);
    if (point == 0)
        QVERIFY(!staged.isEmpty());
    for (const auto& slot : staged)
        QVERIFY(!slot);
    if (guard) {
        QObject::disconnect(connection);
        QVERIFY(!binding.owns(*guard));
        delete guard;
    }
    QCOMPARE(storage.value(storage_key), bytes);
    QVERIFY(!storage.contains(progress_key));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
