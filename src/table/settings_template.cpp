#include "table/settings_template.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "image/card_preview_carousel.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/table.hpp"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QDateTime>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStyle>
#include <QSvgRenderer>
#include <QTableWidget>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
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

QIcon palette_swatch_icon(const QColor& color) {
    constexpr int swatch_size = 14;
    QPixmap swatch(swatch_size, swatch_size);
    swatch.fill(color);
    QPainter painter(&swatch);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(30, 30, 30)));
    painter.drawRect(0, 0, swatch_size - 1, swatch_size - 1);
    painter.end();
    return QIcon(swatch);
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
    return QIcon(icon);
}

QIcon suit_icon_for_index(int suit_index) {
    const QStringList symbols
        = { str_label("♣"), str_label("♦"), str_label("♥"), str_label("♠") };
    const QColor color = (suit_index == 1 || suit_index == 2)
        ? QColor(170, 0, 0)
        : QColor(20, 20, 20);
    const QString symbol = (suit_index >= 0 && suit_index < symbols.size())
        ? symbols.at(suit_index)
        : QString();
    return suit_icon(symbol, color);
}

QSize preview_card_size() {
    const auto [long_side, short_side] = card_sheet_ratio();
    const int target_long = 88;
    if (long_side <= 0 || short_side <= 0) {
        return QSize(63, 88);
    }
    const double scale = static_cast<double>(target_long) / long_side;
    const int width
        = std::max(1, static_cast<int>(std::lround(short_side * scale)));
    return QSize(width, target_long);
}

raster_cache& settings_theme_preview_cache_service() {
    static raster_cache service;
    static const bool configured = []() {
        service.set_namespace_entry_limit(
            raster_cache::cache_namespace::settings, 12
        );
        return true;
    }();
    Q_UNUSED(configured);
    return service;
}

qint64 next_theme_preview_instance_id() {
    static qint64 next_id = 1;
    return next_id++;
}

QString theme_preview_render_scope(int rank_index, int suit_index) {
    const QStringList& ids = card_element_ids();
    const int card_index = suit_index * 13 + rank_index;
    if (card_index < 0 || card_index >= ids.size()) {
        return QString();
    }
    return ids.at(card_index);
}

QString theme_preview_generation_render_scope(
    const QString& element_id, qint64 instance_id, qint64 generation_id
) {
    if (element_id.isEmpty() || instance_id <= 0 || generation_id <= 0) {
        return QString();
    }
    return QStringLiteral("subset:%1#w%2#g%3")
        .arg(element_id)
        .arg(instance_id)
        .arg(generation_id);
}

struct theme_preview_scope_parse {
    QString element_id;
    qint64 instance_id;
    qint64 generation_id;
    bool valid;
};

theme_preview_scope_parse
parse_theme_preview_render_scope(const QString& scope) {
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
    const bool use_red_label = suit_index == 0 || suit_index == 3;
    painter.setPen(use_red_label ? QColor(170, 0, 0) : QColor(20, 20, 20));

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

QString themed_cards_asset_path(int theme_index) {
    return str_label("assets/cards_%1.svg").arg(theme_index);
}
} // namespace

settings_shared_state::settings_shared_state(QObject* parent)
    : QObject(parent)
    , default_suit_value(0)
    , table_color_index_value(
          theme_index_from_color(theme_settings::base_color())
      ) { }

void settings_shared_state::set_default_suit(int index) {
    if (index == default_suit_value) {
        return;
    }
    default_suit_value = index;
    emit default_suit_changed(default_suit_value);
}

int settings_shared_state::default_suit() const { return default_suit_value; }

void settings_shared_state::set_table_color_index(int index) {
    if (index == table_color_index_value) {
        return;
    }
    table_color_index_value = index;
    emit table_color_index_changed(table_color_index_value);
}

int settings_shared_state::table_color_index() const {
    return table_color_index_value;
}

settings_template_widget::settings_template_widget(
    settings_tab_kind tab_kind, BaseWidget* parent,
    const QString& selected_strategy, table* table_widget,
    settings_shared_state* shared_state
)
    : BaseWidget(parent)
    , tab_kind(tab_kind)
    , table_widget(table_widget)
    , shared_state(
          shared_state != nullptr ? shared_state
                                  : new settings_shared_state(this)
      )
    , strategies()
    , strategy_list_widget(nullptr)
    , strategy_title_label(nullptr)
    , strategy_description_label(nullptr)
    , notes_title_label(nullptr)
    , notes_label(nullptr)
    , references_title_label(nullptr)
    , references_label(nullptr)
    , weights_carousel(nullptr)
    , general_table(nullptr)
    , metrics_table(nullptr)
    , suit_combo_box(nullptr)
    , theme_combo_box(nullptr)
    , orientation_combo_box(nullptr)
    , theme_palette_preview(nullptr)
    , theme_button_group(nullptr)
    , theme_carousel(nullptr)
    , active_theme_preview_suit_index(0)
    , active_weights_preview_suit_index(0)
    , theme_preview_instance_id(0)
    , active_theme_preview_source_id()
    , active_theme_preview_bucket_px(0)
    , active_theme_preview_generation_id(0)
    , active_theme_preview_requested_element_ids()
    , warming_theme_preview_source_id()
    , warming_theme_preview_bucket_px(0)
    , warming_theme_preview_generation_id(0)
    , warming_theme_preview_requested_element_ids()
    , next_theme_preview_generation_id(1)
    , theme_preview_render_watcher(this)
    , active_theme_preview_render_key(std::nullopt)
    , pending_theme_preview_render_queue()
    , pending_theme_preview_render_set()
    , theme_preview_render_scheduled(false)
    , theme_preview_refresh_scheduled(false)
    , theme_preview_needs_refresh(false)
    , weights_preview_needs_refresh(false)
    , displayed_theme_preview_entries()
    , displayed_weights_preview_entries() {
    theme_preview_instance_id = next_theme_preview_instance_id();
    QObject::connect(
        &settings_theme_preview_cache_service(), &raster_cache::result_updated,
        this, &settings_template_widget::on_theme_preview_cache_result_updated
    );
    QObject::connect(
        &theme_preview_render_watcher, &QFutureWatcher<QImage>::finished, this,
        &settings_template_widget::on_theme_preview_render_finished
    );
    setup_ui(selected_strategy);
}

