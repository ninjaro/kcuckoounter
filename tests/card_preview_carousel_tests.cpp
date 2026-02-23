#include "include/card_preview_carousel_tests.hpp"

#include "helpers/card_preview_carousel.hpp"

#include <QtTest/QtTest>

#include <QSet>

namespace {
QPixmap test_card_pixmap(const QSize& size) {
    QPixmap pixmap(size);
    pixmap.fill(Qt::blue);
    return pixmap;
}

QToolButton*
carousel_button(card_preview_carousel& carousel, Qt::ArrowType arrow) {
    const auto buttons = carousel.findChildren<QToolButton*>();
    for (auto* button : buttons) {
        if (button != nullptr && button->arrowType() == arrow) {
            return button;
        }
    }
    return nullptr;
}
} // namespace

void card_preview_carousel_tests::default_prefetch_loads_adjacent_cards() {
    card_preview_carousel carousel;
    carousel.resize(640, 200);
    carousel.set_visible_count(3);
    carousel.set_card_size(QSize(64, 96));

    QSet<int> requested_indices;
    carousel.set_card_provider(
        8, [&requested_indices](int index, const QSize& size) {
            requested_indices.insert(index);
            return test_card_pixmap(size);
        }
    );

    QVERIFY(requested_indices.contains(0));
    QVERIFY(requested_indices.contains(1));
    QVERIFY(requested_indices.contains(2));
    QVERIFY(requested_indices.contains(7));
    QVERIFY(requested_indices.contains(3));
}

void card_preview_carousel_tests::disabled_prefetch_only_loads_visible_cards() {
    card_preview_carousel carousel;
    carousel.resize(640, 200);
    carousel.set_visible_count(3);
    carousel.set_prefetch_adjacent_cards(false);
    carousel.set_card_size(QSize(64, 96));

    QSet<int> requested_indices;
    carousel.set_card_provider(
        8, [&requested_indices](int index, const QSize& size) {
            requested_indices.insert(index);
            return test_card_pixmap(size);
        }
    );

    QCOMPARE(requested_indices.size(), 3);
    QVERIFY(requested_indices.contains(0));
    QVERIFY(requested_indices.contains(1));
    QVERIFY(requested_indices.contains(2));

    QToolButton* next_button = carousel_button(carousel, Qt::RightArrow);
    QVERIFY(next_button != nullptr);
    QVERIFY(next_button->isEnabled());
    next_button->click();

    QVERIFY(requested_indices.contains(3));
    QCOMPARE(requested_indices.size(), 4);
}
