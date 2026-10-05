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

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QtTest/QtTest>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::quiz_hides_skip_when_skipping_disabled() {
    table_slot slot;
    slot.start_quiz(0);
    slot.set_allow_skipping(false);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    QVERIFY(skip_button != nullptr);
    QVERIFY(!skip_button->isVisible());
}

void table_tests::quiz_training_mode_does_not_adjust_score() {
    table_slot slot;
    QSignalSpy score_spy(&slot, &table_slot::score_adjusted);

    auto training_check_box
        = slot.findChild<BaseCheckBox*>(QStringLiteral("training_check_box"));
    QVERIFY(training_check_box != nullptr);
    training_check_box->setChecked(true);

    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(score_spy.count(), 0);

    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    QVERIFY(skip_button != nullptr);
    skip_button->click();
    QCOMPARE(score_spy.count(), 0);
}

void table_tests::quiz_wrong_answer_exhausts_deck_without_training() {
    table_slot slot;
    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto answer_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_answer_button"));
    auto feedback_label
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    QVERIFY(spin_box != nullptr);
    QVERIFY(answer_button != nullptr);
    QVERIFY(feedback_label != nullptr);

    auto card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    const int expected = card->current_total_weight();
    const int provided = expected + 1;
    spin_box->setValue(provided);
    answer_button->click();

    const QString expected_message
        = str_label("You've set %1 while the correct answer is %2.")
              .arg(provided)
              .arg(expected);
    QCOMPARE(feedback_label->text(), expected_message);
    QVERIFY(slot.is_deck_exhausted());
}

void table_tests::quiz_wrong_answer_shows_continue_in_training() {
    table_slot slot;
    auto training_check_box
        = slot.findChild<BaseCheckBox*>(QStringLiteral("training_check_box"));
    QVERIFY(training_check_box != nullptr);
    training_check_box->setChecked(true);

    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto answer_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_answer_button"));
    auto feedback_label
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    auto continue_button = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    QVERIFY(spin_box != nullptr);
    QVERIFY(answer_button != nullptr);
    QVERIFY(feedback_label != nullptr);
    QVERIFY(continue_button != nullptr);

    auto card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    const int expected = card->current_total_weight();
    const int provided = expected + 2;
    spin_box->setValue(provided);
    answer_button->click();

    const QString expected_message
        = str_label("You've set %1 while the correct answer is %2.")
              .arg(provided)
              .arg(expected);
    QCOMPARE(feedback_label->text(), expected_message);
    QVERIFY(continue_button->isVisible());
    QVERIFY(!slot.is_deck_exhausted());
}

void table_tests::quiz_skip_shows_continue_feedback() {
    table_slot slot;
    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    auto feedback_label
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    auto continue_button = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    QVERIFY(spin_box != nullptr);
    QVERIFY(skip_button != nullptr);
    QVERIFY(feedback_label != nullptr);
    QVERIFY(continue_button != nullptr);

    auto card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    const int expected = card->current_total_weight();
    const int provided = expected + 3;
    spin_box->setValue(provided);
    skip_button->click();

    const QString expected_message
        = str_label("You've set %1 while the correct answer is %2.")
              .arg(provided)
              .arg(expected);
    QCOMPARE(feedback_label->text(), expected_message);
    QVERIFY(continue_button->isVisible());
}

