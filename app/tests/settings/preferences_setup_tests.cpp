// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/preferences_tests.hpp"

#include "settings/preferences.hpp"
#include "settings/session_checkpoint.hpp"
#include "settings/strategy_data.hpp"
#include "settings/training_progress.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"

#include <QLabel>
#include <QSettings>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <array>

#include "settings/preferences_fixture.hpp"

using namespace preferences_test;

void preferences_tests::gameplay_setup_defaults_and_accessible_controls() {
    auto owner = setup_session();
    QVERIFY(owner);
    const auto before = owner->configuration();
    gameplay::setup_widget widget(*owner);
    QSignalSpy changed(&widget, &gameplay::setup_widget::configuration_changed);
    widget.refresh();
    QCOMPARE(owner->configuration(), before);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(widget.objectName(), QStringLiteral("gameplay_setup"));
    for (const auto* name :
         { "gameplay_failure_policy", "gameplay_initial_lives",
           "gameplay_allow_extra_slots", "gameplay_quiz_source",
           "gameplay_quiz_scope", "gameplay_global_pause",
           "gameplay_allow_skip", "gameplay_dealing_mode",
           "gameplay_sequential_count" }) {
        auto* control = widget.findChild<QWidget*>(QString::fromLatin1(name));
        QVERIFY(control);
        QVERIFY(!control->accessibleName().isEmpty());
        QVERIFY(!control->toolTip().isEmpty());
        QVERIFY(control->focusPolicy() != Qt::NoFocus);
    }
    const auto* lives
        = widget.findChild<QSpinBox*>(QStringLiteral("gameplay_initial_lives"));
    QCOMPARE(lives->value(), 3);
    const auto* pause
        = widget.findChild<QCheckBox*>(QStringLiteral("gameplay_global_pause"));
    QVERIFY(pause->isChecked());
    QVERIFY(!pause->isEnabled());
    QVERIFY(
        widget.findChild<QLabel*>(QStringLiteral("gameplay_pause_explanation"))
            ->wordWrap()
    );
    auto* skip
        = widget.findChild<QCheckBox*>(QStringLiteral("gameplay_allow_skip"));
    QTest::keyClick(skip, Qt::Key_Space);
    QVERIFY(!owner->configuration().allow_skip);
    QCOMPARE(changed.count(), 1);
}

void preferences_tests::
    gameplay_setup_loads_without_normalizing_configuration() {
    gameplay::session_configuration settings;
    settings.failure = gameplay::failure_policy::block;
    settings.initial_lives = 12;
    settings.source = gameplay::quiz_source::physical_joker;
    settings.scope = gameplay::quiz_scope::multi;
    settings.global_quiz_pause = false;
    settings.allow_skip = false;
    settings.dealing = gameplay::dealing_mode::simultaneous;
    settings.sequential_count = 3;
    settings.allow_extra_slots = true;
    auto owner = setup_session(5, settings);
    QVERIFY(owner);
    gameplay::setup_widget widget(*owner);
    QSignalSpy changed(&widget, &gameplay::setup_widget::configuration_changed);
    widget.refresh();
    QCOMPARE(owner->configuration(), settings);
    QCOMPARE(changed.count(), 0);
    const auto* lives
        = widget.findChild<QSpinBox*>(QStringLiteral("gameplay_initial_lives"));
    QCOMPARE(lives->value(), 12);
    QVERIFY(lives->isHidden());
    const auto* pause
        = widget.findChild<QCheckBox*>(QStringLiteral("gameplay_global_pause"));
    QVERIFY(pause->isEnabled());
    QVERIFY(!pause->isChecked());
    QCOMPARE(
        widget.findChild<QComboBox*>(QStringLiteral("gameplay_quiz_source"))
            ->currentData()
            .toInt(),
        static_cast<int>(settings.source)
    );
    QCOMPARE(
        widget.findChild<QComboBox*>(QStringLiteral("gameplay_quiz_scope"))
            ->currentData()
            .toInt(),
        static_cast<int>(settings.scope)
    );
    QVERIFY(!widget
                 .findChild<QCheckBox*>(QStringLiteral("gameplay_allow_skip"))
                 ->isChecked());
    QCOMPARE(
        widget
            .findChild<QSpinBox*>(QStringLiteral("gameplay_sequential_count"))
            ->value(),
        3
    );
    auto* mode
        = widget.findChild<QComboBox*>(QStringLiteral("gameplay_dealing_mode"));
    choose(mode, gameplay::dealing_mode::sequential);
    QCOMPARE(owner->configuration().sequential_count, std::size_t { 3 });
    QVERIFY(owner->configuration().allow_extra_slots);
    QCOMPARE(changed.count(), 1);
}

