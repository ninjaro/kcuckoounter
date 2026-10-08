// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "arch/str_label.hpp"
#include "settings/gameplay_preferences.hpp"
#include "settings/strategy_data.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/table_fixture.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QtTest/QtTest>

using namespace table_test;

namespace {

const QString defaults_key = QStringLiteral("gameplay_preferences/document");

QVariantMap unrelated_settings(QSettings& settings) {
    settings.sync();
    QVariantMap values;
    for (const auto& key : settings.allKeys())
        if (key != defaults_key
            && key != QStringLiteral("gameplay_recovery/document")
            && key != QStringLiteral("gameplay_progress/document")
            && !key.startsWith(QStringLiteral("desktop_shell/")))
            values.insert(key, settings.value(key));
    return values;
}

} // namespace

void table_tests::launcher_defaults_restore_global_setup_data() {
    QTest::addColumn<int>("slot_count");
    QTest::newRow("single-4") << 4;
    QTest::newRow("multi-24") << 24;
}

void table_tests::launcher_defaults_restore_global_setup() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop opt-in launcher defaults");
#endif
    QFETCH(int, slot_count);
    launcher_preferences_guard preferences;
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    gameplay_preferences expected;
    QVariantMap unrelated;
    QVariant saved;
    {
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
        window.findChild<QAction*>(QStringLiteral("game_new_gameplay"))
            ->trigger();
        auto* dialog = window.findChild<QDialog*>(
            QStringLiteral("gameplay_setup_dialog")
        );
        QVERIFY(dialog);
        dialog->findChild<QSpinBox*>(QStringLiteral("gameplay_slot_count"))
            ->setValue(slot_count);
        auto* owner = view->active_gameplay_session();
        QVERIFY(
            owner
            && owner->slot_constraints().recommended
                >= static_cast<std::size_t>(slot_count)
        );
        auto* fields = dialog->findChild<gameplay::setup_widget*>();
        QVERIFY(fields);
        const auto select = [&](const char* name, int value) {
            auto* combo
                = fields->findChild<QComboBox*>(QString::fromLatin1(name));
            combo->setCurrentIndex(combo->findData(value));
        };
        select(
            "gameplay_failure_policy",
            static_cast<int>(gameplay::failure_policy::block)
        );
        select(
            "gameplay_quiz_source",
            static_cast<int>(
                slot_count == 4 ? gameplay::quiz_source::physical_joker
                                : gameplay::quiz_source::virtual_interrupt
            )
        );
        select(
            "gameplay_quiz_scope",
            static_cast<int>(
                slot_count == 4 ? gameplay::quiz_scope::single
                                : gameplay::quiz_scope::multi
            )
        );
        fields->findChild<QSpinBox*>(QStringLiteral("gameplay_initial_lives"))
            ->setValue(11);
        fields
            ->findChild<QSpinBox*>(QStringLiteral("gameplay_sequential_count"))
            ->setValue(slot_count == 4 ? 3 : 17);
        fields->findChild<QCheckBox*>(QStringLiteral("gameplay_global_pause"))
            ->setChecked(false);
        fields->findChild<QCheckBox*>(QStringLiteral("gameplay_allow_skip"))
            ->setChecked(false);
        QVERIFY(!owner->configuration().allow_extra_slots);
        auto deck = owner->deck({ 0 })->configuration;
        deck.deck_count = 1;
        deck.training = true;
        QVERIFY(owner->configure_deck({ 0 }, deck));
        QVERIFY(owner->set_show_count({ 0 }, true));
        auto* speed = window.findChild<QSlider*>(
            QStringLiteral("pickup_interval_slider")
        );
        QVERIFY(speed);
        speed->setValue(650);
        QCOMPARE(owner->pick_interval_ms(), 650);
        expected = { owner->size(), owner->configuration(),
                     owner->pick_interval_ms() };
        unrelated = unrelated_settings(storage);
        QVERIFY(
            !storage.contains(defaults_key)
        ); // accepted edits are not disk writes
        launcher_start_action(window)->trigger();
        QCOMPARE(owner->phase(), gameplay::session_phase::running);
        view->stop_gameplay_clock();
        QVERIFY(!dialog->isVisible());
        const auto loaded
            = gameplay_preferences_service(storage, { 1, 64 }).load();
        QVERIFY(loaded);
        QCOMPARE(*loaded, expected);
        saved = storage.value(defaults_key);
        QCOMPARE(unrelated_settings(storage), unrelated);
        // Runtime speed/renderer changes are not another default-save path.
        speed->setValue(750);
        QCOMPARE(owner->pick_interval_ms(), 750);
        view->set_frame_style(slot_frame_style::thin);
        QVERIFY(view->pause_gameplay_runtime());
        QCOMPARE(storage.value(defaults_key), saved);
        QVERIFY(view->finish_gameplay_runtime());
    }
    QCOMPARE(
        storage.value(defaults_key), saved
    ); // completion/shutdown do not rewrite global defaults
    QCOMPARE(unrelated_settings(storage), unrelated);
    main_window restored;
    restored.show();
    QCoreApplication::processEvents();
    restored.findChild<QDialog*>()->reject();
    auto* view = restored.findChild<table*>();
    QVERIFY(
        view && !view->active_gameplay_session()
    ); // completion was admitted
    set_gameplay_motion_style(*view, 0);
    view->setFixedSize(320, 240);
    QCoreApplication::processEvents();
    restored.findChild<QAction*>(QStringLiteral("game_new_gameplay"))
        ->trigger();
    auto* owner = view->active_gameplay_session();
    QVERIFY(owner);
    QCOMPARE(owner->phase(), gameplay::session_phase::setup);
    QCOMPARE(owner->size(), expected.slot_count);
    QCOMPARE(owner->configuration(), expected.configuration);
    QCOMPARE(owner->pick_interval_ms(), expected.pickup_interval_ms);
    QVERIFY(owner->slot_constraints().recommended < owner->size());
    QVERIFY(!owner->configuration().allow_extra_slots);
    QVERIFY(!view->gameplay_timer.isActive());
    const auto recommended = gameplay::recommended_configuration(
        strategy_repository(), { "hi_lo", 1, false, false }
    );
    QVERIFY(recommended);
    for (std::size_t id = 0; id < owner->size(); ++id) {
        const auto* deck = owner->deck({ id });
        QCOMPARE(deck->configuration, *recommended);
        QVERIFY(deck->stream.cards.empty() && !deck->show_count && !deck->quiz);
        QCOMPARE(deck->next_card, std::size_t { 0 });
    }
    auto* dialog
        = restored.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"));
    QVERIFY(dialog && dialog->isVisible());
    QCOMPARE(
        dialog->findChild<QSpinBox*>(QStringLiteral("gameplay_slot_count"))
            ->value(),
        slot_count
    );
    auto* start = launcher_start_action(restored);
    start->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::setup);
    QVERIFY(!view->gameplay_timer.isActive());
    QVERIFY(owner->deck({ 0 })->stream.cards.empty());
    QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("gameplay_setup_error"))
                ->text()
                .contains(str_label("Confirm")));
    QCOMPARE(
        storage.value(defaults_key), saved
    ); // no clamp or fabricated consent
    dialog->findChild<QCheckBox*>(QStringLiteral("gameplay_allow_extra_slots"))
        ->setChecked(true);
    start->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::running);
    view->stop_gameplay_clock();
    expected.configuration.allow_extra_slots = true;
    QCOMPARE(
        *gameplay_preferences_service(storage, { 1, 64 }).load(), expected
    );
    QCOMPARE(unrelated_settings(storage), unrelated);
}

