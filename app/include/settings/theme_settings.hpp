#ifndef KCUCKOOUNTER_SETTINGS_THEME_SETTINGS_HPP
#define KCUCKOOUNTER_SETTINGS_THEME_SETTINGS_HPP

#include <QColor>
#include <QIcon>
#include <QString>

// Desktop kit "felt" material: the green-and-gold chrome of the component
// kit, derived from the current table palette. Presentation only.
struct felt_material {
    QColor ink;
    QColor deep;
    QColor inset;
    QColor line;
    QColor text;
    QColor muted;
    // Small spaced instrument labels (SCORE, PICKUP, YOUR COUNT).
    QColor dim;
    QColor gold;
    QColor gold_light;
    QColor right;
    QColor wrong;
    QColor paper;
    QColor paper_ink;
    QString ui_family;
    QString mono_family;
};

class theme_settings {
public:
    static void set_base_color(const QColor& color);
    static QColor base_color();
    static QColor table_color();
    static QColor panel_color();
    static QColor slot_fill_color();
    static QColor slot_border_color();
    static QColor slot_border_selected_color();

    // Window chrome selection shared by the shell and newly created slots.
    static void set_felt_chrome(bool enabled);
    static bool felt_chrome();
    static felt_material felt();
    // Replaces $token$ placeholders (ink, deep, gold, ui, mono, ...).
    static QString felt_style(QString sheet);
    static QString felt_window_style_sheet();
    static QString felt_hud_style_sheet();
    static QString felt_overlay_style_sheet();
    // Kit "paper" cards (D-NEW-1, D-SET-1): warm paper on the felt chrome.
    static QString felt_paper_style_sheet();
    // Single-colour copy of a native icon for legibility on the dark felt.
    static QIcon tinted_icon(const QIcon& icon, const QColor& color);
};

#endif // KCUCKOOUNTER_SETTINGS_THEME_SETTINGS_HPP
