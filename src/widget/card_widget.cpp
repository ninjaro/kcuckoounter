#include "widget/card_widget.hpp"

#include "card_helpers/card_sheet.hpp"
#include "helpers/str_label.hpp"
#include "helpers/theme_settings.hpp"
#include <QColor>
#include <QFutureWatcher>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace {

int total_cards_for_quiz_type(int quiz_type_index) {
    if (quiz_type_index == 1) {
        return 54;
    }
    return 52;
}

qreal compute_font_point_size(const QRectF& card_rect) {
    qreal point_size = card_rect.height() * 0.10;
    return std::clamp(point_size, 8.0, 20.0);
}

QString weight_text_for_value(int weight) {
    if (weight >= 0) {
        return str_label("+%1").arg(weight);
    }
    return QString::number(weight);
}

int rank_index_from_card_index(int card_index) {
    if (card_index < 0 || card_index >= 52) {
        return -1;
    }
    return card_index % 13;
}

QColor blend_color(const QColor& from, const QColor& to, qreal strength) {
    const qreal clamped = std::clamp(strength, 0.0, 1.0);
    const auto lerp = [clamped](int a, int b) {
        return static_cast<int>(a + (b - a) * clamped);
    };

    QColor blended(
        lerp(from.red(), to.red()), lerp(from.green(), to.green()),
        lerp(from.blue(), to.blue()), lerp(from.alpha(), to.alpha())
    );
    return blended;
}

}

void card_rasterize_watcher::waitForFinished() {
    QFutureWatcher<QVector<QImage>>::waitForFinished();
    if (isFinished()) {
        Q_EMIT finished();
    }
}

card_widget::card_widget(BaseWidget* parent)
    : BaseWidget(parent)
    , running(false)
    , swap_selected_flag(false)
    , picker()
    , random_gen()
    , card_rotation_deg(0.0)
    , card_offset(0.0, 0.0)
    , slot_rotated(false)
    , show_card_indexing_flag(false)
    , show_strategy_name_flag(false)
    , training_mode_flag(false)
    , strategy_name()
    , strategy_weights()
    , cards_per_deck(0)
    , decks_count(0)
    , infinity_enabled(false)
    , selection_timer(std::make_unique<time_interface>())
    , selection_phase(0.0)
    , discard_history()
    , highlight_duration_ms(0)
    , highlight_remaining_ms(0)
    , highlight_active(false)
    , hide_cards_flag(false)
    , table_marking(str_label("assets/cuckoo.svg"))
    , card_sheet_source(card_sheet_source_path())
    , card_sheet_renderer()
    , card_faces()
    , card_face_size()
    , card_faces_rasterized()
    , card_face_raster_size()
    , picks_since_rasterize(0)
    , rasterize_watcher()
    , raster_task_size()
    , pending_raster_size()
    , rasterizing(false)
    , shared_card_faces_active(false)
    , shared_card_faces_mode(false) {
    selection_timer->set_interval(45);
    QObject::connect(
        selection_timer.get(), &time_interface::timeout, this,
        &card_widget::update_selection_pulse
    );
    QObject::connect(
        &rasterize_watcher, &QFutureWatcher<QVector<QImage>>::finished, this,
        &card_widget::on_rasterization_finished
    );

    card_sheet_renderer.load(card_sheet_source);
}

card_widget::~card_widget() = default;

void card_widget::set_swap_selected(bool selected) {
    if (swap_selected_flag == selected) {
        return;
    }

    swap_selected_flag = selected;
    if (swap_selected_flag) {
        if (!selection_timer->is_active()) {
            selection_timer->start();
        }
    } else {
        selection_timer->stop();
        selection_phase = 0.0;
    }
    update();
}

bool card_widget::swap_selected() const { return swap_selected_flag; }

void card_widget::start_quiz(
    int quiz_type_index, int decks_count, bool infinity_enabled
) {
    const int total_per_deck = total_cards_for_quiz_type(quiz_type_index);
    if (decks_count <= 0) {
        decks_count = 1;
    }

    picker.setup(total_per_deck, decks_count, infinity_enabled);
    cards_per_deck = total_per_deck;
    this->decks_count = decks_count;
    this->infinity_enabled = infinity_enabled;
    discard_history.clear();
    picks_since_rasterize = 0;
    update_card_jitter();
    update();
}

