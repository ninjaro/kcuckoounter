// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "arch/asset_locator.hpp"
#include "card_helpers/card_sheet.hpp"
#include "shell/main_window.hpp"
#include "shell/mobile_shell.hpp"
#include "table/card_widget.hpp"
#include "table/table.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"
#include "table/table_tests.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QScopeGuard>
#include <QStackedLayout>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace table_test;

namespace {
// Real approved artwork with a distinct inert comment establishes cold
// content identity independently of other tests' shared service residency.
struct theme_fixture {
    QString original = card_sheet_source_path();
    QTemporaryDir directory;
    QStringList sources;

    explicit theme_fixture(const QByteArray& label) {
        for (int set : { 0, 4, 40 }) {
            QFile source(bundled_asset_path(
                QStringLiteral("sets/cards/cards_%1.svg").arg(set)
            ));
            if (!source.open(QIODevice::ReadOnly))
                return;
            const auto path
                = directory.filePath(QStringLiteral("theme_%1.svg").arg(set));
            QFile destination(path);
            const auto bytes = source.readAll() + "\n<!-- " + label + " -->\n";
            if (!destination.open(QIODevice::WriteOnly)
                || destination.write(bytes) != bytes.size())
                return;
            sources.push_back(path);
        }
    }

    ~theme_fixture() { set_card_sheet_source_path(original); }
};
}

void table_tests::shared_faces_track_set_and_clear() {
    table_slot slot;
    QVERIFY(!slot.has_presented_card_images());
    QImage image(24, 36, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::red);
    slot.set_presented_card_images({ { card_back_element_id(), image } });
    QVERIFY(slot.has_presented_card_images());
    slot.clear_presented_card_images();
    QVERIFY(!slot.has_presented_card_images());

    table view;
    view.resize(500, 400);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    view.update_card_image_demands(true);
    QTRY_VERIFY(!view.is_rasterization_busy());
    QCOMPARE(view.displayed_card_images.size(), 1);
    QVERIFY(view.slot_widgets.front()->has_presented_card_images());
    view.hide();
    QTRY_VERIFY(view.desired_card_sizes.isEmpty());
    QVERIFY(view.displayed_card_images.isEmpty());
    view.show();
    QTRY_COMPARE(view.desired_card_sizes.size(), 1);
    QTRY_VERIFY(!view.is_rasterization_busy());
    QCOMPARE(view.displayed_card_images.size(), 1);
    view.clear_gameplay_session();
    QTRY_VERIFY(!view.is_rasterization_busy());
    QVERIFY(view.displayed_card_images.isEmpty());
    QVERIFY(view.slot_widgets.empty());
}

void table_tests::shared_cache_rasterization_populates_visible_slots() {
    theme_fixture fixture("populate");
    QCOMPARE(fixture.sources.size(), 3);
    set_card_sheet_source_path(fixture.sources.front());
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    view.show();
    QCoreApplication::processEvents();
    view.update_card_image_demands(true);
    QTRY_VERIFY_WITH_TIMEOUT(!view.is_rasterization_busy(), 4000);
    QCOMPARE(table::shared_image_service(), &application_image_service());
    QCOMPARE(view.displayed_card_images.size(), 1);
    QVERIFY(view.slot_widgets.front()->has_presented_card_images());
    QVERIFY(view.findChildren<rasterflow::qt::image_service*>().isEmpty());
}

void table_tests::shared_cache_generation_cutover_stays_bounded() {
    theme_fixture fixture("cutover-bounds");
    QCOMPARE(fixture.sources.size(), 3);
    set_card_sheet_source_path(fixture.sources.front());
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    view.show();
    QCoreApplication::processEvents();
    view.update_card_image_demands(true);
    for (int index : { 1, 2, 0 }) {
        set_card_sheet_source_path(fixture.sources[index]);
        view.apply_theme();
    }
    QTRY_VERIFY_WITH_TIMEOUT(!view.is_rasterization_busy(), 15000);
    QCOMPARE(view.displayed_card_source, fixture.sources.front());
    QCOMPARE(view.displayed_card_images.size(), 1);
    QVERIFY(
        table::shared_image_service()->observations().scheduling.running_jobs
        <= 2
    );
    QVERIFY(view.findChildren<rasterflow::qt::image_service*>().isEmpty());
}

