#include "image/card_preview_carousel.hpp"

#include <QAccessible>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QStyle>

#include <algorithm>
#include <cmath>
#include <utility>

card_preview_carousel::card_preview_carousel(BaseWidget* parent)
    : BaseWidget(parent)
    , cached_cards()
    , card_provider()
    , card_accessible_name_provider()
    , card_labels()
    , first_index(0)
    , visible_count_value(5)
    , min_visible_count_value(3)
    , max_visible_count_value(5)
    , total_cards_value(0)
    , min_card_width_value(88)
    , card_spacing_value(0)
    , prefetch_adjacent_cards(true)
    , card_aspect_ratio(1.0)
    , base_card_size()
    , card_size()
    , previous_button(new QToolButton(this))
    , next_button(new QToolButton(this))
    , cards_container(new BaseWidget(this))
    , cards_layout(new QHBoxLayout(cards_container)) {
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    previous_button->setArrowType(Qt::LeftArrow);
    previous_button->setAutoRaise(true);
    previous_button->setEnabled(false);
    previous_button->setObjectName(QStringLiteral("carousel_previous_button"));
    previous_button->setAccessibleName(QStringLiteral("Previous cards"));
    previous_button->setAccessibleDescription(
        QStringLiteral("Show the previous cards in the preview")
    );
    previous_button->setToolTip(QStringLiteral("Previous cards"));
    layout->addWidget(previous_button);

    cards_layout->setContentsMargins(0, 0, 0, 0);
    cards_layout->setSpacing(card_spacing_value);
    cards_container->setAccessibleName(QStringLiteral("Card previews"));
    layout->addWidget(cards_container, 1);

    next_button->setArrowType(Qt::RightArrow);
    next_button->setAutoRaise(true);
    next_button->setEnabled(false);
    next_button->setObjectName(QStringLiteral("carousel_next_button"));
    next_button->setAccessibleName(QStringLiteral("Next cards"));
    next_button->setAccessibleDescription(
        QStringLiteral("Show the next cards in the preview")
    );
    next_button->setToolTip(QStringLiteral("Next cards"));
    layout->addWidget(next_button);

    QObject::connect(
        previous_button, &QToolButton::clicked, this,
        &card_preview_carousel::show_previous
    );
    QObject::connect(
        next_button, &QToolButton::clicked, this,
        &card_preview_carousel::show_next
    );

    rebuild_labels();
    refresh_view();
}

void card_preview_carousel::set_cards(const QVector<QPixmap>& next_cards) {
    cached_cards = next_cards;
    card_provider = {};
    total_cards_value = static_cast<int>(cached_cards.size());
    first_index = 0;
    refresh_view();
}

void card_preview_carousel::set_card_provider(
    int count, std::function<QPixmap(int, const QSize&)> provider
) {
    total_cards_value = std::max(0, count);
    card_provider = std::move(provider);
    cached_cards = QVector<QPixmap>(total_cards_value);
    first_index = 0;
    refresh_view();
}

void card_preview_carousel::set_card_accessible_name_provider(
    std::function<QString(int)> provider
) {
    card_accessible_name_provider = std::move(provider);
    refresh_view();
}

void card_preview_carousel::set_visible_count(int count) {
    if (count < 1) {
        count = 1;
    }
    set_visible_range(count, count);
}

void card_preview_carousel::set_visible_range(int min_count, int max_count) {
    if (min_count < 1) {
        min_count = 1;
    }
    if (max_count < min_count) {
        std::swap(min_count, max_count);
    }
    min_visible_count_value = min_count;
    max_visible_count_value = max_count;
    update_visible_count();
    update_card_dimensions();
}

void card_preview_carousel::set_card_size(const QSize& size) {
    if (base_card_size == size) {
        return;
    }
    base_card_size = size;
    if (base_card_size.isValid() && base_card_size.width() > 0) {
        card_aspect_ratio = static_cast<double>(base_card_size.height())
            / static_cast<double>(base_card_size.width());
    }
    update_card_dimensions();
}

