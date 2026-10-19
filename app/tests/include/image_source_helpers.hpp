#ifndef KCUCKOOUNTER_TESTS_IMAGE_SOURCE_HELPERS_HPP
#define KCUCKOOUNTER_TESTS_IMAGE_SOURCE_HELPERS_HPP

#include "image/image_sources.hpp"
#include <QtTest/QtTest>

// Tests await the actual asynchronous acquisition path; no alternate reader,
// decoder, renderer or cache is supplied by this helper.
inline rasterflow::qt::source_handle
await_image_source(const QString& path, QString* error = nullptr) {
    image_source_request request;
    request.load(path);
    if (!QTest::qWaitFor([&] { return !request.is_pending(); }, 15000)) {
        QTest::qFail("SVG acquisition did not complete", __FILE__, __LINE__);
        return {};
    }
    if (error)
        *error = request.error();
    return request.source();
}

#endif
