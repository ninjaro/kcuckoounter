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
#include <QSlider>
#include <QtTest/QtTest>

#include <array>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::gameplay_hud_projects_owned_session_data() {
    QTest::addColumn<bool>("instruments");
    QTest::addColumn<bool>("global_pause");
    for (const bool instruments : { false, true })
        for (const bool global_pause : { false, true })
            QTest::newRow(
                qPrintable(QStringLiteral("hud-%1-global-%2")
                               .arg(instruments)
                               .arg(global_pause))
            ) << instruments
              << global_pause;
}

void table_tests::gameplay_hud_projects_owned_session() {
    QFETCH(bool, instruments);
    QFETCH(bool, global_pause);

    struct restore_preferences {
        desktop_ui_preferences ui = load_desktop_ui_preferences();

        ~restore_preferences() {
            static_cast<void>(save_desktop_ui_preferences(ui));
        }
    } guard;

    auto ui = guard.ui;
    ui.hud_override = instruments ? desktop_hud_style::instruments
                                  : desktop_hud_style::classic;
    QVERIFY(save_desktop_ui_preferences(ui));
    main_window window;
    window.resize(1600, 1000);
    window.show();
    if (auto* setup = window.findChild<QDialog*>())
        setup->reject();
    auto* view = window.findChild<table*>();
    auto* status = window.findChild<QLabel*>(QStringLiteral("session_status"));
    auto* speed
        = window.findChild<QSlider*>(QStringLiteral("pickup_interval_slider"));
    QVERIFY(view && status && speed);
    view->clear_quiz();
    const auto legacy_speed = speed->value();
    const auto legacy_preferences = load_trainer_preferences();
    auto fixture = owned_clock_fixture(
        4, global_pause, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1, 4 }
    );
    QVERIFY(fixture->set_pick_interval_ms(700));
    QVERIFY(view->install_gameplay_session(std::move(fixture)));
    QCOMPARE(speed->value(), 700);
    QCOMPARE(status->text(), view->gameplay_status_text());
    QVERIFY(view->start_gameplay_runtime(38));
    view->stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view->advance_gameplay_runtime(700, answers));
    view->publish_gameplay_runtime(answers);
    auto* owner = view->active_gameplay_session();
    QVERIFY(status->text().contains(str_label("Running")));
    QCOMPARE(
        status->text().count(str_label("Quiz remaining")), global_pause ? 1 : 0
    );
    QVERIFY(!status->text().contains(QStringLiteral("%1")));
    QVERIFY(!status->text().contains(QStringLiteral("I18N_ARGUMENT_MISSING")));
    const auto label_before = status->text();
    auto* legacy_clock
        = window.findChild<BaseClock*>(QString(), Qt::FindDirectChildrenOnly);
    legacy_clock->set_elapsed_time_ms(900000);
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_table_score_adjusted", Qt::DirectConnection,
            Q_ARG(int, 50), Q_ARG(int, 60)
        )
    );
    QCOMPARE(status->text(), label_before);
    QVERIFY(view->pause_gameplay_runtime());
    QVERIFY(status->text().contains(str_label("Paused")));
    const auto remaining = owner->current_quiz_batch()->remaining_ms;
    speed->setValue(500);
    QCOMPARE(owner->pick_interval_ms(), 500);
    QCOMPARE(owner->current_quiz_batch()->remaining_ms, remaining);
    QVERIFY(view->finish_gameplay_runtime());
    QVERIFY(status->text().contains(str_label("Finished")));
    QVERIFY(!speed->isEnabled());
    QVERIFY(!status->text().contains(str_label("Quiz remaining")));
    // 64-bit owned time is not wrapped at one day or cast to uint seconds.
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(1)));
    QVERIFY(view->start_gameplay_runtime(39));
    view->stop_gameplay_clock();
    QVERIFY(view->advance_gameplay_runtime(90061000, answers));
    view->publish_gameplay_runtime(answers);
    QCOMPARE(view->gameplay_clock_text(), QStringLiteral("25:01:01"));
    QVERIFY(status->text().contains(QStringLiteral("25:01:01")));
    if (auto* clock
        = window.findChild<QLabel*>(QStringLiteral("session_clock")))
        QCOMPARE(clock->text(), view->gameplay_clock_text());
    QCOMPARE(load_trainer_preferences(), legacy_preferences);
    view->clear_gameplay_session();
    QVERIFY(speed->isEnabled());
    QCOMPARE(speed->value(), legacy_speed);
    QCOMPARE(view->pick_interval(), legacy_speed);
    QVERIFY(!status->text().contains(str_label("Verified")));
}

