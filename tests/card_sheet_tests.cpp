#include "include/card_sheet_tests.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"

#include <QFileInfo>
#include <QSet>
#include <QSvgRenderer>
#include <QtTest/QtTest>

void card_sheet_tests::loads_svg() {
    const QString source = card_sheet_source_path();
    QFileInfo info(source);
    QVERIFY2(info.exists(), "card sheet svg missing");
    QVERIFY2(info.isFile(), "card sheet path is not a file");

    QSvgRenderer renderer(source);
    QVERIFY2(renderer.isValid(), "card sheet svg failed to load");
}

void card_sheet_tests::contains_expected_elements() {
    const QString source = card_sheet_source_path();
    QSvgRenderer renderer(source);
    QVERIFY2(renderer.isValid(), "card sheet svg failed to load");

    const auto& element_ids = card_element_ids();
    QVERIFY2(!element_ids.isEmpty(), "card sheet element id list is empty");

    QSet<QString> seen;
    for (const QString& element_id : element_ids) {
        QVERIFY2(!element_id.isEmpty(), "element id is empty");
        QVERIFY2(renderer.elementExists(element_id), "element id missing");
        QVERIFY2(!seen.contains(element_id), "duplicate element id");
        seen.insert(element_id);
    }

    QCOMPARE(card_label_from_index(0), str_label("A of clubs"));
    QCOMPARE(
        card_label_from_index(static_cast<int>(element_ids.size()) - 1),
        str_label("Joker")
    );
}

void card_sheet_tests::source_path_switches_between_themes() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    const QString theme_one = str_label("assets/cards_1.svg");
    const QString theme_two = str_label("assets/cards_2.svg");

    set_card_sheet_source_path(theme_one);
    QCOMPARE(card_sheet_source_path(), theme_one);
    QVERIFY(preload_card_sheet());
    const auto ratio_one = card_sheet_ratio();
    QVERIFY(ratio_one.first > 0);
    QVERIFY(ratio_one.second > 0);

    set_card_sheet_source_path(theme_two);
    QCOMPARE(card_sheet_source_path(), theme_two);
    QVERIFY(preload_card_sheet());
    const auto ratio_two = card_sheet_ratio();
    QVERIFY(ratio_two.first > 0);
    QVERIFY(ratio_two.second > 0);
}

void card_sheet_tests::
    required_ids_and_fallback_resolution_are_deterministic() {
    const QStringList required_ids = required_card_element_ids_with_back();
    QVERIFY(!required_ids.isEmpty());
    QCOMPARE(
        required_ids.size(), static_cast<int>(card_element_ids().size()) + 1
    );
    QCOMPARE(required_ids.constLast(), card_back_element_id());

    const card_sheet_fallback_resolution active_resolution
        = resolve_required_card_face_sources(str_label("assets/cards_1.svg"));
    QCOMPARE(active_resolution.active_theme_keys, required_ids.size());
    QCOMPARE(active_resolution.default_theme_keys, 0);
    QCOMPARE(active_resolution.placeholder_keys, 0);

    card_sheet_fallback_resolution active_raster_resolution;
    const QVector<QImage> active_images
        = rasterize_required_card_faces_with_fallback(
            str_label("assets/cards_1.svg"), QSize(72, 72),
            &active_raster_resolution
        );
    QCOMPARE(active_images.size(), required_ids.size());
    QCOMPARE(active_raster_resolution.active_theme_keys, required_ids.size());
    QCOMPARE(active_raster_resolution.default_theme_keys, 0);
    QCOMPARE(active_raster_resolution.placeholder_keys, 0);
    for (const QImage& image : active_images) {
        QVERIFY(!image.isNull());
    }

    const card_sheet_fallback_resolution default_resolution
        = resolve_required_card_face_sources(
            str_label("assets/non_existent_cards.svg")
        );
    QCOMPARE(default_resolution.active_theme_keys, 0);
    QCOMPARE(default_resolution.default_theme_keys, required_ids.size());
    QCOMPARE(default_resolution.placeholder_keys, 0);

    card_sheet_fallback_resolution fallback_raster_resolution;
    const QVector<QImage> fallback_images
        = rasterize_required_card_faces_with_fallback(
            str_label("assets/non_existent_cards.svg"), QSize(72, 72),
            &fallback_raster_resolution
        );
    QCOMPARE(fallback_images.size(), required_ids.size());
    QCOMPARE(fallback_raster_resolution.active_theme_keys, 0);
    QCOMPARE(
        fallback_raster_resolution.default_theme_keys, required_ids.size()
    );
    QCOMPARE(fallback_raster_resolution.placeholder_keys, 0);
    for (const QImage& image : fallback_images) {
        QVERIFY(!image.isNull());
    }
}
