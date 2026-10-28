// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/preview_carousel_tests.hpp"

#include "card_helpers/card_sheet.hpp"
#include "shared_ui/qt/image_carousel.hpp"
#include "table/settings_template.hpp"
#include "table/settings_template_support.hpp"
#include "table/table.hpp"
#include "table/table_fixture.hpp"
#include "table/table_slot.hpp"

#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFile>
#include <QLabel>
#include <QPointer>
#include <QScopeGuard>
#include <QSet>
#include <QTemporaryDir>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

static QToolButton*
carousel_button(shared_ui::qt::image_carousel& carousel, Qt::ArrowType arrow) {
    const auto buttons = carousel.findChildren<QToolButton*>();
    for (auto* button : buttons) {
        if (button != nullptr && button->arrowType() == arrow) {
            return button;
        }
    }
    return nullptr;
}

namespace {
QString copy_theme(QTemporaryDir& directory, int set, const QByteArray& tag) {
    QFile source(default_card_sheet_source_path());
    if (set != 0)
        source.setFileName(
            QStringLiteral(":/kcuckoounter/sets/cards/cards_%1.svg").arg(set)
        );
    if (!source.open(QIODevice::ReadOnly))
        return {};
    const auto path
        = directory.filePath(QStringLiteral("theme_%1.svg").arg(set));
    QFile destination(path);
    const auto bytes = source.readAll() + "\n<!-- " + tag + " -->\n";
    return destination.open(QIODevice::WriteOnly)
            && destination.write(bytes) == bytes.size()
        ? path
        : QString();
}
}

bool preview_carousel_tests::select_source(
    settings_template_widget& editor, const QString& path
) {
    auto* button = editor.theme_button_group
        ? editor.theme_button_group->checkedButton()
        : nullptr;
    if (!button || path.isEmpty())
        return false;
    // Select the same real artwork through the production source property.
    button->setProperty("theme_source", path);
    editor.on_theme_source_button_clicked(button);
    return true;
}

void preview_carousel_tests::settings_previews_share_master_images() {
    settings_template_widget first(settings_tab_kind::appearance),
        second(settings_tab_kind::appearance);
    for (auto* editor : { &first, &second }) {
        editor->resize(800, 600);
        editor->show();
        editor->theme_carousel->set_visible_count(2);
        editor->theme_carousel->set_prefetch_adjacent(false);
        editor->update_preview_demands();
    }
    QTRY_VERIFY(
        !first.preview_images.is_pending()
        && !second.preview_images.is_pending()
    );
    for (const auto* label : first.theme_carousel->findChildren<QLabel*>()) {
        QVERIFY(!label->accessibleDescription().isEmpty());
        QVERIFY(
            label->accessibleName().contains(
                card_label_from_index(first.theme_suit * 13)
            )
            || label->accessibleName().contains(
                card_label_from_index(first.theme_suit * 13 + 1)
            )
        );
    }
    QVERIFY(!first.preview_requested_ids.isEmpty());
    QCOMPARE(first.preview_requested_ids, second.preview_requested_ids);
    for (const auto& id : first.preview_requested_ids)
        QCOMPARE(
            first.preview_images.image_for(id).cacheKey(),
            second.preview_images.image_for(id).cacheKey()
        );
    QTRY_COMPARE(
        first.displayed_preview_images.size(),
        first.preview_requested_ids.size()
    );
}

