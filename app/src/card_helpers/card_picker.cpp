#include "card_helpers/card_picker.hpp"

#include <algorithm>

card_picker::card_picker()
    : deck()
    , deck_position(0)
    , infinity(false)
    , random_gen() { }

void card_picker::setup(
    int cards_per_deck, int decks_count, bool infinity_enabled
) {
    fill_deck(cards_per_deck, decks_count);
    shuffle_deck();
    deck_position = 0;
    infinity = infinity_enabled;
}

void card_picker::set_infinity(bool enabled) { infinity = enabled; }

void card_picker::advance() {
    if (deck.empty()) {
        return;
    }

    if (infinity) {
        if (deck_position + 1 >= static_cast<int>(deck.size())) {
            shuffle_deck();
            deck_position = 0;
        } else {
            ++deck_position;
        }
        return;
    }

    if (deck_position + 1 >= static_cast<int>(deck.size())) {
        deck_position = static_cast<int>(deck.size());
        return;
    }

    ++deck_position;
}

void card_picker::mark_depleted() {
    if (deck.empty()) {
        return;
    }
    deck_position = static_cast<int>(deck.size());
}

bool card_picker::has_cards() const { return !deck.empty(); }

int card_picker::current_card_index() const {
    if (deck_position < 0 || deck_position >= static_cast<int>(deck.size())) {
        return -1;
    }
    return deck[static_cast<size_t>(deck_position)];
}

int card_picker::current_position() const {
    if (deck_position < 0 || deck_position >= static_cast<int>(deck.size())) {
        return -1;
    }
    return deck_position;
}

int card_picker::total_cards() const { return static_cast<int>(deck.size()); }

bool card_picker::is_depleted() const {
    if (infinity) {
        return false;
    }
    if (deck.empty()) {
        return true;
    }
    return deck_position >= static_cast<int>(deck.size());
}

int card_picker::card_index_at(int position) const {
    if (position < 0 || position >= static_cast<int>(deck.size())) {
        return -1;
    }
    return deck[static_cast<size_t>(position)];
}

void card_picker::fill_deck(int cards_per_deck, int decks_count) {
    deck.clear();

    if (cards_per_deck <= 0 || decks_count <= 0) {
        return;
    }

    deck.reserve(
        static_cast<size_t>(cards_per_deck) * static_cast<size_t>(decks_count)
    );
    for (int deck_index = 0; deck_index < decks_count; ++deck_index) {
        for (int card_index = 0; card_index < cards_per_deck; ++card_index) {
            deck.push_back(card_index);
        }
    }
}

void card_picker::shuffle_deck() {
    if (deck.empty()) {
        return;
    }

    random_gen.shuffle(deck.begin(), deck.end());
}