void preferences_tests::gameplay_setup_enforces_lives_pause_and_bounds() {
    auto settings = gameplay::session_configuration {};
    settings.failure = gameplay::failure_policy::block;
    settings.global_quiz_pause = false;
    auto owner = setup_session(4, settings);
    QVERIFY(owner);
    gameplay::setup_widget widget(*owner);
    auto* failure = widget.findChild<QComboBox*>(
        QStringLiteral("gameplay_failure_policy")
    );
    auto* lives
        = widget.findChild<QSpinBox*>(QStringLiteral("gameplay_initial_lives"));
    auto* pause
        = widget.findChild<QCheckBox*>(QStringLiteral("gameplay_global_pause"));
    QSignalSpy changed(&widget, &gameplay::setup_widget::configuration_changed);
    choose(failure, gameplay::failure_policy::lives);
    QCOMPARE(owner->configuration().failure, gameplay::failure_policy::lives);
    QVERIFY(owner->configuration().global_quiz_pause);
    QVERIFY(pause->isChecked());
    QVERIFY(!pause->isEnabled());
    QVERIFY(!lives->isHidden());
    QCOMPARE(changed.count(), 1);
    lives->setValue(0);
    QCOMPARE(owner->configuration().initial_lives, 1);
    lives->setValue(13);
    QCOMPARE(owner->configuration().initial_lives, 12);
    QCOMPARE(lives->minimum(), 1);
    QCOMPARE(lives->maximum(), 12);
    pause->setChecked(false); // even a programmatic edit cannot bypass Lives
    QVERIFY(owner->configuration().global_quiz_pause);
    QVERIFY(pause->isChecked());
    QCOMPARE(changed.count(), 3);
    choose(failure, gameplay::failure_policy::block);
    QVERIFY(lives->isHidden());
    QVERIFY(pause->isEnabled());
    QVERIFY(pause->isChecked()); // no automatic change to an explicit pause
    pause->setChecked(false);
    QVERIFY(!owner->configuration().global_quiz_pause);
    choose(failure, gameplay::failure_policy::lives);
    QCOMPARE(owner->configuration().initial_lives, 12);
    QVERIFY(owner->configuration().global_quiz_pause);
    QVERIFY(
        widget.findChild<QLabel*>(QStringLiteral("gameplay_pause_explanation"))
            ->text()
            .contains(QStringLiteral("Lives"))
    );
}

void preferences_tests::setup_keeps_source_scope_and_pause_independent() {
    auto settings = gameplay::session_configuration {};
    settings.failure = gameplay::failure_policy::block;
    settings.global_quiz_pause = false;
    auto owner = setup_session(4, settings);
    QVERIFY(owner);
    gameplay::setup_widget widget(*owner);
    auto* source
        = widget.findChild<QComboBox*>(QStringLiteral("gameplay_quiz_source"));
    auto* scope
        = widget.findChild<QComboBox*>(QStringLiteral("gameplay_quiz_scope"));
    auto* pause
        = widget.findChild<QCheckBox*>(QStringLiteral("gameplay_global_pause"));
    auto* skip
        = widget.findChild<QCheckBox*>(QStringLiteral("gameplay_allow_skip"));
    for (const auto source_value :
         { gameplay::quiz_source::physical_joker,
           gameplay::quiz_source::virtual_interrupt }) {
        choose(source, source_value);
        for (const auto scope_value :
             { gameplay::quiz_scope::multi, gameplay::quiz_scope::single }) {
            choose(scope, scope_value);
            QCOMPARE(owner->configuration().source, source_value);
            QCOMPARE(owner->configuration().scope, scope_value);
            QVERIFY(pause->isEnabled());
            QVERIFY(!pause->isChecked());
            QVERIFY(!owner->configuration().global_quiz_pause);
            pause->setChecked(true);
            QVERIFY(owner->configuration().global_quiz_pause);
            pause->setChecked(false);
            skip->setChecked(false);
            QVERIFY(!owner->configuration().allow_skip);
            skip->setChecked(true);
            QVERIFY(owner->configuration().allow_skip);
        }
    }
}

