// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "include/image_cacher_tests.hpp"
#include "image/image_cacher.hpp"
#include "include/image_source_helpers.hpp"

#include <QFile>
#include <QPainter>
#include <QSignalSpy>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest/QtTest>
#include <future>
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

void image_cacher_tests::source_acquisition_is_asynchronous_and_shares_content(
) {
    svg_fixture fixture;
    image_source_request first;
    image_source_request second;
    QSignalSpy finished(&first, &image_source_request::finished);
    first.load(fixture.path);
    second.load(fixture.path);
    QVERIFY(first.is_pending());
    QVERIFY(second.is_pending());
    QVERIFY(!first.source().is_valid());
    QCOMPARE(finished.size(), 0);
    QTRY_VERIFY(!first.is_pending() && !second.is_pending());
    QCOMPARE(finished.size(), 1);
    QVERIFY(first.error().isEmpty());
    QVERIFY(first.source().is_valid());
    QVERIFY(first.source() == second.source());
}

void image_cacher_tests::source_queue_replaces_obsolete_intent_data() {
    QTest::addColumn<bool>("running");
    QTest::newRow("queued") << false;
    QTest::newRow("started") << true;
}

void image_cacher_tests::source_queue_replaces_obsolete_intent() {
    QFETCH(bool, running);
    QTRY_COMPARE(observe_source_acquisition().running_requests, std::size_t(0));
    QTRY_COMPARE(observe_source_acquisition().queued_requests, std::size_t(0));
    svg_fixture first;
    svg_fixture last;
    const auto blue = QByteArray(last.bytes).replace("red", "blue");
    last.write(blue);
    image_source_request source;
    QSignalSpy started(&source, &image_source_request::started);
    QSignalSpy finished(&source, &image_source_request::finished);
    bool replaced = false;
    const auto replace = [&] {
        if (replaced)
            return;
        replaced = true;
        for (int index = 0; index < 4096; ++index)
            source.load(index % 2 == 0 ? first.path : last.path);
        source.load(last.path);
        QCOMPARE(observe_source_acquisition().queued_requests, std::size_t(1));
        QCOMPARE(
            observe_source_acquisition().running_requests,
            running ? std::size_t(1) : std::size_t(0)
        );
    };
    if (running) {
        connect(&source, &image_source_request::started, this, replace);
        source.load(first.path);
    } else {
        replace();
    }
    QTRY_VERIFY(!source.is_pending());
    QCOMPARE(finished.size(), 1);
    QCOMPARE(started.size(), running ? 2 : 1);
    QVERIFY(source.error().isEmpty());
    QCOMPARE(
        source.source().content_id(),
        rasterflow::qt::source_content(blue).content_id()
    );
}

void image_cacher_tests::source_started_observer_can_retire_data() {
    QTest::addColumn<QString>("action");
    for (const char* action : { "clear", "delete-request", "delete-parent" })
        QTest::newRow(action) << QString::fromLatin1(action);
}

void image_cacher_tests::source_started_observer_can_retire() {
    QFETCH(QString, action);
    svg_fixture fixture;
    auto parent = std::make_unique<QObject>();
    auto* source = new image_source_request(parent.get());
    QPointer<image_source_request> alive(source);
    QSignalSpy finished(source, &image_source_request::finished);
    bool started = false;
    connect(source, &image_source_request::started, this, [&] {
        started = true;
        if (action == QStringLiteral("clear"))
            source->clear();
        else if (action == QStringLiteral("delete-request"))
            delete source;
        else
            parent.reset();
    });
    source->load(fixture.path);
    QTRY_VERIFY(started);
    QTRY_COMPARE(observe_source_acquisition().running_requests, std::size_t(0));
    QTRY_COMPARE(observe_source_acquisition().queued_requests, std::size_t(0));
    QCOMPARE(finished.size(), 0);
    if (action == QStringLiteral("clear")) {
        QVERIFY(alive);
        QVERIFY(!source->is_pending());
        QVERIFY(!source->source().is_valid());
    } else {
        QVERIFY(!alive);
    }
    image_source_request recovery;
    recovery.load(fixture.path);
    QTRY_VERIFY(!recovery.is_pending());
    QVERIFY(recovery.source().is_valid());
    QVERIFY(recovery.error().isEmpty());
}

void image_cacher_tests::source_finished_observer_can_retarget_data() {
    QTest::addColumn<QString>("action");
    for (const char* action : { "replace", "clear", "delete" })
        QTest::newRow(action) << QString::fromLatin1(action);
}

