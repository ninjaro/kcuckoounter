#include "table/card_widget.hpp"

#include "arch/asset_locator.hpp"
#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "settings/theme_settings.hpp"
#include <QColor>
#include <QFontMetricsF>
#include <QPaintEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

qreal card_widget::compute_font_point_size(const QRectF& card_rect) {
    qreal point_size = card_rect.height() * 0.10;
    return std::clamp(point_size, 8.0, 20.0);
}

int card_widget::blend_color_channel(int from, int to, qreal strength) {
    const qreal clamped = std::clamp(strength, 0.0, 1.0);
    return static_cast<int>(from + (to - from) * clamped);
}

QColor
card_widget::blend_color(const QColor& from, const QColor& to, qreal strength) {
    QColor blended(
        blend_color_channel(from.red(), to.red(), strength),
        blend_color_channel(from.green(), to.green(), strength),
        blend_color_channel(from.blue(), to.blue(), strength),
        blend_color_channel(from.alpha(), to.alpha(), strength)
    );
    return blended;
}

QPointF card_widget::paint_geometry::bounded_offset(const QPointF& offset
) const {
    return { std::clamp(offset.x(), -jitter_limit, jitter_limit),
             std::clamp(offset.y(), -jitter_limit, jitter_limit) };
}

card_widget::paint_geometry card_widget::layout_geometry() const {
    return layout_geometry(QSizeF(size()));
}

card_widget::paint_geometry card_widget::layout_geometry(const QSizeF& size
) const {
    paint_geometry result;
    const qreal outer_margin
        = std::min(3.0, std::min(size.width(), size.height()) / 8.0);
    const QRectF slot_rect
        = QRectF(QPointF(), size)
              .adjusted(
                  outer_margin, outer_margin, -outer_margin, -outer_margin
              );
    result.min_dim = std::min(slot_rect.width(), slot_rect.height());
    if (result.min_dim <= 0.0) {
        return result;
    }
    const qreal frame_margin = std::min(
        std::clamp(result.min_dim * 0.05, 4.0, 10.0), result.min_dim / 8.0
    );
    const QRectF base_slot_frame_rect = slot_rect.adjusted(
        frame_margin, frame_margin, -frame_margin, -frame_margin
    );
    QPointF selection_offset(0.0, 0.0);
    if (swap_selected_flag) {
        const qreal jitter = 1.8;
        selection_offset = QPointF(
            std::sin(selection_phase) * jitter,
            std::cos(selection_phase * 1.3) * jitter
        );
    }
    result.frame = base_slot_frame_rect.translated(selection_offset);
    const qreal frame_short
        = std::min(result.frame.width(), result.frame.height());
    const qreal inset = std::min(
        std::clamp(result.min_dim * 0.08, 4.0, 12.0), frame_short / 4.0
    );
    result.jitter_limit = std::min(inset * 0.6, frame_short / 16.0);
    QSizeF card_size
        = result.frame.adjusted(inset, inset, -inset, -inset).size();

    // Bound all rotations in [-3.5, 3.5], not only one random draw. cos(theta)
    // <= 1 gives a conservative envelope even for very unusual aspect ratios.
    // Reserve room for the frame/card strokes and integer pixmap rounding.
    const qreal stroke_room = std::min(4.0, frame_short / 8.0);
    const qreal sine = std::sin(3.5 * std::acos(-1.0) / 180.0);
    result.slot_rotation
        = gameplay_layout_rotation_deg.value_or(slot_rotated ? 0.0 : 90.0);
    // Fit the card plus its existing bounded jitter at every intermediate
    // orientation. At 0/90 this is exactly the endpoint calculation.
    const qreal fraction = result.slot_rotation / 90.0;
    card_size = QSizeF(
        std::lerp(card_size.width(), card_size.height(), fraction),
        std::lerp(card_size.height(), card_size.width(), fraction)
    );
    const qreal radians = result.slot_rotation * std::acos(-1.0) / 180.0;
    const qreal cosine = result.slot_rotation == 0.0
        ? 1.0
        : (result.slot_rotation == 90.0 ? 0.0 : std::cos(radians));
    const qreal slot_sine = result.slot_rotation == 0.0
        ? 0.0
        : (result.slot_rotation == 90.0 ? 1.0 : std::sin(radians));
    const qreal envelope_width
        = cosine * (card_size.width() + card_size.height() * sine)
        + slot_sine * (card_size.height() + card_size.width() * sine);
    const qreal envelope_height
        = slot_sine * (card_size.width() + card_size.height() * sine)
        + cosine * (card_size.height() + card_size.width() * sine);
    const qreal scale = std::min(
        { 1.0,
          (result.frame.width() - 2.0 * (stroke_room + result.jitter_limit))
              / envelope_width,
          (result.frame.height() - 2.0 * (stroke_room + result.jitter_limit))
              / envelope_height }
    );
    card_size *= std::max(0.0, scale);
    result.card = QRectF(
        result.frame.center()
            - QPointF(card_size.width() / 2.0, card_size.height() / 2.0),
        card_size
    );
    return result;
}

