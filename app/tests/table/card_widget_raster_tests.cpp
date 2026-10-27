// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "arch/asset_locator.hpp"
#include "card_helpers/card_sheet.hpp"
#include "table/card_widget.hpp"
#include "table/card_widget_fixture.hpp"
#include "table/card_widget_tests.hpp"
#include <QEvent>
#include <QFile>
#include <QPointer>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <cmath>

namespace {
bool write_deck(const QString& path, const QByteArray& color) {
    QFile file(path);
    QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" "
                     "height=\"80\">";
    for (const auto& id : required_card_ids_with_back())
        svg += "<rect id=\"" + id.toUtf8()
            + "\" width=\"40\" height=\"80\" fill=\"" + color + "\"/>";
    svg += "</svg>";
    return file.open(QIODevice::WriteOnly) && file.write(svg) == svg.size();
}

QHash<QString, QImage> shared_images() {
    QImage image(32, 48, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::red);
    return { { card_element_ids().front(), image } };
}

std::unique_ptr<gameplay::session> prime_paused_card(card_widget& widget) {
    auto owner
        = std::make_unique<gameplay::session>(card_test::renderer_session());
    if (!widget.bind_gameplay_deck(*owner, { 0 }))
        qFatal("Could not bind raster fixture");
    widget.show();
    // Deliver the causal state/geometry request before explicit resize probes.
    QCoreApplication::processEvents();
    return owner;
}

int back_index() { return static_cast<int>(card_element_ids().size()); }

QSize physical_size(const card_widget& widget, const QSize& logical) {
    return { static_cast<int>(
                 std::ceil(logical.width() * widget.devicePixelRatioF())
             ),
             static_cast<int>(
                 std::ceil(logical.height() * widget.devicePixelRatioF())
             ) };
}
}

void card_widget_tests::visible_faces_preserve_variants_data() {
    QTest::addColumn<QString>("source");
    for (const auto& theme : available_card_themes())
        QTest::newRow(qPrintable(theme.source_path)) << theme.source_path;
}

void card_widget_tests::visible_faces_preserve_variants() {
    std::unique_ptr<gameplay::session> widget_owner;

    QFETCH(QString, source);
    card_widget widget;
    widget.card_sheet_source = source;
    widget_owner = prime_paused_card(widget);
    const QSize logical(32, 48);
    widget.update_card_faces(logical);
    QTRY_VERIFY_WITH_TIMEOUT(!widget.rasterizing, 15000);
    QCOMPARE(widget.displayed_card_images.size(), 1);
    QVERIFY(!widget.card_images->image_for(card_back_element_id()).isNull());
    QVERIFY(widget.card_images);
    const auto requested = physical_size(widget, logical);
    for (const auto& face : widget.displayed_card_images) {
        QVERIFY(!face.isNull());
        QVERIFY(face.width() > requested.width());
        QVERIFY(face.height() > requested.height());
    }
    const auto& master = widget.displayed_card_images.cbegin().value();
    const auto& pixmap = widget.card_face_pixmap(back_index());
    QVERIFY(!pixmap.isNull());
    // Integer rounding of a fitted display can change its ratio by one pixel.
    QVERIFY(
        std::abs(
            static_cast<double>(pixmap.width()) / pixmap.height()
            - static_cast<double>(master.width()) / master.height()
        )
        < 0.04
    );
    QCOMPARE(pixmap.devicePixelRatio(), widget.devicePixelRatioF());
}

void card_widget_tests::local_resize_tracks_latest_demand_data() {
    QTest::addColumn<bool>("ready");
    QTest::addColumn<QSize>("final_size");
    for (bool ready : { false, true })
        for (const auto& size :
             { QSize(20, 30), QSize(40, 60), QSize(80, 100) })
            QTest::newRow(
                qPrintable(QStringLiteral("%1-%2x%3")
                               .arg(ready)
                               .arg(size.width())
                               .arg(size.height()))
            ) << ready
              << size;
}

void card_widget_tests::local_resize_tracks_latest_demand() {
    std::unique_ptr<gameplay::session> widget_owner;

    QFETCH(bool, ready);
    QFETCH(QSize, final_size);
    card_widget widget;
    widget_owner = prime_paused_card(widget);
    widget.update_card_faces(QSize(40, 60));
    if (ready)
        QTRY_VERIFY(!widget.rasterizing);
    widget.update_card_faces(QSize(120, 140));
    widget.update_card_faces(QSize(10, 20));
    widget.update_card_faces(final_size);
    QTRY_VERIFY(!widget.rasterizing);
    QCOMPARE(widget.card_face_size, final_size);
    QCOMPARE(widget.displayed_card_images.size(), 1);
    const auto requested = physical_size(widget, final_size);
    for (const auto& face : widget.displayed_card_images) {
        QVERIFY(face.width() > requested.width());
        QVERIFY(face.height() > requested.height());
    }
}

