#ifndef KCUCKOOUNTER_TESTS_INCLUDE_CARD_PACKER_TESTS_HPP
#define KCUCKOOUNTER_TESTS_INCLUDE_CARD_PACKER_TESTS_HPP

#include <QObject>

class card_packer_tests : public QObject {
    Q_OBJECT

private slots:
    void simple_pack();
    void deck_52();
    void bounds_and_overlap();
    void more_cards_smaller_scale();
};

#endif // KCUCKOOUNTER_TESTS_INCLUDE_CARD_PACKER_TESTS_HPP
