// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "settings/gameplay_recovery_tests.hpp"

#include "settings/gameplay_progress_fixture.hpp"
#include "settings/gameplay_recovery_fixture.hpp"

#include <QDataStream>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace recovery_test;

namespace {
enum class failure_point { none, reserve, update, stage, progress, cleanup };
failure_point failure = failure_point::none;
QStringList attempts;

bool read_settings(QIODevice& device, QSettings::SettingsMap& map) {
    QDataStream stream(&device);
    stream >> map;
    return stream.status() == QDataStream::Ok;
}

bool write_settings(QIODevice& device, const QSettings::SettingsMap& map) {
    const auto root
        = QJsonDocument::fromJson(map.value(storage_key).toByteArray())
              .object();
    const auto phase = root.value(QStringLiteral("phase")).toString();
    const bool progress = map.contains(progress_key);
    attempts.append(
        (phase.isEmpty() ? QStringLiteral("absent") : phase)
        + (progress ? QStringLiteral("+progress") : QStringLiteral("+empty"))
    );
    if (((failure == failure_point::reserve || failure == failure_point::update)
         && phase == QStringLiteral("active"))
        || (failure == failure_point::stage
            && phase == QStringLiteral("pending"))
        || (failure == failure_point::progress
            && phase == QStringLiteral("pending") && progress)
        || (failure == failure_point::cleanup && !map.contains(storage_key)
            && progress))
        return false;
    QDataStream stream(&device);
    stream << map;
    return stream.status() == QDataStream::Ok;
}
} // namespace

void gameplay_recovery_tests::write_failures_data() {
    QTest::addColumn<int>("point");
    QTest::newRow("reservation") << static_cast<int>(failure_point::reserve);
    QTest::newRow("checkpoint-update")
        << static_cast<int>(failure_point::update);
    QTest::newRow("terminal-staging") << static_cast<int>(failure_point::stage);
    QTest::newRow("progress-admission")
        << static_cast<int>(failure_point::progress);
    QTest::newRow("recovery-cleanup")
        << static_cast<int>(failure_point::cleanup);
}

void gameplay_recovery_tests::write_failures() {
    QFETCH(int, point);
    static const auto format = QSettings::registerFormat(
        "recovery_test", read_settings, write_settings
    );
    const auto target = static_cast<failure_point>(point);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto file = directory.filePath("settings.recovery_test");
    auto model = progress_test::ready_session();
    const auto initial = snapshot(model);
    const auto active_bytes
        = encode_gameplay_recovery(
              gameplay_active_record { 1, initial }, { 3, 64 }
        )
              .value();
    gameplay_result_record record;
    failure = failure_point::none;
    attempts.clear();
    {
        QSettings settings(file, format);
        gameplay_recovery_service service(settings, { 3, 64 });
        QString error;
        if (target == failure_point::reserve) {
            failure = target;
            const auto sequence = service.begin(initial, &error);
            failure = failure_point::none;
            QVERIFY(!sequence && !error.isEmpty());
            QVERIFY(!settings.contains(storage_key));
            QVERIFY(!settings.contains(progress_key));
        } else {
            QVERIFY(service.begin(initial) == 1);
            if (target == failure_point::update) {
                drive(model, 16);
                failure = target;
                const bool saved = service.save(1, snapshot(model), &error);
                failure = failure_point::none;
                QVERIFY(!saved && !error.isEmpty());
                QCOMPARE(
                    settings.value(storage_key).toByteArray(), active_bytes
                );
            } else {
                record = finish(model);
                failure = target;
                const bool completed = service.complete(record, &error);
                failure = failure_point::none;
                QVERIFY(!completed && !error.isEmpty());
                const auto current = decode_gameplay_recovery(
                    settings.value(storage_key).toByteArray(), { 3, 64 }
                );
                QVERIFY(current);
                if (target == failure_point::stage) {
                    QVERIFY(
                        std::get<gameplay_active_record>(*current).checkpoint
                        == initial
                    );
                    QVERIFY(!settings.contains(progress_key));
                } else {
                    QVERIFY(
                        std::get<gameplay_result_record>(*current) == record
                    );
                    QCOMPARE(
                        settings.contains(progress_key),
                        target == failure_point::cleanup
                    );
                }
            }
        }
    }
    QSettings reopened(file, format);
    gameplay_recovery_service again(reopened, { 3, 64 });
    QString error;
    const auto current = again.load(&error);
    QVERIFY(error.isEmpty());
    if (target == failure_point::reserve) {
        QVERIFY(!current);
        QVERIFY(again.begin(initial) == 1);
        record = finish(model);
    } else if (target == failure_point::update) {
        QVERIFY(
            current
            && std::get<gameplay_active_record>(*current).checkpoint == initial
        );
        QVERIFY(again.save(1, snapshot(model)));
        record = finish(model);
    } else if (target == failure_point::stage) {
        QVERIFY(
            current
            && std::get<gameplay_active_record>(*current).checkpoint == initial
        );
    } else {
        QVERIFY(
            current && std::get<gameplay_result_record>(*current) == record
        );
        record = std::get<gameplay_result_record>(
            *current
        ); // Reopen uses the exact timestamp.
    }
    attempts.clear();
    QVERIFY(again.complete(record, &error) && error.isEmpty());
    const auto progress = gameplay_progress_service(reopened, { 3, 64 }).load();
    QVERIFY(
        progress && progress->completed_sessions == 1
        && progress->recent_results.front() == record
    );
    QVERIFY(!reopened.contains(storage_key));
    if (target == failure_point::stage || target == failure_point::reserve
        || target == failure_point::update)
        QCOMPARE(
            attempts,
            QStringList(
                { "pending+empty", "pending+progress", "absent+progress" }
            )
        );
    else if (target == failure_point::progress)
        QCOMPARE(
            attempts, QStringList({ "pending+progress", "absent+progress" })
        );
    else
        QCOMPARE(attempts, QStringList({ "absent+progress" }));
    const auto stored = reopened.value(progress_key);
    attempts.clear();
    QVERIFY(again.complete(record));
    QVERIFY(attempts.isEmpty());
    QCOMPARE(reopened.value(progress_key), stored);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