void card_preview_carousel::set_minimum_card_width(int width) {
    if (width < 1) {
        width = 1;
    }
    if (min_card_width_value == width) {
        return;
    }
    min_card_width_value = width;
    update_visible_count();
    update_card_dimensions();
}

void card_preview_carousel::set_card_spacing(int spacing) {
    if (spacing < 0) {
        spacing = 0;
    }
    if (card_spacing_value == spacing) {
        return;
    }
    card_spacing_value = spacing;
    cards_layout->setSpacing(card_spacing_value);
    update_visible_count();
    update_card_dimensions();
}

void card_preview_carousel::set_prefetch_adjacent_cards(bool enabled) {
    if (prefetch_adjacent_cards == enabled) {
        return;
    }
    prefetch_adjacent_cards = enabled;
    if (!prefetch_adjacent_cards && card_provider && !cached_cards.isEmpty()) {
        const int total_cards = total_cards_value;
        const int label_count = static_cast<int>(card_labels.size());
        QVector<bool> keep_cache(total_cards, false);
        for (int i = 0; i < label_count; ++i) {
            if (total_cards <= 0) {
                break;
            }
            const int card_index = (first_index + i) % total_cards;
            if (card_index >= 0 && card_index < keep_cache.size()) {
                keep_cache[card_index] = true;
            }
        }
        for (int i = 0; i < cached_cards.size(); ++i) {
            if (!keep_cache[i]) {
                cached_cards[i] = QPixmap();
            }
        }
    }
}

void card_preview_carousel::refresh_cards() {
    clear_cached_cards();
    refresh_view();
}

void card_preview_carousel::show_previous() {
    if (total_cards_value == 0) {
        return;
    }
    const int total_cards = total_cards_value;
    if (total_cards <= 0) {
        return;
    }
    first_index = (first_index - 1 + total_cards) % total_cards;
    refresh_view();
}

void card_preview_carousel::show_next() {
    if (total_cards_value == 0) {
        return;
    }
    const int total_cards = total_cards_value;
    if (total_cards <= 0) {
        return;
    }
    first_index = (first_index + 1) % total_cards;
    refresh_view();
}

void card_preview_carousel::resizeEvent(QResizeEvent* event) {
    BaseWidget::resizeEvent(event);
    update_visible_count();
    update_card_dimensions();
}

void card_preview_carousel::rebuild_labels() {
    qDeleteAll(card_labels);
    card_labels.clear();

    for (int i = 0; i < visible_count_value; ++i) {
        auto label = new QLabel(cards_container);
        label->setAlignment(Qt::AlignCenter);
        label->setScaledContents(true);
        label->setAccessibleName(QStringLiteral("Card preview %1").arg(i + 1));
        if (card_size.isValid()) {
            label->setFixedSize(card_size);
        }
        cards_layout->addWidget(label);
        card_labels.push_back(label);
    }
}

void card_preview_carousel::update_visible_count() {
    const int available_width = cards_container->width();
    if (available_width <= 0) {
        return;
    }
    const int spacing = card_spacing_value;
    const int slot_size = min_card_width_value + spacing;
    int fit_count = slot_size > 0 ? (available_width + spacing) / slot_size
                                  : max_visible_count_value;
    int next_count = std::clamp(
        fit_count, min_visible_count_value, max_visible_count_value
    );
    if (next_count < 1) {
        next_count = 1;
    }
    if (visible_count_value == next_count) {
        return;
    }
    visible_count_value = next_count;
    rebuild_labels();
    clear_cached_cards();
    refresh_view();
}

void card_preview_carousel::update_card_dimensions() {
    if (!base_card_size.isValid()) {
        return;
    }
    const int available_width = cards_container->width();
    if (available_width <= 0 || visible_count_value <= 0) {
        return;
    }
    const int spacing = card_spacing_value;
    const int total_spacing = spacing * std::max(0, visible_count_value - 1);
    int card_width = (available_width - total_spacing) / visible_count_value;
    if (card_width < min_card_width_value) {
        card_width = min_card_width_value;
    }
    const int card_height = std::max(
        1, static_cast<int>(std::lround(card_width * card_aspect_ratio))
    );
    const QSize next_size(card_width, card_height);
    if (card_size == next_size) {
        return;
    }
    card_size = next_size;
    clear_cached_cards();
    for (QLabel* label : card_labels) {
        if (label == nullptr) {
            continue;
        }
        if (card_size.isValid()) {
            label->setFixedSize(card_size);
        } else {
            label->setMinimumSize(QSize(1, 1));
        }
    }
    refresh_view();
}