settings_template_widget::~settings_template_widget() {
    clear_displayed_theme_preview_entries(
        raster_cache::debug_consumer_scope::settings_theme_carousel,
        displayed_theme_preview_entries
    );
    clear_displayed_theme_preview_entries(
        raster_cache::debug_consumer_scope::settings_strategy_preview,
        displayed_weights_preview_entries
    );
    if (theme_preview_render_watcher.isRunning()) {
        theme_preview_render_watcher.waitForFinished();
    }
    retire_theme_preview_generation(
        active_theme_preview_source_id, active_theme_preview_bucket_px,
        active_theme_preview_generation_id
    );
    retire_theme_preview_generation(
        warming_theme_preview_source_id, warming_theme_preview_bucket_px,
        warming_theme_preview_generation_id
    );
}

void settings_template_widget::setup_ui(const QString& selected_strategy) {
    if (tab_kind == settings_tab_kind::appearance) {
        setup_appearance_ui();
        return;
    }
    setup_strategy_ui(selected_strategy);
}

void settings_template_widget::setup_strategy_ui(
    const QString& selected_strategy
) {
    auto main_layout = new QHBoxLayout(this);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(8);

    auto dock_widget = new BaseWidget(this);
    dock_widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    dock_widget->setMinimumWidth(220);

    auto dock_layout = new BaseVBoxLayout(dock_widget);
    dock_layout->setContentsMargins(0, 0, 0, 0);
    dock_layout->setSpacing(4);

    auto dock_label
        = new QLabel(str_label("Available strategies"), dock_widget);
    dock_layout->addWidget(dock_label);

    strategy_list_widget = new QListWidget(dock_widget);
    strategy_list_widget->setSelectionMode(QAbstractItemView::SingleSelection);
    strategies = load_strategies();
    for (const strategy_data& strategy : strategies) {
        strategy_list_widget->addItem(strategy.name);
    }
    dock_layout->addWidget(strategy_list_widget, 1);

    auto suits_widget = new BaseWidget(dock_widget);
    auto suits_layout = new QFormLayout(suits_widget);
    suits_layout->setContentsMargins(4, 4, 4, 4);
    suits_layout->setSpacing(4);

    suit_combo_box = new BaseComboBox(suits_widget);
    suit_combo_box->addItems(
        QStringList() << str_label("Clubs") << str_label("Diamonds")
                      << str_label("Hearts") << str_label("Spades")
    );
    for (int i = 0; i < suit_combo_box->count(); ++i) {
        suit_combo_box->setItemIcon(i, suit_icon_for_index(i));
    }
    suits_layout->addRow(str_label("Default suit"), suit_combo_box);
    dock_layout->addWidget(suits_widget);

    auto detail_container = new BaseWidget(this);
    auto detail_layout = new BaseVBoxLayout(detail_container);
    detail_layout->setContentsMargins(0, 0, 0, 0);
    detail_layout->setSpacing(8);

    strategy_title_label = new QLabel(detail_container);
    QFont title_font = strategy_title_label->font();
    title_font.setBold(true);
    title_font.setPointSizeF(title_font.pointSizeF() + 8.0);
    strategy_title_label->setFont(title_font);
    detail_layout->addWidget(strategy_title_label);

    weights_carousel = new card_preview_carousel(detail_container);
    weights_carousel->set_visible_range(3, 5);
    weights_carousel->set_minimum_card_width(88);
    weights_carousel->set_prefetch_adjacent_cards(false);
    const QSize card_size = preview_card_size();
    weights_carousel->set_card_size(card_size);
    detail_layout->addWidget(weights_carousel);

    auto scroll_area = new QScrollArea(detail_container);
    scroll_area->setWidgetResizable(true);
    scroll_area->setFrameShape(QFrame::NoFrame);

    auto scroll_content = new BaseWidget(scroll_area);
    auto scroll_layout = new QHBoxLayout(scroll_content);
    scroll_layout->setContentsMargins(0, 0, 0, 0);
    scroll_layout->setSpacing(12);

    auto left_column = new BaseWidget(scroll_content);
    auto left_layout = new BaseVBoxLayout(left_column);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(8);

    strategy_description_label = new QLabel(left_column);
    strategy_description_label->setWordWrap(true);
    left_layout->addWidget(strategy_description_label);

    notes_title_label
        = new QLabel(str_label("Notes / Unique fields"), left_column);
    QFont section_font = notes_title_label->font();
    section_font.setBold(true);
    notes_title_label->setFont(section_font);
    left_layout->addWidget(notes_title_label);

    notes_label = new QLabel(left_column);
    notes_label->setWordWrap(true);
    notes_label->setTextFormat(Qt::RichText);
    left_layout->addWidget(notes_label);

    references_title_label = new QLabel(str_label("References"), left_column);
    references_title_label->setFont(section_font);
    left_layout->addWidget(references_title_label);

    references_label = new QLabel(left_column);
    references_label->setWordWrap(true);
    references_label->setTextFormat(Qt::RichText);
    references_label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    references_label->setOpenExternalLinks(true);
    left_layout->addWidget(references_label);
    left_layout->addStretch();

    auto right_column = new BaseWidget(scroll_content);
    auto right_layout = new BaseVBoxLayout(right_column);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(8);

    general_table = build_readonly_table(6, 2, right_column);
    general_table->setHorizontalHeaderLabels(
        QStringList() << str_label("General") << str_label("Value")
    );
    general_table->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents
    );
    general_table->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch
    );
    right_layout->addWidget(general_table);

    metrics_table = build_readonly_table(4, 2, right_column);
    metrics_table->setHorizontalHeaderLabels(
        QStringList() << str_label("Metrics") << str_label("Value")
    );
    metrics_table->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents
    );
    metrics_table->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch
    );
    right_layout->addWidget(metrics_table);
    right_layout->addStretch();

    scroll_layout->addWidget(left_column, 2);
    scroll_layout->addWidget(right_column, 1);
    scroll_area->setWidget(scroll_content);
    detail_layout->addWidget(scroll_area, 1);

    main_layout->addWidget(dock_widget);
    main_layout->addWidget(detail_container, 1);

    QObject::connect(
        strategy_list_widget, &QListWidget::currentRowChanged, this,
        &settings_template_widget::update_strategy_details
    );
    QObject::connect(
        suit_combo_box, &BaseComboBox::currentIndexChanged, this,
        &settings_template_widget::update_suit_selection
    );
    QObject::connect(
        shared_state, &settings_shared_state::default_suit_changed, this,
        &settings_template_widget::update_suit_selection
    );
    QObject::connect(
        shared_state, &settings_shared_state::default_suit_changed, this,
        &settings_template_widget::update_weights_carousel
    );

    update_suit_selection(shared_state->default_suit());

    int selected_index = 0;
    if (!selected_strategy.isEmpty()) {
        for (int i = 0; i < strategies.size(); ++i) {
            if (strategies[i].name == selected_strategy) {
                selected_index = i;
                break;
            }
        }
    }
    if (strategy_list_widget->count() > 0) {
        strategy_list_widget->setCurrentRow(selected_index);
        update_strategy_details(selected_index);
    }
}

