#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "settings/theme_palette.hpp"
#include "table/card_widget.hpp"
#include <QPaintEvent>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

shared_ui::qt::item_geometry card_widget::layout_geometry() const {
    return layout_geometry(QSizeF(size()));
}

shared_ui::qt::item_geometry card_widget::layout_geometry(
    const QSizeF& size, std::optional<qreal> rotation
) const {
    const QPointF selection_offset = swap_selected_flag
        ? QPointF(
              std::sin(selection_phase) * 1.8,
              std::cos(selection_phase * 1.3) * 1.8
          )
        : QPointF();
    const qreal angle = rotation.value_or(
        gameplay_layout_rotation_deg.value_or(slot_rotated ? 0.0 : 90.0)
    );
    if (deck_frame_enabled) {
        // Six pixels each side: twelve between neighbouring slots. The top
        // keeps the inside legend clear of the cards.
        auto geometry = shared_ui::qt::deck_geometry_for(
            size, angle, {}, QMarginsF(6, 36, 6, 12)
        );
        geometry.frame = QRectF(QPointF(), size).adjusted(6, 6, -6, -6);
        return geometry;
    }
    return shared_ui::qt::item_geometry_for(size, angle, selection_offset);
}

shared_ui::qt::item_scene card_widget::visual_scene() const {
    using namespace shared_ui::qt;
    item_scene scene;
    scene.style = presentation_for(*this);
    scene.palette = palette();
    const auto contextual = scene.style.context.felt_surface;
    scene.surface = contextual.isValid()
        ? contextual
        : theme_palette_registry::option(
              load_table_appearance_preferences().palette
          )
              .base_color();
    switch (frame_style) {
    case slot_frame_style::classic:
        scene.frame = item_frame::classic;
        break;
    case slot_frame_style::thin:
        scene.frame = item_frame::thin;
        break;
    case slot_frame_style::piping:
        scene.frame = item_frame::piping;
        break;
    }
    const auto* deck = gameplay_deck();
    if (deck && deck->status == gameplay::deck_status::failed)
        scene.tone = item_tone::negative;
    else if (deck && deck->status == gameplay::deck_status::completed)
        scene.tone = item_tone::positive;
    scene.selected = swap_selected_flag;
    scene.alert = verdict_glow;
    if (slot_number > 0) {
        const QString number
            = QStringLiteral("%1").arg(slot_number, 2, 10, QLatin1Char('0'));
#ifdef KC_KDE
        scene.notch = i18n("SLOT %1", number);
#else
        scene.notch = str_label("SLOT %1").arg(number);
#endif
    }
    if (show_strategy_name_flag)
        scene.leading_legend = strategy_name;
    scene.trailing_legend = current_index_text();
    scene.marking = table_marking.image();
    scene.marking_size = table_marking.target().logical_size;
    scene.marking_fallback = str_label("kcuckoounter");
    const auto* bound = gameplay_deck();
    if (deck_frame_enabled) {
        scene.frame = item_frame::deck;
        scene.leading_legend = deck_label;
        scene.deck = deck_marks;
        scene.trailing_legend.clear();
        if (bound && bound->dealt_physical_cards > 0) {
            const auto position
                = static_cast<qulonglong>(bound->dealt_physical_cards);
#ifdef KC_KDE
            scene.trailing_legend = bound->configuration.infinite
                ? i18n("card %1", position)
                : i18n(
                      "%1/%2", position,
                      static_cast<qulonglong>(bound->stream.cards.size())
                  );
#else
            scene.trailing_legend = bound->configuration.infinite
                ? str_label("card %1").arg(position)
                : str_label("%1/%2").arg(position).arg(
                      static_cast<qulonglong>(bound->stream.cards.size())
                  );
#endif
        }
    }
    if (!has_cards())
        return scene;

    scene.transform = { card_rotation_deg, card_offset };
    scene.annotations = gameplay_extra_lines();
    if (scene.frame == item_frame::piping && show_strategy_name_flag
        && !scene.annotations.isEmpty()
        && scene.annotations.constFirst() == strategy_name)
        scene.annotations.removeFirst();
    scene.index = current_index_text();
    // The deck frame shows a card back where other frames show text only:
    // the hidden face still never leaks while a question is open.
    const bool hidden = display_hidden();
    if (hidden && !deck_frame_enabled) {
        scene.content = item_content::annotations;
        return scene;
    }
    scene.content = item_content::image;
    for (const auto& discard : discard_history)
        scene.underlays.append({ discard.rotation_deg, discard.offset });
    const int index = display_card_index();
    const bool back = hidden || !display_running() || index < 0;
    const auto& ids = card_element_ids();
    const int last = static_cast<int>(ids.size()) - 1;
    scene.image = card_face_image(
        back ? static_cast<int>(ids.size())
             : (index < 0 ? -1 : std::min(index, last))
    );
    scene.fallback = back
        ? str_label("Back")
        : (index >= 0 ? card_label_from_index(index) : str_label("Card"));
    scene.centered_fallback = back;
    scene.highlight = back ? 0.0 : highlight_strength();
    if (back)
        scene.index.clear();
    return scene;
}

void card_widget::paintEvent(QPaintEvent* event) {
    BaseWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    shared_ui::qt::paint_item_scene(painter, layout_geometry(), visual_scene());
}

int card_widget::bottom_legend_clearance() const {
    return layout_geometry().legend_clearance(
        frame_style == slot_frame_style::piping
            ? shared_ui::qt::item_frame::piping
            : shared_ui::qt::item_frame::classic,
        height()
    );
}

void card_widget::resizeEvent(QResizeEvent* event) {
    BaseWidget::resizeEvent(event);
    schedule_card_preparation();
    if (!gameplay_layout_rotation_deg) {
        update_card_jitter();
        update_table_marking();
    }
}

void card_widget::update_card_jitter() {
    const qreal max_offset = layout_geometry().jitter_limit;

    const auto rotation
        = static_cast<qreal>(random_gen.uniform_real(-3.5, 3.5));
    const auto offset_x
        = static_cast<qreal>(random_gen.uniform_real(-max_offset, max_offset));
    const auto offset_y
        = static_cast<qreal>(random_gen.uniform_real(-max_offset, max_offset));

    card_rotation_deg = rotation;
    card_offset = QPointF(offset_x, offset_y);
}

void card_widget::update_table_marking() {
    const QRectF slot_frame_rect = layout_geometry().frame;
    const qreal target_dim
        = std::min(slot_frame_rect.width(), slot_frame_rect.height()) * 0.5;
    const int size = static_cast<int>(std::max(1.0, target_dim));
    auto target = table_marking.target();
    target.device_pixel_ratio = devicePixelRatioF();
    target.logical_size = QSize(size, size);
    table_marking.set_target(target);
}

QString card_widget::current_index_text() const {
    if (!show_card_indexing_flag) {
        return {};
    }

    if (const auto* deck = gameplay_deck()) {
        if (deck->dealt_physical_cards == 0)
            return {};
        const auto position
            = static_cast<qulonglong>(deck->dealt_physical_cards);
        if (deck->configuration.infinite)
            return QString::number(position);
#ifdef KC_KDE
        return i18n(
            "%1/%2", position,
            static_cast<qulonglong>(deck->stream.cards.size())
        );
#else
        return str_label("%1/%2").arg(position).arg(
            static_cast<qulonglong>(deck->stream.cards.size())
        );
#endif
    }

    return {};
}
