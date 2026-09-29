// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/preferences_tests.hpp"

#include "settings/preferences.hpp"
#include "settings/session_checkpoint.hpp"
#include "settings/strategy_data.hpp"
#include "settings/training_progress.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

strategy_catalog test_catalog() {
    strategy_catalog catalog;

    strategy_data first;
    first.id = 11;
    first.slug = QStringLiteral("first_strategy");
    first.name = QStringLiteral("Translated first name");
    catalog.strategies.push_back(first);

    strategy_data second;
    second.id = 22;
    second.slug = QStringLiteral("renamed_strategy");
    second.name = QStringLiteral("Translated second name");
    catalog.strategies.push_back(second);
    return catalog;
}

QSettings temporary_settings(const QTemporaryDir& directory) {
    return { directory.filePath(QStringLiteral("preferences.ini")),
             QSettings::IniFormat };
}

trainer_session_checkpoint test_checkpoint() {
    table_slot_session_state slot;
    slot.card.cards_per_deck = 4;
    slot.card.decks_count = 2;
    slot.card.deck = { 0, 3, 2, 1, 2, 0, 1, 3 };
    slot.card.deck_position = 5;
    slot.deck_count = 2;
    slot.strategy_slug = QStringLiteral("first_strategy");
    slot.strategy_id = 11;
    slot.show_card_indexing = true;
    slot.quiz_prompt_active = true;
    slot.quiz_feedback_active = true;
    slot.quiz_continue_visible = true;
    slot.quiz_input_value = -3;
    slot.quiz_feedback_text = QStringLiteral("Try again");

    trainer_session_checkpoint checkpoint;
    checkpoint.table.slot_states.append(slot);
    checkpoint.table.pick_elapsed_ms = 175;
    checkpoint.table.quiz_running = true;
    checkpoint.table.quiz_paused = false;
    checkpoint.table.allow_skipping = false;
    checkpoint.table.dealing_mode = 1;
    checkpoint.score_correct = 3;
    checkpoint.score_total = 5;
    checkpoint.elapsed_ms = 12345;
    checkpoint.was_running = true;
    checkpoint.saved_at_utc_ms = 1700000000000LL;
    return checkpoint;
}

QJsonObject stored_checkpoint_root(QSettings& settings) {
    return QJsonDocument::fromJson(
               settings.value(QStringLiteral("trainer_session/payload"))
                   .toByteArray()
    )
        .object();
}

void store_checkpoint_root(QSettings& settings, const QJsonObject& root) {
    settings.setValue(
        QStringLiteral("trainer_session/payload"),
        QJsonDocument(root).toJson(QJsonDocument::Compact)
    );
    settings.sync();
}

} // namespace

void preferences_tests::round_trip_preserves_valid_values() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    preferences_service service(settings);
    const strategy_catalog catalog = test_catalog();

    trainer_preferences expected;
    expected.slot_count = 12;
    expected.quiz_type = 1;
    expected.wait_for_answers = true;
    expected.allow_skipping = false;
    expected.dealing_mode = 2;
    expected.pickup_interval_ms = 875;
    expected.palette = theme_palette_id::blue;
    expected.card_orientation = card_orientation_mode::horizontal;
    expected.preferred_strategy_slug = QStringLiteral("renamed_strategy");
    expected.preferred_strategy_id = 22;

    service.save(expected, catalog);
    QCOMPARE(service.load(catalog), expected);
    QVERIFY(
        !settings.contains(QStringLiteral("trainer_preferences/schema_version"))
    );
}

void preferences_tests::invalid_values_fall_back_independently() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    const strategy_catalog catalog = test_catalog();

    settings.beginGroup(QStringLiteral("trainer_preferences"));
    settings.setValue(QStringLiteral("setup/slot_count"), 0);
    settings.setValue(QStringLiteral("setup/quiz_type"), 4);
    settings.setValue(
        QStringLiteral("setup/wait_for_answers"), QStringLiteral("perhaps")
    );
    settings.setValue(
        QStringLiteral("setup/allow_skipping"), QStringLiteral("false")
    );
    settings.setValue(QStringLiteral("setup/dealing_mode"), -1);
    settings.setValue(QStringLiteral("setup/pickup_interval_ms"), 5000);
    settings.setValue(
        QStringLiteral("appearance/palette"), QStringLiteral("purple")
    );
    settings.setValue(
        QStringLiteral("appearance/card_orientation"),
        QStringLiteral("absolute")
    );
    settings.setValue(
        QStringLiteral("strategy/slug"),
        QStringLiteral("Translated second name")
    );
    settings.setValue(QStringLiteral("strategy/id"), -10);
    settings.endGroup();

    preferences_service service(settings);
    const trainer_preferences actual = service.load(catalog);
    const trainer_preferences defaults = preferences_service::defaults(catalog);
    QCOMPARE(actual.slot_count, defaults.slot_count);
    QCOMPARE(actual.quiz_type, defaults.quiz_type);
    QCOMPARE(actual.wait_for_answers, defaults.wait_for_answers);
    QCOMPARE(actual.allow_skipping, false);
    QCOMPARE(actual.dealing_mode, defaults.dealing_mode);
    QCOMPARE(actual.pickup_interval_ms, defaults.pickup_interval_ms);
    QCOMPARE(actual.palette, defaults.palette);
    QCOMPARE(actual.card_orientation, defaults.card_orientation);
    QCOMPARE(actual.preferred_strategy_slug, QStringLiteral("first_strategy"));
    QCOMPARE(actual.preferred_strategy_id, 11);
}

