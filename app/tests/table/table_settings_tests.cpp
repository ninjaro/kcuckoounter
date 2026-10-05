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
#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QtTest/QtTest>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::settings_surfaces_stage_changes_data() {
    QTest::addColumn<int>("style_id");
    QTest::addColumn<bool>("compact");
    QTest::addColumn<bool>("rotated");
    for (const auto style :
         { slot_settings_style::card, slot_settings_style::drawer,
           slot_settings_style::sill }) {
        for (const bool compact : { false, true }) {
            for (const bool rotated : { false, true }) {
                const auto name
                    = QStringLiteral("style=%1/compact=%2/rotated=%3")
                          .arg(static_cast<int>(style))
                          .arg(compact)
                          .arg(rotated)
                          .toLatin1();
                QTest::newRow(name.constData())
                    << static_cast<int>(style) << compact << rotated;
            }
        }
    }
}

void table_tests::settings_surfaces_stage_changes() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop settings surfaces");
#endif
    QFETCH(int, style_id);
    QFETCH(bool, compact);
    QFETCH(bool, rotated);
    const auto style = static_cast<slot_settings_style>(style_id);
    table_slot slot;
    slot.set_shared_card_faces_mode(true);
    slot.set_rotated(rotated);
    slot.resize(compact ? QSize(120, 85) : QSize(800, 600));
    slot.set_settings_style(style);
    slot.show();
    slot.start_quiz(0);
    for (int i = 0; i < 8; ++i)
        slot.advance_card();
    slot.set_paused(true);
    const auto original = slot.capture_session_state();
    auto* card = slot.findChild<card_widget*>();
    const auto card_rect = card->geometry();
    const auto raster_need = card->card_face_target_short_px();
    auto* live = slot.findChild<QFrame*>(QStringLiteral("settings_bar_frame"));
    QVERIFY(live && live->isHidden());
    auto* trigger
        = slot.findChild<QToolButton*>(QStringLiteral("compact_slot_controls"));
    auto* details = slot.findChild<BasePushButton*>(
        QStringLiteral("slot_details_button")
    );
    QVERIFY(trigger && details);
    QSignalSpy paused(&slot, &table_slot::dialog_opened);
    if (trigger->isVisible())
        QTest::mouseClick(trigger, Qt::LeftButton);
    QTest::mouseClick(details, Qt::LeftButton);
    auto* panel
        = slot.findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    QCOMPARE(paused.count(), 1);
    auto* draft = panel->findChild<slot_settings*>();
    auto* buttons = panel->findChild<QDialogButtonBox*>();
    QVERIFY(draft && buttons);
    for (int i = 0; i < 4; ++i)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QCOMPARE(slot.capture_session_state(), original);
    QCOMPARE(card->geometry(), card_rect);
    QCOMPARE(card->card_face_target_short_px(), raster_need);
    if (!compact) {
        QCOMPARE(panel->parentWidget(), &slot);
        QVERIFY(slot.rect().contains(panel->geometry()));
        if (style == slot_settings_style::drawer)
            QCOMPARE(panel->geometry().right(), slot.width() - 9);
        if (style == slot_settings_style::sill)
            QCOMPARE(panel->geometry().bottom(), slot.height() - 9);
    } else {
        QVERIFY(qobject_cast<QDialog*>(panel->parentWidget()));
    }
    QVERIFY(panel->rect().contains(
        QRect(buttons->mapTo(panel, QPoint()), buttons->size())
    ));
    QCOMPARE(draft->deck_count_spin_box()->minimum(), original.deck_count);
    draft->show_card_indexing()->setChecked(!original.show_card_indexing);
    draft->show_strategy_name()->setChecked(!original.show_strategy_name);
    QCOMPARE(
        slot.capture_session_state(), original
    ); // Draft is never a checkpoint.
    slot.set_settings_style(slot_settings_style::classic);
    QVERIFY(panel->isVisible());
    QCOMPARE(
        draft->show_card_indexing()->isChecked(), !original.show_card_indexing
    );
    slot.set_settings_style(style);
    slot.set_action_style(slot_action_style::rail);
    QCOMPARE(slot.capture_session_state(), original);
    slot.resize(
        80, 60
    ); // Promotion retains the actual editor and its unsaved values.
    auto* host = qobject_cast<QDialog*>(panel->parentWidget());
    QVERIFY(host && host->isVisible());
    QCOMPARE(panel->findChild<slot_settings*>(), draft);
    slot.resize(900, 700);
    QCOMPARE(
        panel->parentWidget(), host
    ); // Do not bounce an open window on resize.
    QTest::keyClick(host, Qt::Key_Escape);
    QVERIFY(!panel->isVisible());
    QCOMPARE(slot.capture_session_state(), original);
    QVERIFY(details->isVisible());
    QCOMPARE(slot.focusWidget(), details);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QTest::mouseClick(details, Qt::LeftButton);
    panel = slot.findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    draft = panel->findChild<slot_settings*>();
    draft->show_card_indexing()->setChecked(!original.show_card_indexing);
    draft->show_strategy_name()->setChecked(!original.show_strategy_name);
    buttons = panel->findChild<QDialogButtonBox*>();
    QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
    auto expected = original;
    expected.show_card_indexing = !original.show_card_indexing;
    expected.show_strategy_name = !original.show_strategy_name;
    QCOMPARE(slot.capture_session_state(), expected);
    QVERIFY(!panel->isVisible());
    QCOMPARE(card->geometry(), slot.rect());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // Inline Escape discards the draft and returns focus to Details too.
    QTest::mouseClick(details, Qt::LeftButton);
    panel = slot.findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    draft = panel->findChild<slot_settings*>();
    draft->show_card_indexing()->toggle();
    slot.activateWindow();
    draft->show_card_indexing()->setFocus();
    // Activate the window so the scoped Escape shortcut is dispatched.
    QTest::qWait(1);
    QTest::keyClick(draft->show_card_indexing(), Qt::Key_Escape);
    QVERIFY(!panel->isVisible());
    QCOMPARE(slot.capture_session_state(), expected);
    QCOMPARE(slot.focusWidget(), details);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // The shared editor preserves existing irreversible-in-session toggles.
    expected.training_mode = true;
    expected.infinity_enabled = true;
    expected.card.infinity_enabled = true;
    QVERIFY(slot.restore_session_state(expected));
    QTest::mouseClick(details, Qt::LeftButton);
    panel = slot.findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    draft = panel->findChild<slot_settings*>();
    QVERIFY(!draft->infinity_check_box()->isEnabled());
    QVERIFY(!draft->training_check_box()->isEnabled());
    QTest::mouseClick(
        panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel),
        Qt::LeftButton
    );
    QVERIFY(!panel->isVisible());
}