void table_tests::shared_generation_cutover_keeps_single_visible_generation() {
    theme_fixture fixture("atomic-theme");
    QCOMPARE(fixture.sources.size(), 3);
    set_card_sheet_source_path(fixture.sources.front());
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(owned_table_fixture(3)));
    view.show();
    QCoreApplication::processEvents();
    view.update_card_image_demands(true);
    QTRY_VERIFY_WITH_TIMEOUT(!view.is_rasterization_busy(), 10000);
    const auto previous = view.displayed_card_images;
    QVERIFY(!previous.isEmpty());
    set_card_sheet_source_path(fixture.sources[1]);
    view.apply_theme();
    // Registration is asynchronous: only the old visible subset is retained.
    QCOMPARE(view.displayed_card_images, previous);
    for (auto* slot : view.slot_widgets)
        QVERIFY(slot->has_presented_card_images());
    QTRY_VERIFY_WITH_TIMEOUT(!view.is_rasterization_busy(), 10000);
    QCOMPARE(view.displayed_card_source, fixture.sources[1]);
    QCOMPARE(view.displayed_card_images.size(), 1);
    QVERIFY(
        view.displayed_card_images.value(card_back_element_id()).cacheKey()
        != previous.value(card_back_element_id()).cacheKey()
    );
    for (auto* slot : view.slot_widgets)
        QVERIFY(slot->has_presented_card_images());
}

void table_tests::theme_apply_clears_stale_worker_state() {
    theme_fixture fixture("stale-theme");
    QCOMPARE(fixture.sources.size(), 3);
    set_card_sheet_source_path(fixture.sources.front());
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    view.show();
    QCoreApplication::processEvents();
    set_card_sheet_source_path(fixture.sources[1]);
    view.apply_theme();
    QTRY_VERIFY_WITH_TIMEOUT(!view.is_rasterization_busy(), 15000);
    QCOMPARE(view.displayed_card_source, fixture.sources[1]);
    QVERIFY(!view.card_images.is_pending());
    QVERIFY(
        table::shared_image_service()->observations().scheduling.running_jobs
        <= 2
    );
}

void table_tests::theme_and_resize_transitions_are_non_blocking() {
    theme_fixture fixture("theme-resize");
    QCOMPARE(fixture.sources.size(), 3);
    set_card_sheet_source_path(fixture.sources.front());
    table view;
    view.resize(920, 720);
    QVERIFY(view.install_gameplay_session(owned_table_fixture(4)));
    view.show();
    QCoreApplication::processEvents();
    view.update_card_image_demands(true);
    set_card_sheet_source_path(fixture.sources[2]);
    QElapsedTimer timer;
    timer.start();
    view.apply_theme();
    QVERIFY(timer.elapsed() < 350);
    timer.restart();
    view.resize(1000, 760);
    QCoreApplication::processEvents();
    QVERIFY(timer.elapsed() < 350);
    QTRY_VERIFY_WITH_TIMEOUT(!view.is_rasterization_busy(), 15000);
    QCOMPARE(view.displayed_card_source, fixture.sources[2]);
    QCOMPARE(view.displayed_card_images.size(), 1);
}

void table_tests::shared_tables_reuse_and_withdraw_data() {
    QTest::addColumn<bool>("ready");
    QTest::newRow("before-delivery") << false;
    QTest::newRow("after-delivery") << true;
}

