// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "image/card_images.hpp"
#include "include/card_sheet_tests.hpp"
#include "include/image_source_helpers.hpp"
#include <QFile>
#include <QPointer>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <stdexcept>

namespace {
bool write_source(const QString& path, const QByteArray& elements) {
    QFile file(path);
    const QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                           "width=\"100\" height=\"160\">"
        + elements + "</svg>";
    return file.open(QIODevice::WriteOnly) && file.write(svg) == svg.size();
}

const QByteArray red_face
    = "<rect id=\"1_club\" width=\"40\" height=\"80\" fill=\"red\"/>";
const QByteArray blue_back
    = "<rect id=\"card_back\" width=\"40\" height=\"80\" fill=\"blue\"/>";
}

void card_sheet_tests::rasterflow_metadata_follows_current_aliases_and_frame() {
    QTemporaryDir directory;
    const auto path = directory.filePath("large-atlas.svg");
    QByteArray elements = red_face + blue_back
        + R"(<rect id="card_base" width="40" height="80"/>)";
    for (int index = 0; index < 2048; ++index)
        elements += R"(<rect id="unrelated-)" + QByteArray::number(index)
            + R"(" width="5" height="5"/>)";
    QVERIFY(write_source(path, elements));
    const auto previous = card_sheet_source_path();
    const auto restore
        = qScopeGuard([&] { set_card_sheet_source_path(previous); });
    set_card_sheet_source_path(path);
    auto& layout = current_card_sheet_metadata();
    QTRY_VERIFY(!layout.is_pending());
    QVERIFY(layout.is_valid());
    QCOMPARE(layout.ratio(), (std::pair<int, int> { 80, 40 }));
    const auto snapshot = await_image_source(path);
    QVERIFY(snapshot.is_ready());
    QVERIFY(!snapshot.is_catalog_ready());
    QVERIFY(!snapshot.is_inspected(QStringLiteral("unrelated-0")));
    QVERIFY(!snapshot.is_inspected(QStringLiteral("unrelated-2047")));
    card_image_consumer consumer;
    consumer.set_source(path, path);
    consumer.replace_desired({ { "club_1", { 20, 30 } } });
    QTRY_VERIFY(!consumer.is_pending());
    QVERIFY(snapshot.is_inspected(QStringLiteral("1_club")));
    QVERIFY(snapshot.is_inspected(QStringLiteral("card_base")));
    QCOMPARE(consumer.image_for("club_1").pixelColor(10, 10), QColor(Qt::red));
    consumer.replace_desired({ { "back", { 20, 30 } } });
    QTRY_VERIFY(!consumer.is_pending());
    QVERIFY(consumer.image_for("club_1").isNull());
    QCOMPARE(consumer.image_for("back").pixelColor(10, 10), QColor(Qt::blue));
    QVERIFY(snapshot.list_element_ids().isEmpty());
    QVERIFY(!snapshot.is_inspected(QStringLiteral("unrelated-0")));
}

void card_sheet_tests::rasterflow_aliases_resolve_explicit_demands() {
    QTemporaryDir directory;
    const auto path = directory.filePath("aliases.svg");
    QByteArray elements;
    for (const auto& id : required_card_ids_with_back()) {
        const auto aliases = candidate_card_element_ids(id);
        QVERIFY(!aliases.isEmpty());
        elements += "<rect id=\"" + aliases.back().toUtf8()
            + "\" width=\"40\" height=\"80\" fill=\"red\"/>";
    }
    QVERIFY(write_source(path, elements));
    card_image_consumer consumer;
    consumer.set_source(path, path);
    std::vector<card_image_demand> desired;
    for (const auto& id : required_card_ids_with_back())
        desired.push_back({ id, QSize(20, 30) });
    consumer.replace_desired(desired);
    QVERIFY(consumer.is_pending());
    QVERIFY(consumer.is_pending_required());
    QTRY_VERIFY(!consumer.is_pending());
    QCOMPARE(
        consumer.source_resolution().active_theme_keys,
        required_card_ids_with_back().size()
    );
    for (const auto& id : required_card_ids_with_back()) {
        const auto image = consumer.image_for(id);
        QVERIFY(image.width() > 20 && image.height() > 30);
        QCOMPARE(
            image.pixelColor(image.width() / 2, image.height() / 2),
            QColor(Qt::red)
        );
    }
}

