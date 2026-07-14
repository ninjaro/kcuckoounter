#include "monitor/debug_broadcaster.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>

#if defined(Q_OS_UNIX)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

bool endpoint_has_live_owner(const QString& endpoint_path) {
#if defined(Q_OS_UNIX)
    const QByteArray encoded_path = QFile::encodeName(endpoint_path);
    sockaddr_un address {};
    if (encoded_path.isEmpty()
        || encoded_path.size()
            >= static_cast<qsizetype>(sizeof(address.sun_path))) {
        // An address-in-use endpoint that cannot be probed safely must never
        // be unlinked speculatively.
        return true;
    }
    address.sun_family = AF_UNIX;
    std::memcpy(
        address.sun_path, encoded_path.constData(),
        static_cast<std::size_t>(encoded_path.size() + 1)
    );
    const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) {
        return true;
    }
    const auto address_size = static_cast<socklen_t>(
        offsetof(sockaddr_un, sun_path)
        + static_cast<std::size_t>(encoded_path.size() + 1)
    );
    const bool connected
        = ::connect(
              descriptor, reinterpret_cast<const sockaddr*>(&address),
              address_size
          )
        == 0;
    const int connect_error = errno;
    ::close(descriptor);
    if (connected) {
        return true;
    }
    return connect_error != ECONNREFUSED && connect_error != ENOENT;
#else
    QLocalSocket probe;
    QEventLoop event_loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &probe, &QLocalSocket::connected, &event_loop, &QEventLoop::quit
    );
    QObject::connect(
        &probe, &QLocalSocket::errorOccurred, &event_loop, &QEventLoop::quit
    );
    QObject::connect(
        &timeout, &QTimer::timeout, &event_loop, &QEventLoop::quit
    );

    probe.connectToServer(endpoint_path);
    if (probe.state() != QLocalSocket::ConnectedState) {
        timeout.start(500);
        event_loop.exec();
    }
    const bool connected = probe.state() == QLocalSocket::ConnectedState;
    probe.abort();
    return connected;
#endif
}

} // namespace

debug_broadcaster::debug_broadcaster(
    QObject* parent, qint64 max_queue_bytes, qint64 socket_backpressure_bytes
)
    : QObject(parent)
    , queue_byte_limit(std::max<qint64>(32 * 1024, max_queue_bytes))
    , socket_backpressure_byte_limit(
          std::max<qint64>(8 * 1024, socket_backpressure_bytes)
      )
    , runtime_enabled(false)
    , endpoint_name()
    , endpoint_lock()
    , local_server(nullptr)
    , listener_socket(nullptr)
    , pending_packets()
    , pending_packet_bytes(0)
    , sent_messages(0)
    , dropped_low_priority_messages(0)
    , dropped_medium_priority_messages(0)
    , dropped_high_priority_messages(0)
    , write_error_count(0) { }

debug_broadcaster::~debug_broadcaster() { close_transport(); }

bool debug_broadcaster::set_enabled(
    bool enabled, const QString& requested_endpoint_name
) {
    if (!compile_time_enabled()) {
        runtime_enabled = false;
        close_transport();
        if (enabled) {
            emit warning_raised(
                QStringLiteral("broadcaster_compile_time_disabled"),
                QStringLiteral("debug broadcaster unavailable in this build")
            );
        }
        return false;
    }

    if (!enabled) {
        runtime_enabled = false;
        close_transport();
        return true;
    }

    if (runtime_enabled) {
        return true;
    }

    const QString requested = requested_endpoint_name.trimmed().isEmpty()
        ? build_default_endpoint_name()
        : requested_endpoint_name;
    const QString endpoint_path = endpoint_path_for_requested_name(requested);
    if (endpoint_path.isEmpty()) {
        emit warning_raised(
            QStringLiteral("broadcaster_invalid_endpoint"),
            QStringLiteral("unable to determine local IPC endpoint")
        );
        return false;
    }

    auto lock
        = std::make_unique<QLockFile>(endpoint_path + QStringLiteral(".lock"));
    if (!lock->tryLock(0)) {
        if (!endpoint_has_live_owner(endpoint_path)) {
            lock->removeStaleLockFile();
        }
        if (!lock->tryLock(0)) {
            emit warning_raised(
                QStringLiteral("broadcaster_endpoint_in_use"),
                QStringLiteral("telemetry endpoint ownership is already locked")
            );
            return false;
        }
    }

    if (endpoint_has_live_owner(endpoint_path)) {
        emit warning_raised(
            QStringLiteral("broadcaster_endpoint_in_use"),
            QStringLiteral("telemetry endpoint already has a live owner")
        );
        return false;
    }
    if (QFileInfo::exists(endpoint_path)
        && !QLocalServer::removeServer(endpoint_path)) {
        emit warning_raised(
            QStringLiteral("broadcaster_listen_failed"),
            QStringLiteral("unable to remove a stale telemetry endpoint")
        );
        return false;
    }

    auto* server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    QObject::connect(
        server, &QLocalServer::newConnection, this,
        &debug_broadcaster::on_new_connection
    );

    if (!server->listen(endpoint_path)) {
        emit warning_raised(
            QStringLiteral("broadcaster_listen_failed"), server->errorString()
        );
        server->deleteLater();
        return false;
    }

    local_server = server;
    endpoint_name = endpoint_path;
    endpoint_lock = std::move(lock);
    runtime_enabled = true;
    return true;
}

