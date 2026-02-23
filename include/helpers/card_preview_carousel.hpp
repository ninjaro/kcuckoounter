#ifndef KCUCKOOUNTER_HELPERS_CARD_PREVIEW_CAROUSEL_HPP
#define KCUCKOOUNTER_HELPERS_CARD_PREVIEW_CAROUSEL_HPP

#include "helpers/widget_helpers.hpp"

#include <QLabel>
#include <QPixmap>
#include <QSize>
#include <QToolButton>
#include <QVector>

#include <functional>

class QHBoxLayout;
class QResizeEvent;

class card_preview_carousel : public BaseWidget {
    Q_OBJECT

public:
    explicit card_preview_carousel(BaseWidget* parent = nullptr);

    void set_cards(const QVector<QPixmap>& cards);
    void set_card_provider(
        int count, std::function<QPixmap(int, const QSize&)> provider
    );
    void set_visible_count(int count);
    void set_visible_range(int min_count, int max_count);
    void set_card_size(const QSize& size);
    void set_minimum_card_width(int width);
    void set_card_spacing(int spacing);
    void set_prefetch_adjacent_cards(bool enabled);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void show_previous();
    void show_next();

private:
    void rebuild_labels();
    void update_visible_count();
    void update_card_dimensions();
    void refresh_view();

    void clear_cached_cards();

    QVector<QPixmap> cached_cards;
    std::function<QPixmap(int, const QSize&)> card_provider;
    QVector<QLabel*> card_labels;
    int first_index;
    int visible_count_value;
    int min_visible_count_value;
    int max_visible_count_value;
    int total_cards_value;
    int min_card_width_value;
    int card_spacing_value;
    bool prefetch_adjacent_cards;
    double card_aspect_ratio;
    QSize base_card_size;
    QSize card_size;
    QToolButton* previous_button;
    QToolButton* next_button;
    BaseWidget* cards_container;
    QHBoxLayout* cards_layout;
};

#endif // KCUCKOOUNTER_HELPERS_CARD_PREVIEW_CAROUSEL_HPP
