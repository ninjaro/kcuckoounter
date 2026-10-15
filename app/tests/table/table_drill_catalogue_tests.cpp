// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/table_tests.hpp"

#include "shell/drill_picker.hpp"
#include "table/drill_fixture.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QtTest>

using namespace table_test;

void table_tests::drill_catalogue_uses_current_setups() {
    QTemporaryDir directory;
    QSettings storage(directory.filePath("drills.ini"), QSettings::IniFormat);
    const auto original = target_drill();
    gameplay_drill_service service(storage, drill_limits);
    QVERIFY(service.save({ original }));
    drill_picker picker(storage, original, { drill_limits });
    auto* list
        = picker.findChild<QListWidget*>(QStringLiteral("saved_drills_list"));
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->text(), original.name);
    auto* name
        = picker.findChild<QLineEdit*>(QStringLiteral("saved_drill_name"));
    auto* save
        = picker.findChild<QPushButton*>(QStringLiteral("save_current_drill"));
    name->setText(QStringLiteral("Named setup"));
    save->click();
    auto saved = original;
    saved.name = name->text();
    QCOMPARE(*service.load(), (QVector<gameplay_drill> { original, saved }));
    save->click();
    QCOMPARE(service.load()->size(), 2);
    QVERIFY(!picker.findChild<QLabel*>(QStringLiteral("saved_drill_message"))
                 ->text()
                 .isEmpty());
    answer_drill_question(picker, QMessageBox::No);
    picker.findChild<QPushButton*>(QStringLiteral("delete_drill"))->click();
    QCOMPARE(service.load()->size(), 2);
    answer_drill_question(picker, QMessageBox::Yes);
    picker.findChild<QPushButton*>(QStringLiteral("delete_drill"))->click();
    QCOMPARE(*service.load(), QVector<gameplay_drill> { original });
    QVERIFY(!picker.chosen());
}

void table_tests::drill_catalogues_recheck_changes() {
    QTemporaryDir directory;
    QSettings storage(directory.filePath("drills.ini"), QSettings::IniFormat);
    auto target = target_drill();
    gameplay_drill_service service(storage, drill_limits);
    QVERIFY(service.save({ target }));
    drill_picker picker(storage, target, { drill_limits });
    target.name = QStringLiteral("External replacement");
    QVERIFY(service.save({ target }));
    const auto bytes
        = storage.value(QStringLiteral("gameplay_drills/document"));
    picker.findChild<QPushButton*>(QStringLiteral("launch_drill"))->click();
    QVERIFY(!picker.chosen());
    QCOMPARE(storage.value(QStringLiteral("gameplay_drills/document")), bytes);
    auto* list
        = picker.findChild<QListWidget*>(QStringLiteral("saved_drills_list"));
    QCOMPARE(list->item(0)->text(), target.name);
    list->setCurrentRow(0);
    picker.findChild<QLineEdit*>(QStringLiteral("saved_drill_name"))
        ->setText(QStringLiteral("New copy"));
    target.pickup_interval_ms = 800;
    QVERIFY(service.save({ target }));
    picker.findChild<QPushButton*>(QStringLiteral("save_current_drill"))
        ->click();
    QCOMPARE(*service.load(), QVector<gameplay_drill> { target });
    list->setCurrentRow(0);
    QTimer::singleShot(0, &picker, [&] {
        auto changed = target;
        changed.name = QStringLiteral("During confirmation");
        QVERIFY(service.save({ changed }));
        auto* box = picker.findChild<QMessageBox*>();
        QVERIFY(box);
        box->button(QMessageBox::Yes)->click();
    });
    picker.findChild<QPushButton*>(QStringLiteral("delete_drill"))->click();
    QCOMPARE(service.load()->size(), 1);
    QCOMPARE(
        service.load()->first().name, QStringLiteral("During confirmation")
    );
}

void table_tests::drill_catalogues_isolate_errors() {
    QTemporaryDir directory;
    QSettings storage(directory.filePath("drills.ini"), QSettings::IniFormat);
    auto target = target_drill();
    const auto damaged = QByteArray("{\"version\":2,\"drills\":[]}");
    storage.setValue(QStringLiteral("gameplay_drills/document"), damaged);
    {
        drill_picker picker(storage, target, { drill_limits });
        QVERIFY(
            !picker
                 .findChild<QPushButton*>(QStringLiteral("save_current_drill"))
                 ->isEnabled()
        );
        QVERIFY(!picker.findChild<QPushButton*>(QStringLiteral("launch_drill"))
                     ->isEnabled());
        QVERIFY(!picker
                     .findChild<QLabel*>(QStringLiteral("saved_drill_message"))
                     ->text()
                     .isEmpty());
        QCOMPARE(
            storage.value(QStringLiteral("gameplay_drills/document"))
                .toByteArray(),
            damaged
        );
    }
    storage.remove(QStringLiteral("gameplay_drills/document"));
    target.decks[0].configuration.strategy_slug = "unavailable";
    gameplay_drill_service service(storage, drill_limits);
    QVERIFY(service.save({ target }));
    drill_picker picker(storage, target, { drill_limits });
    QVERIFY(!picker.findChild<QPushButton*>(QStringLiteral("launch_drill"))
                 ->isEnabled());
    QVERIFY(picker
                .findChild<QPlainTextEdit*>(QStringLiteral("saved_drill_details"
                ))
                ->toPlainText()
                .contains(QStringLiteral("Cannot launch:")));
    picker.findChild<QLineEdit*>(QStringLiteral("saved_drill_name"))
        ->setText(QStringLiteral("Kept missing strategy"));
    picker.findChild<QPushButton*>(QStringLiteral("rename_drill"))->click();
    QCOMPARE(
        service.load()->first().decks[0].configuration.strategy_slug,
        std::string("unavailable")
    );
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
