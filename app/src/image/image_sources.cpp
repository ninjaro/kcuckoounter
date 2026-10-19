#include "image/image_sources.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <zlib.h>

namespace {
QByteArray decode_source(
    QByteArray bytes, const std::shared_ptr<std::atomic_bool>& canceled
) {
    if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0x1F
        || static_cast<unsigned char>(bytes[1]) != 0x8B)
        return bytes;

    if (bytes.size() > std::numeric_limits<uInt>::max())
        throw std::runtime_error("compressed SVG source is too large");
    z_stream stream {};
    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK)
        throw std::runtime_error("cannot initialize SVGZ decoder");

    struct stream_cleanup {
        z_stream* value;

        ~stream_cleanup() { inflateEnd(value); }
    } cleanup { &stream };

    stream.next_in = reinterpret_cast<Bytef*>(bytes.data());
    stream.avail_in = static_cast<uInt>(bytes.size());
    QByteArray decoded;
    std::array<char, 64 * 1024> block {};
    int result = Z_OK;
    while (result == Z_OK) {
        if (canceled->load())
            return {};
        stream.next_out = reinterpret_cast<Bytef*>(block.data());
        stream.avail_out = static_cast<uInt>(block.size());
        result = inflate(&stream, Z_NO_FLUSH);
        decoded.append(
            block.data(),
            static_cast<qsizetype>(block.size() - stream.avail_out)
        );
    }
    if (result != Z_STREAM_END || stream.avail_in != 0)
        throw std::runtime_error("invalid or truncated SVGZ source");
    return decoded;
}

QCoreApplication* require_application_thread() {
    auto* application = QCoreApplication::instance();
    if (!application || QThread::currentThread() != application->thread())
        throw std::logic_error("image sources require the application thread");
    return application;
}

struct acquired_source {
    rasterflow::qt::source_content content;
    QString error;
    std::uint64_t bytes_read {};
    std::uint64_t decoded_bytes {};
    std::uint64_t elapsed_us {};
};

acquired_source read_source(
    const QString& path, const std::shared_ptr<std::atomic_bool>& canceled
) {
    acquired_source result;
    if (canceled->load())
        return result;
    QElapsedTimer clock;
    clock.start();
    try {
        QFile file(path);
        if (path.isEmpty() || !QFileInfo(path).isFile()
            || !file.open(QIODevice::ReadOnly))
            throw std::runtime_error("cannot open SVG source");
        QByteArray encoded;
        const auto size = file.size();
        if (size > 0 && size <= std::numeric_limits<qsizetype>::max())
            encoded.reserve(static_cast<qsizetype>(size));
        while (!file.atEnd()) {
            if (canceled->load())
                break;
            const auto block = file.read(64 * 1024);
            if (file.error() != QFileDevice::NoError)
                throw std::runtime_error("cannot read SVG source");
            result.bytes_read += static_cast<std::uint64_t>(block.size());
            encoded.append(block);
        }
        if (!canceled->load()) {
            auto bytes = decode_source(std::move(encoded), canceled);
            if (!canceled->load()) {
                if (bytes.isEmpty())
                    throw std::runtime_error("SVG source is empty");
                result.decoded_bytes = static_cast<std::uint64_t>(bytes.size());
                result.content
                    = rasterflow::qt::source_content(std::move(bytes));
            }
        }
    } catch (const std::exception& exception) {
        result.error = QString::fromUtf8(exception.what());
    }
    result.elapsed_us = static_cast<std::uint64_t>(clock.nsecsElapsed() / 1000);
    return result;
}
}

class image_source_loader final : public QObject {
public:
    explicit image_source_loader(QCoreApplication* application)
        : QObject(application) {
        pool.setMaxThreadCount(1);
    }

    ~image_source_loader() override {
        if (active) {
            active->canceled->store(true);
            active->watcher->disconnect(this);
        }
        pool.waitForDone();
    }

