#ifndef KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_SUPPORT_HPP
#define KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_SUPPORT_HPP

#include "image/raster_cache.hpp"
#include "settings/preferences.hpp"

#include <QIcon>
#include <QPixmap>

class QTableWidget;

namespace settings_template_support {

QColor theme_color_from_label(const QString& label);
int theme_index_from_color(const QColor& color);
QStringList theme_labels();
int orientation_index(card_orientation_mode orientation) noexcept;
card_orientation_mode orientation_from_index(int index) noexcept;
QIcon palette_swatch_icon(const QColor& color);
QIcon suit_icon(const QString& symbol, const QColor& color);
QColor suit_color_for_index(int suit_index);
QIcon suit_icon_for_index(int suit_index);
QSize preview_card_size();
/** One process-wide cache shared by settings-preview widgets. */
raster_cache& preview_cache();
qint64 next_preview_id();
QString preview_scope(int rank_index, int suit_index);
QString generation_scope(
    const QString& element_id, qint64 instance_id, qint64 generation_id
);

struct parsed_scope {
    QString element_id;
    qint64 instance_id;
    qint64 generation_id;
    bool valid;
};

parsed_scope parse_preview_scope(const QString& scope);
QString weight_text_for_value(int weight);
QString format_key_label(QString key);
QPixmap build_weighted_card_preview(
    const QImage& base_face, int rank_index, int suit_index,
    const QSize& card_size, const QVector<int>& weights
);
QTableWidget* build_readonly_table(int rows, int columns, QWidget* parent);
QString build_ieee_list(const QStringList& entries);
QString build_bullet_list(const QStringList& entries);

} // namespace settings_template_support

#endif // KCUCKOOUNTER_TABLE_SETTINGS_TEMPLATE_SUPPORT_HPP