void card_widget_tests::shared_faces_disable_local_rasterization() {
    card_widget widget;
    const auto images = shared_images();
    widget.set_presented_card_images(images);
    widget.update_card_faces(QSize(20, 30));
    QVERIFY(widget.has_presented_card_images());
    QVERIFY(!widget.card_images);
    QVERIFY(!widget.rasterizing);
    QCOMPARE(
        widget.displayed_card_images.value(card_element_ids().front())
            .constBits(),
        images.value(card_element_ids().front()).constBits()
    );
    QCOMPARE(widget.card_face_pixmap(0).size(), images.cbegin()->size());
}

void card_widget_tests::shared_faces_outlive_source_file() {
    QTemporaryDir directory;
    const auto path = directory.filePath("cards.svg");
    QVERIFY(write_deck(path, "red"));
    card_widget widget;
    widget.card_sheet_source = path;
    widget.set_presented_image_mode(true);
    widget.set_presented_card_images(shared_images());
    QVERIFY(QFile::remove(path));
    widget.update_card_faces(QSize(20, 30));
    QVERIFY(!widget.card_face_pixmap(0).isNull());
    QVERIFY(!widget.card_images);
    QVERIFY(!widget.rasterizing);
}

void card_widget_tests::standalone_source_validation_rejects_invalid_svg() {
    std::unique_ptr<gameplay::session> widget_owner;

    QTemporaryDir directory;
    QFile source(directory.filePath("invalid.svg"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("not an SVG"), qint64 { 10 });
    source.close();
    card_widget widget;
    widget.card_sheet_source = source.fileName();
    widget_owner = prime_paused_card(widget);
    widget.update_card_faces(QSize(20, 30));
    QTRY_VERIFY(!widget.rasterizing);
    QVERIFY(widget.displayed_card_images.isEmpty());
    QVERIFY(widget.card_face_pixmap(back_index()).isNull());
    QVERIFY(!widget.card_images);
    widget.sync_card_sheet_source();
    widget.update_card_faces(QSize(20, 30));
    QTRY_VERIFY(!widget.rasterizing);
    QVERIFY(!widget.card_face_pixmap(back_index()).isNull());
}

void card_widget_tests::shared_faces_reject_local_notifications_data() {
    QTest::addColumn<bool>("ready");
    QTest::addColumn<bool>("shared_ready");
    QTest::newRow("pending-to-shared-mode") << false << false;
    QTest::newRow("pending-to-shared-pixels") << false << true;
    QTest::newRow("ready-to-shared-mode") << true << false;
    QTest::newRow("ready-to-shared-pixels") << true << true;
}

void card_widget_tests::shared_faces_reject_local_notifications() {
    std::unique_ptr<gameplay::session> departing_owner;
    std::unique_ptr<gameplay::session> survivor_owner;

    QFETCH(bool, ready);
    QFETCH(bool, shared_ready);
    QTemporaryDir directory;
    const auto path = directory.filePath("ownership.svg");
    const auto color = ready ? (shared_ready ? "#257629" : "#276329")
                             : (shared_ready ? "#127629" : "#126329");
    QVERIFY(write_deck(path, color));
    card_widget departing, survivor;
    departing.card_sheet_source = path;
    survivor.card_sheet_source = path;
    departing_owner = prime_paused_card(departing);
    survivor_owner = prime_paused_card(survivor);
    departing.update_card_faces(QSize(30, 40));
    survivor.update_card_faces(QSize(30, 40));
    if (ready)
        QTRY_VERIFY(!departing.rasterizing);
    const auto images = shared_images();
    if (shared_ready)
        departing.set_presented_card_images(images);
    else
        departing.set_presented_image_mode(true);
    QVERIFY(!departing.card_images);
    QTRY_VERIFY(!survivor.rasterizing);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QVERIFY(!departing.rasterizing);
    if (shared_ready)
        QCOMPARE(
            departing.displayed_card_images.value(card_element_ids().front())
                .constBits(),
            images.value(card_element_ids().front()).constBits()
        );
    departing.clear_presented_card_images();
    departing.set_presented_image_mode(false);
    departing.update_card_faces(QSize(30, 40));
    QTRY_VERIFY(!departing.rasterizing);
    // Geometry updates can retain different sufficient masters. Both owners
    // must receive current source pixels, never the departing shared red image.
    for (const auto* widget : { &departing, &survivor }) {
        QVERIFY(!widget->displayed_card_images.isEmpty());
        const auto image = widget->displayed_card_images.cbegin().value();
        QVERIFY(!image.isNull());
        QCOMPARE(
            image.pixelColor(image.width() / 2, image.height() / 2),
            QColor(color)
        );
        const auto needed = physical_size(*widget, QSize(30, 40));
        QVERIFY(
            image.width() >= needed.width() && image.height() >= needed.height()
        );
    }
}

void card_widget_tests::local_withdrawal_preserves_other_subscribers_data() {
    QTest::addColumn<bool>("ready");
    for (bool ready : { false, true })
        QTest::newRow(ready ? "after-delivery" : "before-delivery") << ready;
}

void card_widget_tests::local_withdrawal_preserves_other_subscribers() {
    std::unique_ptr<gameplay::session> departing_owner;
    std::unique_ptr<gameplay::session> survivor_owner;

    QFETCH(bool, ready);
    QTemporaryDir directory;
    const auto path = directory.filePath("withdrawal.svg");
    QVERIFY(write_deck(path, ready ? "#552629" : "#553329"));
    auto departing = std::make_unique<card_widget>();
    card_widget survivor;
    departing->card_sheet_source = path;
    survivor.card_sheet_source = path;
    departing_owner = prime_paused_card(*departing);
    survivor_owner = prime_paused_card(survivor);
    departing->update_card_faces(QSize(40, 60));
    survivor.update_card_faces(QSize(40, 60));
    if (ready)
        QTRY_VERIFY(!departing->rasterizing);
    departing->update_card_faces(QSize());
    QVERIFY(!departing->card_images);
    QVERIFY(departing->displayed_card_images.isEmpty());
    QVERIFY(departing->selected_card_face.isNull());
    QVERIFY(!departing->rasterizing);
    departing.reset();
    QTRY_VERIFY(!survivor.rasterizing);
    QCOMPARE(survivor.displayed_card_images.size(), 1);
    QVERIFY(!survivor.card_face_pixmap(back_index()).isNull());
}

void card_widget_tests::selected_presentation_reuses_and_invalidates() {
    auto images = shared_images();
    images.insert(card_element_ids().at(1), QImage());
    card_widget first, second;
    for (auto* widget : { &first, &second }) {
        widget->set_presented_card_images(images);
        widget->update_card_faces(QSize(20, 30));
        QCOMPARE(
            widget->displayed_card_images.value(card_element_ids().front())
                .constBits(),
            images.value(card_element_ids().front()).constBits()
        );
        QVERIFY(widget->selected_card_face.isNull());
    }
    const auto key = first.card_face_pixmap(0).cacheKey();
    QCOMPARE(first.card_face_pixmap(0).cacheKey(), key);
    first.update_card_faces(QSize(20, 30));
    QCOMPARE(first.card_face_pixmap(0).cacheKey(), key);
    QVERIFY(second.selected_card_face.isNull());
    QVERIFY(first.card_face_pixmap(1).isNull());
    QVERIFY(first.card_face_pixmap(-1).isNull());
    QVERIFY(first.card_face_pixmap(55).isNull());
    QVERIFY(!first.card_face_pixmap(0).isNull());
    first.update_card_faces(QSize(24, 36));
    QVERIFY(first.selected_card_face.isNull());
    QCOMPARE(
        first.card_face_pixmap(0).size(),
        images.value(card_element_ids().front()).size()
    );
    first.set_presented_card_images(images);
    QVERIFY(first.selected_card_face.isNull());
    QCOMPARE(images.value(card_element_ids().front()).size(), QSize(32, 48));
}

void card_widget_tests::theme_source_change_invalidates_stale_raster_result() {
    std::unique_ptr<gameplay::session> widget_owner;

    QTemporaryDir directory;
    const auto red = directory.filePath("red.svg");
    const auto blue = directory.filePath("blue.svg");
    QVERIFY(write_deck(red, "red"));
    QVERIFY(write_deck(blue, "blue"));
    const auto original = card_sheet_source_path();
    const auto restore
        = qScopeGuard([&original] { set_card_sheet_source_path(original); });
    set_card_sheet_source_path(red);
    card_widget widget;
    widget_owner = prime_paused_card(widget);
    widget.update_card_faces(QSize(20, 30));
    set_card_sheet_source_path(blue);
    widget.sync_card_sheet_source();
    QVERIFY(widget.displayed_card_images.isEmpty());
    QTRY_VERIFY(!widget.rasterizing);
    QCOMPARE(widget.displayed_card_images.size(), 1);
    for (const auto& face : widget.displayed_card_images)
        QCOMPARE(
            face.pixelColor(face.width() / 2, face.height() / 2),
            QColor(Qt::blue)
        );
}

void card_widget_tests::visible_face_does_not_wait_for_unrelated_elements() {
    std::unique_ptr<gameplay::session> widget_owner;

    QTemporaryDir directory;
    const auto path = directory.filePath("lazy.svg");
    QVERIFY(write_deck(path, "#943873"));
    card_widget widget;
    widget.card_sheet_source = path;
    // Empty widgets do not turn the SVG inventory into image demand.
    widget.update_card_faces(QSize(30, 40));
    QVERIFY(!widget.card_images);
    QVERIFY(widget.displayed_card_images.isEmpty());
    widget_owner = prime_paused_card(widget);
    widget.update_card_faces(QSize(30, 40));
    QTRY_VERIFY(!widget.rasterizing);
    QCOMPARE(widget.displayed_card_images.size(), 1);
    QCOMPARE(widget.card_images->source_resolution().active_theme_keys, 1);
    QVERIFY(widget.card_images->image_for(QStringLiteral("club_1")).isNull());
    QVERIFY(!widget.card_face_pixmap(back_index()).isNull());
    widget.set_hide_cards(true);
    QTRY_VERIFY(widget.displayed_card_images.isEmpty());
    QVERIFY(widget.card_images->image_for(card_back_element_id()).isNull());
    widget.set_hide_cards(false);
    QTRY_VERIFY(!widget.displayed_card_images.isEmpty());
    QCOMPARE(widget.displayed_card_images.size(), 1);
    QVERIFY(widget_owner->start());
    (void)widget_owner->deal_step();
    widget.refresh_gameplay_deck();
    const auto face_id
        = card_element_id_from_index(widget.display_card_index());
    QTRY_VERIFY(!widget.card_images->image_for(face_id).isNull());
    QTRY_COMPARE(
        widget.displayed_card_images.cbegin().value().cacheKey(),
        widget.card_images->image_for(face_id).cacheKey()
    );
    QCOMPARE(widget.displayed_card_images.size(), 1);
    QVERIFY(widget.card_images->image_for(card_back_element_id()).isNull());
    QTRY_VERIFY(widget.table_marking.is_ready());
    QTRY_COMPARE(
        application_image_service().observations().scheduling.running_jobs, 0
    );
    const auto jobs
        = application_image_service().observations().scheduling.completed_jobs;
    QImage frame(widget.size(), QImage::Format_ARGB32_Premultiplied);
    for (int repeat = 0; repeat < 3; ++repeat)
        widget.render(&frame);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(
        application_image_service().observations().scheduling.completed_jobs,
        jobs
    );
    QEvent metrics(QEvent::DevicePixelRatioChange);
    QCoreApplication::sendEvent(&widget, &metrics);
    QVERIFY(widget.preparation_queued);
    QTRY_VERIFY(!widget.preparation_queued);
    const auto physical = physical_size(widget, widget.card_face_target_size());
    const auto master = widget.card_images->image_for(face_id);
    QVERIFY(master.width() > physical.width());
    QVERIFY(master.height() > physical.height());
}

void card_widget_tests::completion_observer_can_destroy_widget() {
    std::unique_ptr<gameplay::session> widget_owner;

    QTemporaryDir directory;
    const auto path = directory.filePath("destroy.svg");
    QVERIFY(write_deck(path, "#182b37"));
    QPointer<card_widget> widget = new card_widget;
    const auto cleanup = qScopeGuard([&widget] { delete widget.data(); });
    widget->card_sheet_source = path;
    widget_owner = prime_paused_card(*widget);
    widget->show();
    connect(
        widget, &card_widget::rasterization_busy_changed, this,
        [&widget](bool busy) {
            if (!busy)
                delete widget.data();
        }
    );
    widget->update_card_faces(QSize(47, 69));
    QTRY_VERIFY(widget.isNull());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