void table_tests::shared_tables_reuse_and_withdraw() {
    QFETCH(bool, ready);
    theme_fixture fixture(ready ? "depart-ready" : "depart-pending");
    QCOMPARE(fixture.sources.size(), 3);
    set_card_sheet_source_path(fixture.sources.front());
#ifdef Q_OS_ANDROID
    // Android owns top-level geometry. Keep both demand sizes stable when
    // one table retires, rather than testing a platform window cutover too.
    QWidget host;
    host.show();
    QCoreApplication::processEvents();
    auto departing = std::make_unique<table>(&host);
    table survivor(&host);
#else
    auto departing = std::make_unique<table>();
    table survivor;
#endif
    for (auto* view : { departing.get(), &survivor }) {
        view->resize(300, 240);
        QVERIFY(view->install_gameplay_session(owned_table_fixture(1)));
        // This counter checks card reuse, not independent decorative rasters.
        for (auto* card : view->findChildren<card_widget*>())
            card->set_table_marking_source({});
        view->show();
    }
    QCoreApplication::processEvents();
    if (ready)
        QTRY_VERIFY_WITH_TIMEOUT(!departing->is_rasterization_busy(), 10000);
    departing.reset();
    QTRY_VERIFY_WITH_TIMEOUT(!survivor.is_rasterization_busy(), 10000);
    QCOMPARE(survivor.displayed_card_images.size(), 1);
    card_image_consumer third;
    third.set_source(fixture.sources.front());
    const auto jobs
        = application_image_service().observations().scheduling.completed_jobs;
    third.replace_desired({ { card_back_element_id(), QSize(10, 20) } });
    QTRY_VERIFY(!third.is_pending());
    QCOMPARE(
        third.image_for(card_back_element_id()).cacheKey(),
        survivor.displayed_card_images.value(card_back_element_id()).cacheKey()
    );
    QCOMPARE(
        application_image_service().observations().scheduling.completed_jobs,
        jobs
    );
}

void table_tests::replacing_shared_demand_rejects_obsolete_images() {
    theme_fixture fixture("replace-inflight");
    QCOMPARE(fixture.sources.size(), 3);
    set_card_sheet_source_path(fixture.sources.front());
    table view;
    view.resize(300, 240);
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    view.show();
    QCoreApplication::processEvents();
    for (int index : { 1, 2, 0, 2 }) {
        set_card_sheet_source_path(fixture.sources[index]);
        view.apply_theme();
    }
    QTRY_VERIFY_WITH_TIMEOUT(!view.is_rasterization_busy(), 15000);
    QCOMPARE(view.displayed_card_source, fixture.sources[2]);
    const auto accepted_source = view.displayed_card_source;
    const auto accepted_ids = view.displayed_card_images.keys();
    QCOMPARE(accepted_ids, QList<QString> { card_back_element_id() });
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QTRY_VERIFY_WITH_TIMEOUT(!view.is_rasterization_busy(), 15000);
    QCOMPARE(view.displayed_card_source, accepted_source);
    QCOMPARE(view.displayed_card_images.keys(), accepted_ids);
    // Source metadata can settle geometry and replace a sufficient larger
    // master with a smaller current one. Reject obsolete source/request
    // results, not a legitimate change in cached master dimensions.
    for (const auto& id : accepted_ids) {
        QTRY_COMPARE(
            view.displayed_card_images.value(id), view.card_images.image_for(id)
        );
        const auto image = view.displayed_card_images.value(id);
        const auto needed = view.desired_card_sizes.value(id);
        QVERIFY(rasterflow::satisfies(
            { image.width(), image.height() },
            { needed.width(), needed.height() }
        ));
    }
    view.clear_gameplay_session();
    QTRY_VERIFY(!view.is_rasterization_busy());
    QVERIFY(view.displayed_card_images.isEmpty());
    QVERIFY(view.install_gameplay_session(owned_table_fixture(1)));
    view.update_card_image_demands(true);
    QTRY_VERIFY_WITH_TIMEOUT(!view.is_rasterization_busy(), 10000);
    QCOMPARE(view.displayed_card_source, fixture.sources[2]);
}

