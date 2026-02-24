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
