#include "settings/theme_settings.hpp"

#include "arch/asset_locator.hpp"

#include <QFontDatabase>
#include <QPainter>
#include <QPixmap>
#include <QStringList>

#include <initializer_list>
#include <utility>

static QColor& theme_base_color() {
    static QColor base_color(10, 80, 40);
    return base_color;
}

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

QColor theme_settings::slot_border_color() { return { 0xD4, 0xAF, 0x37 }; }

QColor theme_settings::slot_border_selected_color() {
    return slot_border_color();
}

static bool& felt_chrome_flag() {
    static bool enabled = false;
    return enabled;
}

void theme_settings::set_felt_chrome(bool enabled) {
    felt_chrome_flag() = enabled;
}

bool theme_settings::felt_chrome() { return felt_chrome_flag(); }

namespace {

QColor mix_colors(const QColor& from, const QColor& to, qreal amount) {
    const auto channel = [amount](int a, int b) {
        return static_cast<int>(a + (b - a) * amount + 0.5);
    };
    return { channel(from.red(), to.red()), channel(from.green(), to.green()),
             channel(from.blue(), to.blue()) };
}

// The kit's faces ship as Latin subsets under assets/brand/fonts (OFL).
void register_kit_fonts() {
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    for (const char* name :
         { "Archivo-Regular", "Archivo-Medium", "Archivo-SemiBold",
           "Archivo-Bold", "JetBrainsMono-Regular", "JetBrainsMono-Medium",
           "JetBrainsMono-SemiBold", "JetBrainsMono-Bold" }) {
        const QString path = bundled_asset_path(
            QStringLiteral("brand/fonts/%1.ttf").arg(QLatin1String(name))
        );
        if (!path.isEmpty())
            QFontDatabase::addApplicationFont(path);
    }
}

QString first_family(const QStringList& wanted, const QString& fallback) {
    register_kit_fonts();
    const QStringList available = QFontDatabase::families();
    for (const QString& family : wanted)
        if (available.contains(family, Qt::CaseInsensitive))
            return family;
    return fallback;
}

QString rgba(const QColor& color, qreal alpha) {
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha, 0, 'f', 2);
}

} // namespace

