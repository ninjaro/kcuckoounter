#include "monitor/monitor_visual_widgets.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QRect chart_plot_rect(const QRect& bounds) {
    return bounds.adjusted(56, 34, -18, -34);
}

bool is_finite(double value) { return std::isfinite(value); }

double safe_span(double min_value, double max_value) {
    const double span = max_value - min_value;
    return span > 1e-9 ? span : 1.0;
}

} // namespace

monitor_line_chart_widget::monitor_line_chart_widget(QWidget* parent)
    : QWidget(parent)
    , title_text()
    , unit_text()
    , chart_series()
    , footer_text_lines() {
    setAutoFillBackground(true);
}

void monitor_line_chart_widget::set_title(const QString& title) {
    title_text = title;
    update();
}

void monitor_line_chart_widget::set_unit_label(const QString& unit_label) {
    unit_text = unit_label;
    update();
}

void monitor_line_chart_widget::set_series(const QVector<series>& series_list) {
    chart_series = series_list;
    update();
}

void monitor_line_chart_widget::set_footer_lines(
    const QStringList& footer_lines
) {
    footer_text_lines = footer_lines;
    update();
}

QSize monitor_line_chart_widget::minimumSizeHint() const {
    return QSize(360, 180);
}

void monitor_line_chart_widget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), palette().color(QPalette::Base));

    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        QRect(12, 8, width() - 24, 20), Qt::AlignLeft | Qt::AlignVCenter,
        title_text
    );

    const QRect plot = chart_plot_rect(rect());
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.drawRect(plot);

    double min_value = std::numeric_limits<double>::infinity();
    double max_value = -std::numeric_limits<double>::infinity();
    int max_count = 0;
    int visible_series_count = 0;
    for (const series& line : chart_series) {
        if (line.values.isEmpty()) {
            continue;
        }
        ++visible_series_count;
        max_count = std::max(max_count, static_cast<int>(line.values.size()));
        for (double value : line.values) {
            if (!is_finite(value)) {
                continue;
            }
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
        }
    }

    if (visible_series_count <= 0 || !is_finite(min_value)
        || !is_finite(max_value) || max_count <= 0) {
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(
            plot.adjusted(8, 8, -8, -8), Qt::AlignCenter,
            QStringLiteral("No chart data")
        );
        return;
    }

    const double value_span = safe_span(min_value, max_value);
    const int grid_lines = 4;
    for (int i = 0; i <= grid_lines; ++i) {
        const double t
            = static_cast<double>(i) / static_cast<double>(grid_lines);
        const int y = plot.bottom()
            - static_cast<int>(
                          std::lround(t * static_cast<double>(plot.height()))
            );
        painter.setPen(QPen(palette().color(QPalette::Midlight), 1));
        painter.drawLine(plot.left(), y, plot.right(), y);

        const double axis_value = min_value + (value_span * t);
        painter.setPen(palette().color(QPalette::Text));
        const QString axis_label = unit_text.isEmpty()
            ? QString::number(axis_value, 'f', 1)
            : QStringLiteral("%1 %2").arg(
                  QString::number(axis_value, 'f', 1), unit_text
              );
        painter.drawText(
            QRect(4, y - 10, 48, 20), Qt::AlignRight | Qt::AlignVCenter,
            axis_label
        );
    }

    for (int i = 0; i < chart_series.size(); ++i) {
        const series& line = chart_series.at(i);
        if (line.values.isEmpty()) {
            continue;
        }

        QPainterPath path;
        bool has_segment = false;
        for (int index = 0; index < line.values.size(); ++index) {
            const double value = line.values.at(index);
            if (!is_finite(value)) {
                has_segment = false;
                continue;
            }

            const double x_t = line.values.size() <= 1
                ? 0.0
                : static_cast<double>(index)
                    / static_cast<double>(line.values.size() - 1);
            const double y_t = (value - min_value) / value_span;
            const int x
                = plot.left()
                + static_cast<int>(
                      std::lround(x_t * static_cast<double>(plot.width()))
                );
            const int y
                = plot.bottom()
                - static_cast<int>(
                      std::lround(y_t * static_cast<double>(plot.height()))
                );

            if (!has_segment) {
                path.moveTo(static_cast<qreal>(x), static_cast<qreal>(y));
                has_segment = true;
            } else {
                path.lineTo(static_cast<qreal>(x), static_cast<qreal>(y));
            }
        }

        painter.setPen(QPen(line.color, 2));
        painter.drawPath(path);

        const int legend_y = 14 + i * 16;
        const QRect swatch(180, legend_y, 10, 10);
        painter.fillRect(swatch, line.color);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(
            QRect(194, legend_y - 3, width() - 200, 16),
            Qt::AlignLeft | Qt::AlignVCenter, line.label
        );
    }

    const int footer_line_count = static_cast<int>(footer_text_lines.size());
    int footer_y = height() - (footer_line_count * 14) - 4;
    for (const QString& footer_line : footer_text_lines) {
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(
            QRect(12, footer_y, width() - 24, 14),
            Qt::AlignLeft | Qt::AlignVCenter, footer_line
        );
        footer_y += 14;
    }
}