void image_cacher_tests::source_finished_observer_can_retarget() {
    QFETCH(QString, action);
    svg_fixture first;
    svg_fixture last;
    const auto blue = QByteArray(last.bytes).replace("red", "blue");
    last.write(blue);
    auto source = std::make_unique<image_source_request>();
    bool changed = false;
    connect(source.get(), &image_source_request::finished, this, [&] {
        if (changed)
            return;
        changed = true;
        if (action == QStringLiteral("replace"))
            source->load(last.path);
        else if (action == QStringLiteral("clear"))
            source->clear();
        else
            source.reset();
    });
    source->load(first.path);
    QTRY_VERIFY(changed);
    if (!source)
        return;
    QTRY_VERIFY(!source->is_pending());
    if (action == QStringLiteral("clear"))
        QVERIFY(!source->source().is_valid());
    else
        QCOMPARE(
            source->source().content_id(),
            rasterflow::qt::source_content(blue).content_id()
        );
}

void image_cacher_tests::source_acquisition_errors_recover() {
    svg_fixture fixture;
    image_source_request source;
    source.load(fixture.directory.filePath(QStringLiteral("missing.svg")));
    QVERIFY(source.is_pending());
    QTRY_VERIFY(!source.is_pending());
    QVERIFY(!source.source().is_valid());
    QCOMPARE(source.error(), QStringLiteral("cannot open SVG source"));
    source.load(fixture.path);
    QVERIFY(source.error().isEmpty());
    QTRY_VERIFY(!source.is_pending());
    QVERIFY(source.source().is_valid());
    QVERIFY(source.error().isEmpty());
}

void image_cacher_tests::source_acquisition_rejects_foreign_thread_access() {
    svg_fixture fixture;
    image_source_request source(this);
    auto rejected = std::async(std::launch::async, [&] {
        bool load_rejected = false;
        bool read_rejected = false;
        try {
            source.load(fixture.path);
        } catch (const std::logic_error&) {
            load_rejected = true;
        }
        try {
            static_cast<void>(source.source());
        } catch (const std::logic_error&) {
            read_rejected = true;
        }
        return load_rejected && read_rejected;
    });
    QVERIFY(rejected.get());
    QVERIFY(!source.is_pending());
}

void image_cacher_tests::rasterizes_svg_for_target_size() {
    svg_fixture fixture;
    image_cacher cacher(fixture.path);
    QSignalSpy changes(&cacher, &image_cacher::pixmap_changed);
    cacher.set_target_size(QSize(64, 64));
    QTRY_VERIFY(cacher.has_source());
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
    auto original = await_image_source(fixture.path);
    fixture.write(QByteArray(fixture.bytes).replace("red", "blue"));
    auto replacement = await_image_source(fixture.path);
    QVERIFY(original.content_id() != replacement.content_id());
    QTRY_VERIFY(original.is_ready() && replacement.is_ready());
    QVERIFY(original.contains({}));
    QVERIFY(replacement.contains({}));
}

void image_cacher_tests::source_identity_does_not_need_path_hash_labels() {
    svg_fixture fixture;
    svg_fixture copy;
    copy.write(fixture.bytes);
    auto& service = application_image_service();
    const auto anchor = service.register_source(
        fixture.bytes, QStringLiteral("loader-alias-baseline")
    );
    const auto aliases = service.observations().source_aliases;
    const auto original = await_image_source(fixture.path);
    const auto shared = await_image_source(copy.path);
    QVERIFY(original.is_valid());
    QVERIFY(original == anchor);
    QVERIFY(original == shared);
    QCOMPARE(service.observations().source_aliases, aliases);
    fixture.write(QByteArray(fixture.bytes).replace("red", "blue"));
    const auto changed = await_image_source(fixture.path);
    QVERIFY(changed.is_valid());
    QVERIFY(original.content_id() != changed.content_id());
    fixture.write(fixture.bytes);
    QVERIFY(await_image_source(fixture.path) == original);
    QCOMPARE(service.observations().source_aliases, aliases);
    QTRY_VERIFY(original.is_ready() && changed.is_ready());
}

void image_cacher_tests::decodes_svgz_and_rejects_truncation() {
    svg_fixture fixture;
    auto plain = await_image_source(fixture.path);
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
    auto compressed = await_image_source(fixture.path, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(plain.content_id(), compressed.content_id());
    QTRY_VERIFY(compressed.is_ready());
    image_cacher cacher(fixture.path);
    cacher.set_target_size(QSize(72, 36));
    QTRY_VERIFY(cacher.is_ready());
    encoded.chop(3);
    fixture.write(encoded);
    QVERIFY(!await_image_source(fixture.path, &error).is_valid());
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
    QVERIFY(!await_image_source(fixture.path, &error).is_valid());
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
