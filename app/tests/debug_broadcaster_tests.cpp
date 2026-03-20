#include "include/debug_broadcaster_tests.hpp"

#include "monitor/debug_broadcaster.hpp"
#include "monitor/debug_probe_core.hpp"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSet>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest/QtTest>

static QSet<QString> parse_message_families_from_jsonl(const QByteArray& jsonl) {
    QSet<QString> families;
    const QList<QByteArray> lines = jsonl.split('\n');
    for (const QByteArray& line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(trimmed);
        if (!document.isObject()) {
            continue;
        }

        const QJsonObject protocol
            = document.object().value(QStringLiteral("protocol_v1")).toObject();
        const QString family
            = protocol.value(QStringLiteral("message_family")).toString();
        if (!family.isEmpty()) {
            families.insert(family);
        }
    }
    return families;
}

static debug_probe_core::protocol_identity test_identity() {
    return debug_probe_core::protocol_identity {
        .app_name = QStringLiteral("cppr"),
        .process_id = 42,
        .session_id = QStringLiteral("session-test"),
        .build_id = QStringLiteral("build-test"),
        .protocol_version = debug_probe_core::protocol_version_string(),
        .debug_flags = QStringList() << QStringLiteral("debug_build"),
        .instrumentation_mode = QStringLiteral("realistic"),
    };
}

static QJsonObject build_message(
    const QString& family, qint64 monotonic_timestamp_ms,
    const QString& payload_label
) {
    QJsonObject payload;
    payload.insert(QStringLiteral("label"), payload_label);
    return debug_probe_core::build_protocol_message_v1(
        family, test_identity(), monotonic_timestamp_ms, payload
    );
}

static bool wait_for_message_families(
    QLocalSocket* socket, QByteArray* jsonl_buffer,
    const QSet<QString>& required_families, int timeout_ms
) {
    if (socket == nullptr || jsonl_buffer == nullptr) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms) {
        socket->waitForReadyRead(100);
        jsonl_buffer->append(socket->readAll());
        const QSet<QString> seen_families
            = parse_message_families_from_jsonl(*jsonl_buffer);
        bool all_present = true;
        for (const QString& family : required_families) {
            if (!seen_families.contains(family)) {
                all_present = false;
                break;
            }
        }
        if (all_present) {
            return true;
        }
    }

    return false;
}

void debug_broadcaster_tests::is_disabled_until_explicitly_enabled() {
    debug_broadcaster broadcaster;
    QVERIFY(!broadcaster.is_enabled());

    const debug_broadcaster::runtime_state initial = broadcaster.state();
    QVERIFY(!initial.runtime_enabled);
}

void debug_broadcaster_tests::publishes_messages_over_local_ipc() {
    debug_broadcaster broadcaster(nullptr, 256 * 1024, 64 * 1024);
    if (!broadcaster.state().compile_time_enabled) {
        QSKIP("debug broadcaster is compile-time disabled in this build");
    }

    QSignalSpy warning_spy(&broadcaster, &debug_broadcaster::warning_raised);
    const QString endpoint_name
        = QStringLiteral("cppr_dbg_broadcast_%1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const bool enabled = broadcaster.set_enabled(true, endpoint_name);
    QString warning_details;
    if (!warning_spy.isEmpty()) {
        const QList<QVariant> args = warning_spy.takeFirst();
        warning_details = args.at(1).toString();
    }
    if (!enabled) {
        QSKIP(qPrintable(
            QStringLiteral("unable to enable local IPC broadcaster: %1")
                .arg(warning_details)
        ));
    }
    QVERIFY(broadcaster.is_enabled());

    const QString active_endpoint = broadcaster.state().endpoint_name;
    QVERIFY(!active_endpoint.isEmpty());

    QLocalSocket socket;
    socket.connectToServer(active_endpoint);
    QVERIFY2(socket.waitForConnected(3000), qPrintable(socket.errorString()));

    QVERIFY(broadcaster.publish_json(
        build_message(QStringLiteral("hello"), 1, QStringLiteral("h")),
        debug_broadcaster::message_priority::high, false
    ));
    QVERIFY(broadcaster.publish_json(
        build_message(
            QStringLiteral("sample_batch"), 2, QStringLiteral("sample")
        ),
        debug_broadcaster::message_priority::low, true
    ));

    QByteArray jsonl_buffer;
    QVERIFY(wait_for_message_families(
        &socket, &jsonl_buffer,
        QSet<QString> {
            QStringLiteral("hello"),
            QStringLiteral("sample_batch"),
        },
        3000
    ));
}

void debug_broadcaster_tests::
    backpressure_drops_low_priority_before_high_priority() {
    debug_broadcaster broadcaster(nullptr, 512, 64 * 1024);
    if (!broadcaster.state().compile_time_enabled) {
        QSKIP("debug broadcaster is compile-time disabled in this build");
    }

    QSignalSpy warning_spy(&broadcaster, &debug_broadcaster::warning_raised);
    const QString endpoint_name
        = QStringLiteral("cppr_dbg_backpressure_%1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const bool enabled = broadcaster.set_enabled(true, endpoint_name);
    QString warning_details;
    if (!warning_spy.isEmpty()) {
        const QList<QVariant> args = warning_spy.takeFirst();
        warning_details = args.at(1).toString();
    }
    if (!enabled) {
        QSKIP(qPrintable(
            QStringLiteral("unable to enable local IPC broadcaster: %1")
                .arg(warning_details)
        ));
    }
    QVERIFY(broadcaster.is_enabled());
    QVERIFY(!broadcaster.state().endpoint_name.isEmpty());

    for (int index = 0; index < 32; ++index) {
        const QString payload = QStringLiteral("low_%1_%2")
                                    .arg(index)
                                    .arg(QString(64, QLatin1Char('x')));
        broadcaster.publish_json(
            build_message(QStringLiteral("sample_batch"), index + 1, payload),
            debug_broadcaster::message_priority::low, true
        );
    }

    QVERIFY(broadcaster.publish_json(
        build_message(
            QStringLiteral("marker"), 5000, QStringLiteral("must_survive")
        ),
        debug_broadcaster::message_priority::high, false
    ));

    QLocalSocket socket;
    socket.connectToServer(endpoint_name);
    QVERIFY2(socket.waitForConnected(3000), qPrintable(socket.errorString()));

    QByteArray jsonl_buffer;
    QVERIFY(wait_for_message_families(
        &socket, &jsonl_buffer, QSet<QString> { QStringLiteral("marker") }, 3000
    ));

    const debug_broadcaster::runtime_state state = broadcaster.state();
    QVERIFY(state.dropped_low_priority_messages > 0);
}
