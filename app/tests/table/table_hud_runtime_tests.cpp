// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "arch/str_label.hpp"
#include "settings/training_progress.hpp"
#include "shell/main_window.hpp"
#include "table/settings_template.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QAccessible>
#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QSlider>
#include <QtTest/QtTest>

#include <array>

using namespace table_test;

namespace {

struct hud_preferences_guard {
    desktop_shell_state shell = load_desktop_shell_state();
    int suit = load_default_suit_preference();

    hud_preferences_guard() {
        save_desktop_shell_state({});
        (void)save_default_suit_preference(1);
    }

    ~hud_preferences_guard() {
        save_desktop_shell_state(shell);
        (void)save_default_suit_preference(suit);
    }
};

settings_template_widget* hud_editor(main_window& window) {
    for (auto* editor : window.findChildren<settings_template_widget*>())
        if (editor->findChild<QComboBox*>(QStringLiteral("desktop_ui_hud")))
            return editor;
    return nullptr;
}

void settle_hud() {
    // Deliver geometry requests, not wall-clock ticks or asynchronous cache
    // completion. Native resize and style changes still use the real layouts.
    for (int pass = 0; pass < 12; ++pass)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
}

} // namespace

void table_tests::hud_styles_preserve_owned_quiz_data() {
    QTest::addColumn<bool>("initial_instruments");
    QTest::addColumn<bool>("global_pause");
    QTest::addColumn<bool>("multi");
    for (const bool instruments : { false, true })
        for (const bool global : { false, true })
            for (const bool multi : { false, true })
                QTest::newRow(qPrintable(
                    QStringLiteral("instruments=%1/global=%2/multi=%3")
                        .arg(instruments)
                        .arg(global)
                        .arg(multi)
                )) << instruments
                   << global << multi;
}

