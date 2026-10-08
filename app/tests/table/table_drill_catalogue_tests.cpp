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

void table_tests::drill_catalogues_preserve_separate_formats() {
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath(QStringLiteral("drills.ini")), QSettings::IniFormat
    );
    const auto legacy = legacy_drill();
    const auto target = target_drill();
    training_drill_service old_service(storage);
    gameplay_drill_service service(storage, drill_limits);
    QVERIFY(old_service.save({ legacy }));
    const auto old_bytes
        = storage.value(QStringLiteral("training_drills/document"));
    drill_picker picker(storage, target, { drill_limits });
    auto* list
        = picker.findChild<QListWidget*>(QStringLiteral("saved_drills_list"));
    auto* name
        = picker.findChild<QLineEdit*>(QStringLiteral("saved_drill_name"));
    auto* save
        = picker.findChild<QPushButton*>(QStringLiteral("save_current_drill"));
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->text(), legacy.name);
    name->setText(legacy.name); // Separate catalogues allow the same name.
    save->click();
    QCOMPARE(list->count(), 2);
    auto saved = target;
    saved.name = legacy.name;
    QCOMPARE(*service.load(), QVector<gameplay_drill> { saved });
    QCOMPARE(
        storage.value(QStringLiteral("training_drills/document")), old_bytes
    );
    name->setText(QStringLiteral("Renamed target"));
    picker.findChild<QPushButton*>(QStringLiteral("rename_drill"))->click();
    saved.name = name->text();
    QCOMPARE(*service.load(), QVector<gameplay_drill> { saved });
    save->click(); // Duplicate does not overwrite either record.
    QCOMPARE(service.load()->size(), 1);
    QVERIFY(!picker.findChild<QLabel*>(QStringLiteral("saved_drill_message"))
                 ->text()
                 .isEmpty());
    answer_drill_question(picker, QMessageBox::No);
    picker.findChild<QPushButton*>(QStringLiteral("delete_drill"))->click();
    QCOMPARE(service.load()->size(), 1);
    answer_drill_question(picker, QMessageBox::Yes);
    picker.findChild<QPushButton*>(QStringLiteral("delete_drill"))->click();
    QVERIFY(service.load()->isEmpty());
    QCOMPARE(
        storage.value(QStringLiteral("training_drills/document")), old_bytes
    );
    QVERIFY(!picker.chosen());
}

void table_tests::drill_catalogues_recheck_changes() {
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath(QStringLiteral("drills.ini")), QSettings::IniFormat
    );
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
    QCOMPARE(
        list->item(0)->text(), target.name + QStringLiteral(" [Gameplay]")
    );
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
    QSettings storage(
        directory.filePath(QStringLiteral("drills.ini")), QSettings::IniFormat
    );
    auto target = target_drill();
    const auto legacy = legacy_drill();
    const auto damaged = QByteArray("{\"version\":99,\"drills\":[]}");
    storage.setValue(QStringLiteral("training_drills/document"), damaged);
    gameplay_drill_service service(storage, drill_limits);
    QVERIFY(service.save({ target }));
    {
        drill_picker picker(storage, target, { drill_limits });
        picker.findChild<QLineEdit*>(QStringLiteral("saved_drill_name"))
            ->setText(QStringLiteral("Valid target copy"));
        auto* save = picker.findChild<QPushButton*>(
            QStringLiteral("save_current_drill")
        );
        QVERIFY(save->isEnabled());
        save->click();
        QCOMPARE(service.load()->size(), 2);
        QCOMPARE(
            storage.value(QStringLiteral("training_drills/document")),
            QVariant(damaged)
        );
    }
    storage.remove(QStringLiteral("training_drills/document"));
    QVERIFY(training_drill_service(storage).save({ legacy }));
    storage.setValue(QStringLiteral("gameplay_drills/document"), damaged);
    {
        drill_picker picker(storage, legacy, { drill_limits });
        auto* load
            = picker.findChild<QPushButton*>(QStringLiteral("launch_drill"));
        QVERIFY(load->isEnabled());
        QVERIFY(!picker
                     .findChild<QPushButton*>(QStringLiteral("convert_drill"))
                     ->isEnabled());
        load->click();
        QVERIFY(
            picker.chosen()
            && std::holds_alternative<training_drill>(*picker.chosen())
        );
        QCOMPARE(
            storage.value(QStringLiteral("gameplay_drills/document")),
            QVariant(damaged)
        );
    }
    storage.remove(QStringLiteral("gameplay_drills/document"));
    target.decks[0].configuration.strategy_slug = "unavailable";
    QVERIFY(service.save({ target }));
    drill_picker picker(storage, target, { drill_limits });
    picker.findChild<QListWidget*>(QStringLiteral("saved_drills_list"))
        ->setCurrentRow(1);
    QVERIFY(!picker.findChild<QPushButton*>(QStringLiteral("launch_drill"))
                 ->isEnabled());
    QVERIFY(
        picker
            .findChild<QPlainTextEdit*>(QStringLiteral("saved_drill_details"))
            ->toPlainText()
            .contains(QStringLiteral("Cannot launch:"))
    );
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