void card_widget::set_infinity(bool enabled) {
    picker.set_infinity(enabled);
    infinity_enabled = enabled;
    update();
}

void card_widget::set_running(bool new_running) {
    if (running == new_running) {
        return;
    }

    running = new_running;
    update();
}

void card_widget::set_slot_rotated(bool rotated) {
    if (slot_rotated == rotated) {
        return;
    }

    slot_rotated = rotated;
    update();
}

void card_widget::set_show_card_indexing(bool enabled) {
    if (show_card_indexing_flag == enabled) {
        return;
    }

    show_card_indexing_flag = enabled;
    update();
}

void card_widget::set_show_strategy_name(bool enabled) {
    if (show_strategy_name_flag == enabled) {
        return;
    }

    show_strategy_name_flag = enabled;
    update();
}

void card_widget::set_training_mode(bool enabled) {
    if (training_mode_flag == enabled) {
        return;
    }

    training_mode_flag = enabled;
    update();
}

void card_widget::set_strategy_name(const QString& name) {
    if (strategy_name == name) {
        return;
    }

    strategy_name = name;
    update();
}

void card_widget::set_strategy_weights(const QVector<int>& weights) {
    if (strategy_weights == weights) {
        return;
    }

    strategy_weights = weights;
    update();
}

void card_widget::set_table_marking_source(const QString& source) {
    table_marking.set_source(source);
    update_table_marking();
    update();
}

void card_widget::set_hide_cards(bool hide) {
    if (hide_cards_flag == hide) {
        return;
    }
    hide_cards_flag = hide;
    update();
}

void card_widget::advance_card() {
    if (!running) {
        return;
    }

    record_discard();
    picker.advance();
    ++picks_since_rasterize;
    update_card_jitter();
    update();
}

bool card_widget::has_cards() const { return picker.has_cards(); }

bool card_widget::has_current_card() const {
    return picker.current_card_index() >= 0;
}

bool card_widget::is_deck_exhausted() const { return picker.is_depleted(); }

void card_widget::mark_deck_exhausted() {
    picker.set_infinity(false);
    infinity_enabled = false;
    picker.mark_depleted();
    update();
}

int card_widget::current_position() const { return picker.current_position(); }

int card_widget::current_total_weight() const {
    return total_weight_for_picks();
}

void card_widget::clear_quiz() {
    picker.setup(0, 0, false);
    cards_per_deck = 0;
    decks_count = 0;
    infinity_enabled = false;
    discard_history.clear();
    picks_since_rasterize = 0;
    running = false;
    swap_selected_flag = false;
    highlight_duration_ms = 0;
    highlight_remaining_ms = 0;
    highlight_active = false;
    if (selection_timer) {
        selection_timer->stop();
    }
    selection_phase = 0.0;
    update_card_jitter();
    update();
}

void card_widget::trigger_highlight(int duration_ms) {
    if (duration_ms < 1) {
        duration_ms = 1;
    }
    highlight_duration_ms = duration_ms;
    highlight_remaining_ms = duration_ms;
    highlight_active = true;
    update();
}

void card_widget::tick_highlight(int delta_ms) {
    if (!highlight_active) {
        return;
    }
    if (highlight_duration_ms <= 0) {
        highlight_active = false;
        update();
        return;
    }
    highlight_remaining_ms -= delta_ms;
    if (highlight_remaining_ms <= 0) {
        highlight_active = false;
        highlight_remaining_ms = 0;
    }
    update();
}

void card_widget::prepare_card_faces() {
    const QSize target_size = card_face_target_size();
    if (target_size.isEmpty()) {
        return;
    }
    update_card_faces(target_size);
}