void table_tests::launcher_defaults_reject_unreadable_storage_data() {
    QTest::addColumn<QByteArray>("record");
    QTest::newRow("damaged") << QByteArray("{");
    QTest::newRow("future") << QByteArray("{\"version\":3}");
}

void table_tests::launcher_defaults_reject_unreadable_storage() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop opt-in launcher storage failures");
#endif
    QFETCH(QByteArray, record);
    launcher_preferences_guard preferences;
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    storage.setValue(defaults_key, record);
    storage.sync();
    main_window window;
    window.resize(1600, 1000);
    window.show();
    QCoreApplication::processEvents();
    window.findChild<QDialog*>()->reject();
    auto* view = window.findChild<table*>();
    QVERIFY(view && !view->active_gameplay_session());
    const auto unrelated = unrelated_settings(storage);
    auto* entry
        = window.findChild<QAction*>(QStringLiteral("game_new_gameplay"));
    entry->trigger();
    QVERIFY(!view->active_gameplay_session());
    QVERIFY(
        !window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"))
    );
    QCOMPARE(storage.value(defaults_key).toByteArray(), record);
    QCOMPARE(unrelated_settings(storage), unrelated);
    storage.remove(defaults_key);
    storage.sync();
    entry->trigger();
    auto* owner = view->active_gameplay_session();
    QVERIFY(owner);
    const auto before = *owner->deck({ 0 });
    storage.setValue(
        defaults_key, record
    ); // changed while the setup form was open
    storage.sync();
    launcher_start_action(window)->trigger();
    QCOMPARE(owner->phase(), gameplay::session_phase::setup);
    QVERIFY(!view->gameplay_timer.isActive());
    QCOMPARE(*owner->deck({ 0 }), before); // reject before any generation
    auto* dialog
        = window.findChild<QDialog*>(QStringLiteral("gameplay_setup_dialog"));
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(!dialog->findChild<QLabel*>(QStringLiteral("gameplay_setup_error"))
                 ->text()
                 .isEmpty());
    QCOMPARE(storage.value(defaults_key).toByteArray(), record);
    QCOMPARE(unrelated_settings(storage), unrelated);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