void table_tests::settings_editor_lifecycle_cancels_stale_drafts() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop settings surfaces");
#endif
    table view;
    view.resize(1000, 700);
    view.set_settings_style(slot_settings_style::drawer);
    view.set_slot_count(2);
    view.show();
    auto* slot = view.findChild<table_slot*>();
    slot->start_quiz(0);
    slot->set_paused(true);
    auto original = slot->capture_session_state();
    const auto open = [&]() -> QFrame* {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QMetaObject::invokeMethod(
            slot, "on_settings_button_clicked", Qt::DirectConnection
        );
        auto* panel
            = slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
        if (panel)
            panel->findChild<slot_settings*>()->show_card_indexing()->toggle();
        return panel;
    };
    auto* panel = open();
    QVERIFY(
        panel && panel->isVisible()
    ); // Newly created slots inherit presentation.
    slot->set_paused(false);
    QVERIFY(!panel->isVisible());
    QCOMPARE(
        slot->capture_session_state().show_card_indexing,
        original.show_card_indexing
    );
    slot->set_paused(true);
    panel = open();
    QVERIFY(panel && panel->isVisible());
    QVERIFY(slot->restore_session_state(original));
    QVERIFY(!panel->isVisible());
    QCOMPARE(slot->capture_session_state(), original);
    panel = open();
    QVERIFY(panel && panel->isVisible());
    table_slot source;
    source.set_shared_card_faces_mode(true);
    slot->apply_settings_from(source);
    QVERIFY(!panel->isVisible());
    panel = open();
    QVERIFY(panel && panel->isVisible());
    slot->start_quiz(0);
    QVERIFY(!panel->isVisible());
    for (int i = 0; i < 29; ++i)
        slot->advance_card();
    QVERIFY(slot->is_quiz_prompt_active());
    QVERIFY(open() == nullptr); // Settings cannot replace a question.
    slot->clear_quiz();
    panel = open();
    QVERIFY(panel && panel->isVisible());
    slot->resize(50, 40);
    QPointer<QDialog> host = qobject_cast<QDialog*>(panel->parentWidget());
    QVERIFY(host);
    view.set_slot_count(0);
    QVERIFY(host.isNull());
}