void card_widget::set_shared_card_faces(
    const QVector<QImage>& face_images, const QSize& raster_size
) {
    if (face_images.isEmpty() || raster_size.isEmpty()) {
        return;
    }

    apply_rasterized_images(face_images, raster_size);
    shared_card_faces_active = true;

    if (rasterizing) {
        pending_raster_size = QSize();
        set_rasterizing(false);
    }

    if (!card_face_size.isEmpty()) {
        update_card_faces(card_face_size);
    }
    update();
}

void card_widget::clear_shared_card_faces() {
    shared_card_faces_active = false;
}

bool card_widget::has_shared_card_faces() const {
    return shared_card_faces_active && !card_faces_rasterized.isEmpty();
}

void card_widget::set_shared_card_faces_mode(bool enabled) {
    if (shared_card_faces_mode == enabled) {
        return;
    }

    shared_card_faces_mode = enabled;
    if (shared_card_faces_mode && rasterizing) {
        pending_raster_size = QSize();
        set_rasterizing(false);
    }
    if (!card_face_size.isEmpty()) {
        update_card_faces(card_face_size);
    }
}

int card_widget::card_face_target_short_px() const {
    const QSize target_size = card_face_target_size();
    if (target_size.isEmpty()) {
        return 0;
    }

    return std::min(target_size.width(), target_size.height());
}

