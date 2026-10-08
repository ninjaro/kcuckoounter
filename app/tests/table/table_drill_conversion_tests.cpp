// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/table_tests.hpp"

#include "shell/drill_picker.hpp"
#include "table/drill_fixture.hpp"
#include "table/gameplay_setup.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QtTest>

using namespace table_test;

namespace {

void review_conversion(
    drill_picker& picker, const std::function<void(QDialog&)>& inspect
) {
    QTimer::singleShot(0, &picker, [&] {
        QPointer<QDialog> review = picker.findChild<QDialog*>(
            QStringLiteral("drill_conversion_dialog")
        );
        QVERIFY(review);
        inspect(*review);
        if (review && review->isVisible())
            review->reject();
    });
    picker.findChild<QPushButton*>(QStringLiteral("convert_drill"))->click();
}

} // namespace

void table_tests::drill_conversion_requires_review() {
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath(QStringLiteral("drills.ini")), QSettings::IniFormat
    );
    const auto legacy = legacy_drill();
    QVERIFY(training_drill_service(storage).save({ legacy }));
    const auto original
        = storage.value(QStringLiteral("training_drills/document"));
    gameplay_drill_service service(storage, drill_limits);
    drill_picker picker(storage, legacy, { drill_limits });
    review_conversion(picker, [&](QDialog& review) {
        QVERIFY(
            review
                .findChild<QLabel*>(QStringLiteral("drill_conversion_notice"))
                ->text()
                .contains(QStringLiteral("not a lossless conversion"))
        );
        QTest::keyClick(&review, Qt::Key_Escape);
    });
    QVERIFY(!storage.contains(QStringLiteral("gameplay_drills/document")));
    review_conversion(picker, [&](QDialog& review) {
        answer_drill_question(review, QMessageBox::No);
        review.findChild<QPushButton*>(QStringLiteral("save_converted_drill"))
            ->click();
        QVERIFY(review.isVisible());
        QVERIFY(!storage.contains(QStringLiteral("gameplay_drills/document")));
    });
    review_conversion(picker, [&](QDialog& review) {
        auto* fields = review.findChild<gameplay::setup_widget*>();
        QVERIFY(fields);
        auto* title = review.findChild<QLineEdit*>(
            QStringLiteral("converted_drill_name")
        );
        title->clear();
        review.findChild<QPushButton*>(QStringLiteral("save_converted_drill"))
            ->click();
        QVERIFY(review.isVisible());
        QVERIFY(
            !review
                 .findChild<QLabel*>(QStringLiteral("drill_conversion_error"))
                 ->text()
                 .isEmpty()
        );
        const auto select = [&](const char* key, int value) {
            auto* combo
                = fields->findChild<QComboBox*>(QString::fromLatin1(key));
            combo->setCurrentIndex(combo->findData(value));
        };
        fields->findChild<QSpinBox*>(QStringLiteral("gameplay_initial_lives"))
            ->setValue(9);
        select(
            "gameplay_failure_policy",
            static_cast<int>(gameplay::failure_policy::block)
        );
        select(
            "gameplay_quiz_source",
            static_cast<int>(gameplay::quiz_source::virtual_interrupt)
        );
        select(
            "gameplay_quiz_scope", static_cast<int>(gameplay::quiz_scope::multi)
        );
        fields->findChild<QCheckBox*>(QStringLiteral("gameplay_global_pause"))
            ->setChecked(false);
        review.findChild<QLineEdit*>(QStringLiteral("converted_drill_name"))
            ->setText(QStringLiteral("Reviewed copy"));
        answer_drill_question(review, QMessageBox::Yes);
        review.findChild<QPushButton*>(QStringLiteral("save_converted_drill"))
            ->click();
    });
    const auto saved = service.load();
    QVERIFY(saved && saved->size() == 1);
    const auto& target = saved->first();
    QCOMPARE(target.name, QStringLiteral("Reviewed copy"));
    QCOMPARE(target.configuration.failure, gameplay::failure_policy::block);
    QCOMPARE(target.configuration.initial_lives, 9);
    QCOMPARE(
        target.configuration.source, gameplay::quiz_source::virtual_interrupt
    );
    QCOMPARE(target.configuration.scope, gameplay::quiz_scope::multi);
    QVERIFY(
        !target.configuration.global_quiz_pause
        && !target.configuration.allow_skip
    );
    QCOMPARE(
        target.configuration.dealing, gameplay::dealing_mode::simultaneous
    );
    QCOMPARE(target.pickup_interval_ms, legacy.pickup_interval_ms);
    QCOMPARE(target.decks[0].configuration.deck_count, std::size_t { 2 });
    QVERIFY(
        target.decks[0].configuration.infinite
        && target.decks[0].configuration.training && target.decks[0].show_count
    );
    QCOMPARE(
        storage.value(QStringLiteral("training_drills/document")), original
    );
    QVERIFY(
        !picker.chosen()
    ); // Saving conversion neither selects a launch nor starts.
    picker.findChild<QListWidget*>(QStringLiteral("saved_drills_list"))
        ->setCurrentRow(0);
    review_conversion(picker, [&](QDialog& review) {
        review.findChild<QLineEdit*>(QStringLiteral("converted_drill_name"))
            ->setText(target.name);
        answer_drill_question(review, QMessageBox::Yes);
        review.findChild<QPushButton*>(QStringLiteral("save_converted_drill"))
            ->click();
    });
    QCOMPARE(*service.load(), *saved); // Name collision is non-destructive.
}

void table_tests::drill_conversion_rejects_stale_sources() {
    QTemporaryDir directory;
    QSettings storage(
        directory.filePath(QStringLiteral("drills.ini")), QSettings::IniFormat
    );
    auto legacy = legacy_drill();
    QVERIFY(training_drill_service(storage).save({ legacy }));
    drill_picker picker(storage, legacy, { drill_limits });
    review_conversion(picker, [&](QDialog& review) {
        legacy.name = QStringLiteral("Externally changed");
        QVERIFY(training_drill_service(storage).save({ legacy }));
        answer_drill_question(review, QMessageBox::Yes);
        review.findChild<QPushButton*>(QStringLiteral("save_converted_drill"))
            ->click();
    });
    QVERIFY(!storage.contains(QStringLiteral("gameplay_drills/document")));
    picker.findChild<QListWidget*>(QStringLiteral("saved_drills_list"))
        ->setCurrentRow(0);
    review_conversion(picker, [&](QDialog& review) {
        auto target = target_drill();
        QVERIFY(gameplay_drill_service(storage, drill_limits).save({ target }));
        answer_drill_question(review, QMessageBox::Yes);
        review.findChild<QPushButton*>(QStringLiteral("save_converted_drill"))
            ->click();
    });
    QCOMPARE(gameplay_drill_service(storage, drill_limits).load()->size(), 1);
    picker.findChild<QListWidget*>(QStringLiteral("saved_drills_list"))
        ->setCurrentRow(0);
    review_conversion(picker, [&](QDialog&) { picker.invalidate(); });
    QCOMPARE(gameplay_drill_service(storage, drill_limits).load()->size(), 1);
    QVERIFY(!picker.chosen());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
