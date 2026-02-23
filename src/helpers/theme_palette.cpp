#include "helpers/theme_palette.hpp"
#include "helpers/str_label.hpp"

#include <utility>

namespace {

QVector<theme_palette_option> build_theme_options() {
    return {
        theme_palette_option(
            str_label("Red"), QColor(100, 20, 20), QColor(160, 70, 60),
            { QColor(160, 70, 60), QColor(100, 20, 20), QColor(70, 15, 15) }
        ),
        theme_palette_option(
            str_label("Green"), QColor(8, 70, 35), QColor(90, 140, 100),
            { QColor(90, 140, 100), QColor(8, 70, 35), QColor(6, 50, 25) }
        ),
        theme_palette_option(
            str_label("Blue"), QColor(20, 50, 115), QColor(95, 130, 175),
            { QColor(95, 130, 175), QColor(20, 50, 115), QColor(15, 35, 85) }
        )
    };
}

template <typename Predicate>
theme_palette_id find_palette_id(Predicate&& predicate) {
    const auto& palettes = theme_palette_registry::options();
    for (int i = 0; i < palettes.size(); ++i) {
        if (predicate(palettes.at(i))) {
            return static_cast<theme_palette_id>(i);
        }
    }
    return theme_palette_id::green;
}

} // namespace

theme_palette_option::theme_palette_option(
    QString label, QColor base_color, QColor accent_color,
    QVector<QColor> swatches
)
    : label_(std::move(label))
    , base_color_(std::move(base_color))
    , accent_color_(std::move(accent_color))
    , swatches_(std::move(swatches)) { }

const QString& theme_palette_option::label() const { return label_; }

const QColor& theme_palette_option::base_color() const { return base_color_; }

const QColor& theme_palette_option::accent_color() const {
    return accent_color_;
}

QColor theme_palette_option::input_color() const {
    return base_color_.darker(120);
}

QColor theme_palette_option::panel_color() const {
    QColor panel = base_color_;
    panel.setAlphaF(0.85f);
    return panel;
}

const QVector<QColor>& theme_palette_option::swatches() const {
    return swatches_;
}

const QVector<theme_palette_option>& theme_palette_registry::options() {
    static const QVector<theme_palette_option> options = build_theme_options();
    return options;
}

const theme_palette_option&
theme_palette_registry::option(theme_palette_id id) {
    return options().at(index(id));
}

theme_palette_id theme_palette_registry::id_from_color(const QColor& color) {
    return find_palette_id([&](const theme_palette_option& palette) {
        return palette.base_color() == color;
    });
}

theme_palette_id theme_palette_registry::id_from_label(const QString& label) {
    return find_palette_id([&](const theme_palette_option& palette) {
        return palette.label() == label;
    });
}

int theme_palette_registry::index(theme_palette_id id) {
    return static_cast<int>(id);
}

QStringList theme_palette_registry::labels() {
    QStringList labels;
    labels.reserve(options().size());
    for (const auto& palette : options()) {
        labels.append(palette.label());
    }
    return labels;
}