void card_sheet_tests::rasterflow_fallback_and_missing_elements() {
    QTemporaryDir directory;
    const auto active = directory.filePath("active.svg");
    const auto fallback = directory.filePath("fallback.svg");
    QVERIFY(write_source(active, red_face));
    QVERIFY(write_source(fallback, blue_back));
    card_image_consumer consumer;
    consumer.set_source(active, fallback);
    consumer.replace_desired({ { "club_1", { 20, 30 } },
                               { "back", { 20, 30 } },
                               { "missing", { 20, 30 } } });
    QTRY_VERIFY(!consumer.is_pending());
    QCOMPARE(consumer.image_for("club_1").pixelColor(10, 10), QColor(Qt::red));
    QCOMPARE(consumer.image_for("back").pixelColor(10, 10), QColor(Qt::blue));
    QVERIFY(consumer.image_for("missing").isNull());
    const auto resolution = consumer.source_resolution();
    QCOMPARE(resolution.active_theme_keys, 1);
    QCOMPARE(resolution.default_theme_keys, 1);
    QCOMPARE(resolution.placeholder_keys, 1);
    QVERIFY(QFile::remove(active));
    QVERIFY(QFile::remove(fallback));
    consumer.replace_desired({ { "club_1", { 40, 60 } },
                               { "back", { 40, 60 } } });
    QTRY_VERIFY(!consumer.is_pending());
    QVERIFY(consumer.image_for("back").height() > 60);
}

void card_sheet_tests::rasterflow_base_frame_preserves_aspect() {
    QTemporaryDir directory;
    const auto path = directory.filePath("frame.svg");
    QVERIFY(write_source(
        path,
        "<rect id=\"card_base\" width=\"40\" height=\"80\"/>"
        "<g id=\"1_club\"><rect width=\"60\" height=\"100\" fill=\"blue\"/>"
        "<rect x=\"10\" y=\"10\" width=\"40\" height=\"80\" fill=\"red\"/></g>"
    ));
    card_image_consumer consumer;
    consumer.set_source(path, path);
    consumer.replace_desired({ { "club_1", { 40, 60 } } });
    QTRY_VERIFY(!consumer.is_pending());
    const auto image = consumer.image_for("club_1");
    QVERIFY(image.width() > 40 && image.height() > 60);
    QVERIFY(std::abs(2 * image.width() - image.height()) <= 1);
    QCOMPARE(
        image.pixelColor(image.width() / 2, image.height() / 2), QColor(Qt::red)
    );
    QCOMPARE(image.pixelColor(2, 2), QColor(Qt::red));
}

void card_sheet_tests::rasterflow_consumers_share_and_withdraw() {
    QTemporaryDir directory;
    const auto path = directory.filePath("shared.svg");
    QVERIFY(write_source(path, red_face + "<desc>shared-demand fixture</desc>")
    );
    card_image_consumer first, second;
    first.set_source(path, path);
    second.set_source(path, path);
    const auto before
        = application_image_service().observations().scheduling.completed_jobs;
    first.replace_desired({ { "club_1", { 30, 40 } } });
    second.replace_desired({ { "club_1", { 30, 40 } } });
    first.clear();
    QTRY_VERIFY(!second.is_pending());
    first.replace_desired({ { "club_1", { 20, 30 } } });
    QTRY_VERIFY(!first.is_pending());
    QCOMPARE(
        first.image_for("club_1").cacheKey(),
        second.image_for("club_1").cacheKey()
    );
    QCOMPARE(
        application_image_service().observations().scheduling.completed_jobs
            - before,
        1U
    );
    second.clear();
    QVERIFY(!first.image_for("club_1").isNull());
    first.clear();
    QVERIFY(first.image_for("club_1").isNull());
}