void card_widget::paintEvent(QPaintEvent* event) {
    BaseWidget::paintEvent(event);
    if (gameplay_paint_external)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto geometry = layout_geometry();
    paint_slot_frame(painter, geometry);
    paint_deck_content(painter, geometry);
}

void card_widget::paint_slot_frame(
    QPainter& painter, const paint_geometry& geometry
) const {
    if (geometry.card.isEmpty())
        return;
    const auto* target = gameplay_deck();
    const bool failed
        = target && target->status == gameplay::deck_status::failed;
    const bool completed
        = target && target->status == gameplay::deck_status::completed;
    const QColor slot_fill_color
        = failed ? QColor(70, 70, 70) : theme_settings::slot_fill_color();
    const QColor slot_border_color = swap_selected_flag
        ? theme_settings::slot_border_selected_color()
        : (failed ? QColor(35, 35, 35)
                  : (completed ? QColor(40, 110, 70)
                               : theme_settings::slot_border_color()));
    if (frame_style == slot_frame_style::piping) {
        paint_piping_frame(
            painter, geometry, slot_fill_color, slot_border_color
        );
        return;
    }
    painter.setPen(QPen(
        slot_border_color, frame_style == slot_frame_style::thin ? 1.0 : 6.6
    ));
    painter.setBrush(QBrush(slot_fill_color));
    painter.drawRoundedRect(geometry.frame, 10.0, 10.0);
}