void table_tests::quiz_spin_box_remembers_last_input() {
    table_slot slot;
    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    auto continue_button = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    QVERIFY(spin_box != nullptr);
    QVERIFY(skip_button != nullptr);
    QVERIFY(continue_button != nullptr);

    spin_box->setValue(7);
    skip_button->click();
    continue_button->click();

    for (int i = 0; i < 30; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(spin_box->value(), 7);
}

void table_tests::quiz_variants_preserve_count_semantics_data() {
    QTest::addColumn<bool>("chips");
    QTest::addColumn<bool>("stamp");
    QTest::addColumn<bool>("compact");
    for (const bool chips : { false, true }) {
        for (const bool stamp : { false, true }) {
            for (const bool compact : { false, true }) {
                const auto name = QStringLiteral("chips=%1/stamp=%2/compact=%3")
                                      .arg(chips)
                                      .arg(stamp)
                                      .arg(compact)
                                      .toLatin1();
                QTest::newRow(name.constData()) << chips << stamp << compact;
            }
        }
    }
}

void table_tests::quiz_variants_preserve_count_semantics() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop quiz variants; Android retains its existing controls");
#endif
    QFETCH(bool, chips);
    QFETCH(bool, stamp);
    QFETCH(bool, compact);
    table_slot slot;
    slot.set_shared_card_faces_mode(true);
    slot.resize(compact ? QSize(90, 65) : QSize(800, 600));
    slot.set_quiz_presentation(
        chips ? quiz_answer_style::chips : quiz_answer_style::numeric,
        stamp ? quiz_feedback_style::stamp : quiz_feedback_style::classic
    );
    slot.show();
    slot.start_quiz(0);
    auto* card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    card->set_strategy_weights(QVector<int>(13, 1));
    for (int i = 0; i < 29; ++i)
        slot.advance_card();
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(
        card->current_total_weight(), 30
    ); // Accumulated, not last-card +1.
    const auto question = slot.capture_session_state();
    const QRect card_bounds = card->geometry();
    const int raster_need = card->card_face_target_short_px();
    auto* input = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto* stepper
        = slot.findChild<QWidget*>(QStringLiteral("quiz_chip_stepper"));
    auto* add_two = slot.findChild<QPushButton*>(QStringLiteral("quiz_chip_2"));
    auto* subtract_two
        = slot.findChild<QPushButton*>(QStringLiteral("quiz_chip_-2"));
    auto* stamp_heading
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_stamp"));
    auto* feedback
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    auto* resume = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    auto* skip
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    QVERIFY(
        input && stepper && add_two && subtract_two && stamp_heading && feedback
        && resume && skip
    );
    const auto open_controls = [&] {
        auto* trigger = slot.findChild<QToolButton*>(
            QStringLiteral("compact_slot_controls")
        );
        if (trigger != nullptr && trigger->isVisible())
            QTest::mouseClick(trigger, Qt::LeftButton);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    };
    const auto restore_question = [&](bool training) {
        auto state = question;
        state.training_mode = training;
        const bool restored = slot.restore_session_state(state);
        card->set_strategy_weights(QVector<int>(13, 1));
        open_controls();
        return restored;
    };
    open_controls();
    QVERIFY(input->isVisible());
    QCOMPARE(stepper->isVisible(), chips);
    QVERIFY(
        !stamp_heading->isVisible()
    ); // No answer/result leakage during input.
    QSignalSpy scores(&slot, &table_slot::score_adjusted);
    if (chips) {
        input->setFocus();
        for (const int step : { -2, -1, 1, 2 }) {
            auto* button = slot.findChild<QPushButton*>(
                QStringLiteral("quiz_chip_%1").arg(step)
            );
            QVERIFY(button != nullptr);
            QTest::keyClick(input->window()->focusWidget(), Qt::Key_Tab);
            QCOMPARE(input->window()->focusWidget(), button);
            QVERIFY(!button->accessibleName().isEmpty());
        }
        input->setValue(9998);
        QTest::mouseClick(add_two, Qt::LeftButton);
        QCOMPARE(input->value(), 9999);
        QVERIFY(!add_two->isEnabled());
        input->setValue(-9998);
        QTest::keyClick(subtract_two, Qt::Key_Return);
        QCOMPARE(input->value(), -9999);
        QVERIFY(!subtract_two->isEnabled());
        input->setValue(28);
        QTest::keyClick(add_two, Qt::Key_Space);
        QCOMPARE(input->value(), 30);
    } else {
        input->setValue(30);
    }
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(scores.count(), 0);
    QKeyEvent repeated_enter(
        QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(), true
    );
    QCoreApplication::sendEvent(input, &repeated_enter);
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(scores.count(), 0);
    input->findChild<QLineEdit*>()->setText(QStringLiteral("30"));
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(!slot.is_quiz_prompt_active());
    QCOMPARE(scores.count(), 1);
    QCOMPARE(scores.at(0).at(0).toInt(), 1);
    QVERIFY(!card->is_deck_exhausted());

    QVERIFY(restore_question(true));
    scores.clear();
    input->setValue(1); // Last card's weight is deliberately the wrong answer.
    QTest::keyClick(input->findChild<QLineEdit*>(), Qt::Key_Enter);
    QVERIFY(slot.is_quiz_prompt_active());
    QVERIFY(feedback->isVisible());
    QCOMPARE(
        feedback->text(),
        str_label("You've set %1 while the correct answer is %2.")
            .arg(1)
            .arg(30)
    );
    for (int i = 0; i < 4; ++i)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QVERIFY2(
        feedback->height() >= feedback->heightForWidth(feedback->width()),
        "Wrapped correction must not be clipped"
    );
    QCOMPARE(stamp_heading->isVisible(), stamp);
    QVERIFY(resume->isVisible());
    QCOMPARE(scores.count(), 0);
    const auto feedback_state = slot.capture_session_state();
    slot.set_quiz_presentation(
        quiz_answer_style::numeric, quiz_feedback_style::classic
    );
    QCOMPARE(slot.capture_session_state(), feedback_state);
    QVERIFY(stamp_heading->isHidden());
    QVERIFY(feedback->styleSheet().isEmpty());
    slot.set_quiz_presentation(
        chips ? quiz_answer_style::chips : quiz_answer_style::numeric,
        stamp ? quiz_feedback_style::stamp : quiz_feedback_style::classic
    );
    QCOMPARE(slot.capture_session_state(), feedback_state);
    QCOMPARE(card->geometry(), card_bounds);
    QCOMPARE(card->card_face_target_short_px(), raster_need);
    QVERIFY(slot.restore_session_state(feedback_state));
    open_controls();
    for (int i = 0; i < 4; ++i)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QVERIFY(feedback->height() >= feedback->heightForWidth(feedback->width()));
    QCOMPARE(feedback->text(), feedback_state.quiz_feedback_text);
    QCOMPARE(stamp_heading->isVisible(), stamp);
    QTest::mouseClick(resume, Qt::LeftButton);
    QVERIFY(!slot.is_quiz_prompt_active());

    QVERIFY(restore_question(false));
    scores.clear();
    input->setValue(1);
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(slot.is_deck_exhausted());
    QVERIFY(!resume->isVisible());
    QCOMPARE(stamp_heading->isVisible(), stamp);
    QVERIFY(scores.isEmpty());
    // A queued/repeated submit cannot reinterpret already displayed feedback.
    QVERIFY(
        QMetaObject::invokeMethod(
            &slot, "on_quiz_answer_button_clicked", Qt::DirectConnection
        )
    );
    QCOMPARE(
        feedback->text(),
        str_label("You've set %1 while the correct answer is %2.")
            .arg(1)
            .arg(30)
    );

    QVERIFY(restore_question(false));
    slot.set_allow_skipping(false);
    QVERIFY(!skip->isVisible());
    QVERIFY(
        QMetaObject::invokeMethod(
            &slot, "on_quiz_skip_button_clicked", Qt::DirectConnection
        )
    );
    QVERIFY(!slot.capture_session_state().quiz_feedback_active);
    slot.set_allow_skipping(true);
    QTest::mouseClick(skip, Qt::LeftButton);
    QVERIFY(resume->isVisible());
    const auto skipped = slot.capture_session_state();
    QVERIFY(
        QMetaObject::invokeMethod(
            &slot, "on_quiz_skip_button_clicked", Qt::DirectConnection
        )
    );
    QCOMPARE(slot.capture_session_state(), skipped);
    QCOMPARE(scores.count(), 1);
}