void preferences_tests::gameplay_setup_preserves_sequential_n_across_modes() {
    for (const auto count :
         { std::size_t { 1 }, std::size_t { 5 }, std::size_t { 24 } }) {
        auto owner = setup_session(count);
        QVERIFY(owner);
        gameplay::setup_widget widget(*owner);
        auto* n = widget.findChild<QSpinBox*>(
            QStringLiteral("gameplay_sequential_count")
        );
        auto* mode = widget.findChild<QComboBox*>(
            QStringLiteral("gameplay_dealing_mode")
        );
        QCOMPARE(n->minimum(), 1);
        QCOMPARE(n->maximum(), static_cast<int>(count));
        const auto labels = n->parentWidget()->findChildren<QLabel*>();
        QCOMPARE(labels.size(), 1);
        QVERIFY(labels.front()->text().contains(
            QString::number(static_cast<qulonglong>(count))
        ));
        const int desired = count == 1 ? 1 : (count == 5 ? 3 : 17);
        n->setValue(desired);
        QCOMPARE(
            owner->configuration().sequential_count,
            static_cast<std::size_t>(desired)
        );
        for (const auto value : { gameplay::dealing_mode::random,
                                  gameplay::dealing_mode::simultaneous }) {
            choose(mode, value);
            QVERIFY(!n->isEnabled());
            QVERIFY(n->parentWidget()->isHidden());
            n->setValue(1); // inactive values cannot overwrite the domain
            QCOMPARE(
                owner->configuration().sequential_count,
                static_cast<std::size_t>(desired)
            );
        }
        choose(mode, gameplay::dealing_mode::sequential);
        QVERIFY(n->isEnabled());
        QVERIFY(!n->parentWidget()->isHidden());
        QCOMPARE(n->value(), desired);
        n->setValue(static_cast<int>(count) + 1);
        QCOMPARE(owner->configuration().sequential_count, count);
    }
}

void preferences_tests::gameplay_setup_invalidates_only_through_the_domain() {
    auto owner = setup_session();
    QVERIFY(owner);
    QVERIFY(prepare_setup_session(*owner));
    const auto prepared = owner->deck({ 0 })->stream;
    gameplay::setup_widget widget(*owner);
    QSignalSpy changed(&widget, &gameplay::setup_widget::configuration_changed);
    widget.refresh();
    QCOMPARE(changed.count(), 0);
    QCOMPARE(owner->deck({ 0 })->stream, prepared);
    auto* source
        = widget.findChild<QComboBox*>(QStringLiteral("gameplay_quiz_source"));
    choose(source, gameplay::quiz_source::virtual_interrupt); // no-op
    QCOMPARE(changed.count(), 0);
    QCOMPARE(owner->deck({ 0 })->stream, prepared);
    widget.findChild<QSpinBox*>(QStringLiteral("gameplay_initial_lives"))
        ->setValue(7);
    QCOMPARE(owner->remaining_lives(), 7);
    QCOMPARE(owner->deck({ 0 })->stream, prepared);
    choose(source, gameplay::quiz_source::physical_joker);
    QCOMPARE(changed.count(), 2);
    for (std::size_t index = 0; index < owner->size(); ++index) {
        QVERIFY(owner->deck({ index })->stream.cards.empty());
        QCOMPARE(owner->deck({ index })->running_count, std::int64_t { 0 });
    }
    QVERIFY(prepare_setup_session(*owner));
    auto external = owner->configuration();
    external.allow_skip = false;
    QVERIFY(owner->configure(external));
    widget.refresh();
    QCOMPARE(changed.count(), 2);
    QCOMPARE(owner->deck({ 0 })->stream, prepared);
    QVERIFY(!widget
                 .findChild<QCheckBox*>(QStringLiteral("gameplay_allow_skip"))
                 ->isChecked());
    choose(
        widget.findChild<QComboBox*>(QStringLiteral("gameplay_quiz_scope")),
        gameplay::quiz_scope::multi
    );
    QVERIFY(owner->deck({ 0 })->stream.cards.empty());
    QCOMPARE(changed.count(), 3);
}

