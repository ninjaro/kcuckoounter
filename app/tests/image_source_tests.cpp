// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "include/image_source_tests.hpp"
#include "arch/asset_locator.hpp"
#include "image/image_sources.hpp"
#include "include/image_source_helpers.hpp"
#include <shared_ui/graphics/qt/svg_image_view.hpp>
#include <shared_ui/qt/presentation.hpp>

#include <QFile>
#include <QSignalSpy>
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

void image_source_tests::
    source_acquisition_is_asynchronous_and_shares_content() {
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

void image_source_tests::source_queue_replaces_obsolete_intent_data() {
    QTest::addColumn<bool>("running");
    QTest::newRow("queued") << false;
    QTest::newRow("started") << true;
}

void image_source_tests::source_queue_replaces_obsolete_intent() {
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

void image_source_tests::source_started_observer_can_retire_data() {
    QTest::addColumn<QString>("action");
    for (const char* action : { "clear", "delete-request", "delete-parent" })
        QTest::newRow(action) << QString::fromLatin1(action);
}

void image_source_tests::source_started_observer_can_retire() {
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

void image_source_tests::source_finished_observer_can_retarget_data() {
    QTest::addColumn<QString>("action");
    for (const char* action : { "replace", "clear", "delete" })
        QTest::newRow(action) << QString::fromLatin1(action);
}

void image_source_tests::source_finished_observer_can_retarget() {
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

void image_source_tests::source_acquisition_errors_recover() {
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

void image_source_tests::source_acquisition_rejects_foreign_thread_access() {
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

void image_source_tests::source_replacement_has_new_content_identity() {
    svg_fixture fixture;
    auto original = await_image_source(fixture.path);
    fixture.write(QByteArray(fixture.bytes).replace("red", "blue"));
    auto replacement = await_image_source(fixture.path);
    QVERIFY(original.content_id() != replacement.content_id());
    QTRY_VERIFY(original.is_ready() && replacement.is_ready());
    QVERIFY(original.contains({}));
    QVERIFY(replacement.contains({}));
}

void image_source_tests::source_identity_does_not_need_path_hash_labels() {
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

void image_source_tests::decodes_svgz_and_rejects_truncation() {
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
    encoded.chop(3);
    fixture.write(encoded);
    QVERIFY(!await_image_source(fixture.path, &error).is_valid());
    QCOMPARE(error, QStringLiteral("invalid or truncated SVGZ source"));
}

void image_source_tests::invalid_svg_reports_failure() {
    svg_fixture fixture;
    fixture.write("not SVG");
    image_source_request acquisition;
    acquisition.load(fixture.path);
    QTRY_VERIFY(!acquisition.error().isEmpty());
    fixture.write({});
    QString error;
    QVERIFY(!await_image_source(fixture.path, &error).is_valid());
    QCOMPARE(error, QStringLiteral("SVG source is empty"));
}

void image_source_tests::appearance_svg_views_share_application_service() {
    using shared_ui::qt::svg_image_view;
    auto& service = application_image_service();
    QTRY_COMPARE(service.observations().scheduling.running_jobs, 0U);
    const auto source = await_image_source(
        bundled_asset_path(QStringLiteral("brand/cuckoo.svg"))
    );
    QVERIFY(source.is_valid());
    const auto consumers = service.observations().scheduling.consumers;
    QWidget window;
    auto first = std::make_unique<svg_image_view>(service, &window);
    svg_image_view second(service, &window);
    first->setGeometry(0, 0, 100, 100);
    second.setGeometry(100, 0, 100, 100);
    first->set_source(source);
    second.set_source(source);
    window.resize(210, 120);
    shared_ui::qt::apply_presentation(
        window,
        { shared_ui::qt::material::felt,
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
          shared_ui::qt::platform_profile::android,
#else
          shared_ui::qt::platform_profile::desktop_qt,
#endif
          {} }
    );
    const auto jobs = service.observations().scheduling.completed_jobs;
    window.show();
    QTRY_VERIFY(
        !first->binding().image().isNull() && !second.binding().image().isNull()
    );
    QCOMPARE(
        first->binding().image().cacheKey(), second.binding().image().cacheKey()
    );
    QVERIFY(service.observations().scheduling.completed_jobs - jobs <= 1U);
    QVERIFY(!first->grab().isNull());
    first.reset();
    QCOMPARE(service.observations().scheduling.consumers, consumers + 1);
    QVERIFY(!second.binding().image().isNull());
    second.hide();
    QVERIFY(second.binding().image().isNull());
    second.show();
    QTRY_VERIFY(!second.binding().image().isNull());
}

void image_source_tests::appearance_svg_view_source_and_lifetime() {
    using shared_ui::qt::svg_image_view;
    auto& service = application_image_service();
    const auto source = await_image_source(
        bundled_asset_path(QStringLiteral("brand/cuckoo.svg"))
    );
    const auto alternative = await_image_source(
        bundled_asset_path(QStringLiteral("brand/logo.svg"))
    );
    QVERIFY(source.is_valid() && alternative.is_valid());
    QWidget window;
    auto view = std::make_unique<svg_image_view>(service, &window);
    view->resize(100, 100);
    window.show();
    view->set_source(source);
    view->set_source(alternative);
    view->set_source(source);
    QTRY_VERIFY(!view->binding().image().isNull());
    QCOMPARE(view->binding().target().source, source);
    view->set_source(
        source, QStringLiteral("__appearance_missing_test_element__")
    );
    QTRY_COMPARE(
        view->binding().status().state, rasterflow::qt::image_state::missing
    );
    QVERIFY(view->binding().image().isNull());
    view->set_source(source);
    QTRY_VERIFY(!view->binding().image().isNull());
    const QPointer<svg_image_view> lifetime(view.get());
    connect(
        &view->binding(), &shared_ui::qt::raster_image_binding::changed, this,
        [&view] {
            if (view && !view->binding().image().isNull())
                view.reset();
        }
    );
    view->set_source(alternative);
    QTRY_VERIFY(!lifetime);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
