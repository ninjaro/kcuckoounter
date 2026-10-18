// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "include/image_cacher_tests.hpp"
#include "image/image_cacher.hpp"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest/QtTest>
#include <limits>
#include <memory>
#include <zlib.h>

namespace {
struct svg_fixture {
    QTemporaryDir directory;
    QString path = directory.filePath(QStringLiteral("image.svg"));
    QByteArray bytes
        = QByteArray(
              "<svg xmlns='http://www.w3.org/2000/svg' width='100' "
              "height='50'><rect width='100' height='50' fill='red'/><!--"
          )
        + QUuid::createUuid().toByteArray() + "--></svg>";

    svg_fixture() { write(bytes); }

    void write(const QByteArray& content) const {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(content) != content.size())
            qFatal("cannot write image fixture");
    }
};
}

void image_cacher_tests::rasterizes_svg_for_target_size() {
    svg_fixture fixture;
    image_cacher cacher(fixture.path);
    QSignalSpy changes(&cacher, &image_cacher::pixmap_changed);
    cacher.set_target_size(QSize(64, 64));
    QVERIFY(cacher.has_source());
    QTRY_VERIFY(cacher.is_ready());
    QVERIFY(!changes.isEmpty());
    QCOMPARE(cacher.display_size(), QSize(64, 64));
    QVERIFY(cacher.pixmap().width() > 64);
    QVERIFY(cacher.pixmap().height() > 64);
    QCOMPARE(cacher.pixmap().width(), cacher.pixmap().height() * 2);
}

void image_cacher_tests::shared_consumers_coalesce_geometry_and_dpr() {
    svg_fixture fixture;
    const auto before
        = application_image_service().observations().scheduling.completed_jobs;
    image_cacher first(fixture.path);
    image_cacher second(fixture.path);
    first.set_target_size(QSize(64, 128));
    second.set_device_pixel_ratio(2);
    second.set_target_size(QSize(128, 64));
    QTRY_VERIFY(first.is_ready() && second.is_ready());
    // GUI pixmaps may copy pixels and carry different DPR metadata. Sharing
    // is asserted at the authoritative scheduler, not the presentation layer.
    QCOMPARE(first.pixmap().size(), second.pixmap().size());
    QCOMPARE(first.pixmap().toImage().pixelColor(10, 10), QColor(Qt::red));
    QCOMPARE(second.pixmap().toImage().pixelColor(10, 10), QColor(Qt::red));
    QCOMPARE(second.pixmap().devicePixelRatio(), 2.0);
    QCOMPARE(
        application_image_service().observations().scheduling.completed_jobs
            - before,
        std::uint64_t(1)
    );
    QVERIFY(second.pixmap().width() > 256);
    QVERIFY(second.pixmap().height() > 128);
}

void image_cacher_tests::reuses_raster_inside_size_window() {
    svg_fixture fixture;
    image_cacher cacher(fixture.path);
    cacher.set_target_size(QSize(100, 50));
    QTRY_VERIFY(cacher.is_ready());
    const auto key = cacher.pixmap().cacheKey();
    const auto jobs
        = application_image_service().observations().scheduling.completed_jobs;
    cacher.set_target_size(QSize(110, 55));
    QCOMPARE(cacher.pixmap().cacheKey(), key);
    QCOMPARE(
        application_image_service().observations().scheduling.completed_jobs,
        jobs
    );
}

void image_cacher_tests::withdrawing_consumer_preserves_other_demand() {
    svg_fixture fixture;
    auto producer = std::make_unique<image_cacher>(fixture.path);
    image_cacher observer(fixture.path);
    producer->set_target_size(QSize(64, 32));
    observer.set_target_size(QSize(64, 32));
    producer.reset(); // Retire during metadata preparation / queued rendering.
    QTRY_VERIFY(observer.is_ready());
    const auto pixels = observer.pixmap().toImage();
    image_cacher third(fixture.path);
    third.set_target_size(QSize(64, 32));
    QTRY_VERIFY(third.is_ready());
    QCOMPARE(third.pixmap().toImage(), pixels);
    observer.set_target_size({});
    QVERIFY(!observer.is_ready());
    QVERIFY(third.is_ready());
}