void preferences_tests::gameplay_setup_rejects_live_and_invalid_edits() {
    auto owner = setup_session();
    QVERIFY(owner);
    QVERIFY(prepare_setup_session(*owner));
    gameplay::setup_widget widget(*owner);
    QSignalSpy changed(&widget, &gameplay::setup_widget::configuration_changed);
    auto* failure = widget.findChild<QComboBox*>(
        QStringLiteral("gameplay_failure_policy")
    );
    const auto settings = owner->configuration();
    const auto prepared = owner->deck({ 0 })->stream;
    failure->setCurrentIndex(-1);
    QCOMPARE(owner->configuration(), settings);
    QVERIFY(failure->currentIndex() >= 0);
    QCOMPARE(owner->deck({ 0 })->stream, prepared);
    QVERIFY(owner->start());
    // External start may happen before refresh; the domain rejects the edit.
    choose(failure, gameplay::failure_policy::block);
    QCOMPARE(owner->configuration(), settings);
    for (const auto phase :
         { gameplay::session_phase::running, gameplay::session_phase::paused,
           gameplay::session_phase::finished }) {
        QCOMPARE(owner->phase(), phase);
        widget.refresh();
        for (auto* combo : widget.findChildren<QComboBox*>()) {
            QVERIFY(!combo->isEnabled());
            combo->setCurrentIndex(
                (combo->currentIndex() + 1) % combo->count()
            );
        }
        for (auto* spin : widget.findChildren<QSpinBox*>()) {
            QVERIFY(!spin->isEnabled());
            spin->setValue(2);
        }
        for (auto* check : widget.findChildren<QCheckBox*>()) {
            QVERIFY(!check->isEnabled());
            check->setChecked(!check->isChecked());
        }
        QCOMPARE(owner->configuration(), settings);
        QCOMPARE(owner->deck({ 0 })->stream, prepared);
        QCOMPARE(changed.count(), 0);
        if (phase == gameplay::session_phase::running)
            QVERIFY(owner->pause());
        if (phase == gameplay::session_phase::paused)
            QVERIFY(owner->finish());
    }
}

void preferences_tests::slot_recommendation_and_override_are_domain_owned() {
    auto configuration = gameplay::session_configuration {};
    configuration.sequential_count = 3;
    auto owner = setup_session(24, configuration);
    QVERIFY(owner);
    QVERIFY(prepare_setup_session(*owner));
    std::vector<gameplay::deck_state> before;
    for (std::size_t index = 0; index < owner->size(); ++index)
        before.push_back(*owner->deck({ index }));
    gameplay::setup_widget widget(*owner);
    QSignalSpy changed(&widget, &gameplay::setup_widget::configuration_changed);
    auto* override_control = widget.findChild<QCheckBox*>(
        QStringLiteral("gameplay_allow_extra_slots")
    );
    auto* recommendation = widget.findChild<QLabel*>(
        QStringLiteral("gameplay_slot_recommendation")
    );
    auto* warning = widget.findChild<QLabel*>(
        QStringLiteral("gameplay_slot_limit_warning")
    );
    QVERIFY(override_control && recommendation && warning);
    QVERIFY(widget.update_table_geometry({ 1000, 700 }, { 88, 63 }));
    QCOMPARE(owner->slot_constraints(), (gameplay::slot_limits { 18, 64 }));
    QVERIFY(recommendation->text().contains(QStringLiteral("18")));
    QVERIFY(recommendation->toolTip().contains(QStringLiteral("64")));
    QVERIFY(!warning->isHidden());
    QVERIFY(!override_control->isChecked());
    QCOMPARE(owner->configuration(), configuration);
    QCOMPARE(changed.count(), 0); // geometry never edits gameplay consent
    QCOMPARE(owner->size(), 24U);
    QCOMPARE(
        widget
            .findChild<QSpinBox*>(QStringLiteral("gameplay_sequential_count"))
            ->maximum(),
        24
    );
    for (std::size_t index = 0; index < owner->size(); ++index)
        QCOMPARE(*owner->deck({ index }), before[index]);
    QVERIFY(!owner->start());
    QTest::keyClick(override_control, Qt::Key_Space);
    QVERIFY(owner->configuration().allow_extra_slots);
    QCOMPARE(changed.count(), 1);
    override_control->setChecked(
        false
    ); // cannot turn off without first reducing M
    QVERIFY(override_control->isChecked());
    QVERIFY(owner->configuration().allow_extra_slots);
    QCOMPARE(changed.count(), 1);
    for (std::size_t index = 0; index < owner->size(); ++index)
        QCOMPARE(*owner->deck({ index }), before[index]);
    QVERIFY(owner->start());
    widget.refresh();
    QVERIFY(!override_control->isEnabled());
    override_control->setChecked(false);
    QVERIFY(override_control->isChecked());
    QCOMPARE(changed.count(), 1);
}

