#ifndef KCUCKOOUNTER_SETTINGS_THEME_SETTINGS_HPP
#define KCUCKOOUNTER_SETTINGS_THEME_SETTINGS_HPP
#include <shared_ui/qt/presentation.hpp>

// Host-owned table palette selection. Material definitions belong to
// Appearance.
class theme_settings {
public:
    static void set_base_color(const QColor& color);
    static QColor base_color();
    static QColor table_color(const QWidget* widget = nullptr);
    static QColor panel_color();
    static QColor slot_fill_color(const QWidget* widget = nullptr);
    static QColor slot_border_color();
    static QColor slot_border_selected_color();
    static bool felt_chrome(const QWidget* widget);
    static shared_ui::qt::material_tokens felt(const QWidget* widget);
    static void apply_chrome(
        QWidget& root, bool felt,
        shared_ui::qt::presentation_surface surface
        = shared_ui::qt::presentation_surface::window
    );
};
#endif