felt_material theme_settings::felt() {
    // Kit materials: ink #0b0f0c, deep #04240f, inset #073a1d on the default
    // green; other palettes keep the same relationships around their felt.
    const QColor base = theme_base_color();
    felt_material m;
    m.ink = mix_colors(base, QColor(0x0b, 0x0f, 0x0c), 0.9);
    m.deep = mix_colors(base, QColor(0x02, 0x0a, 0x05), 0.62);
    m.inset = base.darker(118);
    m.line = mix_colors(m.ink, QColor(0xe9, 0xe5, 0xd8), 0.12);
    m.text = QColor(0xe9, 0xe5, 0xd8);
    m.muted = QColor(0x9a, 0xa7, 0x9d);
    m.dim = QColor(0x6f, 0x7d, 0x73);
    m.gold = slot_border_color();
    m.gold_light = QColor(0xf0, 0xdc, 0x9a);
    m.right = QColor(0x2f, 0x8f, 0x78);
    m.wrong = QColor(0xb4, 0x50, 0x3f);
    m.paper = QColor(0xf3, 0xef, 0xe4);
    m.paper_ink = QColor(0x17, 0x22, 0x1a);
    static const QString ui = first_family(
        { QStringLiteral("Archivo"), QStringLiteral("Inter"),
          QStringLiteral("Helvetica Neue"), QStringLiteral("Segoe UI"),
          QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans") },
        QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()
    );
    static const QString mono = first_family(
        { QStringLiteral("JetBrains Mono"), QStringLiteral("JetBrains Mono NL"),
          QStringLiteral("SF Mono"), QStringLiteral("Menlo"),
          QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
          QStringLiteral("DejaVu Sans Mono"),
          QStringLiteral("Noto Sans Mono") },
        QFontDatabase::systemFont(QFontDatabase::FixedFont).family()
    );
    m.ui_family = ui;
    m.mono_family = mono;
    return m;
}

QString theme_settings::felt_style(QString sheet) {
    const felt_material m = felt();
    // Longer tokens first so $gold_light$ is not consumed by $gold$.
    const std::initializer_list<std::pair<const char*, QString>> tokens {
        { "$gold_line_soft$", rgba(m.gold, 0.28) },
        { "$gold_line$", rgba(m.gold, 0.45) },
        { "$gold_soft$", rgba(m.gold, 0.16) },
        { "$gold_light$", m.gold_light.name() },
        { "$gold$", m.gold.name() },
        { "$ink$", m.ink.name() },
        { "$deep_glass$", rgba(m.deep, 0.94) },
        { "$deep$", m.deep.name() },
        { "$inset$", m.inset.name() },
        { "$line_strong$", mix_colors(m.line, m.text, 0.18).name() },
        { "$line$", m.line.name() },
        { "$text$", m.text.name() },
        { "$muted_dim$", mix_colors(m.muted, m.ink, 0.45).name() },
        { "$muted$", m.muted.name() },
        { "$dim$", m.dim.name() },
        { "$table$", theme_base_color().name() },
        { "$right$", m.right.name() },
        { "$wrong$", m.wrong.name() },
        { "$paper_ink$", m.paper_ink.name() },
        { "$paper$", m.paper.name() },
        { "$check$",
          bundled_asset_path(QStringLiteral("brand/kit_check.svg")) },
        { "$ui$", m.ui_family },
        { "$mono$", m.mono_family },
    };
    for (const auto& [token, value] : tokens)
        sheet.replace(QLatin1String(token), value);
    return sheet;
}

QString theme_settings::felt_window_style_sheet() {
    return felt_style(QStringLiteral(R"(
QMainWindow, QDialog { background: $ink$; color: $text$; }
QMainWindow > QWidget { background: $ink$; }
QWidget { font-family: "$ui$"; }
QMenuBar { background: $deep$; color: $text$; border: none; padding: 2px 8px; }
QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 4px; }
QMenuBar::item:selected { background: $deep$; color: $gold_light$; }
QMenu { background: $deep$; color: $text$; border: 1px solid $line$; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 14px; border-radius: 4px; }
QMenu::item:selected { background: $inset$; color: $gold_light$; }
QMenu::item:disabled { color: $muted_dim$; }
QMenu::separator { height: 1px; background: $line$; margin: 4px 8px; }
QToolBar { background: $deep$; border: none; border-bottom: 1px solid $gold_line_soft$; padding: 7px; spacing: 2px; }
QToolBar::separator { background: transparent; width: 0px; }
QToolBar::handle { image: none; width: 0px; height: 0px; }
QToolBar QToolButton { color: $text$; background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 6px 12px; font-size: 13px; }
QToolBar QToolButton:hover { background: $inset$; }
QToolBar QToolButton:pressed { background: $inset$; }
QToolBar QToolButton:focus { border: 1px dashed $gold$; }
QToolBar QToolButton:disabled { color: rgba(230, 226, 212, 0.35); }
QToolBar QToolButton::menu-indicator, QToolBar QToolButton::menu-button { image: none; width: 0px; border: none; }
QToolBar QToolButton#game_start_pause_button { background: $gold$; color: $deep$; border-color: $gold$; font-weight: 600; padding: 6px 14px; }
QToolBar QToolButton#game_start_pause_button:hover { background: $gold_light$; border-color: $gold_light$; }
QToolBar QToolButton#game_start_pause_button:focus { border: 1px dashed $deep$; }
QToolBar QToolButton#game_start_pause_button:disabled { background: transparent; color: rgba(230, 226, 212, 0.35); border-color: $line$; }
QToolBar QToolButton#kit_menu_button { color: rgba(230, 226, 212, 0.62); }
QLabel#kit_wordmark { color: $gold$; font-family: "$mono$"; font-size: 11px; letter-spacing: 2px; padding: 0px 12px 0px 0px; background: transparent; }
QLabel#kit_wordmark_icon { background: transparent; padding: 0px 8px 0px 5px; }
QStatusBar { background: $ink$; color: $text$; border-top: 1px solid $line$; }
QStatusBar QLabel { color: $text$; }
QToolTip { background: $deep$; color: $text$; border: 1px solid $gold_line$; padding: 4px 6px; }
QFrame#desktop_status_surface QLabel { color: $text$; }
QDialog QLabel { color: $text$; background: transparent; }
QDialog QGroupBox { color: $muted$; border: 1px solid $line$; border-radius: 8px; margin-top: 16px; padding: 12px 10px 10px 10px; }
QDialog QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0px 4px; color: $gold$; }
QDialog QCheckBox, QDialog QRadioButton { color: $text$; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; border: 1px solid $muted$; background: $deep$; }
QCheckBox::indicator { border-radius: 3px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: $gold$; border-color: $gold$; }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { border-color: $muted_dim$; background: transparent; }
QCheckBox:focus, QRadioButton:focus { color: $gold_light$; }
QDialog QPushButton { background: $deep$; color: $text$; border: 1px solid $line_strong$; border-radius: 6px; padding: 6px 14px; }
QDialog QPushButton:hover { border-color: $gold$; }
QDialog QPushButton:focus { border: 1px dashed $gold$; }
QDialog QPushButton:checked { background: $inset$; border-color: $gold$; color: $gold_light$; }
QDialog QPushButton:default { background: $gold$; color: $ink$; border-color: $gold$; font-weight: 600; }
QDialog QPushButton:disabled { background: transparent; color: $muted_dim$; border-color: $line$; }
QDialog QToolButton { background: transparent; color: $text$; border: 1px solid $line_strong$; border-radius: 6px; padding: 4px 8px; }
QDialog QToolButton:hover, QDialog QToolButton:focus { border-color: $gold$; }
QDialog QComboBox, QDialog QAbstractSpinBox, QDialog QLineEdit, QDialog QPlainTextEdit, QDialog QTextEdit { background: $deep$; color: $text$; border: 1px solid $line_strong$; border-radius: 5px; padding: 4px 8px; selection-background-color: $gold$; selection-color: $ink$; }
QDialog QComboBox:focus, QDialog QAbstractSpinBox:focus, QDialog QLineEdit:focus { border-color: $gold$; }
QDialog QComboBox:disabled, QDialog QAbstractSpinBox:disabled, QDialog QLineEdit:disabled { color: $muted_dim$; border-color: $line$; }
QComboBox QAbstractItemView { background: $deep$; color: $text$; border: 1px solid $line$; selection-background-color: $inset$; selection-color: $gold_light$; outline: none; }
QDialog QListView, QDialog QListWidget, QDialog QTableView, QDialog QTableWidget, QDialog QTreeView, QDialog QTextBrowser { background: $deep$; color: $text$; border: 1px solid $line$; border-radius: 6px; alternate-background-color: $ink$; gridline-color: $line$; selection-background-color: $inset$; selection-color: $gold_light$; outline: none; }
QDialog QListView::item, QDialog QListWidget::item { padding: 5px 8px; }
QDialog QListView::item:selected, QDialog QListWidget::item:selected { background: $inset$; color: $gold_light$; border-left: 2px solid $gold$; }
QHeaderView::section { background: $ink$; color: $muted$; border: none; border-bottom: 1px solid $line$; border-right: 1px solid $line$; padding: 5px 8px; font-family: "$mono$"; font-size: 11px; }
QTableCornerButton::section { background: $ink$; border: none; }
QDialog QTabWidget::pane { border: 1px solid $line$; border-radius: 8px; top: -1px; background: $ink$; }
QDialog QTabBar::tab { background: transparent; color: $muted$; padding: 7px 16px; margin-right: 2px; border: 1px solid transparent; border-top-left-radius: 6px; border-top-right-radius: 6px; }
QDialog QTabBar::tab:selected { color: $gold$; background: $ink$; border-color: $line$; border-bottom-color: $ink$; }
QDialog QTabBar::tab:hover { color: $gold_light$; }
QDialog QScrollArea { background: transparent; border: none; }
QDialog QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: $line_strong$; border-radius: 3px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: $gold_line$; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QDialog QSplitter::handle { background: $line$; }
QDialog QSlider::groove:horizontal { height: 4px; background: $line$; border-radius: 2px; }
QDialog QSlider::sub-page:horizontal { background: $gold$; border-radius: 2px; }
QDialog QSlider::handle:horizontal { background: $gold$; width: 12px; height: 12px; margin: -4px 0px; border-radius: 6px; }
QDialog QProgressBar { background: $deep$; border: 1px solid $line$; border-radius: 3px; }
QDialog QProgressBar::chunk { background: $gold$; }
QDialog QDialogButtonBox { button-layout: 0; }
QListView#strategy_browser_list { background: $deep$; border: none; border-right: 1px solid $line$; border-radius: 0px; padding: 4px 0px; }
QListView#strategy_browser_list::item { padding: 7px 14px; border: none; color: $text$; }
QListView#strategy_browser_list::item:selected { background: rgba(212, 175, 55, 0.12); color: $gold$; border-left: 3px solid $gold$; font-weight: 600; }
QTextBrowser#strategy_browser_details { background: #0e1611; border: none; border-radius: 0px; padding: 14px 18px; }
QLineEdit#strategy_search { background: $ink$; border: 1px solid $gold_line$; border-radius: 6px; padding: 6px 10px; }
QLabel[kit_caption="true"] { color: $dim$; font-family: "$mono$"; font-size: 10px; letter-spacing: 2px; }
QLabel[kit_note="true"] { color: $dim$; font-size: 11.5px; }
)"));
}

