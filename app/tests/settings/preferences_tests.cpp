// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/preferences_tests.hpp"

#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"

#include <QComboBox>
#include <QMap>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <shared_ui/qt/appearance_editor.hpp>

#include "settings/preferences_fixture.hpp"

using namespace preferences_test;

void preferences_tests::
    desktop_form_preserves_unknown_choices_and_unrelated_preferences() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop settings form; not Android device acceptance");
#else
    QSettings settings(
        "ninjaro", "kcuckoounter"
    ); // main_tests isolates both settings formats
    QMap<QString, QVariant> original;
    for (const auto& key : settings.allKeys())
        original.insert(key, settings.value(key));
    const auto restore = qScopeGuard([&] {
        settings.clear();
        for (auto it = original.cbegin(); it != original.cend(); ++it)
            settings.setValue(it.key(), it.value());
        settings.sync();
    });
    settings.remove("desktop_ui");
    settings.setValue("desktop_ui/preset", "future-preset");
    settings.setValue("desktop_ui/overrides/frame", "");
    settings.setValue("desktop_ui/overrides/feedback", "future-feedback");
    settings.setValue("desktop_ui/overrides/future_component", "future-value");
    settings.sync();
    settings_shared_state shared;
    settings_template_widget editor(
        settings_tab_kind::appearance, nullptr, {}, nullptr, &shared
    );
    editor.resize(900, 700);
    editor.show();
    QCoreApplication::processEvents();
    auto* model = editor.desktop_model();
    auto* shared_editor = editor.desktop_editor();
    QVERIFY(model && shared_editor);
    QCOMPARE(model->state().preset, QStringLiteral("future-preset"));
    QVERIFY(!model->fallbacks().isEmpty());
    QVERIFY(model->is_override("frame")); // Explicit empty is not inheritance.
    settings.setValue("trainer_preferences/future", QByteArray("keep trainer"));
    settings.setValue("unrelated/session", QByteArray("keep session"));
    settings.setValue("desktop_shell/geometry", QByteArray("keep geometry"));
    QMap<QString, QVariant> unrelated;
    for (const auto& key : settings.allKeys())
        if (!key.startsWith("desktop_ui/"))
            unrelated.insert(key, settings.value(key));
    QVERIFY(model->set_override("slot_actions", "rail"));
    shared_editor->command(shared_ui::qt::editor_command::save)->click();
    QVERIFY(!model->dirty());
    settings.sync();
    QCOMPARE(
        settings.value("desktop_ui/preset").toString(),
        QStringLiteral("future-preset")
    );
    QCOMPARE(
        settings.value("desktop_ui/overrides/feedback").toString(),
        QStringLiteral("future-feedback")
    );
    QVERIFY(settings.contains("desktop_ui/overrides/frame"));
    QCOMPARE(
        settings.value("desktop_ui/overrides/slot_actions").toString(),
        QStringLiteral("rail")
    );
    for (auto it = unrelated.cbegin(); it != unrelated.cend(); ++it)
        QCOMPARE(settings.value(it.key()), it.value());
    model->clear_override("frame");
    editor
        .reset_theme_selection(); // cancel draft; saved empty remains explicit
    QVERIFY(model->is_override("frame"));
    model->clear_override("frame");
    QVERIFY(editor.apply_theme_settings());
    QVERIFY(!settings.contains("desktop_ui/overrides/frame"));
    QVERIFY(model->choose_preset("classic"));
    QVERIFY(editor.apply_theme_settings());
    QCOMPARE(
        settings.value("desktop_ui/overrides/feedback").toString(),
        QStringLiteral("future-feedback")
    );
    QVERIFY(settings.contains("desktop_ui/overrides/future_component"));
    QVERIFY(!settings.contains("desktop_ui/overrides/slot_actions"));
    auto* reset = shared_editor->command(shared_ui::qt::editor_command::reset);
    QVERIFY(reset);
    reset->click();
    QVERIFY(editor.apply_theme_settings());
    // A generic reset preserves choices this version cannot offer.
    QCOMPARE(
        settings.value("desktop_ui/overrides/feedback").toString(),
        QStringLiteral("future-feedback")
    );
    QVERIFY(settings.contains("desktop_ui/overrides/future_component"));
    auto blank = model->state();
    blank.preset.clear();
    model->load(blank);
    QVERIFY(!model->fallbacks().isEmpty());
    QVERIFY(editor.apply_theme_settings());
    QCOMPARE(settings.value("desktop_ui/preset").toString(), QString());
#endif
}