bool debug_broadcaster::is_enabled() const { return runtime_enabled; }

debug_broadcaster::runtime_state debug_broadcaster::state() const {
    return runtime_state {
        .compile_time_enabled = compile_time_enabled(),
        .runtime_enabled = runtime_enabled,
        .listener_connected = listener_socket != nullptr
            && listener_socket->state() == QLocalSocket::ConnectedState,
        .endpoint_name = endpoint_name,
        .queued_messages = static_cast<qint64>(pending_packets.size()),
        .queued_bytes = pending_packet_bytes,
        .sent_messages = sent_messages,
        .dropped_low_priority_messages = dropped_low_priority_messages,
        .dropped_medium_priority_messages = dropped_medium_priority_messages,
        .dropped_high_priority_messages = dropped_high_priority_messages,
        .write_error_count = write_error_count,
    };
}

void debug_broadcaster::discard_pending_messages() {
    pending_packets.clear();
    pending_packet_bytes = 0;
}

QString debug_broadcaster::endpoint_path_for_requested_name(
    const QString& requested_endpoint_name
) {
    QString requested = requested_endpoint_name.trimmed();
    if (requested.isEmpty()) {
        return {};
    }
    if (QDir::isAbsolutePath(requested)) {
        return requested;
    }
    const QString endpoint = sanitize_endpoint_name(requested);
    return endpoint.isEmpty()
        ? QString()
        : QDir::temp().filePath(QStringLiteral("%1.sock").arg(endpoint));
}

bool debug_broadcaster::publish_json(
    const QJsonObject& message, message_priority priority, bool droppable
) {
    if (!runtime_enabled) {
        return false;
    }

    QJsonDocument document(message);
    QByteArray payload = document.toJson(QJsonDocument::Compact);
    payload.append('\n');

    const auto payload_bytes = static_cast<qint64>(payload.size());
    if (!make_room_for_packet(payload_bytes, priority, droppable)) {
        mark_dropped(priority);
        return false;
    }

    enqueue_packet(
        queued_packet {
            .payload = std::move(payload),
            .priority = priority,
            .droppable = droppable,
        }
    );
    try_flush();
    return true;
}

void debug_broadcaster::on_new_connection() {
    if (local_server == nullptr) {
        return;
    }

    QLocalSocket* selected_socket = nullptr;
    const bool already_connected = listener_socket != nullptr
        && listener_socket->state() == QLocalSocket::ConnectedState;
    while (local_server->hasPendingConnections()) {
        QLocalSocket* candidate = local_server->nextPendingConnection();
        if (!already_connected) {
            if (selected_socket != nullptr) {
                selected_socket->close();
                selected_socket->deleteLater();
            }
            selected_socket = candidate;
        } else {
            candidate->close();
            candidate->deleteLater();
        }
    }
    if (selected_socket == nullptr) {
        return;
    }

    if (listener_socket != nullptr) {
        listener_socket->disconnect(this);
        listener_socket->close();
        listener_socket->deleteLater();
        listener_socket = nullptr;
    }

    attach_socket(selected_socket);
    emit listener_connection_changed(true);
    try_flush();
}

void debug_broadcaster::on_socket_disconnected() {
    clear_socket();
    emit listener_connection_changed(false);
}

void debug_broadcaster::on_socket_bytes_written(qint64 bytes_written) {
    Q_UNUSED(bytes_written);
    try_flush();
}

bool debug_broadcaster::compile_time_enabled() {
#if defined(NDEBUG)
    return false;
#else
    return true;
#endif
}

QString debug_broadcaster::build_default_endpoint_name() {
    const QString suffix
        = QUuid::createUuid().toString(QUuid::WithoutBraces).left(6);
    return QStringLiteral("cppr_%1_%2")
        .arg(QCoreApplication::applicationPid())
        .arg(suffix);
}

QString debug_broadcaster::sanitize_endpoint_name(const QString& raw_name) {
    QString endpoint = raw_name.trimmed();
    endpoint.replace(
        QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")),
        QStringLiteral("_")
    );

    constexpr int max_endpoint_len = 24;
    if (endpoint.size() > max_endpoint_len) {
        endpoint = endpoint.left(12) + QLatin1Char('_') + endpoint.right(11);
    }
    return endpoint;
}

