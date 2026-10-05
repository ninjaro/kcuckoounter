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
#include <QLineEdit>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <array>
#include <limits>

#include "settings/preferences_fixture.hpp"

using namespace preferences_test;

void preferences_tests::deck_setup_preserves_choices_shows_advisory_guidance() {
    const auto& catalog = strategy_repository();
    QVERIFY(catalog.is_valid());
    for (const auto& entry : catalog.strategies) {
        for (const auto count : { 1U, 32U }) {
            const gameplay::deck_configuration choice {
                entry.slug.toStdString(), count, true, true
            };
            auto owner = deck_setup_session(choice);
            QVERIFY(owner);
            gameplay::deck_setup_widget widget(*owner, { 0 }, catalog);
            QSignalSpy changed(
                &widget, &gameplay::deck_setup_widget::configuration_changed
            );
            QSignalSpy shown(
                &widget, &gameplay::deck_setup_widget::show_count_changed
            );
            auto* strategy = widget.findChild<QComboBox*>(
                QStringLiteral("gameplay_deck_strategy")
            );
            auto* decks = widget.findChild<QSpinBox*>(
                QStringLiteral("gameplay_deck_count")
            );
            auto* guidance = widget.findChild<QLabel*>(
                QStringLiteral("gameplay_deck_recommendation")
            );
            QVERIFY(strategy && decks && guidance);
            QCOMPARE(strategy->currentData().toString(), entry.slug);
            QCOMPARE(decks->minimum(), 1);
            QCOMPARE(decks->value(), static_cast<int>(count));
            QVERIFY(
                guidance->text().contains(QString::number(entry.min_decks))
            );
            QVERIFY(!guidance->text().contains(QStringLiteral("%1")));
            for (const auto* name :
                 { "gameplay_deck_strategy", "gameplay_deck_count",
                   "gameplay_deck_infinite", "gameplay_deck_training",
                   "gameplay_deck_show_count" }) {
                auto* control
                    = widget.findChild<QWidget*>(QString::fromLatin1(name));
                QVERIFY(control);
                QVERIFY(!control->accessibleName().isEmpty());
                QVERIFY(!control->toolTip().isEmpty());
            }
            const auto before = *owner->deck({ 0 });
            widget.refresh();
            QCOMPARE(*owner->deck({ 0 }), before);
            QCOMPARE(owner->deck({ 0 })->configuration, choice);
            QCOMPARE(changed.count(), 0);
            QCOMPARE(shown.count(), 0);
        }
    }
}

void preferences_tests::deck_setup_edits_only_the_selected_deck() {
    const auto& catalog = strategy_repository();
    auto owner = deck_setup_session();
    QVERIFY(owner);
    auto settings = owner->configuration();
    settings.scope = gameplay::quiz_scope::multi;
    QVERIFY(owner->configure(settings));
    QVERIFY(gameplay::prepare_generated_session(*owner, catalog, 5432));
    QVERIFY(!owner->table_quiz_targets().empty());
    const auto first = *owner->deck({ 0 });
    const auto second = *owner->deck({ 1 });
    const std::vector<std::uint64_t> targets {
        owner->table_quiz_targets().begin(), owner->table_quiz_targets().end()
    };
    gameplay::deck_setup_widget widget(*owner, { 0 }, catalog);
    QSignalSpy changed(
        &widget, &gameplay::deck_setup_widget::configuration_changed
    );
    auto* strategy = widget.findChild<QComboBox*>(
        QStringLiteral("gameplay_deck_strategy")
    );
    auto* decks
        = widget.findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"));
    auto* infinite = widget.findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_infinite")
    );
    auto* training = widget.findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_training")
    );
    QVERIFY(strategy && decks && infinite && training);
    widget.refresh();
    decks->setValue(1);
    strategy->setCurrentIndex(strategy->currentIndex());
    QCOMPARE(*owner->deck({ 0 }), first);
    QCOMPARE(*owner->deck({ 1 }), second);
    QVERIFY(std::ranges::equal(owner->table_quiz_targets(), targets));
    QCOMPARE(changed.count(), 0);
    decks->setValue(17); // not the legacy 16 or strategy minimum
    QCOMPARE(owner->deck({ 0 })->configuration.deck_count, 17U);
    QVERIFY(owner->deck({ 0 })->stream.cards.empty());
    QVERIFY(owner->table_quiz_targets().empty());
    QCOMPARE(*owner->deck({ 1 }), second);
    QCOMPARE(changed.count(), 1);
    strategy->setCurrentIndex(strategy->findData(QStringLiteral("ace_five")));
    QCOMPARE(
        owner->deck({ 0 })->configuration.strategy_slug, std::string("ace_five")
    );
    QCOMPARE(owner->deck({ 0 })->configuration.deck_count, 17U);
    QCOMPARE(changed.count(), 2);
    decks->setValue(1); // below the displayed recommendation of six
    QCOMPARE(owner->deck({ 0 })->configuration.deck_count, 1U);
    infinite->setChecked(true);
    QCOMPARE(owner->deck({ 0 })->configuration.infinite, true);
    QCOMPARE(changed.count(), 4);
    training->setChecked(false);
    QCOMPARE(owner->deck({ 0 })->configuration.training, false);
    QCOMPARE(changed.count(), 5);
    QCOMPARE(*owner->deck({ 1 }), second);
    QCOMPARE(owner->configuration(), settings);
    QVERIFY(gameplay::prepare_generated_session(*owner, catalog, 5432));
    const auto updated = *owner->deck({ 0 });
    widget.refresh();
    QCOMPARE(*owner->deck({ 0 }), updated);
    QCOMPARE(changed.count(), 5);
}