QString theme_settings::felt_hud_style_sheet() {
    return felt_style(QStringLiteral(R"(
QFrame#desktop_status_surface { background: $deep$; border: none; border-top: 1px solid $gold_line_soft$; border-radius: 0px; }
QFrame#desktop_status_surface QLabel { color: $text$; font-family: "$mono$"; font-size: 12px; background: transparent; }
QFrame#desktop_status_surface QLabel#pickup_interval_readout { color: $muted$; }
QFrame#desktop_status_surface QLabel#session_clock { color: $gold$; font-size: 17px; border-left: 1px solid rgba(212, 175, 55, 0.22); padding-left: 14px; }
QFrame#desktop_status_surface QSlider::groove:horizontal { height: 4px; background: $line$; border-radius: 2px; }
QFrame#desktop_status_surface QSlider::sub-page:horizontal { background: $gold$; border-radius: 2px; }
QFrame#desktop_status_surface QSlider::handle:horizontal { background: $gold$; width: 10px; height: 14px; margin: -5px 0px; border-radius: 3px; }
QFrame#desktop_status_surface QSlider::handle:horizontal:focus { background: $gold_light$; }
QFrame#desktop_status_surface QProgressBar { background: $ink$; border: 1px solid $line$; border-radius: 3px; max-height: 6px; }
QFrame#desktop_status_surface QProgressBar::chunk { background: $gold$; }
)"));
}

QString theme_settings::felt_overlay_style_sheet() {
    return felt_style(QStringLiteral(R"(
QWidget { font-family: "$ui$"; }
QLabel { color: $text$; background: transparent; }
QCheckBox { color: $text$; spacing: 7px; background: transparent; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid $muted$; border-radius: 3px; background: $deep$; }
QCheckBox::indicator:checked { background: $gold$; border-color: $gold$; }
QCheckBox::indicator:disabled { border-color: $muted_dim$; background: transparent; }
QComboBox, QSpinBox, QLineEdit { color: $text$; background: $ink$; border: 1px solid $line_strong$; border-radius: 5px; padding: 3px 6px; selection-background-color: $gold$; selection-color: $ink$; }
QComboBox:focus, QSpinBox:focus, QLineEdit:focus { border-color: $gold$; }
QComboBox QAbstractItemView { background: $deep$; color: $text$; selection-background-color: $inset$; selection-color: $gold_light$; }
QPushButton, QToolButton { background: $deep$; color: $text$; border: 1px solid $line_strong$; border-radius: 6px; padding: 4px 10px; }
QPushButton:hover, QToolButton:hover { border-color: $gold$; }
QPushButton:focus, QToolButton:focus { border: 1px dashed $gold$; }
QPushButton:checked, QToolButton:checked { background: $paper$; color: $paper_ink$; border-color: $paper$; }
QPushButton:disabled, QToolButton:disabled { color: $muted_dim$; border-color: $line$; background: transparent; }
QFrame#settings_bar_frame, QFrame#slot_settings_editor { background: $deep_glass$; border: 1px solid $gold_line$; border-radius: 10px; }
QFrame#swap_bar_frame { background: transparent; border: none; }
QFrame#quiz_bar_frame { background: transparent; border: none; }
QFrame { background: transparent; }
QLabel#quiz_weight_label { color: rgba(212, 175, 55, 0.65); font-family: "$mono$"; font-size: 10px; letter-spacing: 2px; }
QSpinBox#quiz_spin_box, QLineEdit#gameplay_quiz_input { background: transparent; color: $paper$; border: none; border-bottom: 2px solid $gold$; border-radius: 0px; font-family: "$mono$"; font-size: 54px; font-weight: 500; padding: 0px 0px 6px 0px; selection-background-color: $gold$; selection-color: $deep$; }
QSpinBox#quiz_spin_box { min-width: 120px; }
QSpinBox#quiz_spin_box:focus, QLineEdit#gameplay_quiz_input:focus { border-bottom-color: $gold_light$; }
QPushButton#quiz_answer_button, QPushButton#quiz_continue_button { background: $gold$; color: $deep$; border: 1px solid $gold$; border-radius: 6px; font-size: 14px; font-weight: 600; padding: 9px 20px; }
QPushButton#quiz_answer_button:hover, QPushButton#quiz_continue_button:hover { background: $gold_light$; }
QPushButton#quiz_answer_button:focus, QPushButton#quiz_continue_button:focus { border: 1px dashed $deep$; }
QPushButton#quiz_answer_button:disabled { background: transparent; color: $muted_dim$; border-color: $line$; }
QPushButton#quiz_skip_button { background: transparent; color: $text$; border: 1px solid rgba(243, 239, 228, 0.3); border-radius: 6px; font-size: 14px; padding: 9px 16px; }
QPushButton#quiz_skip_button:hover { border-color: $gold$; }
QLabel#quiz_feedback_label { color: $muted$; font-size: 12px; }
QLabel#quiz_feedback_stamp { color: rgba(212, 175, 55, 0.75); font-family: "$mono$"; font-size: 10px; letter-spacing: 2px; }
QLabel#quiz_feedback_band { background: rgba(180, 80, 63, 0.94); border-radius: 6px; padding: 7px 10px 6px 10px; min-width: 180px; }
)"));
}

QString theme_settings::felt_paper_style_sheet() {
    return felt_style(QStringLiteral(R"(
QDialog { background: $paper$; color: $paper_ink$; }
QWidget { color: $paper_ink$; font-family: "$ui$"; }
QLabel { color: $paper_ink$; background: transparent; font-size: 13.5px; }
QLabel#kit_card_heading { font-size: 18px; font-weight: 600; padding: 2px 0px 12px 0px; border-bottom: 1px solid #dcd7c8; margin-bottom: 4px; }
QCheckBox { color: $paper_ink$; spacing: 10px; font-size: 13.5px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1.5px solid #8f9a91; border-radius: 4px; background: #ffffff; }
QCheckBox::indicator:checked { background: $table$; border-color: $table$; image: url("$check$"); }
QCheckBox:focus { color: $table$; }
QAbstractSpinBox, QLineEdit, QComboBox { background: #ffffff; color: $paper_ink$; border: 1px solid #cfc9b8; border-radius: 6px; padding: 6px 10px; font-size: 13.5px; selection-background-color: $table$; selection-color: $paper$; }
QAbstractSpinBox:focus, QLineEdit:focus, QComboBox:focus { border-color: $table$; }
QComboBox QAbstractItemView { background: #ffffff; color: $paper_ink$; selection-background-color: #e4dfd0; selection-color: $paper_ink$; }
QFrame#kit_segmented { background: #e4dfd0; border: none; border-radius: 7px; }
QFrame#kit_segmented QPushButton { background: transparent; color: #5d675f; border: none; border-radius: 5px; padding: 7px 6px; font-size: 13px; }
QFrame#kit_segmented QPushButton:checked { background: #ffffff; color: $paper_ink$; font-weight: 600; }
QFrame#kit_segmented QPushButton:focus { border: 1px dashed $table$; }
QPushButton { background: transparent; color: $paper_ink$; border: 1px solid #cfc9b8; border-radius: 6px; padding: 8px 14px; font-size: 13.5px; }
QPushButton:hover { border-color: $table$; }
QPushButton:focus { border: 1px dashed $table$; }
QPushButton:default, QPushButton[kit_primary="true"] { background: $table$; color: $paper$; border: 1px solid $table$; font-weight: 600; padding: 10px 14px; }
QPushButton:disabled { color: #a8afa9; border-color: #e4dfd0; }
)"));
}

QIcon theme_settings::tinted_icon(const QIcon& icon, const QColor& color) {
    QIcon result;
    for (const int extent : { 16, 22, 24, 32, 48 }) {
        const QPixmap source = icon.pixmap(QSize(extent, extent));
        if (source.isNull())
            continue;
        QPixmap tinted(source.size());
        tinted.fill(Qt::transparent);
        QPainter painter(&tinted);
        painter.drawPixmap(0, 0, source);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(tinted.rect(), color);
        painter.end();
        result.addPixmap(tinted);
    }
    return result;
}
