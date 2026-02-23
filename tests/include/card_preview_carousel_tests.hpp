#ifndef KCUCKOOUNTER_CARD_PREVIEW_CAROUSEL_TESTS_HPP
#define KCUCKOOUNTER_CARD_PREVIEW_CAROUSEL_TESTS_HPP

#include <QObject>

class card_preview_carousel_tests : public QObject {
    Q_OBJECT

private slots:
    void default_prefetch_loads_adjacent_cards();
    void disabled_prefetch_only_loads_visible_cards();
};

#endif // KCUCKOOUNTER_CARD_PREVIEW_CAROUSEL_TESTS_HPP