void preferences_tests::gameplay_deck_setup_fresh_defaults_remain_explicit() {
    const auto& catalog = strategy_repository();
    const std::array<gameplay::deck_configuration, 1> preset {
        gameplay::deck_configuration { "uston_ss", 17, true, true }
    };
    auto owner = gameplay::create_fresh_session(catalog, {}, preset, { 1, 64 });
    QVERIFY(owner);
    QCOMPARE(owner->deck({ 0 })->configuration.deck_count, 4U);
    QCOMPARE(preset[0].deck_count, 17U);
    gameplay::deck_setup_widget widget(*owner, { 0 }, catalog);
    QSignalSpy changed(
        &widget, &gameplay::deck_setup_widget::configuration_changed
    );
    auto* decks
        = widget.findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"));
    QTest::keyClick(decks, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(decks, QStringLiteral("1"));
    QCOMPARE(owner->deck({ 0 })->configuration.deck_count, 4U);
    QCOMPARE(changed.count(), 0); // no transient edits while typing
    QTest::keyClick(decks, Qt::Key_Return);
    QCOMPARE(changed.count(), 1);
    QVERIFY(gameplay::prepare_generated_session(*owner, catalog, 1234));
    QCOMPARE(owner->deck({ 0 })->running_count, -4);
    QVERIFY(owner->deck({ 0 })->configuration.infinite);
    QVERIFY(owner->deck({ 0 })->configuration.training);
    const auto before = *owner->deck({ 0 });
    widget.refresh();
    QCOMPARE(*owner->deck({ 0 }), before);
    QCOMPARE(decks->value(), 1);
    auto externally_edited = owner->deck({ 0 })->configuration;
    externally_edited.deck_count = 2;
    externally_edited.strategy_slug = "ace_five";
    QVERIFY(owner->configure_deck({ 0 }, externally_edited));
    widget.refresh();
    QCOMPARE(decks->value(), 2);
    QCOMPARE(owner->deck({ 0 })->configuration, externally_edited);
    QCOMPARE(changed.count(), 1); // reading an external edit is not a view edit

    auto stored = gameplay::session::create({}, { preset[0] }, { 1, 64 });
    QVERIFY(stored);
    gameplay::deck_setup_widget loaded(*stored, { 0 }, catalog);
    loaded.refresh();
    QCOMPARE(stored->deck({ 0 })->configuration, preset[0]);
    QCOMPARE(
        loaded.findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
            ->value(),
        17
    );
}

void preferences_tests::deck_setup_rejects_unavailable_and_invalid_choices() {
    const auto& catalog = strategy_repository();
    auto owner = deck_setup_session();
    QVERIFY(owner);
    QVERIFY(gameplay::prepare_generated_session(*owner, catalog, 9876));
    const auto before = *owner->deck({ 0 });
    gameplay::deck_setup_widget widget(*owner, { 0 }, catalog);
    QSignalSpy changed(
        &widget, &gameplay::deck_setup_widget::configuration_changed
    );
    auto* strategy = widget.findChild<QComboBox*>(
        QStringLiteral("gameplay_deck_strategy")
    );
    strategy->addItem(QStringLiteral("Unknown"), QStringLiteral("missing"));
    strategy->setCurrentIndex(strategy->count() - 1);
    QCOMPARE(*owner->deck({ 0 }), before);
    QCOMPARE(strategy->currentData().toString(), QStringLiteral("uston_ss"));
    strategy->setCurrentIndex(-1);
    QCOMPARE(*owner->deck({ 0 }), before);
    QCOMPARE(changed.count(), 0);
    strategy->setCurrentIndex(strategy->findData(QStringLiteral("uston_ss")));
    auto unavailable = catalog;
    unavailable.diagnostics.push_back(QStringLiteral("Bad formula"));
    gameplay::deck_setup_widget invalid_catalog(*owner, { 0 }, unavailable);
    auto* invalid_strategy = invalid_catalog.findChild<QComboBox*>(
        QStringLiteral("gameplay_deck_strategy")
    );
    QVERIFY(!invalid_strategy->isEnabled());
    QCOMPARE(invalid_strategy->currentIndex(), -1);
    invalid_catalog.findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
        ->setValue(2);
    invalid_catalog
        .findChild<QCheckBox*>(QStringLiteral("gameplay_deck_infinite"))
        ->setChecked(true);
    invalid_catalog.refresh();
    QCOMPARE(*owner->deck({ 0 }), before);
    QVERIFY(
        invalid_catalog
            .findChild<QLabel*>(QStringLiteral("gameplay_deck_recommendation"))
            ->toolTip()
            .contains(QStringLiteral("Bad formula"))
    );
    gameplay::deck_setup_widget invalid_id(*owner, { 99 }, catalog);
    for (const auto* name :
         { "gameplay_deck_strategy", "gameplay_deck_count",
           "gameplay_deck_infinite", "gameplay_deck_training",
           "gameplay_deck_show_count" })
        QVERIFY(!invalid_id.findChild<QWidget*>(QString::fromLatin1(name))
                     ->isEnabled());
    invalid_id
        .findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
        ->setChecked(true);
    QCOMPARE(*owner->deck({ 0 }), before);

    auto unknown = deck_setup_session({ "removed_strategy", 2, false, false });
    QVERIFY(unknown);
    const auto original = unknown->deck({ 0 })->configuration;
    gameplay::deck_setup_widget unknown_widget(*unknown, { 0 }, catalog);
    auto* unknown_strategy = unknown_widget.findChild<QComboBox*>(
        QStringLiteral("gameplay_deck_strategy")
    );
    QCOMPARE(unknown_strategy->currentIndex(), -1);
    QVERIFY(
        unknown_widget
            .findChild<QLabel*>(QStringLiteral("gameplay_deck_recommendation"))
            ->text()
            .contains(QStringLiteral("removed_strategy"))
    );
    unknown_widget.refresh();
    QCOMPARE(unknown->deck({ 0 })->configuration, original);
    unknown_strategy->setCurrentIndex(
        unknown_strategy->findData(QStringLiteral("hi_lo"))
    );
    QCOMPARE(
        unknown->deck({ 0 })->configuration.strategy_slug, std::string("hi_lo")
    );
    QCOMPARE(unknown->deck({ 0 })->configuration.deck_count, 2U);
}

void preferences_tests::gameplay_deck_setup_preserves_unrepresentable_counts() {
    const auto& catalog = strategy_repository();
    const auto large
        = static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1;
    auto owner = deck_setup_session({ "hi_lo", large, false, false });
    QVERIFY(owner);
    gameplay::deck_setup_widget widget(*owner, { 0 }, catalog);
    auto* decks
        = widget.findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"));
    QVERIFY(!decks->isEnabled());
    auto* guidance = widget.findChild<QLabel*>(
        QStringLiteral("gameplay_deck_recommendation")
    );
    QVERIFY(guidance->text().contains(
        QString::number(static_cast<qulonglong>(large))
    ));
    widget.findChild<QCheckBox*>(QStringLiteral("gameplay_deck_training"))
        ->setChecked(true);
    widget.findChild<QCheckBox*>(QStringLiteral("gameplay_deck_infinite"))
        ->setChecked(true);
    QCOMPARE(owner->deck({ 0 })->configuration.deck_count, large);
    QVERIFY(owner->deck({ 0 })->configuration.training);
    QVERIFY(owner->deck({ 0 })->configuration.infinite);
    decks->setValue(
        1
    ); // even programmatic changes cannot rewrite a clipped value
    widget.refresh();
    QCOMPARE(owner->deck({ 0 })->configuration.deck_count, large);
}

void preferences_tests::
    gameplay_deck_setup_locks_configuration_but_allows_paused_training_count() {
    const auto& catalog = strategy_repository();
    auto owner = deck_setup_session();
    QVERIFY(owner);
    QVERIFY(gameplay::prepare_generated_session(*owner, catalog, 4567));
    const std::array<std::size_t, 2> traversal { 0, 1 };
    QVERIFY(owner->set_traversal(traversal));
    gameplay::deck_setup_widget widget(*owner, { 0 }, catalog);
    QSignalSpy changed(
        &widget, &gameplay::deck_setup_widget::configuration_changed
    );
    QSignalSpy shown(&widget, &gameplay::deck_setup_widget::show_count_changed);
    auto* strategy = widget.findChild<QComboBox*>(
        QStringLiteral("gameplay_deck_strategy")
    );
    auto* decks
        = widget.findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"));
    auto* infinite = widget.findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_infinite")
    );
    auto* training = widget.findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_training")
    );
    auto* show = widget.findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QVERIFY(show->isEnabled());
    const auto prepared = *owner->deck({ 0 });
    QTest::keyClick(show, Qt::Key_Space);
    QVERIFY(owner->deck({ 0 })->show_count);
    QCOMPARE(shown.count(), 1);
    auto expected = prepared;
    expected.show_count = true;
    QCOMPARE(*owner->deck({ 0 }), expected);
    QVERIFY(owner->start()); // stale enabled controls must still be rejected
    decks->setValue(2);
    QCOMPARE(owner->deck({ 0 })->configuration, prepared.configuration);
    show->setChecked(false);
    QVERIFY(owner->deck({ 0 })->show_count);
    QCOMPARE(shown.count(), 1);
    QCOMPARE(changed.count(), 0);
    for (auto* control :
         std::array<QWidget*, 5> { strategy, decks, infinite, training, show })
        QVERIFY(!control->isEnabled());
    QVERIFY(owner->pause());
    widget.refresh();
    QVERIFY(show->isEnabled());
    for (auto* control :
         std::array<QWidget*, 4> { strategy, decks, infinite, training })
        QVERIFY(!control->isEnabled());
    const auto paused = *owner->deck({ 0 });
    strategy->setCurrentIndex(strategy->findData(QStringLiteral("hi_lo")));
    decks->setValue(3);
    infinite->setChecked(true);
    training->setChecked(false);
    QCOMPARE(*owner->deck({ 0 }), paused);
    QCOMPARE(changed.count(), 0);
    QTest::keyClick(show, Qt::Key_Space);
    auto paused_hidden = paused;
    paused_hidden.show_count = false;
    QCOMPARE(*owner->deck({ 0 }), paused_hidden);
    QCOMPARE(shown.count(), 2);
    gameplay::deck_setup_widget challenge(*owner, { 1 }, catalog);
    auto* challenge_show = challenge.findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    QVERIFY(challenge_show->isHidden());
    QVERIFY(!challenge_show->isEnabled());
    challenge_show->setChecked(true);
    QVERIFY(!owner->deck({ 1 })->show_count);
    QVERIFY(owner->resume());
    widget.refresh();
    QVERIFY(!show->isEnabled());
    QVERIFY(owner->finish());
    widget.refresh();
    const auto finished = *owner->deck({ 0 });
    show->setChecked(true);
    QCOMPARE(*owner->deck({ 0 }), finished);
    QCOMPARE(shown.count(), 2);
    QVERIFY(!show->isEnabled());
}

