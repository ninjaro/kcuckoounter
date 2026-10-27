#include <QToolBar>
#include <shared_ui/qt/answer_panel.hpp>
#include <shared_ui/qt/appearance_editor.hpp>
#include <shared_ui/qt/editor_panel.hpp>
#include <shared_ui/qt/presentation.hpp>
#include <shared_ui/qt/status_strip.hpp>
// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "card_helpers/card_sheet.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSettings>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QtTest/QtTest>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

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
    auto* details = slot->findChild<QAbstractButton*>(
        QStringLiteral("slot_details_button")
    );
    QVERIFY(trigger && details);
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
    QCOMPARE(owned_card(slot)->geometry(), slot->rect());
}

void table_tests::count_visibility_keeps_preparation_and_cache() {
    table view;
    view.resize(1400, 1000);
    view.set_settings_style(slot_settings_style::card);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    view.update_card_image_demands(true);
    QTRY_VERIFY_WITH_TIMEOUT(
        !view.is_rasterization_busy()
            && view.slot_widgets.front()->has_presented_card_images(),
        10000
    );
    auto& game = *view.active_gameplay_session();
    const auto original = *game.deck({ 0 });
    const auto cache_key = view.displayed_card_images;
    auto* slot = view.slot_widgets.front();
    const auto geometry = slot->geometry();
    QSettings preferences(
        QStringLiteral("kcuckoounter"), QStringLiteral("kcuckoounter")
    );
    QMap<QString, QVariant> stored;
    for (const auto& key : preferences.allKeys())
        stored.insert(key, preferences.value(key));
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    auto* details = slot->findChild<QAbstractButton*>(
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
    QCOMPARE(view.displayed_card_images, cache_key);
    QVERIFY(!view.card_images.is_pending());
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
    QCOMPARE(view.displayed_card_images, cache_key);
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
    const auto trainer = load_table_appearance_preferences();
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
    const auto image_demand
        = std::pair(view->desired_card_source, view->desired_card_size);
    auto* cache = table::shared_image_service();
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
    QCOMPARE(table::shared_image_service(), cache);
    QCOMPARE(
        std::pair(view->desired_card_source, view->desired_card_size),
        image_demand
    );
    QCOMPARE(load_table_appearance_preferences(), trainer);
    QVERIFY(view->finish_gameplay_runtime());
    QCOMPARE(status->text().count(QStringLiteral("♠")), 3);
    QVERIFY(save_desktop_ui_preferences(desktop_ui_preferences {}));
    settings_template_widget* appearance = nullptr;
    for (auto* editor : window.findChildren<settings_template_widget*>())
        if (editor->desktop_model())
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

void table_tests::material_contexts_are_window_local() {
    table first;
    table second;
    QVERIFY(first.install_gameplay_session(
        setup_table_fixture(static_cast<std::size_t>(1))
    ));
    QVERIFY(second.install_gameplay_session(
        setup_table_fixture(static_cast<std::size_t>(1))
    ));
    const auto before = *first.active_gameplay_session()->deck({ 0 });
    shared_ui::qt::apply_presentation(
        first,
        { shared_ui::qt::material::felt,
          shared_ui::qt::platform_profile::desktop_qt,
          { QColor("#74182e") } }
    );
    shared_ui::qt::apply_presentation(
        second,
        { shared_ui::qt::material::system,
          shared_ui::qt::platform_profile::desktop_qt,
          {} }
    );
    first.show();
    second.show();
    first.refresh_material();
    second.refresh_material();
    auto* slot = first.findChild<table_slot*>();
    QVERIFY(
        shared_ui::qt::presentation_for(*slot).value
        == shared_ui::qt::material::felt
    );
    QVERIFY(
        shared_ui::qt::presentation_for(second).value
        != shared_ui::qt::material::felt
    );
    QCOMPARE(
        shared_ui::qt::material_for(*slot, shared_ui::qt::material::felt)
            .surface,
        QColor("#74182e")
    );
    shared_ui::qt::apply_presentation(
        second,
        { shared_ui::qt::material::felt,
          shared_ui::qt::platform_profile::desktop_qt,
          { QColor("#163c71") } }
    );
    second.refresh_material();
    QCOMPARE(
        shared_ui::qt::material_for(*slot, shared_ui::qt::material::felt)
            .surface,
        QColor("#74182e")
    );
    QCOMPARE(
        shared_ui::qt::material_for(second, shared_ui::qt::material::felt)
            .surface,
        QColor("#163c71")
    );
    QCOMPARE(
        shared_ui::qt::presentation_for(*slot).context.felt_surface,
        QColor("#74182e")
    );
    QCOMPARE(
        shared_ui::qt::presentation_for(second).context.felt_surface,
        QColor("#163c71")
    );
    QCOMPARE(*first.active_gameplay_session()->deck({ 0 }), before);
}

void table_tests::table_palette_preferences_are_window_local() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop windows and settings previews");
#endif
    struct restore_preferences {
        table_appearance_preferences trainer
            = load_table_appearance_preferences();
        desktop_ui_preferences ui = load_desktop_ui_preferences();
        desktop_shell_state shell = load_desktop_shell_state();

        ~restore_preferences() {
            save_table_appearance_preferences(
                load_table_appearance_preferences(), trainer
            );
            static_cast<void>(save_desktop_ui_preferences(ui));
            save_desktop_shell_state(shell);
        }
    } guard;

    auto trainer = guard.trainer;
    trainer.palette = theme_palette_id::red;
    save_table_appearance_preferences(
        load_table_appearance_preferences(), trainer
    );
    QVERIFY(save_desktop_ui_preferences({}));
    main_window first;
    first.show();
    auto* first_table = first.findChild<table*>();
    QVERIFY(first_table);
    QTRY_VERIFY(first_table->active_gameplay_session());
    const auto* owner = first_table->active_gameplay_session();
    const auto configuration = owner->configuration();
    const auto interval = owner->pick_interval_ms();
    const auto deck_count = owner->size();
    const auto red
        = theme_palette_registry::option(theme_palette_id::red).base_color();
    const auto blue
        = theme_palette_registry::option(theme_palette_id::blue).base_color();
    const auto green
        = theme_palette_registry::option(theme_palette_id::green).base_color();
    QCOMPARE(
        shared_ui::qt::presentation_for(*first_table).context.felt_surface, red
    );
    trainer.palette = theme_palette_id::blue;
    save_table_appearance_preferences(
        load_table_appearance_preferences(), trainer
    );
    main_window second;
    second.show();
    auto* second_table = second.findChild<table*>();
    QVERIFY(second_table);
    QCOMPARE(
        shared_ui::qt::presentation_for(*second_table).context.felt_surface,
        blue
    );
    QCOMPARE(
        shared_ui::qt::presentation_for(*first_table).context.felt_surface, red
    );
    QVERIFY(
        QMetaObject::invokeMethod(
            &first, "on_settings_triggered", Qt::DirectConnection
        )
    );
    auto* draft = first.findChild<settings_shared_state*>();
    QVERIFY(draft);
    QCOMPARE(
        draft->table_color_index(),
        theme_palette_registry::index(theme_palette_id::red)
    );
    draft->set_table_color_index(
        theme_palette_registry::index(theme_palette_id::green)
    );
    // Editing a draft neither changes the live table nor the saved selection.
    QCOMPARE(
        shared_ui::qt::presentation_for(*first_table).context.felt_surface, red
    );
    QCOMPARE(
        load_table_appearance_preferences().palette, theme_palette_id::blue
    );
    settings_template_widget* editor = nullptr;
    for (auto* candidate : first.findChildren<settings_template_widget*>())
        if (candidate->desktop_model())
            editor = candidate;
    QVERIFY(editor);
    auto* pages = editor->window()->findChild<QTabWidget*>(
        QString(), Qt::FindDirectChildrenOnly
    );
    QVERIFY(pages);
    QCOMPARE(pages->count(), 3);
    QTRY_COMPARE(editor->window()->size(), QSize(820, 620));
    QCOMPARE(pages->tabText(1), str_label("Table and cards"));
    pages->setCurrentIndex(1);
    QCoreApplication::processEvents();
    QVERIFY(
        editor->findChild<QComboBox*>("orientation_combo_box")->isVisible()
    );
    QVERIFY(editor->findChild<QComboBox*>("default_suit")->isVisible());
    QVERIFY(editor->apply_theme_settings());
    QCOMPARE(
        load_table_appearance_preferences().palette, theme_palette_id::green
    );
    QCOMPARE(
        shared_ui::qt::presentation_for(*first_table).context.felt_surface,
        green
    );
    QCOMPARE(
        shared_ui::qt::presentation_for(*second_table).context.felt_surface,
        blue
    );
    QCOMPARE(
        shared_ui::qt::presentation_for(first).value,
        shared_ui::qt::material::system
    );
    QCOMPARE(
        shared_ui::qt::presentation_for(second).value,
        shared_ui::qt::material::system
    );
    QCOMPARE(first_table->active_gameplay_session(), owner);
    QCOMPARE(owner->configuration(), configuration);
    QCOMPARE(owner->pick_interval_ms(), interval);
    QCOMPARE(owner->size(), deck_count);
    QWidget preview_root;
    auto preview_presentation = shared_ui::qt::presentation_for(first);
    preview_presentation.value = shared_ui::qt::material::felt;
    preview_presentation.context.felt_surface = red;
    shared_ui::qt::apply_presentation(preview_root, preview_presentation);
    settings_template_widget preview(
        settings_tab_kind::appearance, &preview_root
    );
    auto* preview_draft = preview.findChild<settings_shared_state*>();
    QVERIFY(preview_draft);
    QCOMPARE(
        preview_draft->table_color_index(),
        theme_palette_registry::index(theme_palette_id::red)
    );
    QCOMPARE(
        load_table_appearance_preferences().palette, theme_palette_id::green
    );
    QCOMPARE(
        shared_ui::qt::presentation_for(*first_table).context.felt_surface,
        green
    );
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

void table_tests::appearance_previews_are_real_and_sample_only() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop Appearance editor");
#else
    const auto saved = load_desktop_ui_preferences();
    settings_template_widget form(settings_tab_kind::appearance);
    auto* editor = form.desktop_editor();
    auto* model = form.desktop_model();
    QVERIFY(editor && model);
    form.show();
    for (const QString& preset :
         { QStringLiteral("classic"), QStringLiteral("felt") }) {
        QVERIFY(model->choose_preset(preset));
        for (const auto& family : model->families()) {
            editor->select_family(family.id);
            QCoreApplication::processEvents();
            for (const auto& option : family.options) {
                QVERIFY(model->set_override(family.id, option.id));
                QCoreApplication::processEvents();
                if (family.id == "answer_entry" || family.id == "feedback") {
                    auto* sample
                        = editor->findChild<shared_ui::qt::answer_panel*>();
                    QVERIFY(sample);
                    for (auto* button :
                         sample->findChildren<QAbstractButton*>())
                        if (button->isEnabled())
                            button->click();
                    QVERIFY(!editor->findChild<shared_ui::qt::status_strip*>());
                } else if (family.id == "toolbar") {
                    QVERIFY(editor->findChild<QToolBar*>());
                } else if (family.id == "settings_surface") {
                    QVERIFY(editor->findChild<shared_ui::qt::editor_panel*>());
                } else if (family.id == "frame") {
                    QVERIFY(editor->findChild<card_widget*>());
                } else if (family.id == "hud" || family.id == "speed_readout") {
                    QVERIFY(editor->findChild<shared_ui::qt::status_strip*>());
                }
            }
        }
    }
    QCOMPARE(load_desktop_ui_preferences().appearance(), saved.appearance());
#endif
}