void table_tests::hud_styles_preserve_owned_quiz() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop activated HUD compatibility");
#endif
    QFETCH(bool, initial_instruments);
    QFETCH(bool, global_pause);
    QFETCH(bool, multi);
    launcher_preferences_guard preferences;
    hud_preferences_guard hud;
    auto ui = load_desktop_ui_preferences();
    ui.hud_override = initial_instruments ? desktop_hud_style::instruments
                                          : desktop_hud_style::classic;
    QVERIFY(save_desktop_ui_preferences(ui));
    main_window window;
    window.resize(1600, 1000);
    window.show();
    QCoreApplication::processEvents();
    if (auto* setup = window.findChild<QDialog*>())
        setup->reject();
    auto* view = window.findChild<table*>();
    auto* surface
        = window.findChild<QFrame*>(QStringLiteral("desktop_status_surface"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("session_status"));
    auto* readout
        = window.findChild<QLabel*>(QStringLiteral("pickup_interval_readout"));
    auto* speed
        = window.findChild<QSlider*>(QStringLiteral("pickup_interval_slider"));
    auto* progress
        = window.findChild<QProgressBar*>(QStringLiteral("raster_progress"));
    QVERIFY(view && surface && status && readout && speed && progress);
    set_gameplay_motion_style(*view, 0);
    const auto legacy_interval = view->pick_interval();
    const auto trainer = load_trainer_preferences();
    const auto history = load_training_progress();
    auto* legacy_clock
        = window.findChild<BaseClock*>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(legacy_clock && !legacy_clock->is_active());
    QSignalSpy legacy_score(view, &table::score_adjusted);
    QSignalSpy preparation(view, &table::gameplay_preparation_required);
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(
        4, global_pause, gameplay::dealing_mode::simultaneous,
        multi ? gameplay::quiz_scope::multi : gameplay::quiz_scope::single,
        { 1, 4 }
    )));
    auto& owner = *view->active_gameplay_session();
    QVERIFY(status->text().contains(str_label("Ready")));
    QVERIFY(speed->isEnabled());
    QCOMPARE(speed->value(), 300);
    window.activateWindow();
    speed->setFocus();
    QTest::keyClick(speed, Qt::Key_Right);
    QCOMPARE(owner.pick_interval_ms(), 301);
    QVERIFY(readout->text().contains(QStringLiteral("301")));
    QVERIFY(view->start_gameplay_runtime(83));
    view->stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view->advance_gameplay_runtime(301, answers));
    view->publish_gameplay_runtime(answers);
    QVERIFY(owner.current_quiz_batch());
    QCOMPARE(
        owner.current_quiz_batch()->remaining_ms.has_value(), global_pause
    );
    QVERIFY(status->text().contains(str_label("Running")));
    QCOMPARE(
        status->text().count(str_label("Quiz remaining")), global_pause ? 1 : 0
    );
    const auto running_batch = *owner.current_quiz_batch();
    const auto running_time = owner.timing();
    speed->setFocus();
    QTest::keyClick(speed, Qt::Key_Right);
    QCOMPARE(owner.pick_interval_ms(), 302);
    QCOMPARE(*owner.current_quiz_batch(), running_batch);
    QCOMPARE(owner.timing(), running_time);
    auto* input = owned_input(view->slot_widgets[2]);
    QVERIFY(input && input->isVisible() && input->isEnabled());
    enter_owned_count(input, QStringLiteral("+17"));
    input->setSelection(1, 2);
    const auto cursor = input->cursorPosition();
    const auto selection = input->selectedText();
    const auto prompt = view->gameplay_quiz_prompt({ 2 });
    QVERIFY(prompt);
    // The real Settings route manually pauses, even during automatic freeze.
    QVERIFY(QMetaObject::invokeMethod(&window, "on_settings_triggered"));
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    auto* editor = hud_editor(window);
    QVERIFY(editor);
    auto* hud_choice
        = editor->findChild<QComboBox*>(QStringLiteral("desktop_ui_hud"));
    auto* readout_choice = editor->findChild<QComboBox*>(
        QStringLiteral("desktop_ui_speed_readout")
    );
    QVERIFY(hud_choice && readout_choice);
    editor->window()->hide();
    window.activateWindow();
    speed->setFocus();
    QTest::keyClick(speed, Qt::Key_Right);
    QCOMPARE(owner.pick_interval_ms(), 303);
    QCOMPARE(*owner.current_quiz_batch(), running_batch);
    QCOMPARE(owner.timing(), running_time);
    const std::array records { *owner.deck({ 0 }), *owner.deck({ 1 }),
                               *owner.deck({ 2 }), *owner.deck({ 3 }) };
    const auto batch = *owner.current_quiz_batch();
    const auto timing = owner.timing();
    const auto statistics = owner.statistics();
    const auto configuration = owner.configuration();
    const auto widgets = view->slot_widgets;
    auto* cache = view->shared_raster_cache_service();
    const auto original_status = status->text();
    legacy_clock->set_elapsed_time_ms(900'000);
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_clock_ticked", Q_ARG(qlonglong, 900'000),
            Q_ARG(qlonglong, 1'000)
        )
    );
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_table_score_adjusted", Q_ARG(int, 50), Q_ARG(int, 60)
        )
    );
    QCOMPARE(status->text(), original_status);
    for (const bool instruments : { !initial_instruments, initial_instruments })
        for (const bool show_readout : { false, true }) {
            hud_choice->setCurrentIndex(instruments ? 2 : 1);
            readout_choice->setCurrentIndex(show_readout ? 1 : 2);
            QVERIFY(editor->apply_theme_settings());
            QCOMPARE(
                surface->frameShape(),
                instruments ? QFrame::StyledPanel : QFrame::NoFrame
            );
            QCOMPARE(
                speed->tickPosition(),
                instruments ? QSlider::TicksBelow : QSlider::NoTicks
            );
            QCOMPARE(status->font().bold(), instruments);
            QCOMPARE(readout->isHidden(), !show_readout);
            QVERIFY(speed->isEnabled());
            QCOMPARE(speed->value(), 303);
            QVERIFY(readout->text().contains(QStringLiteral("303")));
            speed->setFocus();
            QTest::keyClick(speed, Qt::Key_Right);
            QCOMPARE(owner.pick_interval_ms(), 304);
            QTest::keyClick(speed, Qt::Key_Left);
            QCOMPARE(owner.pick_interval_ms(), 303);
            for (const QSize size : { QSize(1600, 1000), QSize(480, 640) }) {
                window.resize(size);
                settle_hud();
                // Preparation is a separate status projection, not a Pause or
                // countdown action. Exercise the actual connected signal.
                view->rasterization_busy_changed(true);
                settle_hud();
                QVERIFY(progress->isVisible());
                QCOMPARE(progress->minimum(), 0);
                QCOMPARE(progress->maximum(), 0);
                QCOMPARE(
                    status->text(),
                    str_label("Processing") + QStringLiteral(" / ")
                        + view->gameplay_status_text()
                );
                QCOMPARE(window.size(), size);
                QList<QWidget*> fields { status, readout, speed, progress };
                if (auto* clock = window.findChild<QLabel*>(
                        QStringLiteral("session_clock")
                    )) {
                    fields.append(clock);
                    QCOMPARE(clock->text(), view->gameplay_clock_text());
                }
                QList<QRect> field_bounds;
                for (auto* field : fields) {
                    if (field->isHidden())
                        continue;
                    QVERIFY(field->isVisible());
                    const QRect bounds(
                        field->mapTo(surface, QPoint()), field->size()
                    );
                    QVERIFY2(
                        surface->rect().contains(bounds),
                        qPrintable(field->objectName())
                    );
                    for (const auto& previous : field_bounds)
                        QVERIFY(!previous.intersects(bounds));
                    field_bounds.append(bounds);
                    if (auto* label = qobject_cast<QLabel*>(field))
                        QVERIFY(
                            label->height()
                            >= label->heightForWidth(label->width())
                        );
                }
                QVERIFY(speed->width() >= speed->minimumWidth());
                auto* accessible = QAccessible::queryAccessibleInterface(speed);
                QVERIFY(accessible);
                QCOMPARE(
                    accessible->text(QAccessible::Name),
                    str_label("Card pickup interval (ms)")
                );
                QVERIFY(!speed->toolTip().isEmpty());
                QVERIFY(!progress->accessibleName().isEmpty());
                QList<QRect> slot_bounds;
                for (std::size_t id = 0; id < records.size(); ++id) {
                    QCOMPARE(*owner.deck({ id }), records[id]);
                    QCOMPARE(view->slot_widgets[id], widgets[id]);
                    const auto bounds = widgets[id]->geometry();
                    QVERIFY(view->rect().contains(bounds));
                    QCOMPARE(
                        bounds,
                        widget_rectangle(
                            view->gameplay_layout->deck_motion({ id })->to
                        )
                    );
                    for (const auto& previous : slot_bounds)
                        QVERIFY(!previous.intersects(bounds));
                    slot_bounds.append(bounds);
                    QVERIFY(widgets[id]->card_face_need_short_px() > 0);
                }
                QCOMPARE(owner.configuration(), configuration);
                QCOMPARE(*owner.current_quiz_batch(), batch);
                QCOMPARE(owner.timing(), timing);
                QCOMPARE(owner.statistics(), statistics);
                QCOMPARE(view->gameplay_quiz_prompt({ 2 }), prompt);
                QCOMPARE(owned_input(widgets[2]), input);
                QCOMPARE(input->text(), QStringLiteral("+17"));
                QCOMPARE(input->cursorPosition(), cursor);
                QCOMPARE(input->selectedText(), selection);
                QVERIFY(!input->isEnabled());
                QCOMPARE(view->shared_raster_cache_service(), cache);
                QVERIFY(!view->gameplay_timer.isActive());
                QVERIFY(!view->advance_gameplay_runtime(1000, answers));
                view->rasterization_busy_changed(false);
                QVERIFY(progress->isHidden());
                QCOMPARE(status->text(), view->gameplay_status_text());
            }
        }
    QVERIFY(view->resume_gameplay_runtime());
    view->stop_gameplay_clock();
    QVERIFY(status->text().contains(str_label("Running")));
    QVERIFY(input->isEnabled());
    QCOMPARE(view->gameplay_quiz_prompt({ 2 }), prompt);
    enter_owned_count(input, QString::number(owner.deck({ 2 })->running_count));
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(!owner.deck({ 2 })->quiz);
    QVERIFY(owner.statistics().verified_cards > 0);
    QCOMPARE(status->text(), view->gameplay_status_text());
    QVERIFY(view->finish_gameplay_runtime());
    const auto result = *owner.result();
    QVERIFY(!speed->isEnabled());
    QTest::keyClick(speed, Qt::Key_Right);
    QCOMPARE(owner.pick_interval_ms(), 303);
    QCOMPARE(*owner.result(), result);
    QVERIFY(!legacy_clock->is_active());
    // pick_interval() projects the active owner, not the dormant legacy field.
    QCOMPARE(view->pick_interval_ms, legacy_interval);
    QCOMPARE(load_trainer_preferences(), trainer);
    QCOMPARE(load_training_progress(), history);
    QCOMPARE(legacy_score.count(), 0);
    QCOMPARE(preparation.count(), 0);
}