void preferences_tests::strategy_id_repairs_a_renamed_or_removed_slug() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    const strategy_catalog catalog = test_catalog();

    settings.beginGroup(QStringLiteral("trainer_preferences"));
    settings.setValue(
        QStringLiteral("strategy/slug"), QStringLiteral("old_strategy_slug")
    );
    settings.setValue(QStringLiteral("strategy/id"), 22);
    settings.endGroup();

    preferences_service service(settings);
    const trainer_preferences migrated = service.load(catalog);
    QCOMPARE(
        migrated.preferred_strategy_slug, QStringLiteral("renamed_strategy")
    );
    QCOMPARE(migrated.preferred_strategy_id, 22);
    QCOMPARE(
        settings.value(QStringLiteral("trainer_preferences/strategy/slug"))
            .toString(),
        QStringLiteral("renamed_strategy")
    );
}

void preferences_tests::desktop_shell_state_round_trip_preserves_qt_state() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    desktop_shell_state_service service(settings);

    const desktop_shell_state expected {
        .geometry = QByteArrayLiteral("qt-window-geometry"),
        .main_window_state = QByteArrayLiteral("qt-main-window-state"),
    };
    service.save(expected);

    QCOMPARE(service.load(), expected);
    QVERIFY(!settings.contains(QStringLiteral("desktop_shell/schema_version")));
    QVERIFY(!settings.contains(
        QStringLiteral("trainer_preferences/window/geometry")
    ));
}

void preferences_tests::
    session_checkpoint_round_trip_preserves_exact_progress() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    trainer_session_checkpoint_service service(settings);
    const trainer_session_checkpoint expected = test_checkpoint();

    QVERIFY(service.save(expected));
    const auto actual = service.load();
    QVERIFY(actual.has_value());
    QCOMPARE(*actual, expected);
}

void preferences_tests::
    session_checkpoint_rejects_duplicate_or_missing_cards() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    trainer_session_checkpoint_service service(settings);
    QVERIFY(service.save(test_checkpoint()));

    QJsonObject root = stored_checkpoint_root(settings);
    QJsonObject table_object = root.value(QStringLiteral("table")).toObject();
    QJsonArray slot_array
        = table_object.value(QStringLiteral("slots")).toArray();
    QJsonObject slot_object = slot_array.first().toObject();
    QJsonObject card_object
        = slot_object.value(QStringLiteral("card")).toObject();
    card_object.insert(
        QStringLiteral("deck"), QJsonArray { 0, 0, 0, 0, 0, 0, 0, 0 }
    );
    slot_object.insert(QStringLiteral("card"), card_object);
    slot_array[0] = slot_object;
    table_object.insert(QStringLiteral("slots"), slot_array);
    root.insert(QStringLiteral("table"), table_object);
    store_checkpoint_root(settings, root);

    QVERIFY(!service.load().has_value());
}

void preferences_tests::session_checkpoint_rejects_huge_numeric_values() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    trainer_session_checkpoint_service service(settings);
    QVERIFY(service.save(test_checkpoint()));

    QJsonObject root = stored_checkpoint_root(settings);
    root.insert(QStringLiteral("elapsed_ms"), 1e100);
    store_checkpoint_root(settings, root);
    QVERIFY(!service.load().has_value());
}