void card_widget::paintEvent(QPaintEvent* event) {
    BaseWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF slot_rect = rect().adjusted(3.0, 3.0, -3.0, -3.0);
    const qreal min_dim = std::min(slot_rect.width(), slot_rect.height());
    const qreal frame_margin = std::clamp(min_dim * 0.05, 4.0, 10.0);
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
    const QRectF slot_frame_rect
        = base_slot_frame_rect.translated(selection_offset);

    const bool has_deck = picker.has_cards();
    const int card_index = picker.current_card_index();
    const bool has_current_card = card_index >= 0;
    const bool show_back = has_deck && (!running || !has_current_card);
    const qreal inset = std::clamp(min_dim * 0.08, 4.0, 12.0);
    const QRectF card_rect
        = slot_frame_rect.adjusted(inset, inset, -inset, -inset);
    const bool slot_is_horizontal = !slot_rotated;
    const qreal slot_rotation_deg = slot_is_horizontal ? 90.0 : 0.0;
    const QSizeF oriented_card_size = slot_is_horizontal
        ? QSizeF(card_rect.height(), card_rect.width())
        : card_rect.size();
    const QRectF oriented_card_rect(
        card_rect.center().x() - oriented_card_size.width() / 2.0,
        card_rect.center().y() - oriented_card_size.height() / 2.0,
        oriented_card_size.width(), oriented_card_size.height()
    );

    const QColor gold_marking(str_label("#D4AF37"));
    const QColor slot_fill_color = theme_settings::slot_fill_color();
    const QColor slot_border_color = swap_selected_flag
        ? theme_settings::slot_border_selected_color()
        : theme_settings::slot_border_color();
    painter.setPen(QPen(slot_border_color, 6.6));
    painter.setBrush(QBrush(slot_fill_color));
    painter.drawRoundedRect(slot_frame_rect, 10.0, 10.0);

    const qreal strength = highlight_strength();

    auto draw_marking = [&]() {
        if (table_marking.is_ready()) {
            const QPixmap& marking = table_marking.pixmap();
            const QSizeF marking_size = table_marking.display_size();
            const QPointF marking_top_left(
                slot_frame_rect.center().x() - marking_size.width() / 2.0,
                slot_frame_rect.center().y() - marking_size.height() / 2.0
            );
            const QRectF marking_rect(marking_top_left, marking_size);
            painter.drawPixmap(marking_rect, marking, marking.rect());
        } else {
            const QString marking_text = str_label("kcuckoounter");
            QColor marking_color = gold_marking;
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
                -slot_frame_rect.width() * 0.08,
                -slot_frame_rect.height() * 0.08
            );
            painter.drawText(marking_rect, Qt::AlignCenter, marking_text);
            painter.restore();
        }
    };

    auto draw_index = [&]() {
        if (!show_card_indexing_flag) {
            return;
        }
        const int position = picker.current_position();
        QString index_text;
        if (position >= 0) {
            const int current_value = position + 1;
            if (infinity_enabled) {
                index_text = QString::number(current_value);
            } else {
                const int total = std::max(1, cards_per_deck * decks_count);
                index_text = str_label("%1/%2").arg(current_value).arg(total);
            }
        }

        if (index_text.isEmpty()) {
            return;
        }
        QFont index_font = painter.font();
        index_font.setBold(true);
        index_font.setPointSizeF(
            std::clamp(
                compute_font_point_size(oriented_card_rect) * 0.6, 6.0, 12.0
            )
        );
        painter.setFont(index_font);
        painter.setPen(QColor(40, 80, 50));

        painter.save();
        const QPointF index_center = oriented_card_rect.center() + card_offset;
        painter.translate(index_center);
        painter.rotate(card_rotation_deg + slot_rotation_deg);
        painter.translate(-oriented_card_rect.center());

        const QRectF index_rect = oriented_card_rect.adjusted(
            8.0, 8.0, -8.0, -oriented_card_rect.height() * 0.7
        );
        painter.drawText(index_rect, Qt::AlignRight | Qt::AlignTop, index_text);
        painter.restore();
    };

    if (!has_deck) {
        draw_marking();
        return;
    }

    if (hide_cards_flag) {
        draw_marking();
        draw_index();
        return;
    }

    const QString marking_text = str_label("kcuckoounter");
    QColor marking_color = gold_marking;
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
    painter.drawText(marking_rect, Qt::AlignCenter, marking_text);
    painter.restore();

    auto draw_card = [&](qreal rotation_deg, const QPointF& offset,
                         const QColor& fill, const QColor& border) {
        painter.save();
        const QPointF transform_center = oriented_card_rect.center() + offset;
        painter.translate(transform_center);
        painter.rotate(rotation_deg + slot_rotation_deg);
        painter.translate(-oriented_card_rect.center());

        painter.setPen(QPen(border, 1.6));
        painter.setBrush(QBrush(fill));
        painter.drawRoundedRect(oriented_card_rect, 9.0, 9.0);
        painter.restore();
    };

    const QColor discard_fill_color(248, 248, 248, 235);
    const QColor discard_border_color(220, 220, 220, 210);
    for (const discard_card& discard : discard_history) {
        draw_card(
            discard.rotation_deg, discard.offset, discard_fill_color,
            discard_border_color
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
        draw_card(
            card_rotation_deg, card_offset, base_card_fill, base_card_border
        );

        const QSize target_size
            = oriented_card_rect.size().toSize().expandedTo(QSize(1, 1));
        update_card_faces(target_size);
        const bool can_draw_back = back_index >= 0
            && back_index < card_faces.size()
            && !card_faces[back_index].isNull();

        if (can_draw_back) {
            painter.save();
            const QPointF transform_center
                = oriented_card_rect.center() + card_offset;
            painter.translate(transform_center);
            painter.rotate(card_rotation_deg + slot_rotation_deg);
            painter.translate(-oriented_card_rect.center());
            painter.drawPixmap(
                oriented_card_rect.toRect(), card_faces[back_index]
            );
            painter.restore();
        } else {
            QFont font = painter.font();
            font.setBold(true);
            font.setPointSizeF(compute_font_point_size(oriented_card_rect));
            painter.setFont(font);
            painter.setPen(QColor(20, 60, 35));

            painter.save();
            const QPointF transform_center
                = oriented_card_rect.center() + card_offset;
            painter.translate(transform_center);
            painter.rotate(card_rotation_deg + slot_rotation_deg);
            painter.translate(-oriented_card_rect.center());
            painter.drawText(
                oriented_card_rect, Qt::AlignCenter, str_label("Back")
            );
            painter.restore();
        }
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

    draw_card(
        card_rotation_deg, card_offset, card_fill_color, card_border_color
    );

    const QSize target_size
        = oriented_card_rect.size().toSize().expandedTo(QSize(1, 1));
    update_card_faces(target_size);
    const bool can_draw_face = mapped_card_index >= 0
        && mapped_card_index < card_faces.size()
        && !card_faces[mapped_card_index].isNull();

    if (can_draw_face) {
        painter.save();
        const QPointF transform_center
            = oriented_card_rect.center() + card_offset;
        painter.translate(transform_center);
        painter.rotate(card_rotation_deg + slot_rotation_deg);
        painter.translate(-oriented_card_rect.center());
        painter.drawPixmap(
            oriented_card_rect.toRect(), card_faces[mapped_card_index]
        );
        painter.restore();
    } else if (!text.isEmpty()) {
        QFont font = painter.font();
        font.setBold(true);
        font.setPointSizeF(compute_font_point_size(oriented_card_rect));
        painter.setFont(font);
        painter.setPen(QColor(20, 60, 35));

        painter.save();
        const QPointF transform_center
            = oriented_card_rect.center() + card_offset;
        painter.translate(transform_center);
        painter.rotate(card_rotation_deg + slot_rotation_deg);
        painter.translate(-oriented_card_rect.center());

        const qreal bottom_margin = oriented_card_rect.height() * 0.45;
        const QRectF text_rect
            = oriented_card_rect.adjusted(8.0, 8.0, -8.0, -bottom_margin);

        painter.drawText(
            text_rect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, text
        );
        painter.restore();
    }

    QStringList extra_lines;
    if (show_strategy_name_flag && !strategy_name.isEmpty()) {
        extra_lines.append(strategy_name);
    }
    if (training_mode_flag) {
        if (picker.current_card_index() >= 0) {
            const int total_weight = total_weight_for_picks();
            extra_lines.append(weight_text_for_value(total_weight));
        }
    }

    if (!extra_lines.isEmpty()) {
        QFont extra_font = painter.font();
        extra_font.setBold(false);
        extra_font.setPointSizeF(
            std::clamp(
                compute_font_point_size(oriented_card_rect) * 0.75, 7.0, 14.0
            )
        );
        painter.setFont(extra_font);
        painter.setPen(QColor(30, 70, 40));

        painter.save();
        const QPointF extra_center = oriented_card_rect.center() + card_offset;
        painter.translate(extra_center);
        painter.rotate(card_rotation_deg + slot_rotation_deg);
        painter.translate(-oriented_card_rect.center());

        const QRectF extra_rect = oriented_card_rect.adjusted(
            8.0, oriented_card_rect.height() * 0.58, -8.0, -8.0
        );
        painter.drawText(
            extra_rect, Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap,
            extra_lines.join('\n')
        );
        painter.restore();
    }

    draw_index();
}

void card_widget::resizeEvent(QResizeEvent* event) {
    BaseWidget::resizeEvent(event);
    update_card_jitter();
    update_table_marking();
}

void card_widget::update_card_jitter() {
    const QRectF slot_rect = rect().adjusted(3.0, 3.0, -3.0, -3.0);
    const qreal min_dim = std::min(slot_rect.width(), slot_rect.height());
    const qreal frame_margin = std::clamp(min_dim * 0.05, 4.0, 10.0);
    const QRectF slot_frame_rect = slot_rect.adjusted(
        frame_margin, frame_margin, -frame_margin, -frame_margin
    );
    const qreal frame_min_dim
        = std::min(slot_frame_rect.width(), slot_frame_rect.height());
    const qreal inset = std::clamp(frame_min_dim * 0.08, 4.0, 12.0);
    const qreal max_offset = inset * 0.6;

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
    const QRectF slot_rect = rect().adjusted(3.0, 3.0, -3.0, -3.0);
    const qreal min_dim = std::min(slot_rect.width(), slot_rect.height());
    const qreal frame_margin = std::clamp(min_dim * 0.05, 4.0, 10.0);
    const QRectF slot_frame_rect = slot_rect.adjusted(
        frame_margin, frame_margin, -frame_margin, -frame_margin
    );
    const qreal target_dim
        = std::min(slot_frame_rect.width(), slot_frame_rect.height()) * 0.5;
    const int size = static_cast<int>(std::max(1.0, target_dim));
    table_marking.set_target_size(QSize(size, size));
}

QSize card_widget::card_face_target_size() const {
    const QRectF slot_rect = rect().adjusted(3.0, 3.0, -3.0, -3.0);
    const qreal min_dim = std::min(slot_rect.width(), slot_rect.height());
    if (min_dim <= 0.0) {
        return QSize();
    }
    const qreal frame_margin = std::clamp(min_dim * 0.05, 4.0, 10.0);
    const QRectF slot_frame_rect = slot_rect.adjusted(
        frame_margin, frame_margin, -frame_margin, -frame_margin
    );
    const qreal inset = std::clamp(min_dim * 0.08, 4.0, 12.0);
    const QRectF card_rect
        = slot_frame_rect.adjusted(inset, inset, -inset, -inset);
    if (card_rect.isEmpty()) {
        return QSize();
    }
    const bool slot_is_horizontal = !slot_rotated;
    const QSizeF oriented_card_size = slot_is_horizontal
        ? QSizeF(card_rect.height(), card_rect.width())
        : card_rect.size();
    return oriented_card_size.toSize().expandedTo(QSize(1, 1));
}

QSize card_widget::raster_cache_size(const QSize& target_size) const {
    if (target_size.isEmpty()) {
        return QSize();
    }
    const qreal base_scale = 1.75;
    const qreal min_side = std::min(target_size.width(), target_size.height());
    qreal scale = base_scale;
    if (min_side > 0.0) {
        scale = std::max(scale, 63.0 / min_side);
    }
    const int width
        = std::max(1, static_cast<int>(std::ceil(target_size.width() * scale)));
    const int height = std::max(
        1, static_cast<int>(std::ceil(target_size.height() * scale))
    );
    return QSize(width, height);
}

void card_widget::update_card_faces(const QSize& target_size) {
    if (target_size.isEmpty()) {
        card_faces.clear();
        card_face_size = QSize();
        card_faces_rasterized.clear();
        card_face_raster_size = QSize();
        picks_since_rasterize = 0;
        pending_raster_size = QSize();
        return;
    }

    if (!card_sheet_renderer.isValid()) {
        card_sheet_renderer.load(card_sheet_source);
    }

    if (!card_sheet_renderer.isValid()) {
        card_faces.clear();
        card_face_size = QSize();
        card_faces_rasterized.clear();
        card_face_raster_size = QSize();
        picks_since_rasterize = 0;
        pending_raster_size = QSize();
        return;
    }

    const bool size_changed = card_face_size != target_size;
    const QSize raster_target_size = raster_cache_size(target_size);
    const bool raster_size_changed
        = card_face_raster_size != raster_target_size;
    const bool raster_cache_ready
        = !card_faces_rasterized.isEmpty() && !card_face_raster_size.isEmpty();
    const bool allow_local_rasterization
        = !shared_card_faces_mode && !shared_card_faces_active;
    const bool should_rasterize = !raster_cache_ready
        || (allow_local_rasterization && raster_size_changed
            && picks_since_rasterize >= 3);

    if (should_rasterize && allow_local_rasterization) {
        if (!rasterizing) {
            start_rasterization(raster_target_size);
        } else if (raster_task_size != raster_target_size) {
            pending_raster_size = raster_target_size;
        }
    }

    if (!size_changed && !should_rasterize) {
        return;
    }

    if (!card_faces_rasterized.isEmpty()) {
        card_faces.clear();
        card_faces.reserve(card_faces_rasterized.size());
        for (const QPixmap& pixmap : card_faces_rasterized) {
            if (pixmap.isNull()) {
                card_faces.push_back(QPixmap());
                continue;
            }
            card_faces.push_back(pixmap.scaled(
                target_size, Qt::IgnoreAspectRatio, Qt::FastTransformation
            ));
        }
    }
    card_face_size = target_size;
}

void card_widget::start_rasterization(const QSize& target_size) {
    if (target_size.isEmpty()) {
        return;
    }

    raster_task_size = target_size;
    pending_raster_size = QSize();
    set_rasterizing(true);

    const QString source = card_sheet_source;
    QStringList element_ids = card_element_ids();
    const QString back_id = card_back_element_id();
    if (!back_id.isEmpty()) {
        element_ids.append(back_id);
    }
    const QSize raster_size = target_size;

    rasterize_watcher.setFuture(
        QtConcurrent::run([source, element_ids, raster_size]() {
            QVector<QImage> images;
            images.reserve(element_ids.size());

            QSvgRenderer renderer(source);
            if (!renderer.isValid()) {
                for (int i = 0; i < element_ids.size(); ++i) {
                    images.push_back(QImage());
                }
                return images;
            }

            for (const QString& element_id : element_ids) {
                if (element_id.isEmpty()
                    || !renderer.elementExists(element_id)) {
                    images.push_back(QImage());
                    continue;
                }

                QImage card_image(
                    raster_size, QImage::Format_ARGB32_Premultiplied
                );
                card_image.fill(Qt::transparent);
                QPainter card_painter(&card_image);
                renderer.render(
                    &card_painter, element_id,
                    QRectF(QPointF(0.0, 0.0), QSizeF(raster_size))
                );
                card_painter.end();
                images.push_back(card_image);
            }

            return images;
        })
    );
}

void card_widget::apply_rasterized_images(
    const QVector<QImage>& images, const QSize& target_size
) {
    card_faces_rasterized.clear();
    card_faces_rasterized.reserve(images.size());
    for (const QImage& image : images) {
        if (image.isNull()) {
            card_faces_rasterized.push_back(QPixmap());
            continue;
        }
        card_faces_rasterized.push_back(QPixmap::fromImage(image));
    }
    card_face_raster_size = target_size;
    if (!card_face_size.isEmpty()) {
        card_faces.clear();
        card_faces.reserve(card_faces_rasterized.size());
        for (const QPixmap& pixmap : card_faces_rasterized) {
            if (pixmap.isNull()) {
                card_faces.push_back(QPixmap());
                continue;
            }
            card_faces.push_back(pixmap.scaled(
                card_face_size, Qt::IgnoreAspectRatio, Qt::FastTransformation
            ));
        }
    }
    picks_since_rasterize = 0;
}

void card_widget::set_rasterizing(bool active) {
    if (rasterizing == active) {
        return;
    }

    rasterizing = active;
    emit rasterization_busy_changed(active);
}

void card_widget::on_rasterization_finished() {
    const QVector<QImage> images = rasterize_watcher.result();
    apply_rasterized_images(images, raster_task_size);

    if (!pending_raster_size.isEmpty()
        && pending_raster_size != raster_task_size) {
        const QSize next_size = pending_raster_size;
        start_rasterization(next_size);
        update();
        return;
    }

    set_rasterizing(false);
    update();
}

void card_widget::record_discard() {
    if (!picker.has_cards()) {
        return;
    }

    const int card_index = picker.current_card_index();
    if (card_index < 0) {
        return;
    }

    discard_history.push_back({ card_rotation_deg, card_offset });
    while (discard_history.size() > 5) {
        discard_history.pop_front();
    }
}

int card_widget::total_weight_for_picks() const {
    if (strategy_weights.isEmpty()) {
        return 0;
    }

    const int current_position = picker.current_position();
    if (current_position < 0) {
        return 0;
    }

    int total_weight = 0;
    for (int position = 0; position <= current_position; ++position) {
        const int card_index = picker.card_index_at(position);
        const int rank_index = rank_index_from_card_index(card_index);
        if (rank_index >= 0 && rank_index < strategy_weights.size()) {
            total_weight += strategy_weights.at(rank_index);
        }
    }
    return total_weight;
}

qreal card_widget::highlight_strength() const {
    if (!highlight_active || highlight_duration_ms <= 0) {
        return 0.0;
    }
    const qreal remaining_ratio
        = highlight_remaining_ms / static_cast<qreal>(highlight_duration_ms);
    return std::clamp(remaining_ratio, 0.0, 1.0);
}

void card_widget::update_selection_pulse() {
    if (!swap_selected_flag) {
        return;
    }
    selection_phase += 0.35;
    if (selection_phase > 6.283) {
        selection_phase -= 6.283;
    }
    update();
}