void image_cacher_tests::device_pixel_ratio_updates_reused_presentation() {
    svg_fixture fixture;
    image_cacher cacher(fixture.path);
    cacher.set_target_size(QSize(100, 50));
    QTRY_VERIFY(cacher.is_ready());
    const auto size = cacher.pixmap().size();
    const auto jobs
        = application_image_service().observations().scheduling.completed_jobs;
    cacher.set_device_pixel_ratio(1.1);
    QCOMPARE(cacher.pixmap().devicePixelRatio(), 1.1);
    QCOMPARE(cacher.pixmap().size(), size);
    QCOMPARE(
        application_image_service().observations().scheduling.completed_jobs,
        jobs
    );
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument, cacher.set_device_pixel_ratio(0)
    );
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        cacher.set_device_pixel_ratio(std::numeric_limits<qreal>::infinity())
    );
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        cacher.set_device_pixel_ratio(std::numeric_limits<qreal>::max())
    );
    QCOMPARE(cacher.pixmap().devicePixelRatio(), 1.1);
}

void image_cacher_tests::published_pixels_outlive_producer_changes() {
    svg_fixture fixture;
    QImage retained;
    {
        image_cacher producer(fixture.path);
        producer.set_target_size(QSize(72, 36));
        QTRY_VERIFY(producer.is_ready());
        retained = producer.pixmap().toImage();
        producer.set_source({});
        QVERIFY(!producer.is_ready());
    }
    QVERIFY(!retained.isNull());
    QCOMPARE(
        retained.pixelColor(retained.width() / 2, retained.height() / 2),
        QColor(Qt::red)
    );
}

void image_cacher_tests::publication_observers_can_retarget_producer_data() {
    QTest::addColumn<QString>("change");
    for (const char* change :
         { "size", "source", "clear-size", "clear-source", "destroy" })
        QTest::newRow(change) << QString::fromLatin1(change);
}

void image_cacher_tests::publication_observers_can_retarget_producer() {
    QFETCH(QString, change);
    svg_fixture fixture;
    svg_fixture next;
    next.write(QByteArray(next.bytes).replace("red", "blue"));
    auto producer = std::make_unique<image_cacher>(fixture.path);
    bool changed = false;
    QObject::connect(producer.get(), &image_cacher::pixmap_changed, this, [&] {
        if (changed || !producer->is_ready())
            return;
        changed = true;
        if (change == QStringLiteral("size"))
            producer->set_target_size(QSize(144, 72));
        else if (change == QStringLiteral("source"))
            producer->set_source(next.path);
        else if (change == QStringLiteral("clear-size"))
            producer->set_target_size({});
        else if (change == QStringLiteral("clear-source"))
            producer->set_source({});
        else
            producer.reset();
    });
    producer->set_target_size(QSize(72, 36));
    QTRY_VERIFY(changed);
    if (!producer)
        return;
    if (change.startsWith(QStringLiteral("clear"))) {
        QVERIFY(!producer->is_ready());
        producer->set_source(fixture.path);
        producer->set_target_size(QSize(72, 36));
    }
    QTRY_VERIFY(producer->is_ready());
    if (change == QStringLiteral("size"))
        QTRY_VERIFY(producer->pixmap().width() > 144);
    if (change == QStringLiteral("source"))
        QCOMPARE(
            producer->pixmap().toImage().pixelColor(10, 10), QColor(Qt::blue)
        );
}

void image_cacher_tests::replaces_oversized_and_undersized_rasters() {
    svg_fixture fixture;
    image_cacher cacher(fixture.path);
    cacher.set_target_size(QSize(200, 100));
    QTRY_VERIFY(cacher.is_ready());
    const auto initial = cacher.pixmap().size();
    cacher.set_target_size(QSize(80, 40));
    QTRY_VERIFY(cacher.pixmap().width() < initial.width());
    cacher.set_target_size(QSize(300, 150));
    QTRY_VERIFY(cacher.pixmap().width() > 300);
    QVERIFY(cacher.pixmap().height() > 150);
}