void table_tests::classic_settings_dialog_preserves_transaction() {
    table_slot slot;
    slot.set_shared_card_faces_mode(true);
    slot.resize(80, 60);
    slot.show();
    slot.start_quiz(0);
    slot.set_paused(true);
    const auto original = slot.capture_session_state();
    for (const bool accept : { false, true }) {
        bool inspected = false;
        QTimer::singleShot(0, &slot, [&] {
            auto* dialog
                = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto* editor = dialog->findChild<slot_settings*>();
            inspected = editor != nullptr;
            if (editor)
                editor->show_card_indexing()->toggle();
            dialog->done(accept ? QDialog::Accepted : QDialog::Rejected);
        });
        QVERIFY(
            QMetaObject::invokeMethod(
                &slot, "on_settings_button_clicked", Qt::DirectConnection
            )
        );
        QVERIFY(inspected);
        auto expected = original;
        if (accept)
            expected.show_card_indexing = !expected.show_card_indexing;
        QCOMPARE(slot.capture_session_state(), expected);
    }
}

void table_tests::gameplay_settings_surfaces_stage_changes_data() {
    QTest::addColumn<int>("settings_id");
    QTest::addColumn<int>("actions_id");
    QTest::addColumn<bool>("compact");
    QTest::addColumn<bool>("rotated");
    for (const auto style :
         { slot_settings_style::classic, slot_settings_style::card,
           slot_settings_style::drawer, slot_settings_style::sill })
        for (const auto actions :
             { slot_action_style::classic, slot_action_style::rail,
               slot_action_style::pills })
            for (const bool compact : { false, true })
                for (const bool rotated : { false, true }) {
                    const auto name
                        = QStringLiteral(
                              "settings=%1/actions=%2/compact=%3/rotated=%4"
                        )
                              .arg(static_cast<int>(style))
                              .arg(static_cast<int>(actions))
                              .arg(compact)
                              .arg(rotated)
                              .toLatin1();
                    QTest::newRow(name.constData())
                        << static_cast<int>(style) << static_cast<int>(actions)
                        << compact << rotated;
                }
}

