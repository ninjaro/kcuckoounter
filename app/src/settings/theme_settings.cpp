#include "settings/theme_settings.hpp"

static QColor& theme_base_color() {
    static QColor base_color(10, 80, 40);
    return base_color;
}

void theme_settings::set_base_color(const QColor& color) {
    theme_base_color() = color;
}

QColor theme_settings::base_color() { return theme_base_color(); }

QColor theme_settings::table_color(const QWidget* widget) {
    const QColor contextual = widget != nullptr
        ? shared_ui::qt::presentation_for(*widget).context.felt_surface
        : QColor();
    return contextual.isValid() ? contextual : base_color();
}

QColor theme_settings::panel_color() {
    QColor panel_color = theme_base_color();
    panel_color.setAlphaF(0.85f);
    return panel_color;
}

QColor theme_settings::slot_fill_color(const QWidget* widget) {
    return table_color(widget).darker(125);
}

QColor theme_settings::slot_border_color() {
    return shared_ui::qt::tokens_for(
               shared_ui::qt::material::felt,
               shared_ui::qt::platform_profile::desktop_qt, QPalette()
    )
        .accent;
}

QColor theme_settings::slot_border_selected_color() {
    return slot_border_color();
}

bool theme_settings::felt_chrome(const QWidget* widget) {
    return widget != nullptr
        && shared_ui::qt::presentation_for(*widget).value
        == shared_ui::qt::material::felt;
}

shared_ui::qt::material_tokens theme_settings::felt(const QWidget* widget) {
    return shared_ui::qt::material_for(*widget, shared_ui::qt::material::felt);
}

void theme_settings::apply_chrome(
    QWidget& root, const bool felt,
    const shared_ui::qt::presentation_surface surface
) {
    auto value = shared_ui::qt::presentation_for(root);
    value.value = felt ? shared_ui::qt::material::felt
                       : shared_ui::qt::material::system;
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    value.profile = shared_ui::qt::platform_profile::android;
#elif defined(KC_KDE)
    value.profile = shared_ui::qt::platform_profile::desktop_kde;
#endif
    value.context.felt_surface = base_color();
    shared_ui::qt::apply_presentation(root, value, surface);
}
