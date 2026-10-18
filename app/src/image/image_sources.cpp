#include "image/image_sources.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QThread>
#include <array>
#include <limits>
#include <stdexcept>
#include <zlib.h>

namespace {
QByteArray decode_source(QByteArray bytes) {
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

rasterflow::qt::source_handle
load_image_source(const QString& path, QString* error) {
    if (error)
        error->clear();
    try {
        QFile file(path);
        if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
            throw std::runtime_error("cannot open SVG source");
        const auto encoded = file.readAll();
        if (file.error() != QFileDevice::NoError)
            throw std::runtime_error("cannot read SVG source");
        const auto bytes = decode_source(encoded);
        if (bytes.isEmpty())
            throw std::runtime_error("SVG source is empty");
        const auto digest
            = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                  .toHex();
        return application_image_service().register_source(
            bytes, path + QLatin1Char('@') + QString::fromLatin1(digest)
        );
    } catch (const std::runtime_error& exception) {
        if (error)
            *error = QString::fromUtf8(exception.what());
        return {};
    }
}