void table_tests::gameplay_settings_surfaces_stage_changes() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop target settings hosts");
#endif
    QFETCH(int, settings_id);
    QFETCH(int, actions_id);
    QFETCH(bool, compact);
    QFETCH(bool, rotated);
    table view;
    const auto style = static_cast<slot_settings_style>(settings_id);
    view.set_settings_style(style);
    view.set_action_style(static_cast<slot_action_style>(actions_id));
    view.resize(compact ? QSize(120, 90) : QSize(1600, 1200));
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    auto& game = *view.active_gameplay_session();
    auto* slot = view.slot_widgets.front();
    slot->set_rotated(rotated);
    auto* trigger = slot->findChild<QToolButton*>(
        QStringLiteral("compact_slot_controls")
    );
    auto* details = slot->findChild<BasePushButton*>(
        QStringLiteral("slot_details_button")
    );
    QVERIFY(trigger && details);
    QSignalSpy legacy_pause(&view, &table::dialog_opened);
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    const auto original = *game.deck({ 0 });
    const auto geometry = slot->geometry();
    const auto need = slot->card_face_need_short_px();
    if (trigger->isVisible())
        QTest::mouseClick(trigger, Qt::LeftButton);
    QTest::mouseClick(details, Qt::LeftButton);
    auto* panel
        = slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    auto* draft = panel->findChild<gameplay::deck_setup_widget*>();
    auto* buttons = panel->findChild<QDialogButtonBox*>();
    QVERIFY(draft && buttons);
    QVERIFY(panel->findChildren<slot_settings*>().isEmpty());
    for (int i = 0; i < 4; ++i)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    if (!compact) {
        QCOMPARE(panel->parentWidget(), slot);
        QVERIFY(slot->rect().contains(panel->geometry()));
        if (style == slot_settings_style::drawer)
            QCOMPARE(panel->geometry().right(), slot->width() - 9);
        if (style == slot_settings_style::sill)
            QCOMPARE(panel->geometry().bottom(), slot->height() - 9);
    } else {
        QVERIFY(qobject_cast<QDialog*>(panel->parentWidget()));
    }
    draft->findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
        ->setValue(17);
    draft->findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
        ->setChecked(false);
    view.refresh_gameplay_session(); // refresh keeps pending values, not a live
                                     // edit
    QCOMPARE(
        draft->findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
            ->value(),
        17
    );
    QCOMPARE(*game.deck({ 0 }), original);
    QCOMPARE(slot->geometry(), geometry);
    QCOMPARE(slot->card_face_need_short_px(), need);
    QCOMPARE(preparation.count(), 0);
    QCOMPARE(legacy_pause.count(), 0);
    view.set_settings_style(slot_settings_style::classic);
    view.set_settings_style(style);
    slot->resize(80, 60);
    auto* host = qobject_cast<QDialog*>(panel->parentWidget());
    QVERIFY(host && host->isVisible());
    QCOMPARE(panel->findChild<gameplay::deck_setup_widget*>(), draft);
    slot->resize(1000, 1000);
    QCOMPARE(panel->parentWidget(), host);
    QTest::keyClick(host, Qt::Key_Escape);
    QVERIFY(!panel->isVisible());
    QCOMPARE(*game.deck({ 0 }), original);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QTest::mouseClick(details, Qt::LeftButton);
    panel = slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    draft = panel->findChild<gameplay::deck_setup_widget*>();
    draft->findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
        ->setValue(3);
    buttons = panel->findChild<QDialogButtonBox*>();
    QTest::mouseClick(
        buttons->button(QDialogButtonBox::Cancel), Qt::LeftButton
    );
    QCOMPARE(*game.deck({ 0 }), original);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QTest::mouseClick(details, Qt::LeftButton);
    panel = slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    draft = panel->findChild<gameplay::deck_setup_widget*>();
    auto* count
        = draft->findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"));
    count->findChild<QLineEdit*>()->setText(QStringLiteral("3"));
    buttons = panel->findChild<QDialogButtonBox*>();
    QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
    QVERIFY(!panel->isVisible());
    QCOMPARE(game.deck({ 0 })->configuration.deck_count, 3U);
    QVERIFY(game.deck({ 0 })->stream.cards.empty());
    QCOMPARE(game.deck({ 0 })->show_count, original.show_count);
    QCOMPARE(preparation.count(), 1);
    QCOMPARE(legacy_pause.count(), 0);
    QCOMPARE(owned_card(slot)->geometry(), slot->rect());
    QVERIFY(
        !slot->findChild<QWidget*>(QStringLiteral("quiz_spin_box"))->isEnabled()
    );
    QVERIFY(slot->findChild<BasePushButton*>(QStringLiteral("slot_copy_button"))
                ->isHidden());
    QVERIFY(slot->findChild<QFrame*>(QStringLiteral("settings_bar_frame"))
                ->isHidden());
}

void table_tests::count_visibility_keeps_preparation_and_cache() {
    table view;
    view.resize(1400, 1000);
    view.set_settings_style(slot_settings_style::card);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    view.prepare_cards_for_start();
    QTRY_VERIFY_WITH_TIMEOUT(
        !view.is_rasterization_busy()
            && view.slot_widgets.front()->has_shared_card_faces(),
        10000
    );
    auto& game = *view.active_gameplay_session();
    const auto original = *game.deck({ 0 });
    const auto cache_key = view.displayed_shared_faces_key;
    auto* slot = view.slot_widgets.front();
    const auto geometry = slot->geometry();
    QSettings preferences(
        QStringLiteral("kcuckoounter"), QStringLiteral("kcuckoounter")
    );
    QMap<QString, QVariant> stored;
    for (const auto& key : preferences.allKeys())
        stored.insert(key, preferences.value(key));
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    auto* details = slot->findChild<BasePushButton*>(
        QStringLiteral("slot_details_button")
    );
    QTest::mouseClick(details, Qt::LeftButton);
    auto* panel
        = slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    auto* show = panel->findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QVERIFY(show && show->isChecked());
    QTest::keyClick(show, Qt::Key_Space);
    QCOMPARE(*game.deck({ 0 }), original);
    QTest::mouseClick(
        panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),
        Qt::LeftButton
    );
    auto expected = original;
    expected.show_count = false;
    QCOMPARE(*game.deck({ 0 }), expected);
    QVERIFY(!owned_card(slot)->accessibleDescription().contains(
        QString::number(original.running_count)
    ));
    QCOMPARE(preparation.count(), 0);
    QCOMPARE(slot->geometry(), geometry);
    QCOMPARE(view.displayed_shared_faces_key, cache_key);
    QVERIFY(!view.shared_faces_watcher.isRunning());
    QVERIFY(view.rasterizing_slots.isEmpty());

    // The future launcher can prepare with its own seed/budgets. The table
    // requests preparation once after OK, not while typing or toggling hints.
    bool prepared = false;
    connect(&view, &table::gameplay_preparation_required, &view, [&] {
        prepared = gameplay::prepare_generated_session(
            *view.active_gameplay_session(), strategy_repository(), 707
        );
        view.refresh_gameplay_session();
    });
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::mouseClick(details, Qt::LeftButton);
    panel = slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    panel->findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
        ->setValue(2);
    QVERIFY(!prepared);
    QTest::mouseClick(
        panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),
        Qt::LeftButton
    );
    QVERIFY(prepared);
    QCOMPARE(preparation.count(), 1);
    QCOMPARE(game.deck({ 0 })->stream.cards.size(), std::size_t { 104 });
    QCOMPARE(view.displayed_shared_faces_key, cache_key);
    QCOMPARE(preferences.allKeys(), stored.keys());
    for (const auto& key : stored.keys())
        QCOMPARE(preferences.value(key), stored.value(key));
}