void preferences_tests::desktop_unknown_choices_survive_unrelated_saves() {
    QTemporaryDir directory;
    auto settings = temporary_settings(directory);
    settings.setValue("desktop_ui/preset", "future-preset");
    const QStringList families { "frame",        "speed_readout",
                                 "answer_entry", "feedback",
                                 "slot_actions", "settings_surface",
                                 "toolbar",      "hud",
                                 "chrome",       "future_component" };
    for (const auto& family : families)
        settings.setValue("desktop_ui/overrides/" + family, "future-" + family);
    settings.setValue(
        "desktop_ui/overrides/future_typed", QStringList { "one", "two" }
    );
    const QMap<QString, QVariant> unrelated {
        { "trainer_preferences/future", QByteArray("trainer") },
        { "appearance/card_source", "artwork" },
        { "unrelated/session", QByteArray("session") },
        { "desktop_shell/geometry", QByteArray("geometry") },
        { "desktop_ui/future_metadata", 42 }
    };
    for (auto it = unrelated.cbegin(); it != unrelated.cend(); ++it)
        settings.setValue(it.key(), it.value());
    auto preferences = load_desktop_ui_preferences(settings);
    QCOMPARE(preferences.preset, desktop_ui_preset::classic);
    QCOMPARE(preferences.appearance().preset, QStringLiteral("future-preset"));
    QVERIFY(!preferences.frame_override);
    preferences.toolbar_override = desktop_toolbar_style::compact;
    QVERIFY(save_desktop_ui_preferences(settings, preferences));
    for (const auto& family : families) {
        QCOMPARE(
            settings.value("desktop_ui/overrides/" + family).toString(),
            family == "toolbar" ? QStringLiteral("compact") : "future-" + family
        );
    }
    QCOMPARE(
        settings.value("desktop_ui/preset").toString(),
        QStringLiteral("future-preset")
    );
    QCOMPARE(
        settings.value("desktop_ui/overrides/future_typed").toStringList(),
        QStringList({ "one", "two" })
    );
    auto state = load_desktop_ui_preferences(settings).appearance();
    state.overrides.remove("frame"); // explicit per-setting inheritance/reset
    state.overrides.insert("feedback", "classic"); // explicit, not inherited
    preferences = desktop_ui_preferences::from_appearance(state);
    QVERIFY(save_desktop_ui_preferences(settings, preferences));
    QVERIFY(!settings.contains("desktop_ui/overrides/frame"));
    QVERIFY(
        load_desktop_ui_preferences(settings).feedback_override.has_value()
    );
    QCOMPARE(
        settings.value("desktop_ui/overrides/feedback").toString(),
        QStringLiteral("classic")
    );
    preferences.set_preset(desktop_ui_preset::classic);
    QVERIFY(save_desktop_ui_preferences(settings, preferences));
    QCOMPARE(
        settings.value("desktop_ui/preset").toString(),
        QStringLiteral("classic")
    );
    preferences.reset_overrides();
    QVERIFY(save_desktop_ui_preferences(settings, preferences));
    for (const auto& key : settings.allKeys())
        QVERIFY(!key.startsWith("desktop_ui/overrides/"));
    for (auto it = unrelated.cbegin(); it != unrelated.cend(); ++it)
        QCOMPARE(settings.value(it.key()), it.value());

    // Editing a trainer appearance field must not canonicalize unrelated
    // setup fields or a future value of the other appearance field.
    settings.setValue("trainer_preferences/setup/slot_count", 1234);
    settings.setValue(
        "trainer_preferences/appearance/card_orientation", "future-orientation"
    );
    preferences_service service(settings);
    const auto before = service.load();
    auto after = before;
    after.palette = theme_palette_id::blue;
    service.save_appearance(before, after);
    QCOMPARE(
        settings.value("trainer_preferences/setup/slot_count").toInt(), 1234
    );
    QCOMPARE(
        settings.value("trainer_preferences/appearance/card_orientation")
            .toString(),
        QStringLiteral("future-orientation")
    );
    QCOMPARE(
        settings.value("trainer_preferences/appearance/palette").toString(),
        QStringLiteral("blue")
    );
}

void preferences_tests::default_suit_is_independent_persistent_appearance() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto settings = temporary_settings(directory);
    preferences_service service(settings);
    const auto trainer = service.load();
    desktop_ui_preferences ui;
    QVERIFY(save_desktop_ui_preferences(settings, ui));
    settings.setValue(
        QStringLiteral("unrelated/session"), QByteArray("checkpoint")
    );
    settings.setValue(
        QStringLiteral("unrelated/history"), QByteArray("progress")
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
        service.save_appearance(service.load(), trainer);
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
    table_appearance_preferences expected;
    expected.palette = theme_palette_id::blue;
    expected.card_orientation = card_orientation_mode::horizontal;
    settings.setValue("unrelated/data", QByteArray("keep"));
    service.save_appearance(service.load(), expected);
    QCOMPARE(service.load(), expected);
    QCOMPARE(
        settings.value("unrelated/data").toByteArray(), QByteArray("keep")
    );
    QVERIFY(!settings.contains("trainer_preferences/schema_version"));
}