void table_tests::quiz_presentation_reaches_existing_and_new_slots() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop quiz variants");
#endif
    table view;
    view.resize(800, 600);
    view.set_slot_count(1);
    view.show();
    auto* first = view.findChild<table_slot*>();
    QVERIFY(first != nullptr);
    first->start_quiz(0);
    for (int i = 0; i < 29; ++i)
        first->advance_card();
    const auto state = first->capture_session_state();
    view.set_quiz_presentation(
        quiz_answer_style::chips, quiz_feedback_style::stamp
    );
    QCOMPARE(first->capture_session_state(), state);
    for (const int count : { 4, 16, 64, 1 }) {
        view.set_slot_count(count);
        const auto slot_widgets = view.findChildren<table_slot*>();
        QCOMPARE(slot_widgets.size(), count);
        for (const QSize size : { QSize(800, 600), QSize(360, 640) }) {
            view.resize(size);
            QList<QRect> bounds;
            QList<int> demands;
            for (auto* slot : slot_widgets) {
                auto* chips = slot->findChild<QWidget*>(
                    QStringLiteral("quiz_chip_stepper")
                );
                QVERIFY(chips && !chips->isHidden());
                bounds.append(slot->geometry());
                demands.append(slot->card_face_need_short_px());
            }
            for (const bool alternative : { false, true }) {
                view.set_quiz_presentation(
                    alternative ? quiz_answer_style::chips
                                : quiz_answer_style::numeric,
                    alternative ? quiz_feedback_style::stamp
                                : quiz_feedback_style::classic
                );
                QCoreApplication::sendPostedEvents(
                    nullptr, QEvent::LayoutRequest
                );
                QCOMPARE(first->capture_session_state(), state);
                for (qsizetype i = 0; i < slot_widgets.size(); ++i) {
                    QCOMPARE(
                        slot_widgets[i]
                            ->findChild<QWidget*>(
                                QStringLiteral("quiz_chip_stepper")
                            )
                            ->isHidden(),
                        !alternative
                    );
                    QCOMPARE(slot_widgets[i]->geometry(), bounds[i]);
                    QCOMPARE(
                        slot_widgets[i]->findChild<card_widget*>()->geometry(),
                        slot_widgets[i]->rect()
                    );
                    QCOMPARE(
                        slot_widgets[i]->card_face_need_short_px(), demands[i]
                    );
                }
            }
        }
    }
}