void table_tests::gameplay_settings_follow_deck_swap_and_repack() {
    table view;
    view.resize(1500, 1100);
    view.set_settings_style(slot_settings_style::drawer);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture()));
    auto& game = *view.active_gameplay_session();
    QVERIFY(game.start());
    QCOMPARE(game.deal_step().status, gameplay::dealing_step_status::advanced);
    QVERIFY(game.pause());
    view.refresh_gameplay_session();
    auto* slot = view.slot_widgets[0];
    QMetaObject::invokeMethod(
        slot, "on_settings_button_clicked", Qt::DirectConnection
    );
    auto* panel
        = slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    auto* draft = panel->findChild<gameplay::deck_setup_widget*>();
    auto* show = draft->findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QVERIFY(show && show->isEnabled());
    show->setChecked(false);
    const auto original = *game.deck({ 0 });
    const auto pending = *game.deck({ 2 });
    const auto before = slot->geometry();
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QVERIFY(slot->geometry() != before);
    view.resize(750, 1200);
    QCoreApplication::processEvents();
    QVERIFY(panel->isVisible());
    QCOMPARE(panel->findChild<gameplay::deck_setup_widget*>(), draft);
    QVERIFY(draft->pending_changes_current());
    QVERIFY(!show->isChecked());
    QTest::mouseClick(
        panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),
        Qt::LeftButton
    );
    auto expected = original;
    expected.show_count = false;
    QCOMPARE(*game.deck({ 0 }), expected);
    QCOMPARE(*game.deck({ 2 }), pending);
    QCOMPARE(
        slot->gameplay_deck_id(), std::optional { gameplay::deck_id { 0 } }
    );
}

void table_tests::gameplay_settings_discard_stale_and_replaced_views() {
    table view;
    view.resize(1200, 900);
    view.set_settings_style(slot_settings_style::sill);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    auto* slot = view.slot_widgets.front();
    const auto open = [&]() -> QFrame* {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QMetaObject::invokeMethod(
            slot, "on_settings_button_clicked", Qt::DirectConnection
        );
        return slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    };
    auto* panel = open();
    QVERIFY(panel && panel->isVisible());
    panel->findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
        ->setValue(17);
    auto& game = *view.active_gameplay_session();
    auto edited = game.deck({ 0 })->configuration;
    edited.deck_count = 3;
    QVERIFY(game.configure_deck({ 0 }, edited));
    view.refresh_gameplay_session();
    QVERIFY(!panel->isVisible());
    QCOMPARE(game.deck({ 0 })->configuration, edited);
    panel = open();
    QVERIFY(panel && panel->isVisible());
    panel->findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
        ->setChecked(false);
    QVERIFY(game.set_show_count({ 0 }, false));
    view.refresh_gameplay_session();
    QVERIFY(!panel->isVisible());
    panel = open();
    QVERIFY(panel && panel->isVisible());
    slot->resize(40, 40);
    QPointer<QWidget> host = panel->parentWidget();
    QPointer<gameplay::deck_setup_widget> draft
        = panel->findChild<gameplay::deck_setup_widget*>();
    QVERIFY(qobject_cast<QDialog*>(host));
    QVERIFY(view.install_gameplay_session(owned_table_fixture(2)));
    QVERIFY(host.isNull());
    QVERIFY(draft.isNull());
    slot = view.slot_widgets.front();
    panel = open();
    QVERIFY(panel && panel->isVisible());
    draft = panel->findChild<gameplay::deck_setup_widget*>();
    view.clear_gameplay_session();
    QVERIFY(draft.isNull());
}

