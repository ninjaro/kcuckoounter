#ifndef KCUCKOOUNTER_TESTS_IMAGE_LIFETIME_FIXTURE_HPP
#define KCUCKOOUNTER_TESTS_IMAGE_LIFETIME_FIXTURE_HPP

#include <QFuture>
#include <QImage>
#include <QPromise>

#include <atomic>
#include <memory>
#include <utility>

namespace image_lifetime_test {

struct accounting {
    std::atomic<qsizetype> live_bytes { 0 };
    std::atomic<int> live_images { 0 };
};

struct pixel_buffer {
    std::shared_ptr<accounting> totals;
    std::unique_ptr<uchar[]> pixels;
    qsizetype bytes;
};

inline void release_pixels(void* context) {
    const std::unique_ptr<pixel_buffer> buffer(
        static_cast<pixel_buffer*>(context)
    );
    buffer->totals->live_bytes -= buffer->bytes;
    --buffer->totals->live_images;
}

// Count unique external buffers, not implicitly shared QImage handles or RSS.
inline QImage
tracked_image(const std::shared_ptr<accounting>& totals, int side) {
    const qsizetype bytes = static_cast<qsizetype>(side) * side * 4;
    auto buffer = std::make_unique<pixel_buffer>(pixel_buffer {
        totals,
        std::make_unique<uchar[]>(static_cast<std::size_t>(bytes)),
        bytes,
    });
    QImage image(
        buffer->pixels.get(), side, side, side * 4,
        QImage::Format_ARGB32_Premultiplied, release_pixels, buffer.get()
    );
    if (image.isNull())
        return {};
    const auto* const image_owned = buffer.release();
    image_owned->totals->live_bytes += image_owned->bytes;
    ++image_owned->totals->live_images;
    image.fill(Qt::red);
    return image;
}

template <typename T> QFuture<T> completed_future(T value) {
    QPromise<T> producer;
    producer.start();
    if (!producer.addResult(std::move(value)))
        qFatal("Could not publish completed test future");
    producer.finish();
    return producer.future();
}

} // namespace image_lifetime_test

#endif // KCUCKOOUNTER_TESTS_IMAGE_LIFETIME_FIXTURE_HPP
