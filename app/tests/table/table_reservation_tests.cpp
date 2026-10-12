// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_start_tests.hpp"

#include "shell/gameplay_reservation.hpp"
#include "table/table_fixture.hpp"

#include <QDataStream>
#include <QIODevice>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace table_test;

void gameplay_start_tests::reservation_precedes_publication() {
    QTemporaryDir directory;
    QSettings storage(directory.filePath("recovery.ini"), QSettings::IniFormat);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    gameplay_reservation binding;
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(2)));
    const auto expected
        = gameplay::capture_start(*view.gameplay_owner, UINT64_MAX);
    bool bound_on_publication = false;
    QObject::connect(&view, &table::gameplay_runtime_updated, &view, [&] {
        const auto record = recovery.load();
        const auto* active
            = record ? std::get_if<gameplay_active_record>(&*record) : nullptr;
        bound_on_publication = binding.owns(view) && binding.sequence() == 1
            && active && active->sequence == *binding.sequence()
            && active->checkpoint.state == *expected
            && active->checkpoint.pickup_elapsed_ms == 0;
    });
    QString error = QStringLiteral("old diagnostic");
    QVERIFY(binding.start(view, recovery, UINT64_MAX, &error));
    QVERIFY(error.isEmpty() && bound_on_publication);
    view.stop_gameplay_clock();
    QVERIFY(view.pause_gameplay_runtime());
    QVERIFY(binding.owns(view));
    QVERIFY(view.finish_gameplay_runtime());
    QVERIFY(binding.owns(view)); // terminal binding must survive for admission
    const auto durable
        = storage.value(QStringLiteral("gameplay_recovery/document"));
    QVERIFY(!binding.start(view, recovery, 2, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(
        storage.value(QStringLiteral("gameplay_recovery/document")), durable
    );
    QVERIFY(!storage.contains(QStringLiteral("gameplay_progress/document")));
    QVERIFY(!storage.contains(QStringLiteral("training_progress")));
}

void gameplay_start_tests::reservation_rejects_protected_storage_data() {
    QTest::addColumn<int>("conflict");
    QTest::newRow("standalone") << 0;
    QTest::newRow("future-recovery") << 1;
    QTest::newRow("future-progress") << 2;
    QTest::newRow("active-reservation") << 3;
}

void gameplay_start_tests::reservation_rejects_protected_storage() {
    QFETCH(int, conflict);
    QTemporaryDir directory;
    QSettings storage(directory.filePath("recovery.ini"), QSettings::IniFormat);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(2)));
    QString key;
    const QByteArray bytes("{\"version\":900}");
    if (conflict == 0)
        key = QStringLiteral("gameplay_checkpoint/document");
    else if (conflict == 1)
        key = QStringLiteral("gameplay_recovery/document");
    else if (conflict == 2)
        key = QStringLiteral("gameplay_progress/document");
    else {
        const gameplay_checkpoint checkpoint {
            *gameplay::capture_start(*view.gameplay_owner, 3), 0
        };
        QVERIFY(recovery.begin(checkpoint) == 1);
        key = QStringLiteral("gameplay_recovery/document");
    }
    if (conflict != 3)
        storage.setValue(key, bytes);
    storage.sync();
    const auto before = storage.value(key);
    const auto initial = gameplay::capture_start(*view.gameplay_owner, 7);
    gameplay_reservation binding;
    QSignalSpy updates(&view, &table::gameplay_runtime_updated);
    QString error;
    QVERIFY(!binding.start(view, recovery, 7, &error));
    QVERIFY(!error.isEmpty() && !binding.sequence() && !binding.owns(view));
    QVERIFY(gameplay::capture_start(*view.gameplay_owner, 7) == initial);
    QVERIFY(!view.gameplay_timer.isActive() && !view.gameplay_runtime_attached);
    QCOMPARE(updates.count(), 0);
    QCOMPARE(storage.value(key), before);
}

void gameplay_start_tests::reservation_write_failure_keeps_setup() {
    static const auto format = QSettings::registerFormat(
        QStringLiteral("start_rejection"),
        [](QIODevice&, QSettings::SettingsMap&) { return true; },
        [](QIODevice&, const QSettings::SettingsMap&) { return false; }
    );
    QTemporaryDir directory;
    QSettings storage(directory.filePath("recovery.start_rejection"), format);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    gameplay_reservation binding;
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    const auto before = gameplay::capture_start(*view.gameplay_owner, 7);
    QString error;
    QVERIFY(!binding.start(view, recovery, 7, &error));
    QVERIFY(!error.isEmpty() && !binding.sequence());
    QVERIFY(gameplay::capture_start(*view.gameplay_owner, 7) == before);
    QVERIFY(!view.gameplay_start_pending && !view.gameplay_runtime_attached);
    QVERIFY(!storage.contains(QStringLiteral("gameplay_recovery/document")));
}

void gameplay_start_tests::reservation_survives_scene_retirement_data() {
    QTest::addColumn<int>("retirement");
    QTest::newRow("replacement") << 0;
    QTest::newRow("clear") << 1;
    QTest::newRow("destruction") << 2;
}