void card_preview_carousel::clear_cached_cards() {
    if (card_provider) {
        cached_cards.fill(QPixmap());
    }
}

void card_preview_carousel::refresh_view() {
    const int total_cards = total_cards_value;
    if (total_cards <= 0) {
        for (QLabel* label : card_labels) {
            if (label != nullptr) {
                label->clear();
                label->setVisible(false);
            }
        }
        previous_button->setEnabled(false);
        next_button->setEnabled(false);
        return;
    }
    const int label_count = static_cast<int>(card_labels.size());
    QVector<bool> keep_cache;
    if (card_provider && !cached_cards.isEmpty()) {
        keep_cache = QVector<bool>(total_cards, false);
    }
    for (int i = 0; i < label_count; ++i) {
        QLabel* label = card_labels[i];
        if (label == nullptr) {
            continue;
        }
        const int card_index = (first_index + i) % total_cards;
        const QString semantic_name = card_accessible_name_provider
            ? card_accessible_name_provider(card_index).trimmed()
            : QString();
        label->setAccessibleName(
            semantic_name.isEmpty()
                ? QStringLiteral("Card preview %1").arg(card_index + 1)
                : semantic_name
        );
        label->setAccessibleDescription(QStringLiteral("Preview card %1 of %2")
                                            .arg(card_index + 1)
                                            .arg(total_cards));
        if (!keep_cache.isEmpty() && card_index >= 0
            && card_index < keep_cache.size()) {
            keep_cache[card_index] = true;
        }
        QPixmap pixmap;
        if (!cached_cards.isEmpty() && card_index >= 0
            && card_index < cached_cards.size()) {
            pixmap = cached_cards[card_index];
            if (pixmap.isNull() && card_provider) {
                pixmap = card_provider(card_index, card_size);
                cached_cards[card_index] = pixmap;
            }
        } else if (card_provider) {
            pixmap = card_provider(card_index, card_size);
        }
        if (!pixmap.isNull()) {
            label->setPixmap(pixmap);
        } else {
            label->clear();
        }
        label->setVisible(true);
    }

    if (prefetch_adjacent_cards && card_provider && !cached_cards.isEmpty()) {
        const int prev_index = (first_index - 1 + total_cards) % total_cards;
        const int next_index = (first_index + label_count) % total_cards;
        if (prev_index >= 0 && prev_index < cached_cards.size()
            && cached_cards[prev_index].isNull()) {
            cached_cards[prev_index] = card_provider(prev_index, card_size);
        }
        if (next_index >= 0 && next_index < cached_cards.size()
            && cached_cards[next_index].isNull()) {
            cached_cards[next_index] = card_provider(next_index, card_size);
        }
        if (!keep_cache.isEmpty()) {
            if (prev_index >= 0 && prev_index < keep_cache.size()) {
                keep_cache[prev_index] = true;
            }
            if (next_index >= 0 && next_index < keep_cache.size()) {
                keep_cache[next_index] = true;
            }
            for (int i = 0; i < cached_cards.size(); ++i) {
                if (!keep_cache[i]) {
                    cached_cards[i] = QPixmap();
                }
            }
        }
    }

    previous_button->setEnabled(total_cards_value > 1);
    next_button->setEnabled(total_cards_value > 1);
    cards_container->setAccessibleDescription(
        QStringLiteral("Showing %1 card previews starting with item %2 of %3")
            .arg(label_count)
            .arg(first_index + 1)
            .arg(total_cards)
    );
    QAccessibleEvent accessibility_event(
        cards_container, QAccessible::DescriptionChanged
    );
    QAccessible::updateAccessibility(&accessibility_event);
}