void table_tests::
    gameplay_settings_pause_locks_and_preparation_notifications() {
    table view;
    view.resize(1200, 900);
    view.set_settings_style(slot_settings_style::card);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture()));
    auto& game = *view.active_gameplay_session();
    auto* slot = view.slot_widgets[0];
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    QMetaObject::invokeMethod(
        slot, "on_settings_button_clicked", Qt::DirectConnection
    );
    auto* panel
        = slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    panel->findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
        ->setValue(17);
    QVERIFY(game.start());
    view.refresh_gameplay_session();
    QVERIFY(!panel->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QMetaObject::invokeMethod(
        slot, "on_settings_button_clicked", Qt::DirectConnection
    );
    QVERIFY(!slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor")));
    QCOMPARE(game.deal_step().status, gameplay::dealing_step_status::advanced);
    QVERIFY(game.pause());
    view.refresh_gameplay_session();
    QMetaObject::invokeMethod(
        slot, "on_settings_button_clicked", Qt::DirectConnection
    );
    panel = slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    auto* count
        = panel->findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"));
    auto* show = panel->findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QVERIFY(count && !count->isEnabled() && show && show->isEnabled());
    count->setValue(17);
    QCOMPARE(count->value(), 1);
    show->setChecked(false);
    QVERIFY(game.resume());
    // Even before a host refresh, stale-enabled controls cannot commit.
    QTest::mouseClick(
        panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),
        Qt::LeftButton
    );
    QVERIFY(game.deck({ 0 })->show_count);
    QCOMPARE(preparation.count(), 0);
    QVERIFY(game.pause());
    view.refresh_gameplay_session();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto* pending_slot = view.slot_widgets[2];
    QMetaObject::invokeMethod(
        pending_slot, "on_settings_button_clicked", Qt::DirectConnection
    );
    panel = pending_slot->findChild<QFrame*>(
        QStringLiteral("slot_settings_editor")
    );
    QVERIFY(panel && panel->isVisible());
    QVERIFY(
        panel->findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
            ->isHidden()
    );
    const auto pending = *game.deck({ 2 });
    QVERIFY(game.finish());
    view.refresh_gameplay_session();
    QVERIFY(!panel->isVisible());
    QCOMPARE(*game.deck({ 2 }), pending);
    QCOMPARE(preparation.count(), 0);

    // A preparation observer may replace the scene synchronously. No second
    // hint signal or finishing callback may use the destroyed draft/slot.
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    slot = view.slot_widgets.front();
    QMetaObject::invokeMethod(
        slot, "on_settings_button_clicked", Qt::DirectConnection
    );
    panel = slot->findChild<QFrame*>(QStringLiteral("slot_settings_editor"));
    QVERIFY(panel && panel->isVisible());
    QPointer<gameplay::deck_setup_widget> draft
        = panel->findChild<gameplay::deck_setup_widget*>();
    draft->findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
        ->setValue(3);
    draft->findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
        ->setChecked(false);
    bool replaced = false;
    connect(&view, &table::gameplay_preparation_required, &view, [&] {
        replaced = view.install_gameplay_session(owned_table_fixture(2));
    });
    QTest::mouseClick(
        panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),
        Qt::LeftButton
    );
    QVERIFY(replaced);
    QVERIFY(draft.isNull());
    QCOMPARE(view.active_gameplay_session()->size(), std::size_t { 2 });
    QCOMPARE(preparation.count(), 1);
}