void preview_carousel_tests::table_and_preview_share_master_images() {
    QTemporaryDir directory;
    const auto source = copy_theme(directory, 4, "field-preview-sharing");
    QVERIFY(!source.isEmpty());
    const auto original = card_sheet_source_path();
    const auto restore
        = qScopeGuard([&original] { set_card_sheet_source_path(original); });
    set_card_sheet_source_path(source);
    card_image_consumer observer;
    observer.set_source(source);
    observer.replace_desired({ { card_element_ids().front(), QSize(1, 2) } });
    table field;
    field.resize(500, 400);
    QVERIFY(field.install_gameplay_session(table_test::owned_table_fixture(1)));
    auto* owner = field.active_gameplay_session();
    QVERIFY(owner->start());
    QCOMPARE(
        owner->deal_step().status, gameplay::dealing_step_status::advanced
    );
    field.refresh_gameplay_session();
    field.show();
    field.schedule_card_preload();
    const auto field_slots = field.findChildren<table_slot*>();
    QVERIFY(!field_slots.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(
        field_slots.front()->has_presented_card_images()
            && !field.is_rasterization_busy(),
        10000
    );
    const auto master = observer.image_for(card_element_ids().front());
    QVERIFY(!master.isNull());
    QTRY_COMPARE(
        application_image_service().observations().scheduling.running_jobs, 0
    );
    const auto jobs
        = application_image_service().observations().scheduling.completed_jobs;
    settings_template_widget editor(
        settings_tab_kind::appearance, nullptr, {}, &field
    );
    editor.theme_carousel->set_visible_count(13);
    editor.theme_carousel->set_prefetch_adjacent(false);
    QVERIFY(select_source(editor, source));
    editor.show();
    QTRY_VERIFY(!editor.preview_images.is_pending());
    QTRY_COMPARE(editor.displayed_preview_images.size(), 13);
    QCOMPARE(
        editor.preview_images.image_for(card_element_ids().front()).cacheKey(),
        master.cacheKey()
    );
    QCOMPARE(
        application_image_service().observations().scheduling.completed_jobs,
        jobs + 12
    );
}

void preview_carousel_tests::settings_preview_window_replaces_desired() {
    settings_template_widget editor(settings_tab_kind::appearance);
    editor.resize(800, 600);
    editor.show();
    editor.theme_carousel->set_visible_count(2);
    editor.theme_carousel->set_prefetch_adjacent(true);
    auto* next = carousel_button(*editor.theme_carousel, Qt::RightArrow);
    QVERIFY(next);
    for (int step = 0; step < 15; ++step) {
        next->click();
        editor.update_preview_demands();
        QStringList expected;
        for (int rank : editor.theme_carousel->requested_item_indices())
            expected.append(
                settings_template_support::preview_scope(
                    rank, editor.theme_suit
                )
            );
        expected.sort();
        QCOMPARE(editor.preview_requested_ids, expected);
        QVERIFY(editor.preview_requested_ids.size() <= 4);
    }
    QTRY_VERIFY(!editor.preview_images.is_pending());
    QTRY_COMPARE(
        editor.displayed_preview_images.size(),
        editor.preview_requested_ids.size()
    );
    for (const auto& id : editor.displayed_preview_images.keys())
        QVERIFY(editor.preview_requested_ids.contains(id));
    editor.theme_carousel->set_prefetch_adjacent(false);
    QTRY_VERIFY(editor.preview_requested_ids.size() <= 2);
}

void preview_carousel_tests::settings_preview_source_cutover_is_atomic() {
    QTemporaryDir directory;
    const auto source = copy_theme(directory, 4, "preview-atomic");
    QVERIFY(!source.isEmpty());
    const auto game_source = card_sheet_source_path();
    settings_template_widget editor(settings_tab_kind::appearance);
    editor.show();
    editor.update_preview_demands();
    QTRY_VERIFY(!editor.preview_images.is_pending());
    QTRY_VERIFY(!editor.displayed_preview_images.isEmpty());
    const auto previous = editor.displayed_preview_images;
    const auto previous_source = editor.displayed_preview_source;
    QVERIFY(select_source(editor, source));
    QVERIFY(editor.preview_images.is_pending());
    QCOMPARE(editor.displayed_preview_source, previous_source);
    QCOMPARE(editor.displayed_preview_images, previous);
    QTRY_COMPARE(editor.displayed_preview_source, source);
    for (int rank : editor.theme_carousel->visible_item_indices()) {
        const auto id
            = settings_template_support::preview_scope(rank, editor.theme_suit);
        QVERIFY(!editor.displayed_preview_images.value(id).isNull());
        QVERIFY(
            editor.displayed_preview_images.value(id).cacheKey()
            != previous.value(id).cacheKey()
        );
    }
    // Optional neighbors may finish after the coherent visible cutover.
    QTRY_VERIFY(!editor.preview_images.is_pending());
    QTRY_COMPARE(
        editor.displayed_preview_images.size(),
        editor.preview_requested_ids.size()
    );
    for (const auto& id : editor.preview_requested_ids) {
        QVERIFY(!editor.displayed_preview_images.value(id).isNull());
        QVERIFY(
            editor.displayed_preview_images.value(id).cacheKey()
            != previous.value(id).cacheKey()
        );
    }
    QCOMPARE(card_sheet_source_path(), game_source);
}

void preview_carousel_tests::rapid_source_switch_preserves_latest_artwork() {
    QTemporaryDir directory;
    const auto first = copy_theme(directory, 4, "rapid-first");
    const auto second = copy_theme(directory, 11, "rapid-second");
    const auto latest = copy_theme(directory, 23, "rapid-latest");
    QVERIFY(!first.isEmpty() && !second.isEmpty() && !latest.isEmpty());
    const auto game_source = card_sheet_source_path();
    settings_template_widget editor(settings_tab_kind::appearance);
    editor.show();
    QVERIFY(select_source(editor, first));
    QVERIFY(select_source(editor, second));
    QVERIFY(select_source(editor, latest));
    QTRY_COMPARE(editor.displayed_preview_source, latest);
    QTRY_VERIFY(!editor.preview_images.is_pending());
    // Deliver every pending queued publication before checking for stale faces.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(editor.displayed_preview_source, latest);
    for (int rank : editor.theme_carousel->visible_item_indices()) {
        const auto id
            = settings_template_support::preview_scope(rank, editor.theme_suit);
        const auto current = editor.preview_images.image_for(id);
        QVERIFY(!current.isNull());
        QCOMPARE(
            editor.displayed_preview_images.value(id).cacheKey(),
            current.cacheKey()
        );
    }
    QCOMPARE(card_sheet_source_path(), game_source);
}

void preview_carousel_tests::
    speculative_neighbors_do_not_hold_visible_cutover() {
    auto& service = application_image_service();
    const auto restore_limits = qScopeGuard([&] { service.set_limits({}); });
    QTemporaryDir directory;
    settings_template_widget editor(settings_tab_kind::appearance);
    editor.resize(600, 500);
    editor.theme_carousel->set_visible_count(1);
    editor.theme_carousel->set_prefetch_adjacent(false);
    const auto write = [&](const QString& name, const QByteArray& color) {
        const auto path = directory.filePath(name);
        QByteArray bytes
            = "<svg xmlns=\"http://www.w3.org/2000/svg\" "
              "width=\"20\" height=\"10000\" viewBox=\"0 0 20 10000\">";
        for (int rank : { 0, 1, 12 }) {
            const auto id = settings_template_support::preview_scope(
                rank, editor.theme_suit
            );
            bytes += "<rect id=\"" + id.toUtf8() + "\" width=\"20\" height=\""
                + QByteArray::number(rank == 0 ? 40 : 10000) + "\" fill=\""
                + color + "\"/>";
        }
        bytes += "<desc>" + directory.path().toUtf8().toHex() + "</desc></svg>";
        QFile file(path);
        return file.open(QIODevice::WriteOnly)
                && file.write(bytes) == bytes.size()
            ? path
            : QString();
    };
    const auto red = write("red.svg", "red");
    const auto blue = write("blue.svg", "blue");
    QVERIFY(!red.isEmpty() && !blue.isEmpty());
    editor.show();
    QVERIFY(select_source(editor, red));
    QTRY_COMPARE(editor.displayed_preview_source, red);
    const auto id
        = settings_template_support::preview_scope(0, editor.theme_suit);
    const auto original = editor.displayed_preview_images.value(id);
    QVERIFY(!original.isNull());
    const auto logical = editor.theme_carousel->requested_item_size();
    const qreal dpr = editor.theme_carousel->devicePixelRatioF();
    QVERIFY(!logical.isEmpty());
    const auto required = rasterflow::normalized_size(
        std::ceil(logical.width() * dpr), std::ceil(logical.height() * dpr), 2.0
    );
    const auto bytes = rasterflow::pixel_bytes(required);
    // Normal visible content fits. The two tall neighbors cannot be admitted.
    service.set_limits(
        { 1, bytes * 8, std::max<std::uint64_t>(bytes * 2, 10000) }
    );
    editor.theme_carousel->set_prefetch_adjacent(true);
    QVERIFY(select_source(editor, blue));
    QCOMPARE(editor.displayed_preview_source, red);
    QTRY_VERIFY(!editor.preview_images.image_for(id).isNull());
    QTRY_COMPARE(editor.displayed_preview_source, blue);
    QVERIFY(!editor.preview_images.is_pending_required());
    QVERIFY(editor.preview_images.is_pending());
    QCOMPARE(editor.preview_requested_ids.size(), 3);
    QCOMPARE(editor.theme_carousel->visible_item_indices().size(), 1);
    QCOMPARE(editor.displayed_preview_images.size(), 1);
    const auto current = editor.displayed_preview_images.value(id);
    QCOMPARE(
        current.pixelColor(current.width() / 2, current.height() / 2),
        QColor(Qt::blue)
    );
    QVERIFY(current.cacheKey() != original.cacheKey());
    editor.hide();
    QTRY_VERIFY(editor.preview_requested_ids.isEmpty());
    QVERIFY(editor.displayed_preview_images.isEmpty());
    QVERIFY(!editor.preview_images.is_pending());
    editor.show();
    QTRY_COMPARE(editor.displayed_preview_source, blue);
    QVERIFY(!editor.preview_images.is_pending_required());
}

void preview_carousel_tests::
    settings_preview_retirement_preserves_survivor_data() {
    QTest::addColumn<bool>("ready");
    QTest::newRow("pending") << false;
    QTest::newRow("ready") << true;
}

void preview_carousel_tests::settings_preview_retirement_preserves_survivor() {
    QFETCH(bool, ready);
    QTemporaryDir directory;
    const auto source = copy_theme(
        directory, 4, ready ? "preview-retire-ready" : "preview-retire-pending"
    );
    auto departing = std::make_unique<settings_template_widget>(
        settings_tab_kind::appearance
    );
    settings_template_widget survivor(settings_tab_kind::appearance);
    for (auto* editor : { departing.get(), &survivor }) {
        editor->show();
        editor->theme_carousel->set_visible_count(2);
        editor->theme_carousel->set_prefetch_adjacent(false);
        QVERIFY(select_source(*editor, source));
    }
    if (ready)
        QTRY_COMPARE(departing->displayed_preview_source, source);
    departing.reset();
    QTRY_COMPARE(survivor.displayed_preview_source, source);
    QTRY_VERIFY(!survivor.preview_images.is_pending());
    QTRY_COMPARE(
        survivor.displayed_preview_images.size(),
        survivor.preview_requested_ids.size()
    );
    for (const auto& id : survivor.preview_requested_ids)
        QVERIFY(!survivor.preview_images.image_for(id).isNull());
}

void preview_carousel_tests::settings_preview_returns_to_cached_demand() {
    QTemporaryDir directory;
    const auto source = copy_theme(directory, 4, "preview-return");
    settings_template_widget editor(settings_tab_kind::appearance);
    editor.show();
    editor.theme_carousel->set_visible_count(1);
    editor.theme_carousel->set_prefetch_adjacent(false);
    QVERIFY(select_source(editor, source));
    QTRY_VERIFY(!editor.preview_images.is_pending());
    QTRY_COMPARE(editor.displayed_preview_source, source);
    const auto id = editor.preview_requested_ids.front();
    const auto key = editor.preview_images.image_for(id).cacheKey();
    auto* next = carousel_button(*editor.theme_carousel, Qt::RightArrow);
    auto* previous = carousel_button(*editor.theme_carousel, Qt::LeftArrow);
    QVERIFY(next && previous);
    next->click();
    editor.update_preview_demands();
    QTRY_VERIFY(!editor.preview_images.is_pending());
    const auto jobs
        = application_image_service().observations().scheduling.completed_jobs;
    previous->click();
    editor.update_preview_demands();
    QVERIFY(!editor.preview_images.is_pending());
    QCOMPARE(editor.preview_images.image_for(id).cacheKey(), key);
    QCOMPARE(
        application_image_service().observations().scheduling.completed_jobs,
        jobs
    );
}

void preview_carousel_tests::settings_preview_geometry_has_physical_quality() {
    settings_template_widget editor(settings_tab_kind::appearance);
    editor.resize(900, 700);
    editor.show();
    editor.update_preview_demands();
    QTRY_VERIFY(!editor.preview_images.is_pending());
    QVERIFY(!editor.preview_requested_ids.isEmpty());
    const auto logical = editor.theme_carousel->requested_item_size();
    const qreal dpr = editor.theme_carousel->devicePixelRatioF();
    QVERIFY(!logical.isEmpty());
    for (const auto& id : editor.preview_requested_ids) {
        const auto image = editor.preview_images.image_for(id);
        QVERIFY(image.width() > std::ceil(logical.width() * dpr));
        QVERIFY(image.height() > std::ceil(logical.height() * dpr));
    }
}

void preview_carousel_tests::weighted_preview_does_not_mutate_shared_pixels() {
    settings_template_widget editor(settings_tab_kind::strategies);
    editor.show();
    editor.update_preview_demands();
    QTRY_VERIFY(!editor.preview_images.is_pending());
    QTRY_VERIFY(!editor.displayed_preview_images.isEmpty());
    QTRY_COMPARE(
        editor.displayed_preview_images.size(),
        editor.preview_requested_ids.size()
    );
    const auto id = editor.preview_requested_ids.front();
    const auto source = editor.preview_images.image_for(id);
    const auto key = source.cacheKey();
    const auto pixel
        = source.pixelColor(source.width() / 2, source.height() / 2);
    const auto logical = editor.weights_carousel->requested_item_size();
    for (int rank : editor.weights_carousel->requested_item_indices()) {
        const auto pixmap
            = editor.weighted_card(rank, editor.weights_suit, logical);
        QVERIFY(!pixmap.isNull());
        QCOMPARE(pixmap.devicePixelRatio(), editor.devicePixelRatioF());
    }
    QCOMPARE(editor.preview_images.image_for(id).cacheKey(), key);
    QCOMPARE(
        editor.preview_images.image_for(id).pixelColor(
            source.width() / 2, source.height() / 2
        ),
        pixel
    );
}

void preview_carousel_tests::preview_publication_can_destroy_owner_data() {
    QTest::addColumn<bool>("cached");
    QTest::newRow("asynchronous") << false;
    QTest::newRow("cached-source-alias") << true;
}

void preview_carousel_tests::preview_publication_can_destroy_owner() {
    QFETCH(bool, cached);
    QTemporaryDir directory;
    const auto source = copy_theme(directory, 4, "preview-destroy");
    QPointer<settings_template_widget> editor
        = new settings_template_widget(settings_tab_kind::appearance);
    editor->resize(800, 600);
    editor->theme_carousel->set_visible_count(2);
    editor->theme_carousel->set_prefetch_adjacent(false);
    editor->show();
    QVERIFY(select_source(*editor, source));
    if (cached) {
        QTRY_VERIFY(!editor->preview_images.is_pending());
        const auto alias = directory.filePath("cached-alias.svg");
        QVERIFY(QFile::copy(source, alias));
        // Select through the real source property before invoking the update;
        // do not delete a widget while a selection control is still mutating.
        auto* button = editor->theme_button_group->checkedButton();
        QVERIFY(button != nullptr);
        button->setProperty("theme_source", alias);
        // A new path is acquired asynchronously even when its bytes match a
        // resident source. Establish that real precondition, then withdraw
        // image intent without retiring the prepared source or cached pixels.
        editor->update_preview_demands();
        QTRY_VERIFY(!editor->preview_images.is_pending());
        editor->preview_images.replace_desired({});
    }
    connect(
        &editor->preview_images, &card_image_consumer::images_changed, this,
        [&editor] {
            if (editor && !editor->preview_images.is_pending())
                delete editor.data();
        }
    );
    if (cached) {
        editor->update_preview_demands();
        // Exercise deletion during the current cache-hit update, rather than
        // accidentally repeating only the asynchronous completion case.
        QVERIFY(editor.isNull());
    }
    QTRY_VERIFY(editor.isNull());
}

void preview_carousel_tests::
    initial_previews_publish_before_all_requests_finish() {
    QTemporaryDir directory;
    const auto source = copy_theme(directory, 4, "preview-partial-publication");
    settings_template_widget editor(settings_tab_kind::appearance);
    editor.theme_carousel->set_visible_count(13);
    editor.theme_carousel->set_prefetch_adjacent(false);
    editor.show();
    // Model an initial unpublished presentation even if another test warmed
    // the application's default artwork. The selected source itself is cold.
    editor.displayed_preview_images.clear();
    editor.displayed_preview_source.clear();
    auto observed = std::make_shared<std::pair<bool, bool>>(false, false);
    connect(
        &editor.preview_images, &card_image_consumer::images_changed, this,
        [&editor, observed] {
            if (!editor.preview_images.is_pending())
                return;
            bool any_ready = false;
            for (const auto& id : editor.preview_requested_ids)
                any_ready |= !editor.preview_images.image_for(id).isNull();
            if (!any_ready)
                return;
            // The production queued publisher is registered before this
            // observer. Await that exact event boundary, without sleeping or
            // fake workers.
            QMetaObject::invokeMethod(
                &editor,
                [&editor, observed] {
                    if (editor.preview_images.is_pending()) {
                        observed->first = true;
                        observed->second
                            |= !editor.displayed_preview_images.isEmpty();
                    }
                },
                Qt::QueuedConnection
            );
        }
    );
    QVERIFY(select_source(editor, source));
    QCOMPARE(editor.preview_requested_ids.size(), 13);
    QTRY_VERIFY(!editor.preview_images.is_pending());
    QVERIFY(observed->first);
    QVERIFY(observed->second);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