monitor_pie_chart_widget::monitor_pie_chart_widget(QWidget* parent)
    : QWidget(parent)
    , title_text()
    , chart_slices()
    , footer() {
    setAutoFillBackground(true);
}

void monitor_pie_chart_widget::set_title(const QString& title) {
    title_text = title;
    update();
}

void monitor_pie_chart_widget::set_slices(const QVector<slice>& slice_list) {
    chart_slices = slice_list;
    update();
}

void monitor_pie_chart_widget::set_footer_text(const QString& footer_text) {
    footer = footer_text;
    update();
}

QSize monitor_pie_chart_widget::minimumSizeHint() const {
    return QSize(360, 190);
}

void monitor_pie_chart_widget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), palette().color(QPalette::Base));

    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        QRect(12, 8, width() - 24, 20), Qt::AlignLeft | Qt::AlignVCenter,
        title_text
    );

    double total = 0.0;
    for (const slice& part : chart_slices) {
        if (part.value > 0.0 && is_finite(part.value)) {
            total += part.value;
        }
    }

    if (total <= 0.0) {
        painter.drawText(
            QRect(12, 32, width() - 24, height() - 64), Qt::AlignCenter,
            QStringLiteral("No composition data")
        );
        return;
    }

    const int pie_diameter = std::max(80, std::min(width() / 2, height() - 64));
    const QRect pie_rect(16, 30, pie_diameter, pie_diameter);

    int start_angle = 90 * 16;
    for (const slice& part : chart_slices) {
        if (part.value <= 0.0 || !is_finite(part.value)) {
            continue;
        }
        const double fraction = part.value / total;
        const int span_angle
            = static_cast<int>(std::lround(fraction * 360.0 * 16.0));
        painter.setBrush(part.color);
        painter.setPen(QPen(palette().color(QPalette::Base), 1));
        painter.drawPie(pie_rect, start_angle, -span_angle);
        start_angle -= span_angle;
    }

    int legend_y = 36;
    for (const slice& part : chart_slices) {
        if (part.value <= 0.0 || !is_finite(part.value)) {
            continue;
        }

        painter.fillRect(QRect(width() / 2 + 12, legend_y, 10, 10), part.color);
        painter.setPen(palette().color(QPalette::Text));
        const QString legend_text
            = QStringLiteral("%1: %2")
                  .arg(part.label)
                  .arg(QString::number(part.value, 'f', 2));
        painter.drawText(
            QRect(width() / 2 + 28, legend_y - 3, width() / 2 - 40, 16),
            Qt::AlignLeft | Qt::AlignVCenter, legend_text
        );
        legend_y += 16;
    }

    if (!footer.isEmpty()) {
        painter.drawText(
            QRect(12, height() - 20, width() - 24, 16),
            Qt::AlignLeft | Qt::AlignVCenter, footer
        );
    }
}

monitor_geometry_schematic_widget::monitor_geometry_schematic_widget(
    QWidget* parent
)
    : QWidget(parent)
    , has_snapshot(false)
    , current_snapshot() {
    setAutoFillBackground(true);
}

void monitor_geometry_schematic_widget::set_snapshot(
    const geometry_debug_snapshot& snapshot
) {
    has_snapshot = true;
    current_snapshot = snapshot;
    update();
}

void monitor_geometry_schematic_widget::clear_snapshot() {
    has_snapshot = false;
    current_snapshot = geometry_debug_snapshot();
    update();
}

QSize monitor_geometry_schematic_widget::minimumSizeHint() const {
    return QSize(360, 220);
}