void card_widget::paint_piping_frame(
    QPainter& painter, const paint_geometry& geometry, const QColor& fill,
    const QColor& border
) const {
    // D-SLOT-2: a 1.5px gold hairline with a 6px soft outer ring, an inset
    // vignette, the slot number in a top notch and the strategy and shoe
    // position as legends on the bottom line. Nothing occupies the card's
    // safe area, so card size, jitter and history are unchanged.
    const QRectF& frame = geometry.frame;
    const felt_material material = theme_settings::felt();
    const qreal radius = std::clamp(geometry.min_dim * 0.045, 6.0, 16.0);
    const QColor line = verdict_glow ? material.wrong : border;
    painter.save();
    painter.setBrush(Qt::NoBrush);
    // Outer bloom; a correction glows wider and stronger (D-FEED-2's edge).
    const qreal ring = std::min(6.0, geometry.min_dim / 40.0 + 2.0);
    std::vector<std::pair<qreal, qreal>> blooms { { ring, 0.09 } };
    if (verdict_glow)
        blooms = { { ring * 2.4, 0.10 }, { ring * 1.4, 0.22 }, { 3.0, 0.9 } };
    for (const auto& [spread, alpha] : blooms) {
        QColor bloom = line;
        bloom.setAlphaF(static_cast<float>(alpha));
        painter.setPen(QPen(bloom, spread));
        painter.drawRoundedRect(
            frame.adjusted(
                -spread / 2.0, -spread / 2.0, spread / 2.0, spread / 2.0
            ),
            radius + spread / 2.0, radius + spread / 2.0
        );
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(frame, radius, radius);
    // Inset shadow: darker toward the frame, clear in the middle.
    const qreal depth = std::min(40.0, geometry.min_dim * 0.12);
    constexpr int steps = 8;
    for (int i = 0; i < steps; ++i) {
        const qreal t = static_cast<qreal>(i) / steps;
        const qreal inset = depth * t;
        QColor shade(0, 0, 0);
        shade.setAlphaF(static_cast<float>(0.3 * (1.0 - t) * (1.0 - t) / 2.2));
        painter.setPen(QPen(shade, depth / steps + 0.5));
        painter.setBrush(Qt::NoBrush);
        const qreal half = depth / steps / 2.0;
        painter.drawRoundedRect(
            frame.adjusted(
                inset + half, inset + half, -inset - half, -inset - half
            ),
            std::max(0.0, radius - inset), std::max(0.0, radius - inset)
        );
    }
    painter.setPen(QPen(line, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(frame, radius, radius);

    if (geometry.min_dim < 64.0) {
        painter.restore();
        return;
    }
    QFont notch_font(material.mono_family);
    const qreal font_px = std::clamp(geometry.min_dim * 0.035, 9.0, 11.0);
    notch_font.setPixelSize(static_cast<int>(font_px));
    notch_font.setLetterSpacing(QFont::AbsoluteSpacing, font_px * 0.14);
    const QFontMetricsF metrics(notch_font);
    const QColor table_fill = theme_settings::table_color();
    if (slot_number > 0) {
        const QString number
            = QStringLiteral("%1").arg(slot_number, 2, 10, QLatin1Char('0'));
#ifdef KC_KDE
        const QString text = i18n("SLOT %1", number);
#else
        const QString text = str_label("SLOT %1").arg(number);
#endif
        const QSizeF size(
            metrics.horizontalAdvance(text) + 24.0, metrics.height() + 4.0
        );
        QRectF notch(
            QPointF(
                frame.center().x() - size.width() / 2.0,
                frame.top() - size.height() / 2.0
            ),
            size
        );
        notch.moveTop(std::max(0.0, notch.top()));
        painter.setPen(QPen(line, 1.5));
        painter.setBrush(table_fill);
        painter.drawRoundedRect(
            notch, size.height() / 2.0, size.height() / 2.0
        );
        painter.setFont(notch_font);
        painter.setPen(line);
        painter.drawText(notch, Qt::AlignCenter, text);
    }
    // Bottom legends cut the line like a fieldset legend: the strategy at
    // the left, the shoe position at the right.
    const auto legend = [&](const QString& text, const QFont& font,
                            const QColor& color, bool left) {
        const QFontMetricsF legend_metrics(font);
        const qreal available = frame.width() / 2.0 - radius - 12.0;
        const QString shown
            = legend_metrics.elidedText(text, Qt::ElideRight, available);
        if (shown.isEmpty())
            return;
        const qreal width = legend_metrics.horizontalAdvance(shown) + 12.0;
        const qreal height = legend_metrics.height();
        const QRectF box(
            left ? frame.left() + radius + 4.0
                 : frame.right() - radius - 4.0 - width,
            frame.bottom() - height / 2.0, width, height
        );
        painter.fillRect(
            QRectF(box.left(), box.top(), box.width(), box.height() / 2.0),
            table_fill
        );
        painter.fillRect(
            QRectF(
                box.left(), box.center().y(), box.width(), box.height() / 2.0
            ),
            fill
        );
        painter.setFont(font);
        painter.setPen(color);
        painter.drawText(box, Qt::AlignCenter, shown);
    };
    if (show_strategy_name_flag && !strategy_name.isEmpty()) {
        QColor gold = line;
        gold.setAlphaF(0.75f);
        legend(strategy_name.toUpper(), notch_font, gold, true);
    }
    const QString index = current_index_text();
    if (!index.isEmpty()) {
        QFont index_font(material.mono_family);
        index_font.setPixelSize(static_cast<int>(font_px) + 1);
        QColor paper = material.paper;
        paper.setAlphaF(0.75f);
        legend(index, index_font, paper, false);
    }
    painter.restore();
}

int card_widget::bottom_legend_clearance() const {
    if (frame_style != slot_frame_style::piping)
        return 0;
    const paint_geometry geometry = layout_geometry();
    // Legends are not painted below this size (see paint_piping_frame).
    if (geometry.min_dim < 64.0)
        return 0;
    // The legend box is centred on the line: half its height plus a gap.
    return static_cast<int>(std::ceil(height() - geometry.frame.bottom() + 9.0)
    );
}

void card_widget::draw_card_mat(
    QPainter& painter, const QRectF& oriented_card_rect,
    qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset,
    bool top_card
) const {
    if (!theme_settings::felt_chrome())
        return;
    const qreal short_side
        = std::min(oriented_card_rect.width(), oriented_card_rect.height());
    // Shadow falls down the screen whatever the card's orientation.
    const qreal drop
        = std::min(10.0, short_side * 0.05) * (top_card ? 1.0 : 0.5);
    const qreal blur = std::min(24.0, short_side * 0.1);
    painter.save();
    painter.translate(0.0, drop);
    apply_card_transform(
        painter, oriented_card_rect, slot_rotation_deg, rotation_deg, offset
    );
    painter.setPen(Qt::NoPen);
    constexpr int layers = 6;
    for (int i = 0; i < layers; ++i) {
        const qreal spread
            = blur * (1.0 - static_cast<qreal>(i) / layers) / 2.0;
        painter.setBrush(QColor(0, 0, 0, top_card ? 20 : 10));
        painter.drawRoundedRect(
            oriented_card_rect.adjusted(-spread, -spread, spread, spread),
            9.0 + spread, 9.0 + spread
        );
    }
    painter.restore();
    if (!top_card || frame_style != slot_frame_style::piping)
        return;
    const qreal mat = std::clamp(short_side * 0.025, 1.5, 4.0);
    painter.save();
    apply_card_transform(
        painter, oriented_card_rect, slot_rotation_deg, rotation_deg, offset
    );
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme_settings::felt().paper);
    painter.drawRoundedRect(
        oriented_card_rect.adjusted(-mat, -mat, mat, mat), 9.0 + mat, 9.0 + mat
    );
    painter.restore();
}

void card_widget::paint_deck_content(
    QPainter& painter, const paint_geometry& geometry
) {
    const QRectF& slot_frame_rect = geometry.frame;
    const QRectF& oriented_card_rect = geometry.card;
    const qreal min_dim = geometry.min_dim;
    const qreal slot_rotation_deg = geometry.slot_rotation;
    const QPointF rendered_offset = geometry.bounded_offset(card_offset);
    if (oriented_card_rect.isEmpty()) {
        return;
    }

    const auto* target = gameplay_deck();
    const bool has_deck = has_cards();
    const int card_index = display_card_index();
    const bool has_current_card = card_index >= 0;
    const bool show_back
        = has_deck && (!display_running() || !has_current_card);

    const qreal strength = highlight_strength();

    if (!has_deck) {
        draw_table_marking(painter, slot_frame_rect, min_dim);
        return;
    }

    auto target_lines = gameplay_extra_lines();
    // D-SLOT-2 carries the strategy as a bottom legend, never over the card.
    if (frame_style == slot_frame_style::piping && show_strategy_name_flag
        && !target_lines.isEmpty()
        && target_lines.constFirst() == strategy_name)
        target_lines.removeFirst();
    if (display_hidden()) {
        if (theme_settings::felt_chrome()) {
            // Felt docks the answer panel below the quiz marking instead of
            // covering its middle; the marking keeps its raster size.
            draw_table_marking(
                painter,
                slot_frame_rect.adjusted(
                    0.0, 0.0, 0.0, -slot_frame_rect.height() * 0.42
                ),
                min_dim, 0.72
            );
        } else {
            draw_table_marking(painter, slot_frame_rect, min_dim);
        }
        draw_card_index(
            painter, oriented_card_rect, slot_rotation_deg, rendered_offset
        );
        if (!target_lines.isEmpty())
            draw_card_extra_lines(
                painter, oriented_card_rect, slot_rotation_deg, rendered_offset,
                target_lines
            );
        return;
    }
    draw_table_marking(painter, slot_frame_rect, min_dim);

    const QColor discard_fill_color(248, 248, 248, 235);
    const QColor discard_border_color(220, 220, 220, 210);
    for (const discard_card& discard : discard_history) {
        draw_card_mat(
            painter, oriented_card_rect, slot_rotation_deg,
            discard.rotation_deg, geometry.bounded_offset(discard.offset), false
        );
        draw_card_shape(
            painter, oriented_card_rect, slot_rotation_deg,
            discard.rotation_deg, geometry.bounded_offset(discard.offset),
            discard_fill_color, discard_border_color
        );
    }

    const auto& element_ids = card_element_ids();
    const int max_card_index
        = element_ids.isEmpty() ? -1 : static_cast<int>(element_ids.size()) - 1;
    const int mapped_card_index
        = card_index < 0 ? -1 : std::min(card_index, max_card_index);
    const int back_index = static_cast<int>(element_ids.size());

    if (show_back) {
        const QColor base_card_fill(250, 250, 250);
        const QColor base_card_border(210, 210, 210, 220);
        draw_card_mat(
            painter, oriented_card_rect, slot_rotation_deg, card_rotation_deg,
            rendered_offset, true
        );
        draw_card_shape(
            painter, oriented_card_rect, slot_rotation_deg, card_rotation_deg,
            rendered_offset, base_card_fill, base_card_border
        );

        const QPixmap& back = card_face_pixmap(back_index);

        if (!back.isNull()) {
            draw_card_pixmap(
                painter, oriented_card_rect, slot_rotation_deg,
                card_rotation_deg, rendered_offset, back
            );
        } else {
            draw_card_center_text(
                painter, oriented_card_rect, slot_rotation_deg,
                card_rotation_deg, rendered_offset, str_label("Back")
            );
        }
        if (!target_lines.isEmpty())
            draw_card_extra_lines(
                painter, oriented_card_rect, slot_rotation_deg, rendered_offset,
                target_lines
            );
        return;
    }

    QString text;
    if (card_index >= 0) {
        text = card_label_from_index(card_index);
    } else {
        text = str_label("Card");
    }

    const QColor base_card_fill(250, 250, 250);
    const QColor base_card_border(210, 210, 210, 220);
    const QColor highlight_fill_target(214, 232, 255, 250);
    const QColor highlight_border_target(120, 170, 235, 235);
    const QColor card_fill_color
        = blend_color(base_card_fill, highlight_fill_target, strength);
    const QColor card_border_color
        = blend_color(base_card_border, highlight_border_target, strength);

    draw_card_mat(
        painter, oriented_card_rect, slot_rotation_deg, card_rotation_deg,
        rendered_offset, true
    );
    draw_card_shape(
        painter, oriented_card_rect, slot_rotation_deg, card_rotation_deg,
        rendered_offset, card_fill_color, card_border_color
    );

    const QPixmap& face = card_face_pixmap(mapped_card_index);

    if (!face.isNull()) {
        draw_card_pixmap(
            painter, oriented_card_rect, slot_rotation_deg, card_rotation_deg,
            rendered_offset, face
        );
    } else if (!text.isEmpty()) {
        draw_card_text(
            painter, oriented_card_rect, slot_rotation_deg, card_rotation_deg,
            rendered_offset, text
        );
    }

    QStringList extra_lines = target_lines;
    const bool piping = frame_style == slot_frame_style::piping;
    if (!target && !piping && show_strategy_name_flag
        && !strategy_name.isEmpty()) {
        extra_lines.append(strategy_name);
    }
    if (!target && training_mode_flag) {
        if (picker.current_card_index() >= 0) {
            const int total_weight = total_weight_for_picks();
            extra_lines.append(weight_text_for_value(total_weight));
        }
    }

    if (!extra_lines.isEmpty()) {
        draw_card_extra_lines(
            painter, oriented_card_rect, slot_rotation_deg, rendered_offset,
            extra_lines
        );
    }

    draw_card_index(
        painter, oriented_card_rect, slot_rotation_deg, rendered_offset
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
    table_marking.set_device_pixel_ratio(devicePixelRatioF());
    table_marking.set_target_size(QSize(size, size));
}

void card_widget::apply_card_transform(
    QPainter& painter, const QRectF& oriented_card_rect,
    qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset
) {
    const QPointF transform_center = oriented_card_rect.center() + offset;
    painter.translate(transform_center);
    painter.rotate(rotation_deg + slot_rotation_deg);
    painter.translate(-oriented_card_rect.center());
}

void card_widget::draw_table_marking(
    QPainter& painter, const QRectF& slot_frame_rect, qreal min_dim, qreal scale
) const {
    if (table_marking.is_ready()) {
        const QPixmap& marking = table_marking.pixmap();
        const QSizeF marking_size
            = QSizeF(marking.size())
                  .scaled(
                      QSizeF(table_marking.display_size()) * scale,
                      Qt::KeepAspectRatio
                  );
        const QPointF marking_top_left(
            slot_frame_rect.center().x() - marking_size.width() / 2.0,
            slot_frame_rect.center().y() - marking_size.height() / 2.0
        );
        const QRectF marking_rect(marking_top_left, marking_size);
        painter.drawPixmap(marking_rect, marking, marking.rect());
        return;
    }

    QColor marking_color(str_label("#D4AF37"));
    marking_color.setAlpha(150);
    QFont marking_font = painter.font();
    marking_font.setBold(true);
    marking_font.setPointSizeF(std::clamp(min_dim * 0.09, 9.0, 18.0));
    painter.setFont(marking_font);

    painter.save();
    painter.setPen(marking_color);
    const bool long_side_horizontal
        = slot_frame_rect.width() >= slot_frame_rect.height();
    const QPointF marking_center = slot_frame_rect.center();
    if (!long_side_horizontal) {
        painter.translate(marking_center);
        painter.rotate(90.0);
        painter.translate(-marking_center);
    }

    const QRectF marking_rect = slot_frame_rect.adjusted(
        slot_frame_rect.width() * 0.08, slot_frame_rect.height() * 0.08,
        -slot_frame_rect.width() * 0.08, -slot_frame_rect.height() * 0.08
    );
    painter.drawText(marking_rect, Qt::AlignCenter, str_label("kcuckoounter"));
    painter.restore();
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

    const int position = picker.current_position();
    if (position < 0) {
        return {};
    }

    const int current_value = position + 1;
    if (infinity_enabled) {
        return QString::number(current_value);
    }

    const int total = std::max(1, cards_per_deck * decks_count);
#ifdef KC_KDE
    return i18n("%1/%2", current_value, total);
#else
    return str_label("%1/%2").arg(current_value).arg(total);
#endif
}

void card_widget::draw_card_index(
    QPainter& painter, const QRectF& oriented_card_rect,
    qreal slot_rotation_deg, const QPointF& offset
) const {
    const QString index_text = current_index_text();
    if (index_text.isEmpty() || frame_style == slot_frame_style::piping) {
        return;
    }

    QFont index_font = painter.font();
    index_font.setBold(true);
    index_font.setPointSizeF(
        std::clamp(compute_font_point_size(oriented_card_rect) * 0.6, 6.0, 12.0)
    );
    painter.setFont(index_font);
    painter.setPen(QColor(40, 80, 50));

    painter.save();
    apply_card_transform(
        painter, oriented_card_rect, slot_rotation_deg, card_rotation_deg,
        offset
    );
    const QRectF index_rect = oriented_card_rect.adjusted(
        8.0, 8.0, -8.0, -oriented_card_rect.height() * 0.7
    );
    painter.drawText(index_rect, Qt::AlignRight | Qt::AlignTop, index_text);
    painter.restore();
}

void card_widget::draw_card_shape(
    QPainter& painter, const QRectF& oriented_card_rect,
    qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset,
    const QColor& fill, const QColor& border
) {
    painter.save();
    apply_card_transform(
        painter, oriented_card_rect, slot_rotation_deg, rotation_deg, offset
    );
    painter.setPen(QPen(border, 1.6));
    painter.setBrush(QBrush(fill));
    painter.drawRoundedRect(oriented_card_rect, 9.0, 9.0);
    painter.restore();
}

void card_widget::draw_card_pixmap(
    QPainter& painter, const QRectF& oriented_card_rect,
    qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset,
    const QPixmap& pixmap
) {
    painter.save();
    apply_card_transform(
        painter, oriented_card_rect, slot_rotation_deg, rotation_deg, offset
    );
    const auto fit = pixmap.deviceIndependentSize().scaled(
        oriented_card_rect.size(), Qt::KeepAspectRatio
    );
    const QRectF destination(
        oriented_card_rect.center()
            - QPointF(fit.width() / 2, fit.height() / 2),
        fit
    );
    painter.drawPixmap(destination, pixmap, QRectF(pixmap.rect()));
    painter.restore();
}

void card_widget::draw_card_text(
    QPainter& painter, const QRectF& oriented_card_rect,
    qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset,
    const QString& text
) {
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSizeF(compute_font_point_size(oriented_card_rect));
    painter.setFont(font);
    painter.setPen(QColor(20, 60, 35));

    painter.save();
    apply_card_transform(
        painter, oriented_card_rect, slot_rotation_deg, rotation_deg, offset
    );

    const qreal bottom_margin = oriented_card_rect.height() * 0.45;
    const QRectF text_rect
        = oriented_card_rect.adjusted(8.0, 8.0, -8.0, -bottom_margin);
    painter.drawText(
        text_rect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, text
    );
    painter.restore();
}

void card_widget::draw_card_center_text(
    QPainter& painter, const QRectF& oriented_card_rect,
    qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset,
    const QString& text
) {
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSizeF(compute_font_point_size(oriented_card_rect));
    painter.setFont(font);
    painter.setPen(QColor(20, 60, 35));

    painter.save();
    apply_card_transform(
        painter, oriented_card_rect, slot_rotation_deg, rotation_deg, offset
    );
    painter.drawText(oriented_card_rect, Qt::AlignCenter, text);
    painter.restore();
}

void card_widget::draw_card_extra_lines(
    QPainter& painter, const QRectF& oriented_card_rect,
    qreal slot_rotation_deg, const QPointF& offset,
    const QStringList& extra_lines
) const {
    QFont extra_font = painter.font();
    extra_font.setBold(false);
    extra_font.setPointSizeF(std::clamp(
        compute_font_point_size(oriented_card_rect) * 0.75, 7.0, 14.0
    ));
    painter.setFont(extra_font);
    painter.setPen(QColor(30, 70, 40));

    painter.save();
    apply_card_transform(
        painter, oriented_card_rect, slot_rotation_deg, card_rotation_deg,
        offset
    );

    const QRectF extra_rect = oriented_card_rect.adjusted(
        8.0, oriented_card_rect.height() * 0.58, -8.0, -8.0
    );
    if (gameplay_owner) {
        // Target hints also appear over backs/hidden cards. Keep their contrast
        // independent of table color or the selected card-sheet artwork.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(250, 250, 250, 245));
        painter.drawRoundedRect(extra_rect, 4.0, 4.0);
        painter.setPen(QColor(30, 70, 40));
    }
    painter.drawText(
        extra_rect, Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap,
        extra_lines.join('\n')
    );
    painter.restore();
}
