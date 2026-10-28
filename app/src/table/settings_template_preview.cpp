#include "table/settings_template.hpp"

#include "card_helpers/card_sheet.hpp"
#include "table/settings_template_support.hpp"
#include <QListWidget>
#include <QMap>
#include <QPointer>
#include <QTimer>
#include <shared_ui/graphics/qt/svg_image_view.hpp>
#include <shared_ui/qt/image_carousel.hpp>
#include <utility>

void settings_template_widget::update_theme_carousel(int suit_index) {
    if (!theme_carousel)
        return;
    theme_suit = suit_index;
    theme_carousel->set_item_size(
        settings_template_support::preview_card_size()
    );
    theme_carousel->set_item_accessible_name_provider([suit_index](int index) {
        return card_label_from_index(suit_index * 13 + index);
    });
    connect(
        theme_carousel, &shared_ui::qt::image_carousel::demand_changed, this,
        &settings_template_widget::update_preview_demands,
        static_cast<Qt::ConnectionType>(
            Qt::QueuedConnection | Qt::UniqueConnection
        )
    );
    theme_carousel->set_image_provider(
        13, std::bind_front(&settings_template_widget::active_theme_card, this)
    );
    update_preview_demands();
}

void settings_template_widget::update_preview_demands() {
    QMap<QString, card_image_demand> needed;
    const auto collect
        = [&needed](const shared_ui::qt::image_carousel* carousel, int suit) {
              if (!carousel || !carousel->isVisible()
                  || carousel->requested_item_size().isEmpty())
                  return;
              const auto logical = carousel->requested_item_size();
              const auto physical = shared_ui::qt::raster_physical_size(
                  logical, carousel->devicePixelRatioF()
              );
              const auto visible = carousel->visible_item_indices();
              for (int rank : carousel->requested_item_indices()) {
                  const auto id
                      = settings_template_support::preview_scope(rank, suit);
                  if (id.isEmpty())
                      continue;
                  const bool required = visible.contains(rank);
                  auto found = needed.find(id);
                  if (found == needed.end() || (required && !found->required))
                      needed.insert(id, { id, physical, required });
                  else if (found->required == required)
                      found->physical_size
                          = found->physical_size.expandedTo(physical);
                  // A visible use takes precedence over a speculative size
                  // from the other carousel; current pixels must not wait.
              }
          };
    collect(theme_carousel, theme_suit);
    const auto strategy
        = strategy_list_widget ? strategy_list_widget->currentRow() : -1;
    if (strategy >= 0 && strategy < strategies.size())
        collect(weights_carousel, weights_suit);
    preview_requested_ids = needed.keys();
    preview_physical_size = {};
    for (const auto& demand : std::as_const(needed))
        if (demand.required)
            preview_physical_size
                = preview_physical_size.expandedTo(demand.physical_size);
    if (needed.isEmpty()) {
        preview_images.clear();
        displayed_preview_images.clear();
        displayed_preview_source.clear();
        return;
    }
    // The complete current intent includes at most two adjacent candidates per
    // carousel. Only visible IDs are required for presentation.
    preview_source = selected_theme_source_id();
    const QPointer<settings_template_widget> lifetime(this);
    preview_images.set_source(preview_source);
    if (!lifetime)
        return;
    std::vector<card_image_demand> desired;
    for (const auto& demand : std::as_const(needed))
        desired.push_back(demand);
    preview_images.replace_desired(std::move(desired));
    if (!lifetime)
        return;
    for (auto it = displayed_preview_images.begin();
         it != displayed_preview_images.end();) {
        if (!needed.contains(it.key()))
            it = displayed_preview_images.erase(it);
        else
            ++it;
    }
    preview_images_changed();
}

void settings_template_widget::preview_images_changed() {
    if (preview_requested_ids.isEmpty())
        return;
    const bool complete = !preview_images.is_pending();
    const bool required_complete = !preview_images.is_pending_required();
    const bool cutover = !displayed_preview_source.isEmpty()
        && (displayed_preview_source != preview_source
            || displayed_preview_physical_size != preview_physical_size);
    if (cutover && !required_complete)
        return;
    // Initial faces publish independently. Source or geometry replacement
    // waits only for its visible required subset.
    auto next = cutover ? QHash<QString, QImage>() : displayed_preview_images;
    bool changed
        = cutover || (required_complete && displayed_preview_source.isEmpty());
    bool ready = false;
    for (const auto& id : preview_requested_ids) {
        const auto image = preview_images.image_for(id);
        ready |= !image.isNull();
        if (!image.isNull() || complete) {
            changed |= !next.contains(id)
                || next.value(id).cacheKey() != image.cacheKey();
            next.insert(id, image);
        }
    }
    if ((!ready && !required_complete) || !changed)
        return;
    displayed_preview_images = std::move(next);
    displayed_preview_source = preview_source;
    displayed_preview_physical_size = preview_physical_size;
    mark_preview_refresh_pending(true, true);
}

