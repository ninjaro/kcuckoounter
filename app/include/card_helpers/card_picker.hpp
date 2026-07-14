#ifndef KCUCKOOUNTER_CARD_HELPERS_CARD_PICKER_HPP
#define KCUCKOOUNTER_CARD_HELPERS_CARD_PICKER_HPP

#include "arch/random_generator.hpp"
#include <vector>

class card_picker {
public:
    card_picker();

    void setup(int cards_per_deck, int decks_count, bool infinity_enabled);
    void set_infinity(bool enabled);
    void advance();
    void mark_depleted();

    [[nodiscard]] bool has_cards() const;
    [[nodiscard]] int current_card_index() const;
    [[nodiscard]] int current_position() const;
    [[nodiscard]] int total_cards() const;
    [[nodiscard]] bool is_depleted() const;
    [[nodiscard]] int card_index_at(int position) const;

private:
    void fill_deck(int cards_per_deck, int decks_count);
    void shuffle_deck();

    std::vector<int> deck;
    int deck_position;
    bool infinity;
    random_generator random_gen;
};

#endif // KCUCKOOUNTER_CARD_HELPERS_CARD_PICKER_HPP