void gameplay_start_tests::reservation_survives_scene_retirement() {
    QFETCH(int, retirement);
    QTemporaryDir directory;
    QSettings storage(directory.filePath("recovery.ini"), QSettings::IniFormat);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    gameplay_reservation binding;
    auto* view = new table;
    QPointer<table> guard(view);
    view->resize(1000, 800);
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(2)));
    const auto expected = gameplay::capture_start(*view->gameplay_owner, 17);
    bool retired = false;
    const auto connection
        = QObject::connect(view, &table::gameplay_runtime_updated, view, [&] {
              if (retirement == 0)
                  retired
                      = view->install_gameplay_session(owned_clock_fixture(1));
              else if (retirement == 1) {
                  view->clear_gameplay_session();
                  retired = !view->active_gameplay_session();
              } else {
                  delete view;
                  retired = true;
              }
          });
    QString error;
    QVERIFY(!binding.start(*view, recovery, 17, &error));
    QVERIFY(retired && !error.isEmpty());
    QVERIFY(binding.sequence() == 1);
    const auto record = recovery.load();
    QVERIFY(record && std::holds_alternative<gameplay_active_record>(*record));
    QVERIFY(
        std::get<gameplay_active_record>(*record).checkpoint.state == *expected
    );
    if (guard) {
        QObject::disconnect(connection);
        QVERIFY(!binding.owns(*guard));
        const auto durable
            = storage.value(QStringLiteral("gameplay_recovery/document"));
        gameplay_reservation replacement;
        if (!guard->active_gameplay_session())
            QVERIFY(guard->install_gameplay_session(owned_clock_fixture(1)));
        QVERIFY(!replacement.start(*guard, recovery, 18, &error));
        QCOMPARE(
            storage.value(QStringLiteral("gameplay_recovery/document")), durable
        );
        delete guard;
    }
    QVERIFY(!guard);
    QSettings reopened(
        directory.filePath("recovery.ini"), QSettings::IniFormat
    );
    QVERIFY(gameplay_recovery_service(reopened, { 1, 64 }).load() == record);
}

void gameplay_start_tests::reservation_after_failed_activation_data() {
    QTest::addColumn<bool>("destroy");
    QTest::newRow("geometry") << false;
    QTest::newRow("destruction") << true;
}

void gameplay_start_tests::reservation_after_failed_activation() {
    QFETCH(bool, destroy);
    static const auto format = QSettings::registerFormat(
        QStringLiteral("start_geometry"),
        [](QIODevice& device, QSettings::SettingsMap& map) {
            QDataStream stream(&device);
            stream >> map;
            return stream.status() == QDataStream::Ok;
        },
        [](QIODevice& device, const QSettings::SettingsMap& map) {
            // A synchronous writer can expose an event/lifetime boundary.
            auto* view = qobject_cast<table*>(
                qApp->property("start_test_view").value<QObject*>()
            );
            if (view) {
                if (qApp->property("start_test_destroy").toBool()) {
                    qApp->setProperty("start_test_view", {});
                    delete view;
                } else {
                    view->resize(1, 1);
                }
            }
            QDataStream stream(&device);
            stream << map;
            return stream.status() == QDataStream::Ok;
        }
    );
    QTemporaryDir directory;
    QSettings storage(directory.filePath("recovery.start_geometry"), format);
    gameplay_recovery_service recovery(storage, { 1, 64 });
    gameplay_reservation binding;
    auto* view = new table;
    QPointer<table> guard(view);
    view->resize(1000, 800);
    QVERIFY(view->install_gameplay_session(owned_clock_fixture(1)));
    const auto expected = gameplay::capture_start(*view->gameplay_owner, 7);
    qApp->setProperty("start_test_view", QVariant::fromValue<QObject*>(view));
    qApp->setProperty("start_test_destroy", destroy);
    QString error;
    const bool started = binding.start(*view, recovery, 7, &error);
    qApp->setProperty("start_test_view", {});
    qApp->setProperty("start_test_destroy", {});
    QVERIFY(!started && !error.isEmpty());
    QVERIFY(binding.sequence() == 1);
    const auto record = recovery.load();
    QVERIFY(
        record
        && std::get<gameplay_active_record>(*record).checkpoint.state
            == *expected
    );
    QSettings reopened(directory.filePath("recovery.start_geometry"), format);
    QVERIFY(gameplay_recovery_service(reopened, { 1, 64 }).load() == record);
    QCOMPARE(guard.isNull(), destroy);
    if (guard) {
        QVERIFY(!binding.owns(*guard));
        QVERIFY(
            !guard->gameplay_runtime_attached
            && !guard->gameplay_timer.isActive()
        );
        QCOMPARE(
            guard->gameplay_owner->phase(), gameplay::session_phase::setup
        );
    } else {
        guard = new table;
    }
    // The saved state can recover Paused, without a new identity or Start.
    guard->resize(1000, 800);
    QVERIFY(guard->restore_gameplay_checkpoint(
        std::get<gameplay_active_record>(*record).checkpoint
    ));
    QCOMPARE(guard->gameplay_owner->phase(), gameplay::session_phase::paused);
    QVERIFY(!binding.owns(*guard)); // raw restore never binds an identity
    delete guard;
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