void table_tests::first_visible_subset_is_incremental() {
    theme_fixture fixture("incremental-visible-faces");
    QCOMPARE(fixture.sources.size(), 3);
    auto& service = application_image_service();
    service.set_limits({ 1, 128ULL * 1024 * 1024, 128ULL * 1024 * 1024 });
    const auto restore_limits = qScopeGuard([&] { service.set_limits({}); });
    set_card_sheet_source_path(fixture.sources.front());
    table view;
    view.resize(900, 700);
    QVERIFY(view.install_gameplay_session(owned_table_fixture(2)));
    auto* owner = view.active_gameplay_session();
    QVERIFY(owner->start());
    QCOMPARE(
        owner->deal_step().status, gameplay::dealing_step_status::advanced
    );
    view.refresh_gameplay_session();
    bool first_published = false;
    // One executor slot controls completion order. Invoke the actual publisher
    // at the first settled notification, before another result can be delivered
    // on the owner thread. No fake future, worker sleep or pixel substitute.
    connect(
        &view.card_images, &card_image_consumer::images_changed, &view,
        [&] {
            int ready = 0;
            for (const auto& id : view.desired_card_sizes.keys())
                ready += !view.card_images.image_for(id).isNull();
            if (ready == 1 && view.card_images.is_pending()) {
                view.on_card_images_changed();
                first_published |= view.displayed_card_images.size() == 1;
            }
        }
    );
    view.show();
    view.update_card_image_demands(true);
    QTRY_VERIFY_WITH_TIMEOUT(!view.is_rasterization_busy(), 10000);
    QVERIFY(first_published);
    QCOMPARE(view.desired_card_sizes.size(), 2);
    QCOMPARE(view.card_images.source_resolution().active_theme_keys, 2);
    QCOMPARE(view.displayed_card_images.size(), 2);
    QVERIFY(view.card_images.image_for(card_back_element_id()).isNull());
    for (auto it = view.displayed_card_images.cbegin();
         it != view.displayed_card_images.cend(); ++it) {
        const auto requested = view.desired_card_sizes.value(it.key());
        QVERIFY(it->width() > requested.width());
        QVERIFY(it->height() > requested.height());
    }
}

void table_tests::destruction_retires_raster_notifications() {
    launcher_preferences_guard preferences;
    auto window = std::make_unique<main_window>();
    auto* view = window->findChild<table*>();
    QVERIFY(view);
    QVERIFY(view->install_gameplay_session(owned_table_fixture(1)));
    QSignalSpy notifications(view, &table::rasterization_busy_changed);
    QSignalSpy destruction(view, &QObject::destroyed);
    view->update_rasterization_state(view->slot_widgets.front(), true);
    QCOMPARE(notifications.size(), 1);
    notifications.clear();
    window.reset();
    QCOMPARE(notifications.size(), 0);
    QCOMPARE(destruction.size(), 1);
}

void table_tests::visible_window_closes_with_pending_images() {
    launcher_preferences_guard preferences;
    theme_fixture fixture("close-visible-pending");
    QCOMPARE(fixture.sources.size(), 3);
    set_card_sheet_source_path(fixture.sources.front());
    auto window = std::make_unique<main_window>();
    window->resize(900, 700);
    window->show();
    auto* view = window->findChild<table*>();
    QVERIFY(view);
    QVERIFY(view->install_gameplay_session(owned_table_fixture(1)));
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    // Phones open on Home; the table page must be on screen to demand faces.
    auto* shell = window->findChild<mobile_shell*>();
    QVERIFY(shell);
    shell->pages->setCurrentWidget(shell->table_page);
#endif
    view->update_card_image_demands(true);
    QVERIFY(view->is_rasterization_busy());
    const QPointer<table> table_lifetime(view);
    const QPointer<main_window> window_lifetime(window.get());
    window.reset();
    QVERIFY(!table_lifetime);
    QVERIFY(!window_lifetime);
    QTRY_COMPARE_WITH_TIMEOUT(
        application_image_service().observations().scheduling.running_jobs, 0,
        10000
    );
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
