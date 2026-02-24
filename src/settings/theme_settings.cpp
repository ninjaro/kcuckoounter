#include "settings/theme_settings.hpp"

namespace {
QColor& theme_base_color() {
    static QColor base_color(10, 80, 40);
    return base_color;
}
} // namespace

void theme_settings::set_base_color(const QColor& color) {
    theme_base_color() = color;
}

QColor theme_settings::base_color() { return theme_base_color(); }

QColor theme_settings::table_color() { return theme_base_color(); }

QColor theme_settings::panel_color() {
    QColor panel_color = theme_base_color();
    panel_color.setAlphaF(0.85f);
    return panel_color;
}

QColor theme_settings::slot_fill_color() {
    return theme_base_color().darker(125);
}

QColor theme_settings::slot_border_color() { return QColor(0xD4, 0xAF, 0x37); }

QColor theme_settings::slot_border_selected_color() {
    return slot_border_color();
}
