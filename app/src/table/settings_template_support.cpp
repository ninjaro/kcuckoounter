#include "table/settings_template_support.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "image/card_preview_carousel.hpp"
#include "settings/preferences.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/table.hpp"

#include <QAbstractItemView>
#include <QFont>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QTableWidget>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>

namespace settings_template_support {
QColor theme_color_from_label(const QString& label) {
    const auto palette_id = theme_palette_registry::id_from_label(label);
    return theme_palette_registry::option(palette_id).base_color();
}

int theme_index_from_color(const QColor& color) {
    return theme_palette_registry::index(
        theme_palette_registry::id_from_color(color)
    );
}

QStringList theme_labels() { return theme_palette_registry::labels(); }

int orientation_index(card_orientation_mode orientation) noexcept {
    switch (orientation) {
    case card_orientation_mode::automatic:
        return 0;
    case card_orientation_mode::vertical:
        return 1;
    case card_orientation_mode::horizontal:
        return 2;
    }
    return 0;
}

card_orientation_mode orientation_from_index(int index) noexcept {
    switch (index) {
    case 1:
        return card_orientation_mode::vertical;
    case 2:
        return card_orientation_mode::horizontal;
    default:
        return card_orientation_mode::automatic;
    }
}

QIcon palette_swatch_icon(const QColor& color) {
    constexpr int swatch_size = 14;
    QPixmap swatch(swatch_size, swatch_size);
    swatch.fill(color);
    QPainter painter(&swatch);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(30, 30, 30)));
    painter.drawRect(0, 0, swatch_size - 1, swatch_size - 1);
    painter.end();
    return { swatch };
}

QIcon suit_icon(const QString& symbol, const QColor& color) {
    constexpr int icon_size = 18;
    QPixmap icon(icon_size, icon_size);
    icon.fill(Qt::white);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(12);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(icon.rect(), Qt::AlignCenter, symbol);
    painter.setPen(QPen(QColor(30, 30, 30)));
    painter.drawRect(0, 0, icon_size - 1, icon_size - 1);
    painter.end();
    return { icon };
}

QColor suit_color_for_index(int suit_index) {
    return (suit_index == 1 || suit_index == 2) ? QColor(170, 0, 0)
                                                : QColor(20, 20, 20);
}

QIcon suit_icon_for_index(int suit_index) {
    const QStringList symbols
        = { str_label("♣"), str_label("♦"), str_label("♥"), str_label("♠") };
    const QColor color = suit_color_for_index(suit_index);
    const QString symbol = (suit_index >= 0 && suit_index < symbols.size())
        ? symbols.at(suit_index)
        : QString();
    return suit_icon(symbol, color);
}

QSize preview_card_size() {
    const auto [long_side, short_side] = card_sheet_ratio();
    const int target_long = 88;
    if (long_side <= 0 || short_side <= 0) {
        return { 63, 88 };
    }
    const double scale = static_cast<double>(target_long) / long_side;
    const int width
        = std::max(1, static_cast<int>(std::lround(short_side * scale)));
    return { width, target_long };
}

raster_cache& preview_cache() {
    static raster_cache service;
    service.set_namespace_entry_limit(
        raster_cache::cache_namespace::settings, 12
    );
    return service;
}

qint64 next_preview_id() {
    static qint64 next_id = 1;
    return next_id++;
}

QString preview_scope(int rank_index, int suit_index) {
    const QStringList& ids = card_element_ids();
    const int card_index = suit_index * 13 + rank_index;
    if (card_index < 0 || card_index >= ids.size()) {
        return {};
    }
    return ids.at(card_index);
}

QString generation_scope(
    const QString& element_id, qint64 instance_id, qint64 generation_id
) {
    if (element_id.isEmpty() || instance_id <= 0 || generation_id <= 0) {
        return {};
    }
    return QStringLiteral("subset:%1#w%2#g%3")
        .arg(element_id)
        .arg(instance_id)
        .arg(generation_id);
}

parsed_scope parse_preview_scope(const QString& scope) {
    const QString prefix = QStringLiteral("subset:");
    if (!scope.startsWith(prefix)) {
        return { {}, 0, 0, false };
    }

    const QString payload = scope.mid(prefix.size()).trimmed();
    const qsizetype generation_index
        = payload.lastIndexOf(QStringLiteral("#g"));
    if (generation_index <= 0) {
        return { {}, 0, 0, false };
    }
    const qsizetype instance_index
        = payload.lastIndexOf(QStringLiteral("#w"), generation_index - 1);
    if (instance_index <= 0 || instance_index >= generation_index) {
        return { {}, 0, 0, false };
    }

    const QString element_id = payload.left(instance_index).trimmed();
    bool instance_ok = false;
    const qint64 instance_id
        = payload
              .mid(instance_index + 2, generation_index - (instance_index + 2))
              .toLongLong(&instance_ok);
    bool generation_ok = false;
    const qint64 generation_id
        = payload.mid(generation_index + 2).toLongLong(&generation_ok);
    if (!instance_ok || !generation_ok || instance_id <= 0 || generation_id <= 0
        || element_id.isEmpty()) {
        return { {}, 0, 0, false };
    }

    return { element_id, instance_id, generation_id, true };
}

QString weight_text_for_value(int weight) {
    if (weight > 0) {
        return str_label("+%1").arg(weight);
    }
    return QString::number(weight);
}

QString format_key_label(QString key) {
    key.replace('_', ' ');
    if (!key.isEmpty()) {
        key[0] = key.at(0).toUpper();
    }
    return key;
}

QPixmap build_weighted_card_preview(
    const QImage& base_face, int rank_index, int suit_index,
    const QSize& card_size, const QVector<int>& weights
) {
    if (base_face.isNull() || !card_size.isValid()) {
        return {};
    }

    QImage image = base_face;
    if (image.size() != card_size) {
        image = image.scaled(
            card_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation
        );
    }

    QPainter painter(&image);

    const int weight = (rank_index >= 0 && rank_index < weights.size())
        ? weights[rank_index]
        : 0;
    const QString weight_text = weight_text_for_value(weight);

    QFont weight_font = painter.font();
    weight_font.setBold(true);
    weight_font.setPointSizeF(std::clamp(card_size.height() * 0.12, 8.0, 14.0));
    painter.setFont(weight_font);
    painter.setPen(suit_color_for_index(suit_index));

    painter.drawText(
        QRectF(
            card_size.width() * 0.52, card_size.height() * 0.05,
            card_size.width() * 0.4, card_size.height() * 0.2
        ),
        Qt::AlignRight | Qt::AlignTop | Qt::TextWordWrap, weight_text
    );
    painter.drawText(
        QRectF(
            card_size.width() * 0.05, card_size.height() * 0.75,
            card_size.width() * 0.4, card_size.height() * 0.2
        ),
        Qt::AlignLeft | Qt::AlignBottom | Qt::TextWordWrap, weight_text
    );
    painter.end();

    return QPixmap::fromImage(image);
}

QTableWidget* build_readonly_table(int rows, int columns, QWidget* parent) {
    auto table = new QTableWidget(rows, columns, parent);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setShowGrid(true);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    return table;
}

QString build_ieee_list(const QStringList& entries) {
    QStringList lines;
    lines.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        lines.append(str_label("[%1] %2").arg(i + 1).arg(entries.at(i)));
    }
    return lines.join(str_label("<br>"));
}

QString build_bullet_list(const QStringList& entries) {
    QStringList lines;
    lines.reserve(entries.size());
    for (const auto& entry : entries) {
        lines.append(str_label("- %1").arg(entry));
    }
    return lines.join(str_label("<br>"));
}

} // namespace settings_template_support
