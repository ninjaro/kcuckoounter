#ifndef KCUCKOOUNTER_SETTINGS_THEME_SETTINGS_HPP
#define KCUCKOOUNTER_SETTINGS_THEME_SETTINGS_HPP

#include <QColor>

class theme_settings {
public:
    static void set_base_color(const QColor& color);
    static QColor base_color();
    static QColor table_color();
    static QColor panel_color();
    static QColor slot_fill_color();
    static QColor slot_border_color();
    static QColor slot_border_selected_color();
};

#endif // KCUCKOOUNTER_SETTINGS_THEME_SETTINGS_HPP