void settings_template_widget::setup_appearance_ui() {
    auto main_layout = new BaseVBoxLayout(this);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(8);

    auto theme_widget = new BaseWidget(this);
    auto theme_layout = new QFormLayout(theme_widget);
    theme_layout->setContentsMargins(0, 0, 0, 0);
    theme_layout->setSpacing(6);

    theme_combo_box = new BaseComboBox(theme_widget);
    theme_combo_box->addItems(theme_labels());
    const auto& options = theme_palette_registry::options();
    for (int i = 0; i < options.size(); ++i) {
        theme_combo_box->setItemIcon(
            i, palette_swatch_icon(options.at(i).base_color())
        );
    }

    suit_combo_box = new BaseComboBox(theme_widget);
    suit_combo_box->addItems(
        QStringList() << str_label("Clubs ♣") << str_label("Diamonds ♦")
                      << str_label("Hearts ♥") << str_label("Spades ♠")
    );
    for (int i = 0; i < suit_combo_box->count(); ++i) {
        suit_combo_box->setItemIcon(i, suit_icon_for_index(i));
    }

    orientation_combo_box = new BaseComboBox(theme_widget);
    orientation_combo_box->addItems(
        QStringList() << str_label("Automatic") << str_label("Vertical")
                      << str_label("Horizontal") << str_label("Absolute")
    );

    theme_layout->addRow(str_label("Table color"), theme_combo_box);
    theme_palette_preview = new BaseWidget(theme_widget);
    auto palette_layout = new QHBoxLayout(theme_palette_preview);
    palette_layout->setContentsMargins(0, 0, 0, 0);
    palette_layout->setSpacing(4);
    theme_layout->addRow(str_label("Palette"), theme_palette_preview);
    theme_layout->addRow(str_label("Default suit"), suit_combo_box);
    theme_layout->addRow(str_label("Orientation"), orientation_combo_box);
    main_layout->addWidget(theme_widget);

    auto theme_section = new BaseWidget(this);
    auto theme_section_layout = new BaseVBoxLayout(theme_section);
    theme_section_layout->setContentsMargins(0, 0, 0, 0);
    theme_section_layout->setSpacing(6);

    auto theme_label = new QLabel(str_label("Card themes"), theme_section);
    theme_section_layout->addWidget(theme_label);

    auto theme_options_widget = new BaseWidget(theme_section);
    auto theme_options_layout = new BaseVBoxLayout(theme_options_widget);
    theme_options_layout->setContentsMargins(0, 0, 0, 0);
    theme_options_layout->setSpacing(4);

    theme_button_group = new QButtonGroup(theme_options_widget);
    theme_button_group->setExclusive(true);

    auto base_theme_button = new QRadioButton(
        str_label("Base card theme (cards_0.svg)"), theme_options_widget
    );
    base_theme_button->setProperty("theme_source", themed_cards_asset_path(0));
    theme_button_group->addButton(base_theme_button);
    theme_options_layout->addWidget(base_theme_button);

    auto alt_theme_button = new QRadioButton(
        str_label("Alternative card theme 1 (cards_1.svg)"),
        theme_options_widget
    );
    alt_theme_button->setProperty("theme_source", themed_cards_asset_path(1));
    theme_button_group->addButton(alt_theme_button);
    theme_options_layout->addWidget(alt_theme_button);

    auto alt_theme_2_button = new QRadioButton(
        str_label("Alternative card theme 2 (cards_2.svg)"),
        theme_options_widget
    );
    alt_theme_2_button->setProperty("theme_source", themed_cards_asset_path(2));
    theme_button_group->addButton(alt_theme_2_button);
    theme_options_layout->addWidget(alt_theme_2_button);

    const QString active_theme_source = card_sheet_source_path();
    bool matched_runtime_source = false;
    for (QAbstractButton* button : theme_button_group->buttons()) {
        if (button == nullptr
            || button->property("theme_source").toString()
                != active_theme_source) {
            continue;
        }

        button->setChecked(true);
        matched_runtime_source = true;
        break;
    }
    if (!matched_runtime_source) {
        base_theme_button->setChecked(true);
    }

    theme_section_layout->addWidget(theme_options_widget);

    theme_carousel = new card_preview_carousel(theme_section);
    theme_carousel->set_visible_range(3, 5);
    theme_carousel->set_minimum_card_width(88);
    theme_carousel->set_prefetch_adjacent_cards(false);
    const QSize card_size = preview_card_size();
    theme_carousel->set_card_size(card_size);
    update_theme_carousel(shared_state->default_suit());
    theme_section_layout->addWidget(theme_carousel);

    main_layout->addWidget(theme_section);

    main_layout->addStretch(1);

    QObject::connect(
        shared_state, &settings_shared_state::table_color_index_changed, this,
        [this](int index) {
            if (theme_combo_box == nullptr
                || theme_combo_box->currentIndex() == index) {
                return;
            }
            theme_combo_box->setCurrentIndex(index);
            update_theme_palette_preview(index);
        }
    );
    QObject::connect(
        suit_combo_box, &BaseComboBox::currentIndexChanged, this,
        &settings_template_widget::update_suit_selection
    );
    QObject::connect(
        theme_combo_box, &BaseComboBox::currentIndexChanged, this,
        &settings_template_widget::update_theme_palette_preview
    );
    QObject::connect(
        shared_state, &settings_shared_state::default_suit_changed, this,
        &settings_template_widget::update_suit_selection
    );
    QObject::connect(
        shared_state, &settings_shared_state::default_suit_changed, this,
        &settings_template_widget::update_theme_carousel
    );
    QObject::connect(
        theme_button_group, &QButtonGroup::buttonClicked, this,
        [this](QAbstractButton*) {
            prune_pending_theme_preview_queue();
            clear_displayed_theme_preview_entries(
                raster_cache::debug_consumer_scope::settings_theme_carousel,
                displayed_theme_preview_entries
            );
            clear_displayed_theme_preview_entries(
                raster_cache::debug_consumer_scope::settings_strategy_preview,
                displayed_weights_preview_entries
            );
            theme_preview_needs_refresh = true;
            weights_preview_needs_refresh = true;
            flush_coalesced_preview_refresh();
        }
    );

    theme_combo_box->setCurrentIndex(shared_state->table_color_index());
    update_suit_selection(shared_state->default_suit());
    update_theme_palette_preview(theme_combo_box->currentIndex());
}