void card_sheet_tests::
    rasterflow_required_readiness_ignores_speculative_fallback() {
    auto& service = application_image_service();
    service.set_limits({ 1, 10000000, 20000 });
    const auto restore = qScopeGuard([&] { service.set_limits({}); });
    QTemporaryDir directory;
    const auto preferred = directory.filePath("required.svg");
    const auto fallback = directory.filePath("speculative.svg");
    const auto tag
        = QByteArray("<desc>") + directory.path().toUtf8().toHex() + "</desc>";
    QVERIFY(write_source(preferred, red_face + tag));
    // Hold fallback metadata before admission, without sleeps or fake workers.
    QVERIFY(write_source(
        fallback,
        blue_back + tag + "<desc>" + QByteArray(250000, 'x') + "</desc>"
    ));
    card_image_consumer consumer;
    consumer.set_source(preferred, fallback);
    consumer.replace_desired({ { "back", { 20, 30 }, false } });
    QVERIFY(consumer.is_pending());
    QVERIFY(!consumer.is_pending_required());
    consumer.replace_desired({ { "club_1", { 20, 30 } },
                               { "back", { 20, 30 }, false } });
    QVERIFY(consumer.is_pending_required());
    QTRY_VERIFY_WITH_TIMEOUT(!consumer.is_pending_required(), 5000);
    QVERIFY(consumer.is_pending());
    QCOMPARE(consumer.image_for("club_1").pixelColor(10, 10), QColor(Qt::red));
    QVERIFY(consumer.image_for("back").isNull());
    QCOMPARE(consumer.source_resolution().default_theme_keys, 0);
    service.set_limits({ 1, 10000000, 1000000 });
    QTRY_VERIFY_WITH_TIMEOUT(!consumer.is_pending(), 5000);
    QCOMPARE(consumer.image_for("back").pixelColor(10, 10), QColor(Qt::blue));
    QCOMPARE(consumer.source_resolution().default_theme_keys, 1);
}

void card_sheet_tests::rasterflow_source_replacement_rejects_stale_pixels() {
    QTemporaryDir directory;
    const auto red = directory.filePath("red.svg");
    const auto blue = directory.filePath("blue.svg");
    QVERIFY(write_source(red, red_face));
    QVERIFY(write_source(blue, QByteArray(red_face).replace("red", "blue")));
    card_image_consumer consumer;
    consumer.set_source(red, red);
    consumer.replace_desired({ { "club_1", { 20, 30 } } });
    consumer.set_source(blue, blue);
    QTRY_VERIFY(!consumer.is_pending());
    QCOMPARE(consumer.image_for("club_1").pixelColor(10, 10), QColor(Qt::blue));
}

void card_sheet_tests::rasterflow_invalid_demand_preserves_subscription() {
    QTemporaryDir directory;
    const auto path = directory.filePath("valid.svg");
    QVERIFY(write_source(path, red_face));
    card_image_consumer consumer;
    consumer.set_source(path, path);
    consumer.replace_desired({ { "club_1", { 20, 30 } } });
    QTRY_VERIFY(!consumer.is_pending());
    const auto key = consumer.image_for("club_1").cacheKey();
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        consumer.replace_desired({ { "club_1", { 0, 30 } } })
    );
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        consumer.replace_desired({ { "club_1", { 20, 30 } },
                                   { "club_1", { 20, 30 } } })
    );
    QCOMPARE(consumer.image_for("club_1").cacheKey(), key);
}

void card_sheet_tests::rasterflow_observer_can_destroy_consumer() {
    QTemporaryDir directory;
    const auto path = directory.filePath("lifetime.svg");
    QVERIFY(write_source(path, red_face));
    QPointer<card_image_consumer> consumer = new card_image_consumer;
    consumer->set_source(path, path);
    connect(consumer, &card_image_consumer::images_changed, this, [&consumer] {
        if (consumer && !consumer->is_pending())
            delete consumer.data();
    });
    consumer->replace_desired({ { "club_1", { 20, 30 } } });
    QTRY_VERIFY(consumer.isNull());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