void table_tests::gameplay_lives_suit_settings_preserve_domain() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop owned Lives HUD");
#endif
    launcher_preferences_guard preferences;

    struct suit_guard {
        int suit = load_default_suit_preference();

        ~suit_guard() { (void)save_default_suit_preference(suit); }
    } guard;

    QVERIFY(save_default_suit_preference(1));
    auto ui = load_desktop_ui_preferences();
    ui.hud_override = desktop_hud_style::instruments;
    QVERIFY(save_desktop_ui_preferences(ui));
    const auto trainer = load_trainer_preferences();
    const auto progress = load_training_progress();
    main_window window;
    window.resize(1600, 1000);
    window.show();
    auto* view = window.findChild<table*>();
    auto fixture = owned_clock_fixture(4, true);
    auto settings = fixture->configuration();
    settings.failure = gameplay::failure_policy::lives;
    QVERIFY(fixture->configure(settings));
    QVERIFY(view->install_gameplay_session(std::move(fixture)));
    QVERIFY(view->start_gameplay_runtime(107));
    view->stop_gameplay_clock();
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_settings_triggered", Qt::DirectConnection
        )
    );
    QCOMPARE(view->gameplay_owner->phase(), gameplay::session_phase::paused);
    const auto configuration = view->gameplay_owner->configuration();
    const auto timing = view->gameplay_owner->timing();
    const auto statistics = view->gameplay_owner->statistics();
    const auto before = *view->gameplay_owner->deck({ 0 });
    const auto generation = view->next_shared_generation_id;
    auto* cache = view->shared_raster_cache_service();
    auto* status = window.findChild<QLabel*>(QStringLiteral("session_status"));
    auto* shared = window.findChild<settings_shared_state*>();
    QVERIFY(shared);
    const auto selectors
        = window.findChildren<QComboBox*>(QStringLiteral("default_suit"));
    QCOMPARE(selectors.size(), qsizetype { 2 });
    QCOMPARE(shared->default_suit(), 1);
    QSignalSpy changes(shared, &settings_shared_state::default_suit_changed);
    for (auto* selector : selectors) {
        QCOMPARE(selector->currentIndex(), 1);
        QVERIFY(!selector->accessibleName().isEmpty());
        QVERIFY(!selector->toolTip().isEmpty());
    }
    selectors.front()->setCurrentIndex(3);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(load_default_suit_preference(), 3);
    QCOMPARE(selectors.back()->currentIndex(), 3);
    QCOMPARE(status->text().count(QStringLiteral("♠")), 3);
    shared->set_default_suit(-1);
    shared->set_default_suit(4);
    shared->set_default_suit(3);
    QCOMPARE(changes.count(), 1);
    settings_shared_state reopened;
    QCOMPARE(reopened.default_suit(), 3);
    QCOMPARE(view->gameplay_owner->configuration(), configuration);
    QCOMPARE(view->gameplay_owner->timing(), timing);
    QCOMPARE(view->gameplay_owner->statistics(), statistics);
    QCOMPARE(*view->gameplay_owner->deck({ 0 }), before);
    QCOMPARE(view->shared_raster_cache_service(), cache);
    QCOMPARE(view->next_shared_generation_id, generation);
    QCOMPARE(load_trainer_preferences(), trainer);
    QCOMPARE(load_training_progress(), progress);
    QVERIFY(view->finish_gameplay_runtime());
    QCOMPARE(status->text().count(QStringLiteral("♠")), 3);
    QVERIFY(save_desktop_ui_preferences(desktop_ui_preferences {}));
    settings_template_widget* appearance = nullptr;
    for (auto* editor : window.findChildren<settings_template_widget*>())
        if (editor->findChild<QComboBox*>(QStringLiteral("desktop_ui_hud")))
            appearance = editor;
    QVERIFY(appearance);
    appearance->desktop_presentation_applied(); // the real shell connection
    QCOMPARE(status->text().count(QStringLiteral("♥")), 3);
    QCOMPARE(
        load_default_suit_preference(), 3
    ); // preset reset leaves suit alone
    QPointer<settings_shared_state> previous = shared;
    auto* dialog = qobject_cast<QDialog*>(shared->parent());
    QVERIFY(dialog);
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(previous.isNull());
    QVERIFY(
        QMetaObject::invokeMethod(
            &window, "on_settings_triggered", Qt::DirectConnection
        )
    );
    auto* reopened_shared = window.findChild<settings_shared_state*>();
    QVERIFY(reopened_shared);
    QCOMPARE(reopened_shared->default_suit(), 3);
    for (auto* selector :
         window.findChildren<QComboBox*>(QStringLiteral("default_suit")))
        QCOMPARE(selector->currentIndex(), 3);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