void settings_template_widget::apply_theme_settings() {
    if (theme_combo_box == nullptr || shared_state == nullptr) {
        return;
    }

    const QColor base_color
        = theme_color_from_label(theme_combo_box->currentText());
    const QString selected_theme_source = selected_theme_source_id();
    theme_settings::set_base_color(base_color);
    set_card_sheet_source_path(selected_theme_source);
    shared_state->set_table_color_index(theme_combo_box->currentIndex());
    if (table_widget != nullptr) {
        table_widget->apply_theme();
    }
}

void settings_template_widget::reset_theme_selection() {
    if (theme_combo_box == nullptr || shared_state == nullptr) {
        return;
    }

    theme_combo_box->setCurrentIndex(shared_state->table_color_index());
    if (theme_button_group == nullptr) {
        return;
    }

    const QString runtime_source = card_sheet_source_path();
    QAbstractButton* button_to_check = nullptr;
    const QList<QAbstractButton*> buttons = theme_button_group->buttons();
    for (QAbstractButton* button : buttons) {
        if (button == nullptr
            || button->property("theme_source").toString() != runtime_source) {
            continue;
        }
        button_to_check = button;
        break;
    }

    if (button_to_check == nullptr && !buttons.isEmpty()) {
        button_to_check = buttons.first();
    }
    if (button_to_check != nullptr && !button_to_check->isChecked()) {
        button_to_check->setChecked(true);
    }

    prune_pending_theme_preview_queue();
    clear_displayed_theme_preview_entries(
        raster_cache::debug_consumer_scope::settings_theme_carousel,
        displayed_theme_preview_entries
    );
    clear_displayed_theme_preview_entries(
        raster_cache::debug_consumer_scope::settings_strategy_preview,
        displayed_weights_preview_entries
    );
    theme_preview_needs_refresh = true;
    weights_preview_needs_refresh = true;
    flush_coalesced_preview_refresh();
}

void settings_template_widget::update_strategy_details(int index) {
    if (index < 0 || index >= strategies.size()) {
        return;
    }
    const strategy_data& strategy = strategies[index];
    if (strategy_title_label != nullptr) {
        strategy_title_label->setText(strategy.name);
    }
    if (strategy_description_label != nullptr) {
        strategy_description_label->setText(strategy.description);
    }
    update_weights_carousel(shared_state->default_suit());

    QStringList note_entries;
    for (auto it = strategy.unique_fields.constBegin();
         it != strategy.unique_fields.constEnd(); ++it) {
        QString entry_label = format_key_label(it.key());
        QString entry = it.value();
        if (!entry_label.isEmpty()) {
            entry = str_label("%1: %2").arg(entry_label, it.value());
        }
        note_entries.append(entry);
    }

    if (notes_title_label != nullptr && notes_label != nullptr) {
        const bool has_notes = !note_entries.isEmpty();
        notes_title_label->setVisible(has_notes);
        notes_label->setVisible(has_notes);
        notes_label->setText(build_bullet_list(note_entries));
    }

    if (references_title_label != nullptr && references_label != nullptr) {
        QStringList reference_entries;
        for (const auto& ref : strategy.references) {
            QString entry = ref.citation;
            if (!ref.url.isEmpty()) {
                entry += str_label(" <a href=\"%1\">%1</a>").arg(ref.url);
            }
            if (!ref.accessed.isEmpty()) {
                entry += str_label(" (accessed %1)").arg(ref.accessed);
            }
            reference_entries.append(entry);
        }
        const bool has_references = !reference_entries.isEmpty();
        references_title_label->setVisible(has_references);
        references_label->setVisible(has_references);
        references_label->setText(build_ieee_list(reference_entries));
    }

    if (general_table != nullptr) {
        const QString min_decks_value = strategy.min_decks > 0
            ? QString::number(strategy.min_decks)
            : str_label("-");
        const QStringList label_keys
            = { str_label("date"),    str_label("author"),
                str_label("games"),   str_label("min_decks"),
                str_label("balance"), str_label("ace_neutral") };
        const QStringList values
            = { strategy.date,
                strategy.authors.join(", "),
                strategy.games.join(", "),
                min_decks_value,
                strategy.balance ? str_label("true") : str_label("false"),
                strategy.ace_neutral ? str_label("true") : str_label("false") };
        for (int row = 0; row < label_keys.size(); ++row) {
            general_table->setItem(
                row, 0,
                new QTableWidgetItem(format_key_label(label_keys.at(row)))
            );
            general_table->setItem(
                row, 1, new QTableWidgetItem(values.at(row))
            );
        }
    }

    if (metrics_table != nullptr) {
        const QStringList metric_labels
            = { str_label("betting_correlation"),
                str_label("playing_efficiency"),
                str_label("insurance_correlation"), str_label("ease_of_use") };
        for (int row = 0; row < metric_labels.size(); ++row) {
            const QString label = metric_labels.at(row);
            const QString value = strategy.metrics.contains(label)
                ? QString::number(strategy.metrics.value(label))
                : str_label("-");
            metrics_table->setItem(
                row, 0, new QTableWidgetItem(format_key_label(label))
            );
            metrics_table->setItem(row, 1, new QTableWidgetItem(value));
        }
    }
}