    std::uint64_t enqueue(
        image_source_request* request, const QString& path,
        std::uint64_t previous
    ) {
        if (next_ticket == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("source acquisition ticket exhausted");
        const auto current = next_ticket++;
        queued.emplace(current, pending_source { request, path });
        cancel(previous);
        ++metrics.requested_loads;
        schedule();
        return current;
    }

    void cancel(std::uint64_t ticket) {
        queued.erase(ticket);
        if (active && active->ticket == ticket)
            active->canceled->store(true);
    }

    [[nodiscard]] source_acquisition_observations observations() const {
        auto result = metrics;
        result.queued_requests = queued.size();
        result.running_requests = active ? 1 : 0;
        return result;
    }

private:
    struct pending_source {
        QPointer<image_source_request> request;
        QString path;
    };

    struct running_source {
        std::uint64_t ticket {};
        pending_source input;
        std::shared_ptr<std::atomic_bool> canceled;
        QFutureWatcher<acquired_source>* watcher {};
    };

    QThreadPool pool;
    std::map<std::uint64_t, pending_source> queued;
    std::optional<running_source> active;
    std::uint64_t next_ticket { 1 };
    bool pump_queued {};
    source_acquisition_observations metrics;

    void schedule() {
        if (pump_queued)
            return;
        pump_queued = true;
        QTimer::singleShot(0, this, &image_source_loader::pump);
    }

    void pump() {
        pump_queued = false;
        if (active)
            return;
        while (!queued.empty()) {
            auto node = queued.extract(queued.begin());
            const auto ticket = node.key();
            auto input = std::move(node.mapped());
            if (!input.request || input.request->ticket != ticket)
                continue;
            const auto canceled = std::make_shared<std::atomic_bool>(false);
            auto* watcher = new QFutureWatcher<acquired_source>(this);
            active.emplace(ticket, input, canceled, watcher);
            ++metrics.started_requests;
            connect(
                watcher, &QFutureWatcher<acquired_source>::finished, this,
                [this, watcher, ticket] { complete(ticket, watcher); },
                Qt::QueuedConnection
            );
            watcher->setFuture(QtConcurrent::run(
                &pool,
                [path = input.path, canceled] {
                    return read_source(path, canceled);
                }
            ));
            emit input.request->started(); // Observers may retire this request.
            return;
        }
    }

    void
    complete(std::uint64_t ticket, QFutureWatcher<acquired_source>* watcher) {
        if (!active || active->ticket != ticket || active->watcher != watcher)
            return;
        auto job = std::move(*active);
        active.reset();
        acquired_source result;
        try {
            result = watcher->result();
        } catch (const std::exception& exception) {
            result.error = QString::fromUtf8(exception.what());
        }
        watcher->disconnect(this);
        watcher->deleteLater();
        metrics.bytes_read += result.bytes_read;
        metrics.decoded_bytes += result.decoded_bytes;
        metrics.acquisition_us += result.elapsed_us;
        schedule();
        auto* request = job.input.request.data();
        if (job.canceled->load() || !request || request->ticket != job.ticket) {
            ++metrics.retired_completions;
            return;
        }
        if (result.error.isEmpty()) {
            QElapsedTimer clock;
            clock.start();
            try {
                request->snapshot
                    = application_image_service().register_source(result.content
                    );
            } catch (const std::exception& exception) {
                result.error = QString::fromUtf8(exception.what());
            }
            const auto elapsed
                = static_cast<std::uint64_t>(clock.nsecsElapsed() / 1000);
            metrics.owner_registration_us += elapsed;
            metrics.maximum_owner_registration_us
                = std::max(metrics.maximum_owner_registration_us, elapsed);
        }
        request->pending = false;
        request->ticket = 0;
        request->acquisition_error = result.error;
        ++metrics.completed_requests;
        if (!result.error.isEmpty())
            ++metrics.failed_requests;
        emit request->finished(
        ); // No request/service state after notification.
    }
};

namespace {
image_source_loader* application_source_loader() {
    auto* application = require_application_thread();
    static QPointer<image_source_loader> loader;
    if (!loader)
        loader = new image_source_loader(application);
    return loader;
}
}

image_source_request::image_source_request(QObject* parent)
    : QObject(parent)
    , loader(application_source_loader()) { }

image_source_request::~image_source_request() {
    if (loader)
        loader->cancel(ticket);
}

void image_source_request::load(const QString& path) {
    require_application_thread();
    if (path.isEmpty()) {
        clear();
        return;
    }
    if (!loader)
        throw std::logic_error("source acquisition service retired");
    ticket = loader->enqueue(this, path, ticket);
    snapshot = {};
    acquisition_error.clear();
    pending = true;
}

void image_source_request::clear() {
    require_application_thread();
    if (loader)
        loader->cancel(ticket);
    ticket = 0;
    pending = false;
    snapshot = {};
    acquisition_error.clear();
}

bool image_source_request::is_pending() const {
    require_application_thread();
    return pending && !loader.isNull();
}

rasterflow::qt::source_handle image_source_request::source() const {
    require_application_thread();
    return snapshot;
}

QString image_source_request::error() const {
    require_application_thread();
    if (pending && !loader)
        return QStringLiteral("source acquisition service retired");
    return acquisition_error.isEmpty() ? snapshot.error() : acquisition_error;
}

source_acquisition_observations observe_source_acquisition() {
    return application_source_loader()->observations();
}

rasterflow::qt::image_service& application_image_service() {
    auto* application = QCoreApplication::instance();
    if (!application || QThread::currentThread() != application->thread())
        throw std::logic_error("image service requires the application thread");
    static QPointer<rasterflow::qt::image_service> service;
    if (!service)
        service = new rasterflow::qt::image_service(application);
    return *service;
}