void table_tests::hud_styles_preserve_finished_result_data() {
    QTest::addColumn<bool>("initial_instruments");
    QTest::addColumn<bool>("lives");
    for (const bool instruments : { false, true })
        for (const bool lives : { false, true })
            QTest::newRow(
                qPrintable(QStringLiteral("instruments=%1/lives=%2")
                               .arg(instruments)
                               .arg(lives))
            ) << instruments
              << lives;
}

void table_tests::hud_styles_preserve_finished_result() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop terminal HUD compatibility");
#endif
    QFETCH(bool, initial_instruments);
    QFETCH(bool, lives);
    launcher_preferences_guard preferences;
    hud_preferences_guard hud;
    auto ui = load_desktop_ui_preferences();
    ui.hud_override = initial_instruments ? desktop_hud_style::instruments
                                          : desktop_hud_style::classic;
    QVERIFY(save_desktop_ui_preferences(ui));
    main_window window;
    window.resize(1600, 1000);
    window.show();
    QCoreApplication::processEvents();
    if (auto* setup = window.findChild<QDialog*>())
        setup->reject();
    QVERIFY(QMetaObject::invokeMethod(&window, "on_settings_triggered"));
    auto* editor = hud_editor(window);
    QVERIFY(editor);
    editor->window()->hide();
    auto* choice
        = editor->findChild<QComboBox*>(QStringLiteral("desktop_ui_hud"));
    auto* view = window.findChild<table*>();
    auto* status = window.findChild<QLabel*>(QStringLiteral("session_status"));
    auto* speed
        = window.findChild<QSlider*>(QStringLiteral("pickup_interval_slider"));
    QVERIFY(view && status && speed && choice);
    set_gameplay_motion_style(*view, 0);
    auto fixture = owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1, 4 }
    );
    auto settings = fixture->configuration();
    settings.failure = lives ? gameplay::failure_policy::lives
                             : gameplay::failure_policy::block;
    settings.initial_lives = 1;
    QVERIFY(fixture->configure(settings));
    QVERIFY(view->install_gameplay_session(std::move(fixture)));
    auto& owner = *view->active_gameplay_session();
    QVERIFY(view->start_gameplay_runtime(84));
    view->stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view->advance_gameplay_runtime(300, answers));
    view->publish_gameplay_runtime(answers);
    auto prompt = view->gameplay_quiz_prompt({ 1 });
    QVERIFY(prompt && view->skip_gameplay_quiz(*prompt));
    prompt = view->gameplay_quiz_prompt({ 2 });
    QVERIFY(prompt && view->check_gameplay_quiz(*prompt));
    QCOMPARE(owner.statistics().skips, std::uint64_t { 1 });
    QCOMPARE(owner.statistics().errors, std::uint64_t { 1 });
    QCOMPARE(owner.phase(), gameplay::session_phase::running);
    // Zero Lives must still show unresolved peers before the batch settles.
    if (lives)
        QCOMPARE(owner.remaining_lives(), 0);
    for (const bool instruments :
         { !initial_instruments, initial_instruments }) {
        choice->setCurrentIndex(instruments ? 2 : 1);
        QVERIFY(editor->apply_theme_settings());
        QVERIFY(status->text().contains(str_label("Running")));
        QVERIFY(speed->isEnabled());
        if (lives) {
            QCOMPARE(
                status->text().count(
                    instruments ? QStringLiteral("♢") : QStringLiteral("♠")
                ),
                1
            );
            QCOMPARE(
                status->text().count(
                    instruments ? QStringLiteral("♦") : QStringLiteral("♥")
                ),
                0
            );
        } else {
            QCOMPARE(status->text(), view->gameplay_status_text());
        }
    }
    for (const std::size_t id : { std::size_t { 0 }, std::size_t { 3 } }) {
        prompt = view->gameplay_quiz_prompt({ id });
        QVERIFY(prompt);
        QVERIFY(
            view->edit_gameplay_quiz(*prompt, owner.deck({ id })->running_count)
        );
        QVERIFY(view->check_gameplay_quiz(*prompt));
    }
    if (!lives)
        QVERIFY(view->finish_gameplay_runtime());
    QCOMPARE(owner.phase(), gameplay::session_phase::finished);
    const auto result = *owner.result();
    const auto timing = owner.timing();
    const auto interval = owner.pick_interval_ms();
    auto* cache = view->shared_raster_cache_service();
    for (const bool instruments :
         { !initial_instruments, initial_instruments }) {
        choice->setCurrentIndex(instruments ? 2 : 1);
        QVERIFY(editor->apply_theme_settings());
        settle_hud();
        QVERIFY(status->text().contains(str_label("Finished")));
        QVERIFY(!status->text().contains(str_label("Quiz remaining")));
        QVERIFY(!speed->isEnabled());
        QTest::keyClick(speed, Qt::Key_Right);
        QCOMPARE(owner.pick_interval_ms(), interval);
        // A rejected programmatic edit must also re-project the accepted value.
        speed->setValue(interval + 10);
        QCOMPARE(speed->value(), interval);
        QCOMPARE(*owner.result(), result);
        QCOMPARE(owner.timing(), timing);
        QCOMPARE(view->shared_raster_cache_service(), cache);
        QVERIFY(!view->gameplay_timer.isActive());
        QVERIFY(!status->text().contains(QStringLiteral("%1")));
        QVERIFY(
            !status->text().contains(QStringLiteral("I18N_ARGUMENT_MISSING"))
        );
    }
    // HUD widgets outlive scenes; no previous result/life/speed leaks to setup.
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(1)));
    QCOMPARE(
        window.findChild<QLabel*>(QStringLiteral("session_status")), status
    );
    QCOMPARE(
        window.findChild<QSlider*>(QStringLiteral("pickup_interval_slider")),
        speed
    );
    QCOMPARE(status->text(), view->gameplay_status_text());
    QVERIFY(status->text().contains(str_label("Ready")));
    QVERIFY(!status->text().contains(str_label("Remaining lives")));
    QVERIFY(speed->isEnabled());
    QCOMPARE(speed->value(), 300);
    view->clear_gameplay_session();
    QVERIFY(status->text().contains(str_label("Ready")));
    QVERIFY(!status->text().contains(str_label("Verified")));
    QVERIFY(speed->isEnabled());
    QCOMPARE(speed->value(), view->pick_interval());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