void settings_template_widget::update_theme_palette_preview(int index) {
    if (theme_palette_preview == nullptr) {
        return;
    }
    auto palette_layout
        = qobject_cast<QHBoxLayout*>(theme_palette_preview->layout());
    if (palette_layout == nullptr) {
        return;
    }
    while (palette_layout->count() > 0) {
        QLayoutItem* item = palette_layout->takeAt(0);
        if (item == nullptr) {
            continue;
        }
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    const auto& options = theme_palette_registry::options();
    if (index < 0 || index >= options.size()) {
        return;
    }
    for (const QColor& color : options.at(index).swatches()) {
        auto swatch = new QLabel(theme_palette_preview);
        swatch->setPixmap(palette_swatch_icon(color).pixmap(14, 14));
        swatch->setFixedSize(16, 16);
        palette_layout->addWidget(swatch);
    }
    palette_layout->addStretch();
}

void settings_template_widget::update_theme_carousel(int suit_index) {
    if (theme_carousel == nullptr) {
        return;
    }
    active_theme_preview_suit_index = suit_index;
    active_theme_preview_requested_element_ids.clear();
    warming_theme_preview_requested_element_ids.clear();
    prune_pending_theme_preview_queue();
    clear_displayed_theme_preview_entries(
        raster_cache::debug_consumer_scope::settings_theme_carousel,
        displayed_theme_preview_entries
    );
    const QSize card_size = preview_card_size();
    theme_carousel->set_card_size(card_size);
    theme_carousel->set_card_provider(
        13, [this, suit_index](int card_index, const QSize& size) {
            return request_theme_preview_card(card_index, suit_index, size);
        }
    );
}

raster_cache::entry_key settings_template_widget::theme_preview_entry_key(
    const QString& source_id, int target_bucket_px, qint64 generation_id,
    const QString& element_id
) const {
    return raster_cache::entry_key {
        .name_space = raster_cache::cache_namespace::settings,
        .kind = raster_cache::resource_kind::card_sheet_faces,
        .source_id = source_id,
        .render_scope = theme_preview_generation_render_scope(
            element_id, theme_preview_instance_id, generation_id
        ),
        .target_bucket_px = target_bucket_px,
    };
}

bool settings_template_widget::is_theme_preview_key_ready(
    const QString& source_id, int target_bucket_px, qint64 generation_id,
    const QString& element_id
) const {
    if (source_id.isEmpty() || target_bucket_px <= 0 || generation_id <= 0
        || element_id.isEmpty()) {
        return false;
    }

    const raster_cache::entry_key key = theme_preview_entry_key(
        source_id, target_bucket_px, generation_id, element_id
    );
    const std::optional<raster_cache::result> ready
        = settings_theme_preview_cache_service().get_if_ready(key);
    return ready.has_value() && !ready->face_images.isEmpty()
        && !ready->face_images[0].isNull();
}

void settings_template_widget::retire_theme_preview_generation(
    const QString& source_id, int target_bucket_px, qint64 generation_id
) {
    if (source_id.isEmpty() || target_bucket_px <= 0 || generation_id <= 0) {
        return;
    }

    auto& service = settings_theme_preview_cache_service();
    for (const QString& element_id : card_element_ids()) {
        service.erase_result(theme_preview_entry_key(
            source_id, target_bucket_px, generation_id, element_id
        ));
    }
}

void settings_template_widget::begin_theme_preview_warming_generation(
    const QString& source_id, int target_bucket_px
) {
    if (source_id.isEmpty() || target_bucket_px <= 0) {
        return;
    }

    if (warming_theme_preview_generation_id > 0
        && warming_theme_preview_source_id == source_id
        && warming_theme_preview_bucket_px == target_bucket_px) {
        return;
    }

    retire_theme_preview_generation(
        warming_theme_preview_source_id, warming_theme_preview_bucket_px,
        warming_theme_preview_generation_id
    );

    warming_theme_preview_source_id = source_id;
    warming_theme_preview_bucket_px = target_bucket_px;
    warming_theme_preview_generation_id = next_theme_preview_generation_id++;
    warming_theme_preview_requested_element_ids.clear();
}

void settings_template_widget::ensure_theme_preview_generation(
    const QString& source_id, int target_bucket_px
) {
    if (source_id.isEmpty() || target_bucket_px <= 0) {
        return;
    }

    if (active_theme_preview_generation_id <= 0) {
        active_theme_preview_source_id = source_id;
        active_theme_preview_bucket_px = target_bucket_px;
        active_theme_preview_generation_id = next_theme_preview_generation_id++;
        active_theme_preview_requested_element_ids.clear();
        return;
    }

    const bool active_matches = active_theme_preview_source_id == source_id
        && active_theme_preview_bucket_px == target_bucket_px;
    if (active_matches) {
        if (warming_theme_preview_generation_id > 0) {
            retire_theme_preview_generation(
                warming_theme_preview_source_id,
                warming_theme_preview_bucket_px,
                warming_theme_preview_generation_id
            );
            warming_theme_preview_source_id.clear();
            warming_theme_preview_bucket_px = 0;
            warming_theme_preview_generation_id = 0;
            warming_theme_preview_requested_element_ids.clear();
        }
        return;
    }

    begin_theme_preview_warming_generation(source_id, target_bucket_px);
}

bool settings_template_widget::try_cutover_theme_preview_generation() {
    if (warming_theme_preview_generation_id <= 0
        || warming_theme_preview_requested_element_ids.isEmpty()) {
        return false;
    }

    for (const QString& element_id :
         std::as_const(warming_theme_preview_requested_element_ids)) {
        if (!is_theme_preview_key_ready(
                warming_theme_preview_source_id,
                warming_theme_preview_bucket_px,
                warming_theme_preview_generation_id, element_id
            )) {
            return false;
        }
    }

    const QString previous_source_id = active_theme_preview_source_id;
    const int previous_bucket_px = active_theme_preview_bucket_px;
    const qint64 previous_generation_id = active_theme_preview_generation_id;

    active_theme_preview_source_id = warming_theme_preview_source_id;
    active_theme_preview_bucket_px = warming_theme_preview_bucket_px;
    active_theme_preview_generation_id = warming_theme_preview_generation_id;
    active_theme_preview_requested_element_ids
        = warming_theme_preview_requested_element_ids;

    warming_theme_preview_source_id.clear();
    warming_theme_preview_bucket_px = 0;
    warming_theme_preview_generation_id = 0;
    warming_theme_preview_requested_element_ids.clear();

    clear_displayed_theme_preview_entries(
        raster_cache::debug_consumer_scope::settings_theme_carousel,
        displayed_theme_preview_entries
    );
    clear_displayed_theme_preview_entries(
        raster_cache::debug_consumer_scope::settings_strategy_preview,
        displayed_weights_preview_entries
    );

    retire_theme_preview_generation(
        previous_source_id, previous_bucket_px, previous_generation_id
    );
    prune_pending_theme_preview_queue();

    if (theme_carousel != nullptr) {
        theme_preview_needs_refresh = true;
    }
    if (weights_carousel != nullptr) {
        weights_preview_needs_refresh = true;
    }
    if (!theme_preview_refresh_scheduled
        && (theme_preview_needs_refresh || weights_preview_needs_refresh)) {
        theme_preview_refresh_scheduled = true;
        QTimer::singleShot(
            0, this, &settings_template_widget::flush_coalesced_preview_refresh
        );
    }
    return true;
}

std::optional<QImage>
settings_template_widget::request_theme_preview_face_image(
    int card_index, int suit_index, const QSize& size,
    raster_cache::debug_consumer_scope consumer,
    QSet<raster_cache::entry_key>& tracked_keys
) {
    if (!size.isValid()) {
        return std::nullopt;
    }

    const QString element_id
        = theme_preview_render_scope(card_index, suit_index);
    if (element_id.isEmpty()) {
        return std::nullopt;
    }

    const int short_px = std::max(1, std::min(size.width(), size.height()));
    ensure_theme_preview_generation(selected_theme_source_id(), short_px);
    const bool use_warming_generation = warming_theme_preview_generation_id > 0;
    if (use_warming_generation) {
        warming_theme_preview_requested_element_ids.insert(element_id);
        active_theme_preview_requested_element_ids.insert(element_id);
    } else {
        active_theme_preview_requested_element_ids.insert(element_id);
    }
    try_cutover_theme_preview_generation();

    const bool use_warming_after_cutover
        = warming_theme_preview_generation_id > 0;
    const QString request_source_id = use_warming_after_cutover
        ? warming_theme_preview_source_id
        : active_theme_preview_source_id;
    const int request_bucket_px = use_warming_after_cutover
        ? warming_theme_preview_bucket_px
        : active_theme_preview_bucket_px;
    const qint64 request_generation_id = use_warming_after_cutover
        ? warming_theme_preview_generation_id
        : active_theme_preview_generation_id;
    if (request_source_id.isEmpty() || request_bucket_px <= 0
        || request_generation_id <= 0) {
        return std::nullopt;
    }

    auto& service = settings_theme_preview_cache_service();
    std::optional<QImage> active_fallback;
    if (is_theme_preview_key_ready(
            active_theme_preview_source_id, active_theme_preview_bucket_px,
            active_theme_preview_generation_id, element_id
        )) {
        const raster_cache::entry_key key = theme_preview_entry_key(
            active_theme_preview_source_id, active_theme_preview_bucket_px,
            active_theme_preview_generation_id, element_id
        );
        const std::optional<raster_cache::result> ready
            = service.get_if_ready(key);
        if (ready.has_value() && !ready->face_images.isEmpty()
            && !ready->face_images[0].isNull()) {
            active_fallback = ready->face_images[0];
            note_displayed_theme_preview_entry(key, consumer, tracked_keys);
            if (!use_warming_after_cutover) {
                return active_fallback;
            }
        }
    }

    const raster_cache::entry_key request_key = theme_preview_entry_key(
        request_source_id, request_bucket_px, request_generation_id, element_id
    );
    const raster_cache::request req {
        .name_space = request_key.name_space,
        .kind = request_key.kind,
        .source_id = request_key.source_id,
        .render_scope = request_key.render_scope,
        .need_short_px = short_px,
        .target_bucket_px = request_key.target_bucket_px,
        .high_priority = false,
        .interactive = true,
        .preview = true,
    };

    const raster_cache::submit_outcome outcome = service.submit_request(req);
    const bool request_has_ready_image = outcome.ready_result.has_value()
        && !outcome.ready_result->face_images.isEmpty()
        && !outcome.ready_result->face_images[0].isNull();
    if (request_has_ready_image) {
        if (request_generation_id == active_theme_preview_generation_id) {
            note_displayed_theme_preview_entry(
                outcome.key, consumer, tracked_keys
            );
            return outcome.ready_result->face_images[0];
        }
        if (try_cutover_theme_preview_generation()
            && is_theme_preview_key_ready(
                active_theme_preview_source_id, active_theme_preview_bucket_px,
                active_theme_preview_generation_id, element_id
            )) {
            const raster_cache::entry_key key = theme_preview_entry_key(
                active_theme_preview_source_id, active_theme_preview_bucket_px,
                active_theme_preview_generation_id, element_id
            );
            const std::optional<raster_cache::result> ready
                = service.get_if_ready(key);
            if (ready.has_value() && !ready->face_images.isEmpty()
                && !ready->face_images[0].isNull()) {
                note_displayed_theme_preview_entry(key, consumer, tracked_keys);
                return ready->face_images[0];
            }
        }
        if (active_fallback.has_value()) {
            return active_fallback;
        }
    }

    if (!request_has_ready_image) {
        enqueue_theme_preview_render(outcome.key);
    }
    const bool did_cutover = try_cutover_theme_preview_generation();
    if (did_cutover
        && is_theme_preview_key_ready(
            active_theme_preview_source_id, active_theme_preview_bucket_px,
            active_theme_preview_generation_id, element_id
        )) {
        const raster_cache::entry_key key = theme_preview_entry_key(
            active_theme_preview_source_id, active_theme_preview_bucket_px,
            active_theme_preview_generation_id, element_id
        );
        const std::optional<raster_cache::result> ready
            = service.get_if_ready(key);
        if (ready.has_value() && !ready->face_images.isEmpty()
            && !ready->face_images[0].isNull()) {
            note_displayed_theme_preview_entry(key, consumer, tracked_keys);
            return ready->face_images[0];
        }
    }
    if (active_fallback.has_value()) {
        return active_fallback;
    }
    return std::nullopt;
}

QPixmap settings_template_widget::request_theme_preview_card(
    int card_index, int suit_index, const QSize& size
) {
    const std::optional<QImage> face = request_theme_preview_face_image(
        card_index, suit_index, size,
        raster_cache::debug_consumer_scope::settings_theme_carousel,
        displayed_theme_preview_entries
    );
    if (!face.has_value()) {
        return {};
    }
    return QPixmap::fromImage(*face);
}

void settings_template_widget::enqueue_theme_preview_render(
    const raster_cache::entry_key& key
) {
    if (key.render_scope.isEmpty()) {
        return;
    }
    if (!is_theme_preview_key_relevant(key)) {
        return;
    }
    if (pending_theme_preview_render_set.contains(key)) {
        return;
    }
    pending_theme_preview_render_set.insert(key);
    pending_theme_preview_render_queue.enqueue(key);
    if (theme_preview_render_scheduled) {
        return;
    }
    theme_preview_render_scheduled = true;
    QTimer::singleShot(
        0, this, &settings_template_widget::process_pending_theme_preview_render
    );
}

void settings_template_widget::process_pending_theme_preview_render() {
    theme_preview_render_scheduled = false;
    if (theme_preview_render_watcher.isRunning()
        || active_theme_preview_render_key.has_value()) {
        return;
    }

    raster_cache::entry_key key;
    bool has_key = false;
    while (!pending_theme_preview_render_queue.isEmpty()) {
        const raster_cache::entry_key candidate
            = pending_theme_preview_render_queue.dequeue();
        pending_theme_preview_render_set.remove(candidate);
        if (!is_theme_preview_key_relevant(candidate)) {
            continue;
        }
        key = candidate;
        has_key = true;
        break;
    }
    if (!has_key) {
        return;
    }

    const QString render_scope = key.render_scope;
    if (!render_scope.startsWith(QStringLiteral("subset:"))) {
        const raster_cache::family_key family {
            .name_space = key.name_space,
            .kind = key.kind,
            .source_id = key.source_id,
            .render_scope = key.render_scope,
        };
        auto& service = settings_theme_preview_cache_service();
        const raster_cache::finish_outcome finish
            = service.finish_active_request(family, key);
        if (finish.next_entry_to_start.has_value()) {
            enqueue_theme_preview_render(finish.next_entry_to_start.value());
        }
        return;
    }

    const theme_preview_scope_parse parsed
        = parse_theme_preview_render_scope(render_scope);
    if (!parsed.valid) {
        const raster_cache::family_key family {
            .name_space = key.name_space,
            .kind = key.kind,
            .source_id = key.source_id,
            .render_scope = key.render_scope,
        };
        auto& service = settings_theme_preview_cache_service();
        const raster_cache::finish_outcome finish
            = service.finish_active_request(family, key);
        if (finish.next_entry_to_start.has_value()) {
            enqueue_theme_preview_render(finish.next_entry_to_start.value());
        }
        return;
    }

    active_theme_preview_render_key = key;
    const QString source_id = key.source_id;
    const QString element_id = parsed.element_id;
    const int target_bucket_px = key.target_bucket_px;
    theme_preview_render_watcher.setFuture(
        QtConcurrent::run([source_id, element_id, target_bucket_px]() {
            QSvgRenderer renderer(source_id);
            if (!renderer.isValid() || !renderer.elementExists(element_id)) {
                return QImage();
            }

            const QSize raster_size(target_bucket_px, target_bucket_px);
            QImage image(raster_size, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            renderer.render(
                &painter, element_id,
                QRectF(QPointF(0.0, 0.0), QSizeF(raster_size))
            );
            painter.end();
            return image;
        })
    );
}

void settings_template_widget::on_theme_preview_render_finished() {
    if (!active_theme_preview_render_key.has_value()) {
        return;
    }

    const raster_cache::entry_key key = *active_theme_preview_render_key;
    active_theme_preview_render_key.reset();
    const theme_preview_scope_parse parsed
        = parse_theme_preview_render_scope(key.render_scope);

    auto& service = settings_theme_preview_cache_service();
    const QImage image = theme_preview_render_watcher.result();
    const bool key_is_expected
        = parsed.valid && is_theme_preview_key_relevant(key);
    if (!image.isNull() && key_is_expected) {
        const raster_cache::result ready {
            .key = key,
            .raster_size = QSize(key.target_bucket_px, key.target_bucket_px),
            .generation = static_cast<int>(parsed.generation_id),
            .timestamp_ms = QDateTime::currentMSecsSinceEpoch(),
            .use_count = 0,
            .single_image = {},
            .face_images = { image },
        };
        service.insert_or_update_result(ready);
    } else if (!key_is_expected) {
        service.erase_result(key);
    }

    const raster_cache::family_key family {
        .name_space = key.name_space,
        .kind = key.kind,
        .source_id = key.source_id,
        .render_scope = key.render_scope,
    };
    const raster_cache::finish_outcome finish
        = service.finish_active_request(family, key);
    if (finish.next_entry_to_start.has_value()) {
        enqueue_theme_preview_render(finish.next_entry_to_start.value());
    }

    if (!pending_theme_preview_render_queue.isEmpty()) {
        theme_preview_render_scheduled = true;
        QTimer::singleShot(
            0, this,
            &settings_template_widget::process_pending_theme_preview_render
        );
    }
}

void settings_template_widget::on_theme_preview_cache_result_updated(
    const raster_cache::entry_key& key
) {
    if (key.name_space != raster_cache::cache_namespace::settings
        || key.kind != raster_cache::resource_kind::card_sheet_faces) {
        return;
    }
    const theme_preview_scope_parse parsed
        = parse_theme_preview_render_scope(key.render_scope);
    if (!parsed.valid || !is_theme_preview_key_relevant(key)) {
        return;
    }

    if (parsed.generation_id == warming_theme_preview_generation_id) {
        try_cutover_theme_preview_generation();
        return;
    }
    if (parsed.generation_id != active_theme_preview_generation_id) {
        return;
    }

    const QStringList& ids = card_element_ids();
    const int card_index = static_cast<int>(ids.indexOf(parsed.element_id));
    if (card_index < 0) {
        return;
    }
    const int suit_index = card_index / 13;
    if (suit_index == active_theme_preview_suit_index
        && theme_carousel != nullptr) {
        theme_preview_needs_refresh = true;
    }
    if (suit_index == active_weights_preview_suit_index
        && weights_carousel != nullptr) {
        weights_preview_needs_refresh = true;
    }

    if (!theme_preview_refresh_scheduled
        && (theme_preview_needs_refresh || weights_preview_needs_refresh)) {
        theme_preview_refresh_scheduled = true;
        QTimer::singleShot(
            0, this, &settings_template_widget::flush_coalesced_preview_refresh
        );
    }
}

void settings_template_widget::flush_coalesced_preview_refresh() {
    theme_preview_refresh_scheduled = false;

    if (theme_preview_needs_refresh && theme_carousel != nullptr) {
        theme_carousel->refresh_cards();
    }
    if (weights_preview_needs_refresh && weights_carousel != nullptr) {
        weights_carousel->refresh_cards();
    }

    theme_preview_needs_refresh = false;
    weights_preview_needs_refresh = false;
}

void settings_template_widget::update_weights_carousel(int suit_index) {
    if (weights_carousel == nullptr) {
        return;
    }
    active_weights_preview_suit_index = suit_index;
    active_theme_preview_requested_element_ids.clear();
    warming_theme_preview_requested_element_ids.clear();
    prune_pending_theme_preview_queue();
    clear_displayed_theme_preview_entries(
        raster_cache::debug_consumer_scope::settings_strategy_preview,
        displayed_weights_preview_entries
    );
    int strategy_index = strategy_list_widget != nullptr
        ? strategy_list_widget->currentRow()
        : -1;
    if (strategy_index < 0 || strategy_index >= strategies.size()) {
        return;
    }
    const QSize card_size = preview_card_size();
    weights_carousel->set_card_size(card_size);
    weights_carousel->set_card_provider(
        13, [this, suit_index](int card_index, const QSize& size) {
            return request_weighted_preview_card(card_index, suit_index, size);
        }
    );
}

QPixmap settings_template_widget::request_weighted_preview_card(
    int card_index, int suit_index, const QSize& size
) {
    int strategy_index = strategy_list_widget != nullptr
        ? strategy_list_widget->currentRow()
        : -1;
    if (strategy_index < 0 || strategy_index >= strategies.size()) {
        return {};
    }

    const std::optional<QImage> base_face = request_theme_preview_face_image(
        card_index, suit_index, size,
        raster_cache::debug_consumer_scope::settings_strategy_preview,
        displayed_weights_preview_entries
    );
    if (!base_face.has_value()) {
        return {};
    }
    return build_weighted_card_preview(
        *base_face, card_index, suit_index, size,
        strategies[strategy_index].weights
    );
}

void settings_template_widget::note_displayed_theme_preview_entry(
    const raster_cache::entry_key& key,
    raster_cache::debug_consumer_scope consumer,
    QSet<raster_cache::entry_key>& tracked_keys
) {
    settings_theme_preview_cache_service().note_entry_displayed(key, consumer);
    tracked_keys.insert(key);
}

void settings_template_widget::clear_displayed_theme_preview_entries(
    raster_cache::debug_consumer_scope consumer,
    QSet<raster_cache::entry_key>& tracked_keys
) {
    if (tracked_keys.isEmpty()) {
        return;
    }

    auto& service = settings_theme_preview_cache_service();
    for (const raster_cache::entry_key& key : std::as_const(tracked_keys)) {
        service.note_entry_no_longer_displayed(key, consumer);
    }
    tracked_keys.clear();
}

void settings_template_widget::update_suit_selection(int index) {
    if (suit_combo_box != nullptr && suit_combo_box->currentIndex() != index) {
        suit_combo_box->setCurrentIndex(index);
    }
    shared_state->set_default_suit(index);
}

bool settings_template_widget::is_theme_preview_key_relevant(
    const raster_cache::entry_key& key
) const {
    if (key.name_space != raster_cache::cache_namespace::settings
        || key.kind != raster_cache::resource_kind::card_sheet_faces) {
        return false;
    }

    const theme_preview_scope_parse parsed
        = parse_theme_preview_render_scope(key.render_scope);
    if (!parsed.valid || parsed.instance_id != theme_preview_instance_id) {
        return false;
    }

    const QStringList& ids = card_element_ids();
    const int card_index = static_cast<int>(ids.indexOf(parsed.element_id));
    if (card_index < 0) {
        return false;
    }

    const int suit_index = card_index / 13;
    const bool suit_is_relevant = suit_index == active_theme_preview_suit_index
        || suit_index == active_weights_preview_suit_index;
    if (!suit_is_relevant) {
        return false;
    }

    if (parsed.generation_id == active_theme_preview_generation_id) {
        return key.source_id == active_theme_preview_source_id
            && key.target_bucket_px == active_theme_preview_bucket_px
            && active_theme_preview_requested_element_ids.contains(
                parsed.element_id
            );
    }
    if (parsed.generation_id == warming_theme_preview_generation_id) {
        return key.source_id == warming_theme_preview_source_id
            && key.target_bucket_px == warming_theme_preview_bucket_px
            && warming_theme_preview_requested_element_ids.contains(
                parsed.element_id
            );
    }

    return false;
}

QString settings_template_widget::selected_theme_source_id() const {
    if (theme_button_group != nullptr
        && theme_button_group->checkedButton() != nullptr) {
        const QVariant source_value
            = theme_button_group->checkedButton()->property("theme_source");
        if (source_value.isValid()) {
            const QString source_id = source_value.toString();
            if (!source_id.isEmpty()) {
                return source_id;
            }
        }
    }

    return card_sheet_source_path();
}

void settings_template_widget::prune_pending_theme_preview_queue() {
    if (pending_theme_preview_render_queue.isEmpty()) {
        return;
    }

    QQueue<raster_cache::entry_key> filtered;
    QSet<raster_cache::entry_key> filtered_set;
    while (!pending_theme_preview_render_queue.isEmpty()) {
        const raster_cache::entry_key key
            = pending_theme_preview_render_queue.dequeue();
        if (!is_theme_preview_key_relevant(key) || filtered_set.contains(key)) {
            continue;
        }
        filtered.enqueue(key);
        filtered_set.insert(key);
    }

    pending_theme_preview_render_queue = filtered;
    pending_theme_preview_render_set = filtered_set;
}
