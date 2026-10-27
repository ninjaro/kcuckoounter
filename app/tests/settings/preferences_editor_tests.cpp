// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "card_helpers/card_sheet.hpp"
#include "settings/preferences.hpp"
#include "settings/preferences_tests.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"
#include "table/table_fixture.hpp"
#include <QAbstractButton>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QMessageBox>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest/QtTest>
#include <shared_ui/qt/action_group.hpp>
#include <shared_ui/qt/appearance_editor.hpp>
#include <shared_ui/qt/presentation.hpp>

void preferences_tests::desktop_editor_save_snapshot_and_cancel() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop editor; not Android device acceptance");
#endif
    using namespace shared_ui::qt;
    QSettings settings("ninjaro", "kcuckoounter");
    QMap<QString, QVariant> original;
    for (const auto& key : settings.allKeys())
        original.insert(key, settings.value(key));
    const auto source = card_sheet_source_path();
    const auto restore = qScopeGuard([&] {
        settings.clear();
        for (auto it = original.cbegin(); it != original.cend(); ++it)
            settings.setValue(it.key(), it.value());
        settings.sync();
        set_card_sheet_source_path(source);
    });
    QVERIFY(save_desktop_ui_preferences({}));
    table view;
    QVERIFY(view.install_gameplay_session(table_test::setup_table_fixture(1)));
    const auto before = *view.active_gameplay_session()->deck({ 0 });
    settings_template_widget host(
        settings_tab_kind::appearance, nullptr, {}, &view
    );
    auto* editor = host.desktop_editor();
    auto* model = host.desktop_model();
    QVERIFY(editor && model);
    QCOMPARE(model->families().size(), 9);
    QCOMPARE(model->presets().size(), 3);
    QCOMPARE(model->state(), load_desktop_ui_preferences().appearance());
    // Place the real editor independently, as the settings dialog does.
    QDialog dialog;
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(editor);
    dialog.resize(1000, 700);
    dialog.show();
    QCoreApplication::processEvents();
    editor->select_family("slot_actions");
    editor->option_buttons()[2]->click(); // The declared icon rail.
    auto* actions = editor->findChild<action_group*>();
    QVERIFY(actions);
    QCOMPARE(actions->orientation(), Qt::Vertical);
    QCOMPARE(actions->variant(), action_variant::compact);
    editor->select_family("hud");
    QCOMPARE(editor->option_buttons().size(), 3); // inherited + two variants
    editor->option_buttons().last()->click();
    QCOMPARE(model->effective("hud"), QStringLiteral("instruments"));
    QCOMPARE(load_desktop_ui_preferences().hud(), desktop_hud_style::classic);
    const auto submitted = model->state();
    // A synchronous application callback may edit the draft before the ack.
    connect(
        &host, &settings_template_widget::desktop_presentation_applied, &dialog,
        [model] { model->set_override("toolbar", "compact"); }
    );
    QVERIFY(editor->command(editor_command::save)->isEnabled());
    editor->command(editor_command::save)->click();
    QVERIFY(!editor->save_pending());
    QCOMPARE(model->saved(), submitted);
    QVERIFY(model->dirty());
    QCOMPARE(load_desktop_ui_preferences().appearance(), submitted);
    QCOMPARE(*view.active_gameplay_session()->deck({ 0 }), before);
    editor->command(editor_command::cancel)->click();
    QCOMPARE(model->state(), submitted);
    QVERIFY(!model->dirty());
    QCOMPARE(*view.active_gameplay_session()->deck({ 0 }), before);
    QVERIFY(host.findChild<QComboBox*>("orientation_combo_box"));
    QVERIFY(host.findChild<QComboBox*>("default_suit"));
    QVERIFY(!host.findChild<QComboBox*>("desktop_ui_preset"));
    delete editor;
    QVERIFY(!host.desktop_editor());
    QVERIFY(!host.desktop_model());
    host.reset_theme_selection(); // No callback may use the retired editor.
    auto* retired_host
        = new settings_template_widget(settings_tab_kind::appearance);
    QPointer<appearance_editor> retired_editor = retired_host->desktop_editor();
    retired_editor->setParent(&dialog);
    delete retired_host;
    QVERIFY(retired_editor.isNull()); // The editor cannot outlive its host.
}

void preferences_tests::desktop_editor_failed_save_has_no_application() {
#if !defined(Q_OS_LINUX) || defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Linux desktop QSettings filesystem failure control");
#endif
    using namespace shared_ui::qt;
    QDir original_root
        = QFileInfo(QSettings("ninjaro", "kcuckoounter").fileName()).dir();
    QVERIFY(original_root.cdUp());
    const auto restore = qScopeGuard([&] {
        for (const auto format :
             { QSettings::IniFormat, QSettings::NativeFormat })
            QSettings::setPath(
                format, QSettings::UserScope, original_root.path()
            );
    });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const auto format : { QSettings::IniFormat, QSettings::NativeFormat })
        QSettings::setPath(format, QSettings::UserScope, directory.path());
    const auto path = QSettings("ninjaro", "kcuckoounter").fileName();
    QVERIFY(QDir().mkpath(path)); // A directory cannot be a preferences file.
    table view;
    QVERIFY(view.install_gameplay_session(table_test::setup_table_fixture(1)));
    const auto before = *view.active_gameplay_session()->deck({ 0 });
    const auto source = card_sheet_source_path();
    const auto color = presentation_for(view).context.felt_surface;
    settings_template_widget host(
        settings_tab_kind::appearance, nullptr, {}, &view
    );
    auto* model = host.desktop_model();
    auto* editor = host.desktop_editor();
    QVERIFY(model && editor);
    QSignalSpy applied(
        &host, &settings_template_widget::desktop_presentation_applied
    );
    QVERIFY(model->choose_preset("felt"));
    editor->command(editor_command::save)->click();
    QVERIFY(!editor->save_pending());
    QVERIFY(model->dirty());
    QCOMPARE(model->saved().preset, QStringLiteral("classic"));
    QCOMPARE(load_desktop_ui_preferences().appearance(), model->saved());
    QVERIFY(editor->notices()
                .join(QStringLiteral(" "))
                .contains(QStringLiteral("Could not save")));
    QCOMPARE(applied.count(), 0);
    QCOMPARE(*view.active_gameplay_session()->deck({ 0 }), before);
    QCOMPARE(presentation_for(view).context.felt_surface, color);
    QCOMPARE(card_sheet_source_path(), source);
    QVERIFY(QFileInfo(path).isDir());
    QVERIFY(QDir().rmdir(path));
    // A successful dialog Save must also clear the editor's previous failure.
    QTimer::singleShot(0, &host, [] {
        if (auto* message
            = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            message->reject();
    });
    QVERIFY(host.apply_theme_settings());
    QVERIFY(!model->dirty());
    QVERIFY(QFileInfo(path).isFile());
    QVERIFY(!editor->notices()
                 .join(QStringLiteral(" "))
                 .contains(QStringLiteral("Could not save")));
    QCOMPARE(applied.count(), 1);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
