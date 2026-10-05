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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "settings/preferences_fixture.hpp"

using namespace preferences_test;

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

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