QImage
settings_template_widget::preview_face(int card_index, int suit_index) const {
    const auto id
        = settings_template_support::preview_scope(card_index, suit_index);
    return displayed_preview_images.value(id);
}

QPixmap settings_template_widget::active_theme_card(
    int card_index, const QSize& size
) const {
    return shared_ui::qt::raster_image_pixmap(
        preview_face(card_index, theme_suit), size, devicePixelRatioF()
    );
}

void settings_template_widget::flush_preview_refresh() {
    preview_refresh_scheduled = false;
    const bool refresh_theme
        = std::exchange(theme_preview_needs_refresh, false);
    const bool refresh_weights
        = std::exchange(weights_preview_needs_refresh, false);
    const QPointer<settings_template_widget> lifetime(this);
    if (refresh_theme && theme_carousel)
        theme_carousel->refresh_images();
    if (!lifetime)
        return;
    if (refresh_weights && weights_carousel)
        weights_carousel->refresh_images();
}

void settings_template_widget::mark_preview_refresh_pending(
    bool theme_preview, bool weights_preview
) {
    theme_preview_needs_refresh |= theme_preview;
    weights_preview_needs_refresh |= weights_preview;
    if (!preview_refresh_scheduled
        && (theme_preview_needs_refresh || weights_preview_needs_refresh)) {
        preview_refresh_scheduled = true;
        QTimer::singleShot(
            0, this, &settings_template_widget::flush_preview_refresh
        );
    }
}

void settings_template_widget::update_weights_carousel(int suit_index) {
    if (weights_carousel == nullptr) {
        return;
    }
    weights_suit = suit_index;
    int strategy_index = strategy_list_widget != nullptr
        ? strategy_list_widget->currentRow()
        : -1;
    if (strategy_index < 0 || strategy_index >= strategies.size()) {
        return;
    }
    const QSize card_size = settings_template_support::preview_card_size();
    weights_carousel->set_item_size(card_size);
    weights_carousel->set_item_accessible_name_provider([this, suit_index](
                                                            int card_index
                                                        ) {
        QString card_name = card_label_from_index(suit_index * 13 + card_index);
        const int selected_strategy = strategy_list_widget != nullptr
            ? strategy_list_widget->currentRow()
            : -1;
        if (selected_strategy < 0 || selected_strategy >= strategies.size()
            || card_index < 0
            || card_index >= strategies.at(selected_strategy).weights.size()) {
            return card_name;
        }
        return QStringLiteral("%1, count value %2")
            .arg(
                card_name,
                QString::number(
                    strategies.at(selected_strategy).weights.at(card_index)
                )
            );
    });
    weights_carousel->set_image_provider(
        13,
        std::bind_front(&settings_template_widget::active_weighted_card, this)
    );
    connect(
        weights_carousel, &shared_ui::qt::image_carousel::demand_changed, this,
        &settings_template_widget::update_preview_demands,
        static_cast<Qt::ConnectionType>(
            Qt::QueuedConnection | Qt::UniqueConnection
        )
    );
    update_preview_demands();
}

QPixmap settings_template_widget::active_weighted_card(
    int card_index, const QSize& size
) const {
    return weighted_card(card_index, weights_suit, size);
}

QPixmap settings_template_widget::weighted_card(
    int card_index, int suit_index, const QSize& size
) const {
    const int strategy
        = strategy_list_widget ? strategy_list_widget->currentRow() : -1;
    if (strategy < 0 || strategy >= strategies.size())
        return {};
    const auto face = preview_face(card_index, suit_index);
    if (!shared_ui::qt::raster_image_covers(face, size, devicePixelRatioF()))
        return {};
    return settings_template_support::build_weighted_card_preview(
        face, card_index, suit_index, size, strategies[strategy].weights,
        devicePixelRatioF()
    );
}