void image_cacher_tests::empty_display_size_withdraws_demand() {
    svg_fixture fixture;
    image_cacher cacher(fixture.path);
    const auto count
        = application_image_service().observations().scheduling.consumers;
    cacher.set_target_size(QSize(100, 50));
    cacher.set_target_size({}
    ); // No stale callback may restore a cleared display.
    QTRY_COMPARE(
        application_image_service().observations().scheduling.running_jobs,
        std::size_t(0)
    );
    QVERIFY(!cacher.is_ready());
    QCOMPARE(
        application_image_service().observations().scheduling.consumers, count
    );
    cacher.set_target_size(QSize(100, 50));
    QTRY_VERIFY(cacher.is_ready());
}

void image_cacher_tests::source_replacement_has_new_content_identity() {
    svg_fixture fixture;
    auto original = load_image_source(fixture.path);
    fixture.write(QByteArray(fixture.bytes).replace("red", "blue"));
    auto replacement = load_image_source(fixture.path);
    QVERIFY(original.content_id() != replacement.content_id());
    QTRY_VERIFY(original.is_ready() && replacement.is_ready());
    QVERIFY(original.contains({}));
    QVERIFY(replacement.contains({}));
}

void image_cacher_tests::decodes_svgz_and_rejects_truncation() {
    svg_fixture fixture;
    auto plain = load_image_source(fixture.path);
    z_stream stream {};
    QCOMPARE(
        deflateInit2(
            &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8,
            Z_DEFAULT_STRATEGY
        ),
        Z_OK
    );
    QByteArray encoded(4096, '\0');
    stream.next_in = reinterpret_cast<Bytef*>(fixture.bytes.data());
    stream.avail_in = static_cast<uInt>(fixture.bytes.size());
    stream.next_out = reinterpret_cast<Bytef*>(encoded.data());
    stream.avail_out = static_cast<uInt>(encoded.size());
    const int result = deflate(&stream, Z_FINISH);
    encoded.resize(static_cast<qsizetype>(stream.total_out));
    deflateEnd(&stream);
    QCOMPARE(result, Z_STREAM_END);
    fixture.write(encoded);
    QString error;
    auto compressed = load_image_source(fixture.path, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(plain.content_id(), compressed.content_id());
    QTRY_VERIFY(compressed.is_ready());
    image_cacher cacher(fixture.path);
    cacher.set_target_size(QSize(72, 36));
    QTRY_VERIFY(cacher.is_ready());
    encoded.chop(3);
    fixture.write(encoded);
    QVERIFY(!load_image_source(fixture.path, &error).is_valid());
    QCOMPARE(error, QStringLiteral("invalid or truncated SVGZ source"));
}

void image_cacher_tests::invalid_svg_reports_failure() {
    svg_fixture fixture;
    fixture.write("not SVG");
    image_cacher cacher(fixture.path);
    cacher.set_target_size(QSize(100, 50));
    QTRY_VERIFY(!cacher.error().isEmpty());
    QVERIFY(!cacher.is_ready());
    QVERIFY(!cacher.has_source());
    fixture.write({});
    QString error;
    QVERIFY(!load_image_source(fixture.path, &error).is_valid());
    QCOMPARE(error, QStringLiteral("SVG source is empty"));
}

void image_cacher_tests::renderer_matches_reference_without_distortion() {
    svg_fixture fixture;
    image_cacher cacher(fixture.path);
    cacher.set_target_size(QSize(80, 120));
    QTRY_VERIFY(cacher.is_ready());
    const auto actual = cacher.pixmap().toImage();
    QImage reference(actual.size(), QImage::Format_ARGB32_Premultiplied);
    reference.fill(Qt::transparent);
    QPainter painter(&reference);
    QSvgRenderer renderer(fixture.bytes); // Test-only reference renderer.
    renderer.render(&painter, QRectF(QPointF(), QSizeF(reference.size())));
    painter.end();
    QCOMPARE(actual.convertToFormat(reference.format()), reference);
    QCOMPARE(actual.width(), actual.height() * 2);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