void preferences_tests::slot_resize_preserves_preparation_and_choice() {
    auto owner = setup_session();
    QVERIFY(owner);
    QVERIFY(prepare_setup_session(*owner));
    const auto configuration = owner->configuration();
    const auto prepared = *owner->deck({ 0 });
    gameplay::setup_widget widget(*owner);
    QSignalSpy changed(&widget, &gameplay::setup_widget::configuration_changed);
    auto* override_control = widget.findChild<QCheckBox*>(
        QStringLiteral("gameplay_allow_extra_slots")
    );
    auto* warning = widget.findChild<QLabel*>(
        QStringLiteral("gameplay_slot_limit_warning")
    );
    QVERIFY(widget.update_table_geometry({ 640, 480 }, { 88, 63 }));
    QCOMPARE(owner->slot_constraints().recommended, 6U);
    QVERIFY(warning->isHidden());
    QVERIFY(widget.update_table_geometry({ 80, 80 }, { 88, 63 }));
    QCOMPARE(owner->slot_constraints().recommended, 1U);
    QVERIFY(!warning->isHidden());
    QVERIFY(!warning->text().isEmpty());
    QVERIFY(!override_control->isChecked());
    QVERIFY(widget.update_table_geometry({ 1920, 1000 }, { 88, 63 }));
    QCOMPARE(owner->slot_constraints().recommended, 50U);
    QVERIFY(warning->isHidden());
    QCOMPARE(owner->configuration(), configuration);
    QCOMPARE(*owner->deck({ 0 }), prepared);
    QCOMPARE(changed.count(), 0);
    override_control->setChecked(true);
    QCOMPARE(changed.count(), 1);
    QVERIFY(widget.update_table_geometry({ 80, 80 }, { 88, 63 }));
    QVERIFY(override_control->isChecked());
    QVERIFY(owner->configuration().allow_extra_slots);
    QVERIFY(widget.update_table_geometry({ 1920, 1000 }, { 88, 63 }));
    QVERIFY(
        override_control->isChecked()
    ); // growth does not reset an explicit choice
    override_control->setChecked(false);
    QCOMPARE(owner->configuration(), configuration);
    QCOMPARE(*owner->deck({ 0 }), prepared);
    QCOMPARE(changed.count(), 2);
    const auto stored_bounds = owner->slot_constraints();
    QVERIFY(widget.update_table_geometry({ 1920, 1000 }, { 88, 63 })); // no-op
    QCOMPARE(owner->slot_constraints(), stored_bounds);
    QCOMPARE(changed.count(), 2);
    QVERIFY(owner->start());
}