void preferences_tests::session_checkpoint_rejects_inconsistent_slot_state() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    trainer_session_checkpoint_service service(settings);
    QVERIFY(service.save(test_checkpoint()));

    QJsonObject root = stored_checkpoint_root(settings);
    QJsonObject table_object = root.value(QStringLiteral("table")).toObject();
    QJsonArray slot_array
        = table_object.value(QStringLiteral("slots")).toArray();
    QJsonObject slot_object = slot_array.first().toObject();
    slot_object.insert(QStringLiteral("infinity"), true);
    slot_array[0] = slot_object;
    table_object.insert(QStringLiteral("slots"), slot_array);
    root.insert(QStringLiteral("table"), table_object);
    store_checkpoint_root(settings, root);
    QVERIFY(!service.load().has_value());

    QVERIFY(service.save(test_checkpoint()));
    root = stored_checkpoint_root(settings);
    table_object = root.value(QStringLiteral("table")).toObject();
    slot_array = table_object.value(QStringLiteral("slots")).toArray();
    slot_object = slot_array.first().toObject();
    QJsonObject card_object
        = slot_object.value(QStringLiteral("card")).toObject();
    card_object.insert(QStringLiteral("infinity"), true);
    card_object.insert(QStringLiteral("position"), 8);
    slot_object.insert(QStringLiteral("infinity"), true);
    slot_object.insert(QStringLiteral("card"), card_object);
    slot_array[0] = slot_object;
    table_object.insert(QStringLiteral("slots"), slot_array);
    root.insert(QStringLiteral("table"), table_object);
    store_checkpoint_root(settings, root);
    QVERIFY(!service.load().has_value());
}

void preferences_tests::training_progress_is_bounded() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    training_progress_service service(settings);

    for (int index = 0; index < 25; ++index) {
        QVERIFY(service.record(
            {
                .correct = index % 10,
                .answered = 10,
                .elapsed_ms = 1000 + index,
                .completed_at_utc_ms = 1700000000000LL + index,
            }
        ));
    }
    const training_progress progress = service.load();
    QCOMPARE(progress.completed_sessions, 25);
    QCOMPARE(progress.answered_questions, 250);
    QCOMPARE(
        progress.recent_results.size(),
        training_progress::maximum_recent_results
    );
    QCOMPARE(
        progress.recent_results.first().completed_at_utc_ms, 1700000000024LL
    );
    QCOMPARE(progress.best_correct, 9);
    QCOMPARE(progress.best_answered, 10);
}

void preferences_tests::training_progress_rejects_huge_history_values() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    training_progress_service service(settings);
    QVERIFY(service.record(
        {
            .correct = 1,
            .answered = 2,
            .elapsed_ms = 100,
            .completed_at_utc_ms = 1700000000000LL,
        }
    ));

    settings.setValue(
        QStringLiteral("training_progress/recent_results"),
        QJsonDocument(
            QJsonArray { QJsonObject {
                { QStringLiteral("correct"), 1 },
                { QStringLiteral("answered"), 2 },
                { QStringLiteral("elapsed_ms"), 1e100 },
                { QStringLiteral("completed_at_utc_ms"), 1700000000000.0 },
            } }
        ).toJson(QJsonDocument::Compact)
    );
    settings.sync();
    QVERIFY(service.load().recent_results.isEmpty());
}