void table_tests::quiz_presentation_editor_applies_and_resets() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop presentation editor");
#endif
    struct restore_preferences {
        desktop_ui_preferences ui = load_desktop_ui_preferences();
        trainer_preferences trainer = load_trainer_preferences();
        QColor color = theme_settings::base_color();
        QString source = card_sheet_source_path();

        ~restore_preferences() {
            static_cast<void>(save_desktop_ui_preferences(ui));
            save_trainer_preferences(trainer);
            theme_settings::set_base_color(color);
            set_card_sheet_source_path(source);
        }
    } guard;

    QVERIFY(save_desktop_ui_preferences(desktop_ui_preferences {}));
    table view;
    view.resize(800, 600);
    view.set_slot_count(1);
    auto* slot = view.findChild<table_slot*>();
    QVERIFY(slot != nullptr);
    slot->start_quiz(0);
    for (int i = 0; i < 29; ++i)
        slot->advance_card();
    const auto state = slot->capture_session_state();
    auto* chips
        = slot->findChild<QWidget*>(QStringLiteral("quiz_chip_stepper"));
    QVERIFY(chips != nullptr);
    settings_shared_state shared;
    settings_template_widget editor(
        settings_tab_kind::appearance, nullptr, QString(), &view, &shared
    );
    auto* answer = editor.findChild<QComboBox*>(
        QStringLiteral("desktop_ui_answer_entry")
    );
    auto* feedback
        = editor.findChild<QComboBox*>(QStringLiteral("desktop_ui_feedback"));
    auto* actions = editor.findChild<QComboBox*>(
        QStringLiteral("desktop_ui_slot_actions")
    );
    auto* surfaces = editor.findChild<QComboBox*>(
        QStringLiteral("desktop_ui_settings_surface")
    );
    auto* toolbar
        = editor.findChild<QComboBox*>(QStringLiteral("desktop_ui_toolbar"));
    auto* hud = editor.findChild<QComboBox*>(QStringLiteral("desktop_ui_hud"));
    auto* details = slot->findChild<BasePushButton*>(
        QStringLiteral("slot_details_button")
    );
    auto* reset
        = editor.findChild<QPushButton*>(QStringLiteral("desktop_ui_reset"));
    QVERIFY(
        answer && feedback && reset && actions && details && surfaces && toolbar
        && hud
    );
    answer->setCurrentIndex(2);
    feedback->setCurrentIndex(2);
    actions->setCurrentIndex(2);
    surfaces->setCurrentIndex(3);
    toolbar->setCurrentIndex(2);
    hud->setCurrentIndex(2);
    QVERIFY(!details->text().isEmpty());
    QVERIFY(chips->isHidden()); // Pending editor selection is not applied.
    editor.reset_theme_selection();
    QCOMPARE(answer->currentIndex(), 0);
    QCOMPARE(feedback->currentIndex(), 0);
    QCOMPARE(actions->currentIndex(), 0);
    QCOMPARE(surfaces->currentIndex(), 0);
    QCOMPARE(toolbar->currentIndex(), 0);
    QCOMPARE(hud->currentIndex(), 0);
    answer->setCurrentIndex(2);
    feedback->setCurrentIndex(2);
    actions->setCurrentIndex(2);
    surfaces->setCurrentIndex(3);
    toolbar->setCurrentIndex(2);
    hud->setCurrentIndex(2);
    QVERIFY(editor.apply_theme_settings());
    QCOMPARE(load_desktop_ui_preferences().answer(), quiz_answer_style::chips);
    QCOMPARE(
        load_desktop_ui_preferences().feedback(), quiz_feedback_style::stamp
    );
    QVERIFY(!chips->isHidden());
    QVERIFY(details->text().isEmpty());
    QCOMPARE(load_desktop_ui_preferences().actions(), slot_action_style::rail);
    QCOMPARE(
        load_desktop_ui_preferences().settings_surface(),
        slot_settings_style::drawer
    );
    QCOMPARE(
        load_desktop_ui_preferences().toolbar(), desktop_toolbar_style::compact
    );
    QCOMPARE(
        load_desktop_ui_preferences().hud(), desktop_hud_style::instruments
    );
    QCOMPARE(slot->capture_session_state(), state);
    answer->setCurrentIndex(1);
    feedback->setCurrentIndex(1);
    actions->setCurrentIndex(3);
    surfaces->setCurrentIndex(4);
    toolbar->setCurrentIndex(1);
    hud->setCurrentIndex(1);
    editor.reset_theme_selection();
    QCOMPARE(answer->currentIndex(), 2);
    QCOMPARE(feedback->currentIndex(), 2);
    QCOMPARE(actions->currentIndex(), 2);
    QCOMPARE(surfaces->currentIndex(), 3);
    QCOMPARE(toolbar->currentIndex(), 2);
    QCOMPARE(hud->currentIndex(), 2);
    reset->click();
    QCOMPARE(answer->currentIndex(), 0);
    QCOMPARE(feedback->currentIndex(), 0);
    QCOMPARE(actions->currentIndex(), 0);
    QCOMPARE(surfaces->currentIndex(), 0);
    QCOMPARE(toolbar->currentIndex(), 0);
    QCOMPARE(hud->currentIndex(), 0);
    QVERIFY(!chips->isHidden()); // Reset remains pending until Save.
    QVERIFY(editor.apply_theme_settings());
    QVERIFY(chips->isHidden());
    QVERIFY(!load_desktop_ui_preferences().answer_override);
    QVERIFY(!load_desktop_ui_preferences().feedback_override);
    QVERIFY(!load_desktop_ui_preferences().actions_override);
    QVERIFY(!load_desktop_ui_preferences().settings_override);
    QVERIFY(!load_desktop_ui_preferences().toolbar_override);
    QVERIFY(!load_desktop_ui_preferences().hud_override);
    QVERIFY(!details->text().isEmpty());
    QCOMPARE(slot->capture_session_state(), state);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
