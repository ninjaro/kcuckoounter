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

#include <QMap>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "settings/preferences_fixture.hpp"

using namespace preferences_test;

void preferences_tests::default_suit_is_independent_persistent_appearance() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto settings = temporary_settings(directory);
    const auto catalog = test_catalog();
    preferences_service service(settings);
    const auto trainer = service.load(catalog);
    desktop_ui_preferences ui;
    QVERIFY(save_desktop_ui_preferences(settings, ui));
    settings.setValue(
        QStringLiteral("trainer_session/payload"), QByteArray("checkpoint")
    );
    settings.setValue(
        QStringLiteral("training_drills/document"), QByteArray("drills")
    );
    settings.setValue(
        QStringLiteral("training_progress/document"), QByteArray("progress")
    );
    settings.sync();
    const auto original_keys = settings.allKeys();
    QMap<QString, QVariant> original;
    for (const auto& key : original_keys)
        original.insert(key, settings.value(key));
    QCOMPARE(load_default_suit_preference(settings), 0);
    QVERIFY(!settings.contains(QStringLiteral("appearance/default_suit")));
    for (int suit = 0; suit < 4; ++suit) {
        QVERIFY(save_default_suit_preference(settings, suit));
        auto restored = temporary_settings(directory);
        QCOMPARE(load_default_suit_preference(restored), suit);
        // Trainer saves replace their own group, never this appearance key.
        service.save(trainer, catalog);
        ui.preset = desktop_ui_preset::quiet;
        ui.reset_overrides();
        QVERIFY(save_desktop_ui_preferences(settings, ui));
        QCOMPARE(load_default_suit_preference(settings), suit);
    }
    // Restore the UI selection, then check every unrelated value byte-for-byte.
    QVERIFY(save_desktop_ui_preferences(settings, desktop_ui_preferences {}));
    for (auto it = original.cbegin(); it != original.cend(); ++it)
        QCOMPARE(settings.value(it.key()), it.value());
}

void preferences_tests::
    default_suit_rejects_invalid_storage_without_rewriting() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto settings = temporary_settings(directory);
    const auto key = QStringLiteral("appearance/default_suit");
    for (const QVariant& value :
         { QVariant(-1), QVariant(4), QVariant(1.5), QVariant(true),
           QVariant(QStringLiteral("junk")),
           QVariant(QStringLiteral("9223372036854775807")) }) {
        settings.setValue(key, value);
        QCOMPARE(load_default_suit_preference(settings), 0);
        QCOMPARE(settings.value(key), value); // fallback is read-only
        QVERIFY(!save_default_suit_preference(settings, -1));
        QVERIFY(!save_default_suit_preference(settings, 4));
        QCOMPARE(settings.value(key), value);
    }
    QVERIFY(save_default_suit_preference(settings, 3));
    QCOMPARE(load_default_suit_preference(settings), 3);
}

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

void preferences_tests::strategy_id_repairs_renamed_or_removed_slug() {
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

void preferences_tests::shell_round_trip_preserves_qt_state() {
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
