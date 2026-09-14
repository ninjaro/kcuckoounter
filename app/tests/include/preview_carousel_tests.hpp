#ifndef KCUCKOOUNTER_TESTS_PREVIEW_CAROUSEL_TESTS_HPP
#define KCUCKOOUNTER_TESTS_PREVIEW_CAROUSEL_TESTS_HPP

#include <QObject>

class preview_carousel_tests : public QObject {
    Q_OBJECT

private slots:
    void default_prefetch_loads_adjacent_cards();
    void disabled_prefetch_only_loads_visible_cards();
    void navigation_controls_expose_accessible_semantics();
    void visible_cards_expose_domain_accessible_names();
};

#endif // KCUCKOOUNTER_TESTS_PREVIEW_CAROUSEL_TESTS_HPP