void preferences_tests::slot_geometry_rejects_invalid_and_live_updates() {
    auto owner = setup_session();
    QVERIFY(owner);
    QVERIFY(prepare_setup_session(*owner));
    gameplay::setup_widget widget(*owner);
    const auto configuration = owner->configuration();
    const auto prepared = *owner->deck({ 0 });
    const auto bounds = owner->slot_constraints();
    auto* label = widget.findChild<QLabel*>(
        QStringLiteral("gameplay_slot_recommendation")
    );
    const auto description = label->text();
    for (const auto size :
         { packing::extent { 0, 700 }, { 1000, 0 }, { -1, 700 } }) {
        QVERIFY(!widget.update_table_geometry(size, { 88, 63 }));
        QCOMPARE(owner->slot_constraints(), bounds);
        QCOMPARE(owner->configuration(), configuration);
        QCOMPARE(*owner->deck({ 0 }), prepared);
        QCOMPARE(label->text(), description);
    }
    QVERIFY(!widget.update_table_geometry({ 1000, 700 }, { 0, 63 }));
    QVERIFY(owner->start());
    for (const auto phase :
         { gameplay::session_phase::running, gameplay::session_phase::paused,
           gameplay::session_phase::finished }) {
        QCOMPARE(owner->phase(), phase);
        QVERIFY(!widget.update_table_geometry({ 2560, 1400 }, { 88, 63 }));
        QCOMPARE(owner->slot_constraints(), bounds);
        QCOMPARE(owner->configuration(), configuration);
        QCOMPARE(*owner->deck({ 0 }), prepared);
        QCOMPARE(label->text(), description);
        if (phase == gameplay::session_phase::running)
            QVERIFY(owner->pause());
        if (phase == gameplay::session_phase::paused)
            QVERIFY(owner->finish());
    }
    // The generic domain may accept a caller's larger cap, but desktop policy
    // cannot silently adopt it or drop existing decks to satisfy its own cap.
    auto large = setup_session(65);
    QVERIFY(large);
    gameplay::setup_widget large_widget(*large);
    QVERIFY(!large_widget.update_table_geometry({ 2560, 1400 }, { 88, 63 }));
    QCOMPARE(large->size(), 65U);
    QCOMPARE(large->slot_constraints(), (gameplay::slot_limits { 65, 65 }));
}

void preferences_tests::gameplay_setup_does_not_write_legacy_storage() {
    QSettings settings(
        QStringLiteral("kcuckoounter"), QStringLiteral("kcuckoounter")
    );
    settings.clear();
    settings.setValue(
        QStringLiteral("trainer_session/payload"),
        QByteArray("legacy checkpoint")
    );
    settings.setValue(
        QStringLiteral("training_drills/document"), QByteArray("legacy drills")
    );
    settings.setValue(QStringLiteral("trainer_preferences/quiz_type"), 1);
    settings.sync();
    QVariantMap before;
    for (const auto& key : settings.allKeys())
        before.insert(key, settings.value(key));
    auto owner = setup_session();
    QVERIFY(owner);
    {
        gameplay::setup_widget widget(*owner);
        choose(
            widget.findChild<QComboBox*>(
                QStringLiteral("gameplay_failure_policy")
            ),
            gameplay::failure_policy::block
        );
        choose(
            widget.findChild<QComboBox*>(
                QStringLiteral("gameplay_quiz_source")
            ),
            gameplay::quiz_source::physical_joker
        );
        widget
            .findChild<QSpinBox*>(QStringLiteral("gameplay_sequential_count"))
            ->setValue(3);
        QVERIFY(widget.update_table_geometry({ 80, 80 }, { 88, 63 }));
        widget
            .findChild<QCheckBox*>(QStringLiteral("gameplay_allow_extra_slots"))
            ->setChecked(true);
        const std::array<gameplay::deck_configuration, 1> preset {
            gameplay::deck_configuration { "uston_ss", 17, false, true }
        };
        auto fresh = gameplay::create_fresh_session(
            strategy_repository(), {}, preset, { 1, 64 }
        );
        QVERIFY(fresh);
        gameplay::deck_setup_widget deck_widget(
            *fresh, { 0 }, strategy_repository()
        );
        deck_widget.findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
            ->setValue(1);
        deck_widget
            .findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
            ->setChecked(true);
        deck_widget.refresh();
    }
    settings.sync();
    QVariantMap after;
    for (const auto& key : settings.allKeys())
        after.insert(key, settings.value(key));
    QCOMPARE(after, before);
    settings.clear();
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