void table_tests::gameplay_lives_hud_data() {
    QTest::addColumn<int>("lives");
    QTest::addColumn<int>("count");
    QTest::addColumn<bool>("instruments");
    for (const bool instruments : { false, true })
        for (const auto& [lives, count] :
             { std::pair { 1, 1 }, std::pair { 3, 4 }, std::pair { 12, 24 },
               std::pair { 12, 1 } })
            QTest::newRow(
                qPrintable(QStringLiteral("lives-%1-slots-%2-instruments-%3")
                               .arg(lives)
                               .arg(count)
                               .arg(instruments))
            ) << lives
              << count << instruments;
}

void table_tests::gameplay_lives_hud() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop owned Lives HUD");
#endif
    QFETCH(int, lives);
    QFETCH(int, count);
    QFETCH(bool, instruments);
    launcher_preferences_guard preferences;

    struct suit_guard {
        int suit = load_default_suit_preference();

        ~suit_guard() { (void)save_default_suit_preference(suit); }
    } guard;

    auto ui = load_desktop_ui_preferences();
    ui.hud_override = instruments ? desktop_hud_style::instruments
                                  : desktop_hud_style::classic;
    QVERIFY(save_desktop_ui_preferences(ui));
    const auto trainer = load_trainer_preferences();
    main_window window;
    window.resize(1600, 1000);
    window.show();
    auto* view = window.findChild<table*>();
    auto* status = window.findChild<QLabel*>(QStringLiteral("session_status"));
    QVERIFY(view && status);
    QVERIFY(!status->text().contains(str_label("Remaining lives")));
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_settings_triggered", Qt::DirectConnection
        )
    );
    auto* shared = window.findChild<settings_shared_state*>();
    QVERIFY(shared);
    auto fixture = owned_clock_fixture(
        static_cast<std::size_t>(count), true,
        gameplay::dealing_mode::simultaneous, gameplay::quiz_scope::multi,
        { 1, 3 }
    );
    auto settings = fixture->configuration();
    settings.failure = gameplay::failure_policy::lives;
    settings.initial_lives = lives;
    QVERIFY(fixture->configure(settings));
    if (count == 1) {
        auto deck = fixture->deck({ 0 })->configuration;
        deck.training = false;
        QVERIFY(fixture->configure_deck({ 0 }, deck));
        const auto prepared = owned_clock_fixture(1)->deck({ 0 })->stream;
        QVERIFY(fixture->prepare_decks(
            std::array { prepared }, std::array<std::uint64_t, 2> { 1, 3 }
        ));
    }
    QVERIFY(view->install_gameplay_session(std::move(fixture)));
    auto* owner = view->active_gameplay_session();
    const QStringList filled { QStringLiteral("♣"), QStringLiteral("♦"),
                               QStringLiteral("♥"), QStringLiteral("♠") };
    const QStringList outlined { QStringLiteral("♧"), QStringLiteral("♢"),
                                 QStringLiteral("♡"), QStringLiteral("♤") };
    for (int suit = 0; suit < 4; ++suit) {
        shared->set_default_suit(suit);
        QCOMPARE(load_default_suit_preference(), suit);
        const auto symbol = instruments ? filled.at(suit) : QStringLiteral("♥");
        QCOMPARE(status->text().count(symbol), lives);
        QVERIFY(status->text().contains(QString::number(lives)));
        QVERIFY(status->text().contains(str_label("Remaining lives")));
        QVERIFY(status->text().contains(str_label("Lost lives")));
        QCOMPARE(owner->configuration(), settings);
        QCOMPARE(owner->remaining_lives(), lives);
    }
    QVERIFY(view->start_gameplay_runtime(106));
    view->stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    view->publish_gameplay_runtime(answers);
    if (count > 1) {
        QVERIFY(view->check_gameplay_quiz(*view->gameplay_quiz_prompt({ 0 })));
        QCOMPARE(owner->remaining_lives(), lives); // Training wrong is immune
        QVERIFY(view->skip_gameplay_quiz(*view->gameplay_quiz_prompt({ 1 })));
        QCOMPARE(owner->remaining_lives(), lives); // Skip costs no life
        const auto prompt = view->gameplay_quiz_prompt({ 2 });
        QVERIFY(prompt);
        QVERIFY(view->edit_gameplay_quiz(
            *prompt, owner->deck({ 2 })->quiz->expected_count
        ));
        QVERIFY(view->check_gameplay_quiz(*prompt));
        QCOMPARE(owner->remaining_lives(), lives); // correct costs no life
    }
    if (count == 24) {
        for (std::size_t id = 3; id < 16; ++id) {
            const auto prompt = view->gameplay_quiz_prompt({ id });
            QVERIFY(prompt);
            QVERIFY(view->check_gameplay_quiz(*prompt));
        }
        QCOMPARE(owner->remaining_lives(), 0);
        QCOMPARE(owner->statistics().errors, std::uint64_t { 13 });
        QCOMPARE(owner->phase(), gameplay::session_phase::running);
        QVERIFY(!owner->result()); // remaining open questions still settle
        const auto lost_symbol
            = instruments ? outlined.at(3) : QStringLiteral("♠");
        QCOMPARE(status->text().count(lost_symbol), 12); // not thirteen errors
        QVERIFY(status->text().contains(str_label("Running")));
    }
    const auto countdown = owner->current_quiz_batch()->remaining_ms;
    QVERIFY(countdown);
    QVERIFY(view->advance_gameplay_runtime(*countdown, answers));
    view->publish_gameplay_runtime(answers);
    if (count == 4) {
        QCOMPARE(owner->remaining_lives(), 2);
        QVERIFY(view->pause_gameplay_runtime());
        const auto paused = status->text();
        view->publish_gameplay_runtime({});
        QCOMPARE(status->text(), paused);
        QVERIFY(view->resume_gameplay_runtime());
        view->stop_gameplay_clock();
        QVERIFY(view->advance_gameplay_runtime(300, answers));
        QVERIFY(view->advance_gameplay_runtime(300, answers));
        view->publish_gameplay_runtime(answers);
        QVERIFY(view->advance_gameplay_runtime(60'000, answers));
        view->publish_gameplay_runtime(answers);
    }
    const int expected_remaining = count == 1 && lives > 1 ? lives - 1 : 0;
    if (expected_remaining > 0) {
        QCOMPARE(owner->phase(), gameplay::session_phase::running);
        QVERIFY(view->finish_gameplay_runtime()); // unused lives are harmless
    }
    const int expected_lost = lives - expected_remaining;
    QCOMPARE(owner->remaining_lives(), expected_remaining);
    QCOMPARE(owner->phase(), gameplay::session_phase::finished);
    QVERIFY(
        owner->statistics().errors >= static_cast<std::uint64_t>(expected_lost)
    );
    const auto result = *owner->result();
    const auto lost_symbol = instruments ? outlined.at(3) : QStringLiteral("♠");
    QCOMPARE(
        status->text().count(lost_symbol), expected_lost
    ); // never error/slot count
    const auto live_symbol = instruments ? filled.at(3) : QStringLiteral("♥");
    QCOMPARE(status->text().count(live_symbol), expected_remaining);
    QVERIFY(!status->text().contains(QStringLiteral("%1")));
    QVERIFY(!status->text().contains(QStringLiteral("I18N_ARGUMENT_MISSING")));
    // Tick projection uses cached appearance, not a settings read.
    QVERIFY(save_default_suit_preference(2));
    for (int repeat = 0; repeat < 12; ++repeat)
        view->publish_gameplay_runtime({});
    QCOMPARE(status->text().count(lost_symbol), expected_lost);
    QCOMPARE(*owner->result(), result);
    window.resize(480, 640);
    QCoreApplication::processEvents();
    QVERIFY(status->wordWrap());
    QVERIFY(status->geometry().right() <= status->parentWidget()->width());
    view->clear_gameplay_session();
    QVERIFY(!status->text().contains(str_label("Remaining lives")));
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(1)));
    QVERIFY(!status->text().contains(str_label("Remaining lives"))); // Block
    QCOMPARE(load_trainer_preferences(), trainer);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