void preferences_tests::desktop_components_preserve_domain_settings() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("ui.ini"));
    QSettings settings(path, QSettings::IniFormat);
    const QStringList unrelated {
        QStringLiteral("trainer_preferences/setup/slot_count"),
        QStringLiteral("trainer_preferences/appearance/palette"),
        QStringLiteral("desktop_shell/window/geometry"),
        QStringLiteral("session_checkpoint/payload"),
        QStringLiteral("training_progress/payload")
    };
    for (const auto& key : unrelated) {
        settings.setValue(key, QByteArray("unchanged"));
    }
    QCOMPARE(load_desktop_ui_preferences(settings), desktop_ui_preferences {});
    QVERIFY(!settings.contains(QStringLiteral("desktop_ui/preset")));
    desktop_ui_preferences value;
    QCOMPARE(value.frame(), slot_frame_style::classic);
    QCOMPARE(value.answer(), quiz_answer_style::numeric);
    QCOMPARE(value.feedback(), quiz_feedback_style::classic);
    QCOMPARE(value.actions(), slot_action_style::classic);
    QCOMPARE(value.settings_surface(), slot_settings_style::classic);
    QCOMPARE(value.toolbar(), desktop_toolbar_style::classic);
    QCOMPARE(value.hud(), desktop_hud_style::classic);
    QVERIFY(value.show_speed_readout());
    value.preset = desktop_ui_preset::quiet;
    QCOMPARE(value.hud(), desktop_hud_style::classic);
    QCOMPARE(value.toolbar(), desktop_toolbar_style::classic);
    QCOMPARE(value.settings_surface(), slot_settings_style::classic);
    QCOMPARE(value.actions(), slot_action_style::classic);
    QCOMPARE(value.frame(), slot_frame_style::thin);
    QCOMPARE(value.answer(), quiz_answer_style::numeric);
    QCOMPARE(value.feedback(), quiz_feedback_style::classic);
    QVERIFY(!value.show_speed_readout());
    value.frame_override = slot_frame_style::classic;
    value.speed_readout_override = true;
    value.answer_override = quiz_answer_style::chips;
    value.feedback_override = quiz_feedback_style::stamp;
    value.actions_override = slot_action_style::rail;
    value.settings_override = slot_settings_style::drawer;
    value.toolbar_override = desktop_toolbar_style::compact;
    value.hud_override = desktop_hud_style::instruments;
    QVERIFY(save_desktop_ui_preferences(settings, value));
    QSettings reloaded(path, QSettings::IniFormat);
    QCOMPARE(load_desktop_ui_preferences(reloaded), value);
    QCOMPARE(
        load_desktop_ui_preferences(reloaded).hud(),
        desktop_hud_style::instruments
    );
    QCOMPARE(
        load_desktop_ui_preferences(reloaded).toolbar(),
        desktop_toolbar_style::compact
    );
    QCOMPARE(value.answer(), quiz_answer_style::chips);
    QCOMPARE(value.feedback(), quiz_feedback_style::stamp);
    QCOMPARE(value.actions(), slot_action_style::rail);
    for (const auto style :
         { slot_settings_style::card, slot_settings_style::drawer,
           slot_settings_style::sill }) {
        value.settings_override = style;
        QVERIFY(save_desktop_ui_preferences(settings, value));
        QCOMPARE(load_desktop_ui_preferences(settings), value);
        QCOMPARE(
            load_desktop_ui_preferences(settings).settings_surface(), style
        );
    }
    value.actions_override = slot_action_style::pills;
    QVERIFY(save_desktop_ui_preferences(settings, value));
    QCOMPARE(load_desktop_ui_preferences(settings), value);
    QCOMPARE(value.frame(), slot_frame_style::classic);
    QVERIFY(value.show_speed_readout());
    value.reset_overrides();
    QVERIFY(save_desktop_ui_preferences(settings, value));
    QCOMPARE(load_desktop_ui_preferences(settings), value);
    QVERIFY(!settings.contains(QStringLiteral("desktop_ui/overrides/hud")));
    QVERIFY(!settings.contains(QStringLiteral("desktop_ui/overrides/toolbar")));
    QVERIFY(!settings.contains(
        QStringLiteral("desktop_ui/overrides/settings_surface")
    ));
    QVERIFY(
        !settings.contains(QStringLiteral("desktop_ui/overrides/slot_actions"))
    );
    QVERIFY(!settings.contains(QStringLiteral("desktop_ui/overrides/frame")));
    QVERIFY(
        !settings.contains(QStringLiteral("desktop_ui/overrides/answer_entry"))
    );
    QVERIFY(
        !settings.contains(QStringLiteral("desktop_ui/overrides/feedback"))
    );
    QVERIFY(
        !settings.contains(QStringLiteral("desktop_ui/overrides/speed_readout"))
    );
    value.preset = desktop_ui_preset::classic;
    value.speed_readout_override = false;
    value.answer_override = quiz_answer_style::numeric;
    value.feedback_override = quiz_feedback_style::classic;
    value.actions_override = slot_action_style::classic;
    value.settings_override = slot_settings_style::classic;
    value.toolbar_override = desktop_toolbar_style::classic;
    value.hud_override = desktop_hud_style::classic;
    QVERIFY(save_desktop_ui_preferences(settings, value));
    QVERIFY(!load_desktop_ui_preferences(settings).show_speed_readout());
    QCOMPARE(load_desktop_ui_preferences(settings), value);
    settings.setValue(
        QStringLiteral("desktop_ui/overrides/answer_entry"),
        QStringLiteral("future")
    );
    settings.setValue(
        QStringLiteral("desktop_ui/overrides/feedback"),
        QStringLiteral("future")
    );
    settings.setValue(
        QStringLiteral("desktop_ui/overrides/slot_actions"),
        QStringLiteral("future")
    );
    settings.setValue(
        QStringLiteral("desktop_ui/overrides/settings_surface"),
        QStringLiteral("future")
    );
    settings.setValue(
        QStringLiteral("desktop_ui/overrides/toolbar"), QStringLiteral("future")
    );
    settings.setValue(
        QStringLiteral("desktop_ui/overrides/hud"), QStringLiteral("future")
    );
    settings.setValue(
        QStringLiteral("desktop_ui/preset"), QStringLiteral("future")
    );
    settings.setValue(
        QStringLiteral("desktop_ui/overrides/frame"), QStringLiteral("future")
    );
    settings.setValue(
        QStringLiteral("desktop_ui/overrides/speed_readout"),
        QStringLiteral("invalid")
    );
    QCOMPARE(load_desktop_ui_preferences(settings), desktop_ui_preferences {});
    for (const auto& key : unrelated) {
        QCOMPARE(settings.value(key).toByteArray(), QByteArray("unchanged"));
    }
    // A directory is not a writable INI file; the caller must see failure.
    QSettings unwritable(directory.path(), QSettings::IniFormat);
    QVERIFY(!save_desktop_ui_preferences(unwritable, value));
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