void debug_broadcaster::close_transport() {
    clear_socket();

    if (local_server != nullptr) {
        const QString existing_endpoint = endpoint_name;
        local_server->close();
        local_server->deleteLater();
        local_server = nullptr;
        if (!existing_endpoint.isEmpty()) {
            QLocalServer::removeServer(existing_endpoint);
        }
    }

    pending_packets.clear();
    pending_packet_bytes = 0;
    endpoint_name.clear();
    endpoint_lock.reset();
}

void debug_broadcaster::mark_dropped(message_priority priority) {
    switch (priority) {
    case message_priority::high:
        ++dropped_high_priority_messages;
        return;
    case message_priority::medium:
        ++dropped_medium_priority_messages;
        return;
    case message_priority::low:
    default:
        ++dropped_low_priority_messages;
        return;
    }
}

void debug_broadcaster::enqueue_packet(queued_packet&& packet) {
    pending_packet_bytes += static_cast<qint64>(packet.payload.size());
    pending_packets.push_back(std::move(packet));
}

bool debug_broadcaster::drop_oldest_packet_of_priority(
    message_priority priority
) {
    for (int index = 0; index < pending_packets.size(); ++index) {
        const queued_packet& packet = pending_packets.at(index);
        if (!packet.droppable || packet.priority != priority) {
            continue;
        }

        pending_packet_bytes -= static_cast<qint64>(packet.payload.size());
        pending_packets.removeAt(index);
        mark_dropped(priority);
        return true;
    }

    return false;
}

bool debug_broadcaster::make_room_for_packet(
    qint64 packet_bytes, message_priority incoming_priority,
    bool incoming_droppable
) {
    if (packet_bytes <= 0) {
        return false;
    }
    if (packet_bytes > queue_byte_limit && incoming_droppable) {
        return false;
    }
    if (pending_packet_bytes + packet_bytes <= queue_byte_limit) {
        return true;
    }

    QVector<message_priority> drop_order;
    switch (incoming_priority) {
    case message_priority::high:
        drop_order = {
            message_priority::low,
            message_priority::medium,
            message_priority::high,
        };
        break;
    case message_priority::medium:
        drop_order = {
            message_priority::low,
            message_priority::medium,
        };
        break;
    case message_priority::low:
    default:
        drop_order = {
            message_priority::low,
        };
        break;
    }

    while (pending_packet_bytes + packet_bytes > queue_byte_limit) {
        bool dropped = false;
        for (message_priority priority : drop_order) {
            if (drop_oldest_packet_of_priority(priority)) {
                dropped = true;
                break;
            }
        }
        if (!dropped) {
            return false;
        }
    }

    return true;
}

void debug_broadcaster::try_flush() {
    if (listener_socket == nullptr
        || listener_socket->state() != QLocalSocket::ConnectedState) {
        return;
    }

    while (!pending_packets.isEmpty()) {
        if (listener_socket->bytesToWrite() >= socket_backpressure_byte_limit) {
            break;
        }

        queued_packet& packet = pending_packets.first();
        const qint64 written = listener_socket->write(packet.payload);
        if (written < 0) {
            ++write_error_count;
            emit warning_raised(
                QStringLiteral("broadcaster_write_failed"),
                listener_socket->errorString()
            );
            clear_socket();
            emit listener_connection_changed(false);
            return;
        }
        if (written == 0) {
            break;
        }

        const auto packet_payload_bytes
            = static_cast<qint64>(packet.payload.size());
        if (written >= packet_payload_bytes) {
            pending_packet_bytes -= packet_payload_bytes;
            pending_packets.removeFirst();
            ++sent_messages;
            continue;
        }

        const int consumed_bytes = static_cast<int>(written);
        packet.payload.remove(0, consumed_bytes);
        pending_packet_bytes -= written;
        break;
    }
}

void debug_broadcaster::attach_socket(QLocalSocket* socket) {
    if (socket == nullptr) {
        return;
    }

    listener_socket = socket;
    QObject::connect(
        listener_socket, &QLocalSocket::disconnected, this,
        &debug_broadcaster::on_socket_disconnected
    );
    QObject::connect(
        listener_socket, &QLocalSocket::bytesWritten, this,
        &debug_broadcaster::on_socket_bytes_written
    );
    QObject::connect(
        listener_socket, &QLocalSocket::readyRead, this,
        &debug_broadcaster::on_socket_ready_read
    );
}

void debug_broadcaster::on_socket_ready_read() {
    if (listener_socket != nullptr) {
        listener_socket->readAll();
    }
}

void debug_broadcaster::clear_socket() {
    if (listener_socket == nullptr) {
        return;
    }
    listener_socket->disconnect(this);
    listener_socket->close();
    listener_socket->deleteLater();
    listener_socket = nullptr;
}
