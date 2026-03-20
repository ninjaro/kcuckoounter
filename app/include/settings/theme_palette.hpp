#ifndef KCUCKOOUNTER_SETTINGS_THEME_PALETTE_HPP
#define KCUCKOOUNTER_SETTINGS_THEME_PALETTE_HPP

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

enum class theme_palette_id { red = 0, green = 1, blue = 2 };

class theme_palette_option {
public:
    theme_palette_option(
        QString label, QColor base_color, QColor accent_color,
        QVector<QColor> swatches
    );

    const QString& label() const;
    const QColor& base_color() const;
    const QColor& accent_color() const;
    QColor input_color() const;
    QColor panel_color() const;
    const QVector<QColor>& swatches() const;

private:
    QString label_;
    QColor base_color_;
    QColor accent_color_;
    QVector<QColor> swatches_;
};

class theme_palette_registry {
public:
    static const QVector<theme_palette_option>& options();
    static const theme_palette_option& option(theme_palette_id id);
    static theme_palette_id id_from_color(const QColor& color);
    static theme_palette_id id_from_label(const QString& label);
    static int index(theme_palette_id id);
    static QStringList labels();
};

#endif // KCUCKOOUNTER_SETTINGS_THEME_PALETTE_HPP