void preferences_tests::deck_setup_follows_identity_through_swap() {
    const auto& catalog = strategy_repository();
    auto owner = deck_setup_session();
    QVERIFY(owner);
    QVERIFY(gameplay::prepare_generated_session(*owner, catalog, 4321));
    QVERIFY(owner->set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    gameplay::deck_setup_widget widget(*owner, { 0 }, catalog);
    QSignalSpy shown(&widget, &gameplay::deck_setup_widget::show_count_changed);
    auto* show = widget.findChild<QCheckBox*>(
        QStringLiteral("gameplay_deck_show_count")
    );
    show->setChecked(true);
    const auto first = *owner->deck({ 0 });
    const auto second = *owner->deck({ 1 });
    QVERIFY(owner->start());
    QVERIFY(owner->pause());
    QVERIFY(owner->swap_decks({ 0 }, { 1 }));
    widget.refresh();
    QVERIFY(owner->slot_for({ 0 }) == gameplay::physical_slot_id { 1 });
    QCOMPARE(
        widget.findChild<QComboBox*>(QStringLiteral("gameplay_deck_strategy"))
            ->currentData()
            .toString(),
        QStringLiteral("uston_ss")
    );
    QVERIFY(show->isChecked());
    show->setChecked(false);
    auto expected = first;
    expected.show_count = false;
    QCOMPARE(*owner->deck({ 0 }), expected);
    QCOMPARE(*owner->deck({ 1 }), second);
    QCOMPARE(shown.count(), 2);

    // Converting Training to challenge in setup clears Show Count through the
    // domain, not a view-owned count/preferences flag.
    auto setup = deck_setup_session();
    QVERIFY(setup);
    gameplay::deck_setup_widget setup_widget(*setup, { 0 }, catalog);
    setup_widget
        .findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
        ->setChecked(true);
    setup_widget
        .findChild<QCheckBox*>(QStringLiteral("gameplay_deck_training"))
        ->setChecked(false);
    QVERIFY(!setup->deck({ 0 })->show_count);
    QVERIFY(
        !setup_widget
             .findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
             ->isChecked()
    );
}

void preferences_tests::deck_draft_stages_and_commits_before_notifications() {
    const auto& catalog = strategy_repository();
    auto owner = deck_setup_session();
    QVERIFY(owner);
    auto settings = owner->configuration();
    settings.scope = gameplay::quiz_scope::multi;
    QVERIFY(owner->configure(settings));
    QVERIFY(gameplay::prepare_generated_session(*owner, catalog, 5432));
    const auto first = *owner->deck({ 0 });
    const auto second = *owner->deck({ 1 });
    const std::vector<std::uint64_t> targets(
        owner->table_quiz_targets().begin(), owner->table_quiz_targets().end()
    );
    gameplay::deck_setup_widget draft(
        *owner, { 0 }, catalog, nullptr,
        gameplay::deck_setup_widget::edit_mode::staged
    );
    QSignalSpy changed(
        &draft, &gameplay::deck_setup_widget::configuration_changed
    );
    QSignalSpy shown(&draft, &gameplay::deck_setup_widget::show_count_changed);
    draft.findChild<QComboBox*>(QStringLiteral("gameplay_deck_strategy"))
        ->setCurrentIndex(
            draft
                .findChild<QComboBox*>(QStringLiteral("gameplay_deck_strategy"))
                ->findData(QStringLiteral("ace_five"))
        );
    draft.findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
        ->setValue(17);
    draft.findChild<QCheckBox*>(QStringLiteral("gameplay_deck_infinite"))
        ->setChecked(true);
    draft.findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
        ->setChecked(true);
    draft.refresh();
    QCOMPARE(*owner->deck({ 0 }), first);
    QCOMPARE(*owner->deck({ 1 }), second);
    QVERIFY(std::ranges::equal(owner->table_quiz_targets(), targets));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(shown.count(), 0);
    bool complete_at_notification = false;
    connect(
        &draft, &gameplay::deck_setup_widget::configuration_changed, &draft,
        [&] {
            const auto* state = owner->deck({ 0 });
            complete_at_notification
                = state->configuration.strategy_slug == "ace_five"
                && state->configuration.deck_count == 17
                && state->configuration.infinite && state->show_count
                && state->stream.cards.empty()
                && owner->table_quiz_targets().empty();
        }
    );
    QVERIFY(draft.apply_pending_changes());
    QVERIFY(complete_at_notification);
    QCOMPARE(*owner->deck({ 1 }), second);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(shown.count(), 1);
    QVERIFY(draft.apply_pending_changes());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(shown.count(), 1);
    QVERIFY(gameplay::prepare_generated_session(*owner, catalog, 5432));
    const auto prepared = *owner->deck({ 0 });
    draft.refresh();
    QCOMPARE(*owner->deck({ 0 }), prepared);
}

void preferences_tests::gameplay_deck_draft_count_only_preserves_preparation() {
    const auto& catalog = strategy_repository();
    auto owner = deck_setup_session();
    QVERIFY(owner);
    auto settings = owner->configuration();
    settings.scope = gameplay::quiz_scope::multi;
    QVERIFY(owner->configure(settings));
    QVERIFY(gameplay::prepare_generated_session(*owner, catalog, 3210));
    const auto original = *owner->deck({ 0 });
    const auto other = *owner->deck({ 1 });
    const std::vector<std::uint64_t> targets(
        owner->table_quiz_targets().begin(), owner->table_quiz_targets().end()
    );
    gameplay::deck_setup_widget draft(
        *owner, { 0 }, catalog, nullptr,
        gameplay::deck_setup_widget::edit_mode::staged
    );
    QSignalSpy changed(
        &draft, &gameplay::deck_setup_widget::configuration_changed
    );
    QSignalSpy shown(&draft, &gameplay::deck_setup_widget::show_count_changed);
    draft.findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
        ->setChecked(true);
    QCOMPARE(*owner->deck({ 0 }), original);
    QVERIFY(draft.apply_pending_changes());
    auto expected = original;
    expected.show_count = true;
    QCOMPARE(*owner->deck({ 0 }), expected);
    QCOMPARE(*owner->deck({ 1 }), other);
    QVERIFY(std::ranges::equal(owner->table_quiz_targets(), targets));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(shown.count(), 1);
    QVERIFY(draft.apply_pending_changes());
    QCOMPARE(shown.count(), 1);
    gameplay::deck_setup_widget immediate(*owner, { 0 }, catalog);
    QVERIFY(!immediate.apply_pending_changes());
}

void preferences_tests::deck_draft_rejects_stale_and_phase_changes() {
    const auto& catalog = strategy_repository();
    auto owner = deck_setup_session();
    QVERIFY(owner);
    gameplay::deck_setup_widget draft(
        *owner, { 0 }, catalog, nullptr,
        gameplay::deck_setup_widget::edit_mode::staged
    );
    draft.findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
        ->setValue(17);
    auto external = owner->deck({ 0 })->configuration;
    external.deck_count = 3;
    QVERIFY(owner->configure_deck({ 0 }, external));
    const auto updated = *owner->deck({ 0 });
    QVERIFY(!draft.pending_changes_current());
    QVERIFY(!draft.apply_pending_changes());
    draft.refresh();
    QVERIFY(!draft.findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
                 ->isEnabled());
    QCOMPARE(*owner->deck({ 0 }), updated);
    QVERIFY(gameplay::prepare_generated_session(*owner, catalog, 4321));
    QVERIFY(owner->set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    gameplay::deck_setup_widget setup(
        *owner, { 0 }, catalog, nullptr,
        gameplay::deck_setup_widget::edit_mode::staged
    );
    setup.findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
        ->setChecked(true);
    QVERIFY(owner->start());
    QVERIFY(!setup.apply_pending_changes());
    QVERIFY(!owner->deck({ 0 })->show_count);
    QVERIFY(owner->pause());
    gameplay::deck_setup_widget paused(
        *owner, { 0 }, catalog, nullptr,
        gameplay::deck_setup_widget::edit_mode::staged
    );
    paused.findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
        ->setChecked(true);
    QVERIFY(owner->set_show_count({ 0 }, true));
    QVERIFY(
        !paused.apply_pending_changes()
    ); // external Show Count invalidates too
    QVERIFY(owner->resume());
    QVERIFY(owner->finish());
    QVERIFY(!paused.apply_pending_changes());
}

void preferences_tests::deck_draft_interprets_text_preserves_large_counts() {
    const auto& catalog = strategy_repository();
    auto owner = deck_setup_session();
    QVERIFY(owner);
    gameplay::deck_setup_widget draft(
        *owner, { 0 }, catalog, nullptr,
        gameplay::deck_setup_widget::edit_mode::staged
    );
    auto* count
        = draft.findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"));
    auto* text = count->findChild<QLineEdit*>();
    QVERIFY(text);
    text->setText(QStringLiteral("17"));
    QCOMPARE(owner->deck({ 0 })->configuration.deck_count, 1U);
    QVERIFY(draft.apply_pending_changes());
    QCOMPARE(owner->deck({ 0 })->configuration.deck_count, 17U);
    const auto large
        = static_cast<std::size_t>(std::numeric_limits<int>::max()) + 15;
    auto huge = deck_setup_session({ "hi_lo", large, false, true });
    QVERIFY(huge);
    gameplay::deck_setup_widget huge_draft(
        *huge, { 0 }, catalog, nullptr,
        gameplay::deck_setup_widget::edit_mode::staged
    );
    QVERIFY(!huge_draft
                 .findChild<QSpinBox*>(QStringLiteral("gameplay_deck_count"))
                 ->isEnabled());
    huge_draft.findChild<QCheckBox*>(QStringLiteral("gameplay_deck_infinite"))
        ->setChecked(true);
    QVERIFY(huge_draft.apply_pending_changes());
    QCOMPARE(huge->deck({ 0 })->configuration.deck_count, large);
    QVERIFY(huge->deck({ 0 })->configuration.infinite);
}

void preferences_tests::deck_draft_follows_swap_and_training_conversion() {
    const auto& catalog = strategy_repository();
    auto owner = deck_setup_session();
    QVERIFY(owner);
    QVERIFY(gameplay::prepare_generated_session(*owner, catalog, 5432));
    QVERIFY(owner->set_traversal(std::array<std::size_t, 2> { 0, 1 }));
    QVERIFY(owner->start());
    QVERIFY(owner->pause());
    const auto original = *owner->deck({ 0 });
    const auto other = *owner->deck({ 1 });
    gameplay::deck_setup_widget draft(
        *owner, { 0 }, catalog, nullptr,
        gameplay::deck_setup_widget::edit_mode::staged
    );
    draft.findChild<QCheckBox*>(QStringLiteral("gameplay_deck_show_count"))
        ->setChecked(true);
    QVERIFY(owner->swap_decks({ 0 }, { 1 }));
    draft.refresh();
    QVERIFY(draft.pending_changes_current());
    QVERIFY(draft.apply_pending_changes());
    auto expected = original;
    expected.show_count = true;
    QCOMPARE(*owner->deck({ 0 }), expected);
    QCOMPARE(*owner->deck({ 1 }), other);
    auto setup = deck_setup_session();
    QVERIFY(setup && setup->set_show_count({ 0 }, true));
    gameplay::deck_setup_widget conversion(
        *setup, { 0 }, catalog, nullptr,
        gameplay::deck_setup_widget::edit_mode::staged
    );
    conversion.findChild<QCheckBox*>(QStringLiteral("gameplay_deck_training"))
        ->setChecked(false);
    QVERIFY(setup->deck({ 0 })->show_count); // no intermediate mutation
    QVERIFY(conversion.apply_pending_changes());
    QVERIFY(!setup->deck({ 0 })->show_count);
    QVERIFY(!setup->deck({ 0 })->configuration.training);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