void preferences_tests::invalid_values_fall_back_independently() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings = temporary_settings(directory);
    settings.setValue("trainer_preferences/appearance/palette", "purple");
    settings.setValue(
        "trainer_preferences/appearance/card_orientation", "absolute"
    );
    settings.setValue("unrelated/data", QByteArray("keep"));
    preferences_service service(settings);
    const auto before = service.load();
    QCOMPARE(before, table_appearance_preferences {});
    QCOMPARE(
        settings.value("trainer_preferences/appearance/palette").toString(),
        QString("purple")
    );
    auto after = before;
    after.card_orientation = card_orientation_mode::vertical;
    service.save_appearance(before, after);
    QCOMPARE(
        settings.value("trainer_preferences/appearance/palette").toString(),
        QString("purple")
    );
    QCOMPARE(service.load().card_orientation, card_orientation_mode::vertical);
    QCOMPARE(
        settings.value("unrelated/data").toByteArray(), QByteArray("keep")
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
        QStringLiteral("unrelated/checkpoint"),
        QStringLiteral("unrelated/history")
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
    const auto unsupported = load_desktop_ui_preferences(settings);
    QCOMPARE(unsupported.preset, desktop_ui_preset::classic);
    QVERIFY(!unsupported.frame_override);
    QCOMPARE(unsupported.appearance().preset, QStringLiteral("future"));
    for (const auto& key : unrelated) {
        QCOMPARE(settings.value(key).toByteArray(), QByteArray("unchanged"));
    }
    // A directory is not a writable INI file; the caller must see failure.
    QSettings unwritable(directory.path(), QSettings::IniFormat);
    QVERIFY(!save_desktop_ui_preferences(unwritable, value));
}

void preferences_tests::desktop_felt_preset_and_chrome_round_trip() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("felt.ini"));
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(
        QStringLiteral("trainer_preferences/setup/slot_count"), 7
    );
    desktop_ui_preferences value;
    QCOMPARE(value.chrome(), desktop_chrome_style::native);
    value.preset = desktop_ui_preset::quiet;
    QCOMPARE(value.chrome(), desktop_chrome_style::native);
    // Felt fills presentation defaults only; it is not a speed policy.
    value.preset = desktop_ui_preset::felt;
    QCOMPARE(value.frame(), slot_frame_style::piping);
    QCOMPARE(value.answer(), quiz_answer_style::numeric);
    QCOMPARE(value.feedback(), quiz_feedback_style::stamp);
    QCOMPARE(value.actions(), slot_action_style::pills);
    QCOMPARE(value.settings_surface(), slot_settings_style::card);
    QCOMPARE(value.toolbar(), desktop_toolbar_style::classic);
    QCOMPARE(value.hud(), desktop_hud_style::instruments);
    QCOMPARE(value.chrome(), desktop_chrome_style::felt);
    QVERIFY(value.show_speed_readout());
    QVERIFY(save_desktop_ui_preferences(settings, value));
    QCOMPARE(
        settings.value(QStringLiteral("desktop_ui/preset")).toString(),
        QStringLiteral("felt")
    );
    QVERIFY(!settings.contains(QStringLiteral("desktop_ui/overrides/chrome")));
    QCOMPARE(load_desktop_ui_preferences(settings), value);

    value.chrome_override = desktop_chrome_style::native;
    value.frame_override = slot_frame_style::thin;
    QVERIFY(save_desktop_ui_preferences(settings, value));
    QCOMPARE(load_desktop_ui_preferences(settings), value);
    QCOMPARE(
        load_desktop_ui_preferences(settings).chrome(),
        desktop_chrome_style::native
    );
    value.preset = desktop_ui_preset::classic;
    value.chrome_override = desktop_chrome_style::felt;
    value.frame_override = slot_frame_style::piping;
    QVERIFY(save_desktop_ui_preferences(settings, value));
    QCOMPARE(
        settings.value(QStringLiteral("desktop_ui/overrides/frame")).toString(),
        QStringLiteral("piping")
    );
    QCOMPARE(load_desktop_ui_preferences(settings), value);
    QCOMPARE(
        load_desktop_ui_preferences(settings).chrome(),
        desktop_chrome_style::felt
    );
    QCOMPARE(
        load_desktop_ui_preferences(settings).frame(), slot_frame_style::piping
    );

    value.reset_overrides();
    QVERIFY(!value.chrome_override);
    QVERIFY(save_desktop_ui_preferences(settings, value));
    QVERIFY(!settings.contains(QStringLiteral("desktop_ui/overrides/chrome")));
    QVERIFY(!settings.contains(QStringLiteral("desktop_ui/overrides/frame")));
    // Values written by a newer build inherit the preset without rewriting.
    settings.setValue(
        QStringLiteral("desktop_ui/overrides/chrome"), QStringLiteral("future")
    );
    QCOMPARE(load_desktop_ui_preferences(settings).chrome(), value.chrome());
    QCOMPARE(
        settings.value(QStringLiteral("desktop_ui/overrides/chrome"))
            .toString(),
        QStringLiteral("future")
    );
    QCOMPARE(
        settings.value(QStringLiteral("trainer_preferences/setup/slot_count"))
            .toInt(),
        7
    );
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