void monitor_geometry_schematic_widget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), palette().color(QPalette::Base));

    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        QRect(12, 8, width() - 24, 20), Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("Geometry Schematic (lower-left anchored)")
    );

    if (!has_snapshot) {
        painter.drawText(
            QRect(12, 32, width() - 24, height() - 44), Qt::AlignCenter,
            QStringLiteral("No geometry snapshot")
        );
        return;
    }

    const QRect plot = QRect(44, 36, width() - 76, height() - 76);
    const QPoint origin(plot.left(), plot.bottom());
    painter.setPen(QPen(palette().color(QPalette::Text), 1));
    painter.drawLine(origin, QPoint(plot.right(), origin.y()));
    painter.drawLine(origin, QPoint(origin.x(), plot.top()));

    const int max_width = std::max(
        1,
        std::max(
            {
                current_snapshot.window_size.width(),
                current_snapshot.layout_size.width(),
                current_snapshot.display_card_size.width(),
                current_snapshot.cache_raster_size.width(),
                current_snapshot.preloaded_raster_size.width(),
            }
        )
    );
    const int max_height = std::max(
        1,
        std::max(
            {
                current_snapshot.window_size.height(),
                current_snapshot.layout_size.height(),
                current_snapshot.display_card_size.height(),
                current_snapshot.cache_raster_size.height(),
                current_snapshot.preloaded_raster_size.height(),
            }
        )
    );

    auto draw_rect = [&](const QSize& size, const QColor& color,
                         const QString& label, int label_y) {
        if (size.width() <= 0 || size.height() <= 0) {
            return;
        }

        const double width_ratio = static_cast<double>(size.width())
            / static_cast<double>(max_width);
        const double height_ratio = static_cast<double>(size.height())
            / static_cast<double>(max_height);
        const int draw_width = std::max(
            1,
            static_cast<int>(
                std::lround(width_ratio * static_cast<double>(plot.width() - 6))
            )
        );
        const int draw_height = std::max(
            1,
            static_cast<int>(std::lround(
                height_ratio * static_cast<double>(plot.height() - 6)
            ))
        );
        const QRect overlay(
            origin.x() + 1, origin.y() - draw_height, draw_width, draw_height
        );

        painter.setPen(QPen(color, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(overlay);

        painter.setPen(color);
        painter.drawText(
            QRect(width() - 180, label_y, 168, 14),
            Qt::AlignLeft | Qt::AlignVCenter,
            QStringLiteral("%1: %2x%3")
                .arg(label)
                .arg(size.width())
                .arg(size.height())
        );
    };

    draw_rect(current_snapshot.window_size, QColor(40, 120, 220), "W", 36);
    draw_rect(current_snapshot.layout_size, QColor(40, 170, 120), "L", 52);
    draw_rect(
        current_snapshot.display_card_size, QColor(220, 160, 40), "D", 68
    );
    draw_rect(current_snapshot.cache_raster_size, QColor(210, 80, 70), "C", 84);
    draw_rect(
        current_snapshot.preloaded_raster_size, QColor(160, 80, 180), "P", 100
    );
}

monitor_resize_history_widget::monitor_resize_history_widget(QWidget* parent)
    : QWidget(parent)
    , recent_entries() {
    setAutoFillBackground(true);
}

void monitor_resize_history_widget::set_entries(
    const QVector<resize_entry>& entries
) {
    recent_entries = entries;
    update();
}

QSize monitor_resize_history_widget::minimumSizeHint() const {
    return QSize(360, 200);
}

void monitor_resize_history_widget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), palette().color(QPalette::Base));

    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        QRect(12, 8, width() - 24, 20), Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("Resize History Timeline")
    );

    if (recent_entries.isEmpty()) {
        painter.drawText(
            QRect(12, 32, width() - 24, height() - 44), Qt::AlignCenter,
            QStringLiteral("No resize events")
        );
        return;
    }

    const int recent_entry_count = static_cast<int>(recent_entries.size());
    const int entry_start = std::max(0, recent_entry_count - 24);
    const QVector<resize_entry> entries
        = recent_entries.mid(entry_start, recent_entry_count - entry_start);
    const QRect plot(20, 42, width() - 40, height() - 72);
    const int base_y = plot.bottom() - 8;

    qint64 max_prewarm_ms = 0;
    for (const resize_entry& entry : entries) {
        max_prewarm_ms = std::max(
            max_prewarm_ms, std::max<qint64>(0, entry.prewarm_completion_ms)
        );
    }
    if (max_prewarm_ms <= 0) {
        max_prewarm_ms = 1;
    }

    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.drawLine(plot.left(), base_y, plot.right(), base_y);

    for (int i = 0; i < entries.size(); ++i) {
        const resize_entry& entry = entries.at(i);
        const double x_ratio = entries.size() <= 1
            ? 0.0
            : static_cast<double>(i) / static_cast<double>(entries.size() - 1);
        const int x
            = plot.left()
            + static_cast<int>(
                  std::lround(x_ratio * static_cast<double>(plot.width()))
            );
        const bool bucket_changed
            = entry.old_active_bucket_px != entry.new_active_bucket_px;
        const QColor marker_color
            = bucket_changed ? QColor(220, 120, 40) : QColor(60, 140, 220);

        painter.setPen(QPen(marker_color, 2));
        painter.drawLine(x, base_y, x, plot.top());
        painter.setBrush(marker_color);
        painter.drawEllipse(QPoint(x, base_y), 3, 3);

        const int bar_height = static_cast<int>(std::lround(
            (static_cast<double>(
                 std::max<qint64>(0, entry.prewarm_completion_ms)
             )
             / static_cast<double>(max_prewarm_ms))
            * static_cast<double>(plot.height() - 14)
        ));
        painter.fillRect(
            QRect(x - 2, base_y - bar_height, 4, bar_height),
            QColor(180, 70, 180, 160)
        );

        if (entries.size() <= 10) {
            const QString label = QStringLiteral("%1x%2")
                                      .arg(entry.new_window_size.width())
                                      .arg(entry.new_window_size.height());
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(
                QRect(x - 32, base_y + 4, 64, 12), Qt::AlignHCenter, label
            );
        }
    }

    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        QRect(12, height() - 24, width() - 24, 16),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("Vertical bar height = prewarm completion ms")
    );
}
