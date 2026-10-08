#include "table/drill_fixture.hpp"

#include "settings/strategy_data.hpp"
#include "shell/main_window.hpp"

#include <QDialog>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QtTest/QtTest>

namespace table_test {

gameplay_drill target_drill(std::size_t count) {
    gameplay_drill result;
    result.name = QStringLiteral("Target drill");
    result.configuration.failure = gameplay::failure_policy::block;
    result.configuration.initial_lives = 11;
    result.configuration.source = gameplay::quiz_source::virtual_interrupt;
    result.configuration.scope = gameplay::quiz_scope::multi;
    result.configuration.global_quiz_pause = false;
    result.configuration.allow_skip = false;
    result.configuration.dealing = gameplay::dealing_mode::sequential;
    result.configuration.sequential_count = count > 1 ? count - 1 : 1;
    result.pickup_interval_ms = 650;
    const auto& strategies = strategy_repository().strategies;
    for (std::size_t id = 0; id < count; ++id)
        result.decks.push_back(
            { { strategies[static_cast<qsizetype>(
                               id % static_cast<std::size_t>(strategies.size())
                           )]
                    .slug.toStdString(),
                1 + id % 2, id % 2 != 0, id % 2 == 0 },
              id % 2 == 0 }
        );
    return result;
}

training_drill legacy_drill() {
    training_drill result;
    result.name = QStringLiteral("Legacy drill");
    result.quiz_type = 1;
    result.wait_for_answers = true;
    result.allow_skipping = false;
    result.dealing_mode = 2;
    result.pickup_interval_ms = 570;
    result.slot_settings
        = { { 2, true, strategy_repository().strategies.first().slug, true } };
    return result;
}

void visit_drill_picker(
    main_window& window, const std::function<void(QDialog&)>& inspect
) {
    QTimer::singleShot(0, &window, [&] {
        QPointer<QDialog> picker
            = window.findChild<QDialog*>(QStringLiteral("saved_drills_dialog"));
        QVERIFY(picker);
        inspect(*picker);
        if (picker && picker->isVisible())
            picker->reject();
    });
    QVERIFY(QMetaObject::invokeMethod(&window, "on_saved_drills_triggered"));
}

void answer_drill_question(
    QWidget& parent, QMessageBox::StandardButton answer
) {
    QTimer::singleShot(0, &parent, [&parent, answer] {
        auto* box = parent.findChild<QMessageBox*>();
        QVERIFY(box);
        box->button(answer)->click();
    });
}

drill_storage_guard::drill_storage_guard()
    : legacy(storage.value(QStringLiteral("training_drills/document")))
    , target(storage.value(QStringLiteral("gameplay_drills/document"))) {
    storage.remove(QStringLiteral("training_drills/document"));
    storage.remove(QStringLiteral("gameplay_drills/document"));
}

drill_storage_guard::~drill_storage_guard() {
    const auto restore = [&](const QString& key, const QVariant& value) {
        if (value.isValid())
            storage.setValue(key, value);
        else
            storage.remove(key);
    };
    restore(QStringLiteral("training_drills/document"), legacy);
    restore(QStringLiteral("gameplay_drills/document"), target);
}

} // namespace table_test
