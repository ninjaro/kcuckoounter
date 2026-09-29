#include "table/settings_template.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "image/card_preview_carousel.hpp"
#include "settings/preferences.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/table.hpp"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace settings_template_support {
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

int orientation_index(card_orientation_mode orientation) noexcept {
    switch (orientation) {
    case card_orientation_mode::automatic:
        return 0;
    case card_orientation_mode::vertical:
        return 1;
    case card_orientation_mode::horizontal:
        return 2;
    }
    return 0;
}

card_orientation_mode orientation_from_index(int index) noexcept {
    switch (index) {
    case 1:
        return card_orientation_mode::vertical;
    case 2:
        return card_orientation_mode::horizontal;
    default:
        return card_orientation_mode::automatic;
    }
}

QIcon palette_swatch_icon(const QColor& color) {
    constexpr int swatch_size = 14;
    QPixmap swatch(swatch_size, swatch_size);
    swatch.fill(color);
    QPainter painter(&swatch);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(30, 30, 30)));
    painter.drawRect(0, 0, swatch_size - 1, swatch_size - 1);
    painter.end();
    return { swatch };
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
    return { icon };
}

QColor suit_color_for_index(int suit_index) {
    return (suit_index == 1 || suit_index == 2) ? QColor(170, 0, 0)
                                                : QColor(20, 20, 20);
}

QIcon suit_icon_for_index(int suit_index) {
    const QStringList symbols
        = { str_label("♣"), str_label("♦"), str_label("♥"), str_label("♠") };
    const QColor color = suit_color_for_index(suit_index);
    const QString symbol = (suit_index >= 0 && suit_index < symbols.size())
        ? symbols.at(suit_index)
        : QString();
    return suit_icon(symbol, color);
}

QSize preview_card_size() {
    const auto [long_side, short_side] = card_sheet_ratio();
    const int target_long = 88;
    if (long_side <= 0 || short_side <= 0) {
        return { 63, 88 };
    }
    const double scale = static_cast<double>(target_long) / long_side;
    const int width
        = std::max(1, static_cast<int>(std::lround(short_side * scale)));
    return { width, target_long };
}

raster_cache& settings_theme_preview_cache_service() {
    static raster_cache service;
    service.set_namespace_entry_limit(
        raster_cache::cache_namespace::settings, 12
    );
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
        return {};
    }
    return ids.at(card_index);
}

QString theme_preview_generation_render_scope(
    const QString& element_id, qint64 instance_id, qint64 generation_id
) {
    if (element_id.isEmpty() || instance_id <= 0 || generation_id <= 0) {
        return {};
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
    painter.setPen(suit_color_for_index(suit_index));

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

} // namespace settings_template_support

namespace {
enum strategy_browser_role {
    slug_role = Qt::UserRole + 1,
    search_role,
    detail_role,
    number_role
};

const QStringList& comparison_metric_keys() {
    static const QStringList keys { QStringLiteral("betting_correlation"),
                                    QStringLiteral("playing_efficiency"),
                                    QStringLiteral("insurance_correlation"),
                                    QStringLiteral("ease_of_use") };
    return keys;
}

class strategy_comparison_proxy final : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool
    lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        if (left.column() != 0) {
            const auto lhs = left.data(number_role);
            const auto rhs = right.data(number_role);
            // Qt reverses the comparison for descending order. Missing values
            // belong last in either direction, never masquerading as zero.
            if (lhs.isValid() != rhs.isValid())
                return sortOrder() == Qt::AscendingOrder ? lhs.isValid()
                                                         : rhs.isValid();
            if (lhs.isValid() && lhs.toDouble() != rhs.toDouble())
                return lhs.toDouble() < rhs.toDouble();
        } else {
            const int order = QString::localeAwareCompare(
                left.data().toString().toCaseFolded(),
                right.data().toString().toCaseFolded()
            );
            if (order != 0)
                return order < 0;
        }
        return left.siblingAtColumn(0).data(slug_role).toString()
            < right.siblingAtColumn(0).data(slug_role).toString();
    }
};

QString strategy_detail_html(const strategy_data& strategy) {
    const auto text
        = [](const QString& value) { return value.toHtmlEscaped(); };
    const auto absent = str_label("Not provided");
    QString html = QStringLiteral("<h2>%1</h2><p>%2</p>")
                       .arg(text(strategy.name), text(strategy.description));
    html += QStringLiteral(
                "<h3>%1</h3><table border='1' cellspacing='0' "
                "cellpadding='4'><tr>"
    )
                .arg(text(str_label("Count value by rank")));
    const QStringList ranks { QStringLiteral("A"), QStringLiteral("2"),
                              QStringLiteral("3"), QStringLiteral("4"),
                              QStringLiteral("5"), QStringLiteral("6"),
                              QStringLiteral("7"), QStringLiteral("8"),
                              QStringLiteral("9"), QStringLiteral("10"),
                              QStringLiteral("J"), QStringLiteral("Q"),
                              QStringLiteral("K") };
    for (const auto& rank : ranks)
        html += QStringLiteral(
                    "<th><p style='white-space:pre; margin:0'>%1</p></th>"
        )
                    .arg(rank);
    html += QStringLiteral("</tr><tr>");
    for (qsizetype i = 0; i < ranks.size(); ++i) {
        const QString value = i < strategy.weights.size()
            ? (strategy.weights[i] > 0 ? QStringLiteral("+") : QString())
                + QString::number(strategy.weights[i])
            : absent;
        html += QStringLiteral(
                    "<td align='center'><p style='white-space:pre; "
                    "margin:0'>%1</p></td>"
        )
                    .arg(text(value));
    }
    html += QStringLiteral("</tr></table><h3>%1</h3><table cellpadding='3'>")
                .arg(text(str_label("General")));
    const auto field = [&](const QString& label, const QString& value) {
        html += QStringLiteral("<tr><th align='left'>%1</th><td>%2</td></tr>")
                    .arg(text(label), text(value.isEmpty() ? absent : value));
    };
    field(str_label("Published"), strategy.date);
    field(str_label("Authors"), strategy.authors.join(QStringLiteral(", ")));
    field(str_label("Games"), strategy.games.join(QStringLiteral(", ")));
    field(
        str_label("Minimum decks"),
        strategy.min_decks > 0 ? QString::number(strategy.min_decks) : absent
    );
    field(
        str_label("Balanced"),
        strategy.balance ? str_label("Yes") : str_label("No")
    );
    field(
        str_label("Ace neutral"),
        strategy.ace_neutral ? str_label("Yes") : str_label("No")
    );
    html += QStringLiteral("</table><h3>%1</h3><table cellpadding='3'>")
                .arg(text(str_label("Metrics")));
    for (const auto& key : comparison_metric_keys()) {
        const auto value = strategy.metrics.constFind(key);
        field(
            settings_template_support::format_key_label(key),
            value != strategy.metrics.cend() && std::isfinite(value.value())
                ? QString::number(value.value())
                : absent
        );
    }
    html += QStringLiteral("</table><h3>%1</h3>")
                .arg(text(str_label("Notes / Unique fields")));
    if (strategy.unique_fields.isEmpty())
        html += QStringLiteral("<p>%1</p>").arg(text(absent));
    for (auto it = strategy.unique_fields.cbegin();
         it != strategy.unique_fields.cend(); ++it)
        html
            += QStringLiteral("<p><b>%1:</b> %2</p>")
                   .arg(
                       text(
                           settings_template_support::format_key_label(it.key())
                       ),
                       text(it.value())
                   );
    html += QStringLiteral("<h3>%1</h3>").arg(text(str_label("References")));
    if (strategy.references.isEmpty())
        html += QStringLiteral("<p>%1</p>").arg(text(absent));
    for (const auto& ref : strategy.references) {
        html += QStringLiteral("<p>%1").arg(text(ref.citation));
        const QUrl url(ref.url);
        if (url.isValid()
            && (url.scheme() == QStringLiteral("https")
                || url.scheme() == QStringLiteral("http")))
            html
                += QStringLiteral(" <a href=\"%1\">%2</a>")
                       .arg(
                           text(url.toString(QUrl::FullyEncoded)), text(ref.url)
                       );
        else if (!ref.url.isEmpty())
            html += QStringLiteral(" %1").arg(text(ref.url));
        if (!ref.accessed.isEmpty())
            html += QStringLiteral(" (%1: %2)")
                        .arg(text(str_label("Accessed")), text(ref.accessed));
        html += QStringLiteral("</p>");
    }
    return html;
}
} // namespace

QWidget* create_strategy_browser(
    const strategy_catalog& catalog, const QString& selected_slug,
    QWidget* parent
) {
    auto* browser = new QWidget(parent);
    browser->setObjectName(QStringLiteral("strategy_browser"));
    auto* layout = new QVBoxLayout(browser);
    auto* search = new QLineEdit(browser);
    search->setObjectName(QStringLiteral("strategy_search"));
    search->setClearButtonEnabled(true);
    search->setAccessibleName(str_label("Search strategies"));
    search->setPlaceholderText(str_label("Name, author, game or description"));
    auto* search_label = new QLabel(str_label("Search strategies"), browser);
    search_label->setBuddy(search);
    layout->addWidget(search_label);
    layout->addWidget(search);
    auto* state = new QLabel(browser);
    state->setObjectName(QStringLiteral("strategy_search_status"));
    state->setTextFormat(Qt::PlainText);
    state->setWordWrap(true);
    layout->addWidget(state);
    auto* tabs = new QTabWidget(browser);
    tabs->setObjectName(QStringLiteral("strategy_browser_views"));
    auto* split = new QSplitter(Qt::Horizontal, tabs);
    split->setChildrenCollapsible(false);
    auto* list = new QListView(split);
    list->setObjectName(QStringLiteral("strategy_browser_list"));
    list->setAccessibleName(str_label("Available strategies"));
    list->setMinimumWidth(90);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto* details = new QTextBrowser(split);
    details->setObjectName(QStringLiteral("strategy_browser_details"));
    details->setAccessibleName(str_label("Strategy details and rank weights"));
    details->setMinimumWidth(100);
    details->setOpenExternalLinks(true);
    split->setSizes({ 190, 560 });
    tabs->addTab(split, str_label("Details"));
    auto* comparison_page = new QWidget(tabs);
    auto* comparison_layout = new QVBoxLayout(comparison_page);
    comparison_layout->setContentsMargins(0, 0, 0, 0);
    auto* sorting = new QFormLayout;
    sorting->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto* sort_field = new BaseComboBox(comparison_page);
    sort_field->setObjectName(QStringLiteral("strategy_sort_field"));
    sort_field->addItem(str_label("Strategy name"));
    for (const auto& key : comparison_metric_keys())
        sort_field->addItem(settings_template_support::format_key_label(key));
    auto* sort_order = new BaseComboBox(comparison_page);
    sort_order->setObjectName(QStringLiteral("strategy_sort_order"));
    sort_order->addItems({ str_label("Ascending"), str_label("Descending") });
    sorting->addRow(str_label("Sort by"), sort_field);
    sorting->addRow(str_label("Order"), sort_order);
    comparison_layout->addLayout(sorting);
    auto* comparison = new QTableView(comparison_page);
    comparison->setObjectName(QStringLiteral("strategy_comparison"));
    comparison->setAccessibleName(str_label("Compare strategy metrics"));
    comparison->setEditTriggers(QAbstractItemView::NoEditTriggers);
    comparison->setSelectionBehavior(QAbstractItemView::SelectRows);
    comparison->setSelectionMode(QAbstractItemView::SingleSelection);
    comparison->verticalHeader()->hide();
    comparison_layout->addWidget(comparison, 1);
    tabs->addTab(comparison_page, str_label("Compare"));
    layout->addWidget(tabs, 1);
    auto* note = new QLabel(
        str_label(
            "Reference only: browsing does not change a slot's strategy. "
            "Metrics "
            "are catalogue values, not a ranking; missing values are not zero. "
            "Select Details to read weights, context and sources."
        ),
        browser
    );
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* model = new QStandardItemModel(browser);
    model->setHorizontalHeaderLabels(
        { str_label("Strategy"), str_label("BC"), str_label("PE"),
          str_label("IC"), str_label("Ease") }
    );
    for (int column = 1; column <= comparison_metric_keys().size(); ++column) {
        const QString label = settings_template_support::format_key_label(
            comparison_metric_keys().at(column - 1)
        );
        model->setHeaderData(column, Qt::Horizontal, label, Qt::ToolTipRole);
        model->setHeaderData(
            column, Qt::Horizontal, label, Qt::AccessibleTextRole
        );
    }
    if (catalog.is_valid()) {
        for (const auto& strategy : catalog.strategies) {
            auto* name = new QStandardItem(strategy.name);
            name->setData(strategy.slug, slug_role);
            name->setData(
                QStringList { strategy.name, strategy.slug,
                              strategy.description,
                              strategy.authors.join(QLatin1Char(' ')),
                              strategy.games.join(QLatin1Char(' ')) }
                    .join(QLatin1Char(' ')),
                search_role
            );
            name->setData(strategy_detail_html(strategy), detail_role);
            name->setToolTip(strategy.name);
            QList<QStandardItem*> row { name };
            for (const auto& key : comparison_metric_keys()) {
                auto* value = new QStandardItem;
                const auto metric = strategy.metrics.constFind(key);
                if (metric != strategy.metrics.cend()
                    && std::isfinite(metric.value())) {
                    value->setData(metric.value(), Qt::DisplayRole);
                    value->setData(metric.value(), number_role);
                } else {
                    value->setText(str_label("Not provided"));
                }
                row.append(value);
            }
            model->appendRow(row);
        }
    }
    auto* proxy = new strategy_comparison_proxy(browser);
    proxy->setSourceModel(model);
    proxy->setFilterRole(search_role);
    proxy->setFilterKeyColumn(0);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    list->setModel(proxy);
    comparison->setModel(proxy);
    comparison->setSelectionModel(list->selectionModel());
    comparison->setSortingEnabled(true);
    comparison->sortByColumn(0, Qt::AscendingOrder);
    const auto sort = [=] {
        comparison->sortByColumn(
            sort_field->currentIndex(),
            sort_order->currentIndex() == 0 ? Qt::AscendingOrder
                                            : Qt::DescendingOrder
        );
    };
    QObject::connect(
        sort_field, &BaseComboBox::currentIndexChanged, browser, sort
    );
    QObject::connect(
        sort_order, &BaseComboBox::currentIndexChanged, browser, sort
    );
    QObject::connect(
        comparison->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
        browser, [=](int column, Qt::SortOrder order) {
            const QSignalBlocker field_blocker(sort_field);
            const QSignalBlocker order_blocker(sort_order);
            sort_field->setCurrentIndex(column);
            sort_order->setCurrentIndex(order == Qt::AscendingOrder ? 0 : 1);
        }
    );
    comparison->resizeColumnsToContents();
    comparison->setColumnWidth(0, 220);
    comparison->horizontalHeader()->setStretchLastSection(true);
    const auto refresh_details = [=] {
        const auto index = list->currentIndex().siblingAtColumn(0);
        if (index.isValid())
            details->setHtml(index.data(detail_role).toString());
        else
            details->setPlainText(
                str_label("Select a strategy to read its details.")
            );
    };
    QObject::connect(
        list->selectionModel(), &QItemSelectionModel::currentChanged, browser,
        refresh_details
    );
    const auto select_slug = [=](const QString& slug) {
        QModelIndex selected;
        for (int row = 0; row < proxy->rowCount(); ++row)
            if (proxy->index(row, 0).data(slug_role).toString() == slug)
                selected = proxy->index(row, 0);
        if (!selected.isValid() && proxy->rowCount() > 0 && slug.isEmpty())
            selected = proxy->index(0, 0);
        list->selectionModel()->setCurrentIndex(
            selected,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows
        );
        refresh_details();
    };
    const QString unavailable = !catalog.is_valid()
        ? str_label("Strategies unavailable") + QStringLiteral(": ")
            + catalog.diagnostic_summary()
        : QString();
    const auto refresh_status = [=] {
        state->setText(
            !unavailable.isEmpty() ? unavailable
                : proxy->rowCount() == 0
                ? str_label(
                      "No matching strategies. Clear the search to see the "
                      "catalogue."
                  )
                : str_label("Matching strategies") + QStringLiteral(": ")
                    + QString::number(proxy->rowCount())
        );
    };
    QObject::connect(
        search, &QLineEdit::textChanged, browser, [=](const QString& query) {
            const QString slug = list->currentIndex()
                                     .siblingAtColumn(0)
                                     .data(slug_role)
                                     .toString();
            proxy->setFilterFixedString(query.trimmed());
            select_slug(slug);
            refresh_status();
        }
    );
    QObject::connect(comparison, &QTableView::activated, browser, [=] {
        tabs->setCurrentIndex(0);
        details->setFocus();
    });
    QObject::connect(tabs, &QTabWidget::currentChanged, browser, [=](int tab) {
        if (tab == 0)
            list->scrollTo(list->currentIndex().siblingAtColumn(0));
        else
            comparison->scrollTo(comparison->currentIndex());
    });
    select_slug(selected_slug);
    refresh_status();
    return browser;
}

settings_shared_state::settings_shared_state(QObject* parent)
    : QObject(parent)
    , default_suit_value(0)
    , table_color_index_value(
          settings_template_support::theme_index_from_color(
              theme_settings::base_color()
          )
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
    settings_tab_kind tab_kind_value, BaseWidget* parent,
    const QString& selected_strategy, table* table_widget_ptr,
    settings_shared_state* shared_state_ptr
)
    : BaseWidget(parent)
    , tab_kind(tab_kind_value)
    , table_widget(table_widget_ptr)
    , shared_state(
          shared_state_ptr != nullptr ? shared_state_ptr
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
    , active_preview_element_ids()
    , warming_theme_preview_source_id()
    , warming_theme_preview_bucket_px(0)
    , warming_theme_preview_generation_id(0)
    , warming_preview_element_ids()
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
    theme_preview_instance_id
        = settings_template_support::next_theme_preview_instance_id();
    QObject::connect(
        &settings_template_support::settings_theme_preview_cache_service(),
        &raster_cache::result_updated, this,
        &settings_template_widget::on_theme_preview_cache_updated
    );
    QObject::connect(
        &theme_preview_render_watcher, &QFutureWatcher<QImage>::finished, this,
        &settings_template_widget::on_theme_preview_render_finished
    );
    setup_ui(selected_strategy);
}

settings_template_widget::~settings_template_widget() {
    clear_displayed_theme_preview_entries(displayed_theme_preview_entries);
    clear_displayed_theme_preview_entries(displayed_weights_preview_entries);
    QObject::disconnect(&theme_preview_render_watcher, nullptr, this, nullptr);
    auto& preview_cache
        = settings_template_support::settings_theme_preview_cache_service();
    QSet<raster_cache::entry_key> abandoned_keys
        = pending_theme_preview_render_set;
    if (active_theme_preview_render_key.has_value()) {
        abandoned_keys.insert(*active_theme_preview_render_key);
    }
    for (const raster_cache::entry_key& key : abandoned_keys) {
        const raster_cache::family_key family {
            .name_space = key.name_space,
            .kind = key.kind,
            .source_id = key.source_id,
            .render_scope = key.render_scope,
        };
        preview_cache.clear_in_flight(family);
        preview_cache.take_pending_latest(family);
        preview_cache.erase_result(key);
    }
    active_theme_preview_render_key.reset();
    pending_theme_preview_render_queue.clear();
    pending_theme_preview_render_set.clear();
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
    strategy_list_widget->setAccessibleName(str_label("Available strategies"));
    dock_label->setBuddy(strategy_list_widget);
    const strategy_catalog& repository = strategy_repository();
    strategies = repository.is_valid() ? repository.strategies
                                       : QVector<strategy_data>();
    if (!repository.is_valid()) {
        strategy_list_widget->addItem(str_label("Strategies unavailable"));
        strategy_list_widget->setEnabled(false);
        strategy_list_widget->setToolTip(repository.diagnostic_summary());
    } else {
        for (const strategy_data& strategy : strategies) {
            strategy_list_widget->addItem(strategy.name);
        }
    }
    dock_layout->addWidget(strategy_list_widget, 1);

#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    auto* browse = new QPushButton(str_label("Browse / compare…"), dock_widget);
    browse->setObjectName(QStringLiteral("browse_strategies"));
    browse->setAutoDefault(false);
    dock_layout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const int selected = strategy_list_widget->currentRow();
        const QString slug = selected >= 0 && selected < strategies.size()
            ? strategies[selected].slug
            : QString();
        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("strategy_browser_dialog"));
        dialog.setWindowTitle(str_label("Strategy browser"));
        auto* layout = new QVBoxLayout(&dialog);
        layout->addWidget(
            create_strategy_browser(strategy_repository(), slug, &dialog)
        );
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(
            buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject
        );
        layout->addWidget(buttons);
        dialog.resize(820, 600);
        dialog.exec();
    });
#endif

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
        suit_combo_box->setItemIcon(
            i, settings_template_support::suit_icon_for_index(i)
        );
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
    const QSize card_size = settings_template_support::preview_card_size();
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

    general_table
        = settings_template_support::build_readonly_table(6, 2, right_column);
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

    metrics_table
        = settings_template_support::build_readonly_table(4, 2, right_column);
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
    theme_combo_box->addItems(settings_template_support::theme_labels());
    const auto& options = theme_palette_registry::options();
    for (int i = 0; i < options.size(); ++i) {
        theme_combo_box->setItemIcon(
            i,
            settings_template_support::palette_swatch_icon(
                options.at(i).base_color()
            )
        );
    }

    suit_combo_box = new BaseComboBox(theme_widget);
    suit_combo_box->addItems(
        QStringList() << str_label("Clubs ♣") << str_label("Diamonds ♦")
                      << str_label("Hearts ♥") << str_label("Spades ♠")
    );
    for (int i = 0; i < suit_combo_box->count(); ++i) {
        suit_combo_box->setItemIcon(
            i, settings_template_support::suit_icon_for_index(i)
        );
    }

    orientation_combo_box = new BaseComboBox(theme_widget);
    orientation_combo_box->setObjectName(
        QStringLiteral("orientation_combo_box")
    );
    orientation_combo_box->addItems(
        QStringList() << str_label("Automatic") << str_label("Vertical")
                      << str_label("Horizontal")
    );
    // "Absolute" is intentionally not exposed: its layout semantics remain
    // deferred until a dedicated fixed-size packing mode is specified.
    const trainer_preferences preferences = load_trainer_preferences();
    orientation_combo_box->setCurrentIndex(
        settings_template_support::orientation_index(
            preferences.card_orientation
        )
    );

    theme_layout->addRow(str_label("Table color"), theme_combo_box);
    theme_palette_preview = new BaseWidget(theme_widget);
    auto palette_layout = new QHBoxLayout(theme_palette_preview);
    palette_layout->setContentsMargins(0, 0, 0, 0);
    palette_layout->setSpacing(4);
    theme_layout->addRow(str_label("Palette"), theme_palette_preview);
    theme_layout->addRow(str_label("Default suit"), suit_combo_box);
    theme_layout->addRow(str_label("Orientation"), orientation_combo_box);
    setup_desktop_components(theme_layout);
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
    QRadioButton* first_theme_button = nullptr;
    const QVector<card_theme_option>& card_themes = available_card_themes();
    for (const card_theme_option& theme_option : card_themes) {
        auto* theme_button
            = new QRadioButton(theme_option.label, theme_options_widget);
        theme_button->setProperty("theme_source", theme_option.source_path);
        theme_button_group->addButton(theme_button);
        theme_options_layout->addWidget(theme_button);
        if (first_theme_button == nullptr) {
            first_theme_button = theme_button;
        }
    }

    if (first_theme_button == nullptr) {
        first_theme_button = new QRadioButton(
            str_label("Bundled: Base card theme"), theme_options_widget
        );
        first_theme_button->setProperty(
            "theme_source", default_card_sheet_source_path()
        );
        theme_button_group->addButton(first_theme_button);
        theme_options_layout->addWidget(first_theme_button);
    }

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
        first_theme_button->setChecked(true);
    }

    theme_section_layout->addWidget(theme_options_widget);

    theme_carousel = new card_preview_carousel(theme_section);
    theme_carousel->set_visible_range(3, 5);
    theme_carousel->set_minimum_card_width(88);
    theme_carousel->set_prefetch_adjacent_cards(false);
    const QSize card_size = settings_template_support::preview_card_size();
    theme_carousel->set_card_size(card_size);
    update_theme_carousel(shared_state->default_suit());
    theme_section_layout->addWidget(theme_carousel);

    main_layout->addWidget(theme_section);

    main_layout->addStretch(1);

    QObject::connect(
        shared_state, &settings_shared_state::table_color_index_changed, this,
        &settings_template_widget::sync_theme_combo_shared_state
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
        &settings_template_widget::on_theme_source_button_clicked
    );

    theme_combo_box->setCurrentIndex(shared_state->table_color_index());
    update_suit_selection(shared_state->default_suit());
    update_theme_palette_preview(theme_combo_box->currentIndex());
}

void settings_template_widget::sync_theme_combo_shared_state(int index) {
    if (theme_combo_box == nullptr
        || theme_combo_box->currentIndex() == index) {
        return;
    }

    theme_combo_box->setCurrentIndex(index);
    update_theme_palette_preview(index);
}

void settings_template_widget::on_theme_source_button_clicked(
    QAbstractButton* button
) {
    Q_UNUSED(button);

    prune_pending_theme_preview_queue();
    clear_displayed_theme_entries();
    mark_preview_refresh_pending(true, true);
    flush_coalesced_preview_refresh();
}

void settings_template_widget::setup_desktop_components(QFormLayout* layout) {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    ui_preset_combo = new BaseComboBox(this);
    ui_preset_combo->setObjectName(QStringLiteral("desktop_ui_preset"));
    ui_preset_combo->setAccessibleName(str_label("Desktop UI preset"));
    ui_preset_combo->addItems({ str_label("Classic"), str_label("Quiet") });
    ui_preset_combo->setToolTip(str_label(
        "Presentation only; does not change game rules, palette or card "
        "artwork."
    ));
    layout->addRow(str_label("Desktop UI preset"), ui_preset_combo);
    ui_frame_combo = new BaseComboBox(this);
    ui_frame_combo->setObjectName(QStringLiteral("desktop_ui_frame"));
    ui_frame_combo->setAccessibleName(str_label("Slot frame"));
    ui_frame_combo->addItems(
        { str_label("Preset default"), str_label("Classic"),
          str_label("Thin outline") }
    );
    layout->addRow(str_label("Slot frame"), ui_frame_combo);
    ui_speed_combo = new BaseComboBox(this);
    ui_speed_combo->setObjectName(QStringLiteral("desktop_ui_speed_readout"));
    ui_speed_combo->setAccessibleName(str_label("Pickup interval text"));
    ui_speed_combo->addItems(
        { str_label("Preset default"), str_label("Shown"), str_label("Hidden") }
    );
    ui_speed_combo->setToolTip(str_label(
        "Only changes the numeric readout. The speed slider and gameplay speed "
        "policy are unchanged."
    ));
    layout->addRow(str_label("Pickup interval text"), ui_speed_combo);
    ui_answer_combo = new BaseComboBox(this);
    ui_answer_combo->setObjectName(QStringLiteral("desktop_ui_answer_entry"));
    ui_answer_combo->setAccessibleName(str_label("Answer entry"));
    ui_answer_combo->addItems(
        { str_label("Preset default"), str_label("Numeric"),
          str_label("Chip stepper") }
    );
    ui_answer_combo->setToolTip(str_label(
        "Chips adjust the same accumulated count; Check submits it. Typed "
        "entry remains available."
    ));
    layout->addRow(str_label("Answer entry"), ui_answer_combo);
    ui_feedback_combo = new BaseComboBox(this);
    ui_feedback_combo->setObjectName(QStringLiteral("desktop_ui_feedback"));
    ui_feedback_combo->setAccessibleName(str_label("Answer feedback"));
    ui_feedback_combo->addItems(
        { str_label("Preset default"), str_label("Classic text"),
          str_label("Count stamp") }
    );
    layout->addRow(str_label("Answer feedback"), ui_feedback_combo);
    ui_actions_combo = new BaseComboBox(this);
    ui_actions_combo->setObjectName(QStringLiteral("desktop_ui_slot_actions"));
    ui_actions_combo->setAccessibleName(str_label("Slot actions"));
    ui_actions_combo->addItems(
        { str_label("Preset default"), str_label("Classic"),
          str_label("Icon rail"), str_label("Labelled pills") }
    );
    ui_actions_combo->setToolTip(str_label(
        "Same Details, Swap, Copy and Copy all actions. Small slots use the "
        "Card controls window."
    ));
    layout->addRow(str_label("Slot actions"), ui_actions_combo);
    ui_settings_combo = new BaseComboBox(this);
    ui_settings_combo->setObjectName(
        QStringLiteral("desktop_ui_settings_surface")
    );
    ui_settings_combo->setAccessibleName(str_label("Slot settings"));
    ui_settings_combo->addItems(
        { str_label("Preset default"), str_label("Classic"),
          str_label("Settings card"), str_label("Edge drawer"),
          str_label("Bottom sill") }
    );
    ui_settings_combo->setToolTip(str_label(
        "Alternatives open on Details and apply only with OK. Small slots host "
        "the same draft in a window."
    ));
    layout->addRow(str_label("Slot settings"), ui_settings_combo);
    ui_toolbar_combo = new BaseComboBox(this);
    ui_toolbar_combo->setObjectName(QStringLiteral("desktop_ui_toolbar"));
    ui_toolbar_combo->setAccessibleName(str_label("Main toolbar"));
    ui_toolbar_combo->addItems(
        { str_label("Preset default"), str_label("Classic"),
          str_label("Compact") }
    );
    ui_toolbar_combo->setToolTip(str_label(
        "Compact keeps Start/Pause/Resume labelled and uses icons for other "
        "actions. Native menus, shortcuts and toolbar placement are unchanged."
    ));
    layout->addRow(str_label("Main toolbar"), ui_toolbar_combo);
    ui_hud_combo = new BaseComboBox(this);
    ui_hud_combo->setObjectName(QStringLiteral("desktop_ui_hud"));
    ui_hud_combo->setAccessibleName(str_label("Session HUD"));
    ui_hud_combo->addItems(
        { str_label("Preset default"), str_label("Classic"),
          str_label("Instrument strip") }
    );
    ui_hud_combo->setToolTip(str_label(
        "Same status, score, time and pickup controls. Instrument strip adds "
        "emphasis and slider ticks, not new scoring or speed rules."
    ));
    layout->addRow(str_label("Session HUD"), ui_hud_combo);
    auto* reset
        = new QPushButton(str_label("Reset components to preset"), this);
    reset->setObjectName(QStringLiteral("desktop_ui_reset"));
    layout->addRow(reset);
    const auto reset_overrides = [this] {
        ui_frame_combo->setCurrentIndex(0);
        ui_speed_combo->setCurrentIndex(0);
        ui_answer_combo->setCurrentIndex(0);
        ui_feedback_combo->setCurrentIndex(0);
        ui_actions_combo->setCurrentIndex(0);
        ui_settings_combo->setCurrentIndex(0);
        ui_toolbar_combo->setCurrentIndex(0);
        ui_hud_combo->setCurrentIndex(0);
    };
    connect(ui_preset_combo, &QComboBox::activated, this, reset_overrides);
    connect(reset, &QPushButton::clicked, this, reset_overrides);
    reset_desktop_component_selection();
#else
    Q_UNUSED(layout);
#endif
}

void settings_template_widget::reset_desktop_component_selection() {
    if (ui_preset_combo == nullptr) {
        return;
    }
    const auto value = load_desktop_ui_preferences();
    ui_preset_combo->setCurrentIndex(
        value.preset == desktop_ui_preset::quiet ? 1 : 0
    );
    ui_frame_combo->setCurrentIndex(
        !value.frame_override                                 ? 0
            : *value.frame_override == slot_frame_style::thin ? 2
                                                              : 1
    );
    ui_speed_combo->setCurrentIndex(
        !value.speed_readout_override       ? 0
            : *value.speed_readout_override ? 1
                                            : 2
    );
    ui_answer_combo->setCurrentIndex(
        !value.answer_override                                   ? 0
            : *value.answer_override == quiz_answer_style::chips ? 2
                                                                 : 1
    );
    ui_feedback_combo->setCurrentIndex(
        !value.feedback_override                                     ? 0
            : *value.feedback_override == quiz_feedback_style::stamp ? 2
                                                                     : 1
    );
    ui_actions_combo->setCurrentIndex(
        !value.actions_override                                   ? 0
            : *value.actions_override == slot_action_style::rail  ? 2
            : *value.actions_override == slot_action_style::pills ? 3
                                                                  : 1
    );
    ui_settings_combo->setCurrentIndex(
        !value.settings_override                                      ? 0
            : *value.settings_override == slot_settings_style::card   ? 2
            : *value.settings_override == slot_settings_style::drawer ? 3
            : *value.settings_override == slot_settings_style::sill   ? 4
                                                                      : 1
    );
    ui_toolbar_combo->setCurrentIndex(
        !value.toolbar_override                                         ? 0
            : *value.toolbar_override == desktop_toolbar_style::compact ? 2
                                                                        : 1
    );
    ui_hud_combo->setCurrentIndex(
        !value.hud_override                                         ? 0
            : *value.hud_override == desktop_hud_style::instruments ? 2
                                                                    : 1
    );
}

desktop_ui_preferences
settings_template_widget::selected_desktop_components() const {
    desktop_ui_preferences value;
    if (ui_preset_combo == nullptr) {
        return value;
    }
    value.preset = ui_preset_combo->currentIndex() == 1
        ? desktop_ui_preset::quiet
        : desktop_ui_preset::classic;
    if (ui_frame_combo->currentIndex() > 0) {
        value.frame_override = ui_frame_combo->currentIndex() == 2
            ? slot_frame_style::thin
            : slot_frame_style::classic;
    }
    if (ui_speed_combo->currentIndex() > 0) {
        value.speed_readout_override = ui_speed_combo->currentIndex() == 1;
    }
    if (ui_answer_combo->currentIndex() > 0) {
        value.answer_override = ui_answer_combo->currentIndex() == 2
            ? quiz_answer_style::chips
            : quiz_answer_style::numeric;
    }
    if (ui_feedback_combo->currentIndex() > 0) {
        value.feedback_override = ui_feedback_combo->currentIndex() == 2
            ? quiz_feedback_style::stamp
            : quiz_feedback_style::classic;
    }
    if (ui_actions_combo->currentIndex() > 0) {
        value.actions_override = ui_actions_combo->currentIndex() == 2
            ? slot_action_style::rail
            : ui_actions_combo->currentIndex() == 3
            ? slot_action_style::pills
            : slot_action_style::classic;
    }
    if (ui_settings_combo->currentIndex() > 0) {
        value.settings_override = ui_settings_combo->currentIndex() == 2
            ? slot_settings_style::card
            : ui_settings_combo->currentIndex() == 3
            ? slot_settings_style::drawer
            : ui_settings_combo->currentIndex() == 4
            ? slot_settings_style::sill
            : slot_settings_style::classic;
    }
    if (ui_toolbar_combo->currentIndex() > 0) {
        value.toolbar_override = ui_toolbar_combo->currentIndex() == 2
            ? desktop_toolbar_style::compact
            : desktop_toolbar_style::classic;
    }
    if (ui_hud_combo->currentIndex() > 0) {
        value.hud_override = ui_hud_combo->currentIndex() == 2
            ? desktop_hud_style::instruments
            : desktop_hud_style::classic;
    }
    return value;
}

bool settings_template_widget::apply_theme_settings() {
    if (theme_combo_box == nullptr || shared_state == nullptr) {
        return false;
    }

    if (ui_preset_combo != nullptr
        && !save_desktop_ui_preferences(selected_desktop_components())) {
        QMessageBox::warning(
            this, str_label("Desktop presentation"),
            str_label("Could not save the desktop presentation settings.")
        );
        return false;
    }

    const QColor base_color = settings_template_support::theme_color_from_label(
        theme_combo_box->currentText()
    );
    const QString selected_theme_source = selected_theme_source_id();
    const bool theme_changed = theme_settings::base_color() != base_color
        || card_sheet_source_path() != selected_theme_source;
    theme_settings::set_base_color(base_color);
    set_card_sheet_source_path(selected_theme_source);
    shared_state->set_table_color_index(theme_combo_box->currentIndex());
    trainer_preferences preferences = load_trainer_preferences();
    preferences.palette = theme_palette_registry::id_from_color(base_color);
    preferences.card_orientation
        = settings_template_support::orientation_from_index(
            orientation_combo_box != nullptr
                ? orientation_combo_box->currentIndex()
                : 0
        );
    save_trainer_preferences(preferences);
    if (table_widget != nullptr) {
        table_widget->set_card_orientation(preferences.card_orientation);
        if (theme_changed) {
            table_widget->apply_theme();
        }
        if (ui_preset_combo != nullptr) {
            const auto components = selected_desktop_components();
            table_widget->set_frame_style(components.frame());
            table_widget->set_action_style(components.actions());
            table_widget->set_settings_style(components.settings_surface());
            table_widget->set_quiz_presentation(
                components.answer(), components.feedback()
            );
        }
    }
    emit desktop_presentation_applied();
    return true;
}

void settings_template_widget::reset_theme_selection() {
    reset_desktop_component_selection();
    if (theme_combo_box == nullptr || shared_state == nullptr) {
        return;
    }

    theme_combo_box->setCurrentIndex(shared_state->table_color_index());
    if (orientation_combo_box != nullptr) {
        orientation_combo_box->setCurrentIndex(
            settings_template_support::orientation_index(
                load_trainer_preferences().card_orientation
            )
        );
    }
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
    clear_displayed_theme_entries();
    mark_preview_refresh_pending(true, true);
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
        QString entry_label
            = settings_template_support::format_key_label(it.key());
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
        notes_label->setText(
            settings_template_support::build_bullet_list(note_entries)
        );
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
        references_label->setText(
            settings_template_support::build_ieee_list(reference_entries)
        );
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
                new QTableWidgetItem(
                    settings_template_support::format_key_label(
                        label_keys.at(row)
                    )
                )
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
            const QString& label = metric_labels.at(row);
            const QString value = strategy.metrics.contains(label)
                ? QString::number(strategy.metrics.value(label))
                : str_label("-");
            metrics_table->setItem(
                row, 0,
                new QTableWidgetItem(
                    settings_template_support::format_key_label(label)
                )
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
        swatch->setPixmap(
            settings_template_support::palette_swatch_icon(color).pixmap(14, 14)
        );
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
    active_preview_element_ids.clear();
    warming_preview_element_ids.clear();
    prune_pending_theme_preview_queue();
    clear_displayed_theme_preview_entries(displayed_theme_preview_entries);
    const QSize card_size = settings_template_support::preview_card_size();
    theme_carousel->set_card_size(card_size);
    theme_carousel->set_card_accessible_name_provider(
        [suit_index](int card_index) {
            return card_label_from_index(suit_index * 13 + card_index);
        }
    );
    theme_carousel->set_card_provider(
        13,
        std::bind_front(
            &settings_template_widget::request_active_theme_preview_card, this
        )
    );
}

QPixmap settings_template_widget::request_active_theme_preview_card(
    int card_index, const QSize& size
) {
    return request_theme_preview_card(
        card_index, active_theme_preview_suit_index, size
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
        .render_scope
        = settings_template_support::theme_preview_generation_render_scope(
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
        = settings_template_support::settings_theme_preview_cache_service()
              .get_if_ready(key);
    return ready.has_value() && !ready->face_images.isEmpty()
        && !ready->face_images[0].isNull();
}

void settings_template_widget::retire_theme_preview_generation(
    const QString& source_id, int target_bucket_px, qint64 generation_id
) {
    if (source_id.isEmpty() || target_bucket_px <= 0 || generation_id <= 0) {
        return;
    }

    auto& service
        = settings_template_support::settings_theme_preview_cache_service();
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
    warming_preview_element_ids.clear();
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
        active_preview_element_ids.clear();
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
            warming_preview_element_ids.clear();
        }
        return;
    }

    begin_theme_preview_warming_generation(source_id, target_bucket_px);
}

bool settings_template_widget::try_cutover_theme_preview_generation() {
    if (warming_theme_preview_generation_id <= 0
        || warming_preview_element_ids.isEmpty()) {
        return false;
    }

    for (const QString& element_id :
         std::as_const(warming_preview_element_ids)) {
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
    active_preview_element_ids = warming_preview_element_ids;

    warming_theme_preview_source_id.clear();
    warming_theme_preview_bucket_px = 0;
    warming_theme_preview_generation_id = 0;
    warming_preview_element_ids.clear();

    clear_displayed_theme_preview_entries(displayed_theme_preview_entries);
    clear_displayed_theme_preview_entries(displayed_weights_preview_entries);

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
    QSet<raster_cache::entry_key>& tracked_keys
) {
    if (!size.isValid()) {
        return std::nullopt;
    }

    const QString element_id
        = settings_template_support::theme_preview_render_scope(
            card_index, suit_index
        );
    if (element_id.isEmpty()) {
        return std::nullopt;
    }

    const int short_px = std::max(1, std::min(size.width(), size.height()));
    ensure_theme_preview_generation(selected_theme_source_id(), short_px);
    const bool use_warming_generation = warming_theme_preview_generation_id > 0;
    if (use_warming_generation) {
        warming_preview_element_ids.insert(element_id);
        active_preview_element_ids.insert(element_id);
    } else {
        active_preview_element_ids.insert(element_id);
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

    auto& service
        = settings_template_support::settings_theme_preview_cache_service();
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
            note_displayed_theme_preview_entry(key, tracked_keys);
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
        .target_bucket_px = request_key.target_bucket_px,
    };

    const raster_cache::submit_outcome outcome = service.submit_request(req);
    const bool request_has_ready_image = outcome.ready_result.has_value()
        && !outcome.ready_result->face_images.isEmpty()
        && !outcome.ready_result->face_images[0].isNull();
    if (request_has_ready_image) {
        if (request_generation_id == active_theme_preview_generation_id) {
            note_displayed_theme_preview_entry(outcome.key, tracked_keys);
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
                note_displayed_theme_preview_entry(key, tracked_keys);
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
            note_displayed_theme_preview_entry(key, tracked_keys);
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
        card_index, suit_index, size, displayed_theme_preview_entries
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
        auto& service
            = settings_template_support::settings_theme_preview_cache_service();
        const raster_cache::finish_outcome finish
            = service.finish_active_request(family, key);
        if (finish.next_entry_to_start.has_value()) {
            enqueue_theme_preview_render(finish.next_entry_to_start.value());
        }
        return;
    }

    const settings_template_support::theme_preview_scope_parse parsed
        = settings_template_support::parse_theme_preview_render_scope(
            render_scope
        );
    if (!parsed.valid) {
        const raster_cache::family_key family {
            .name_space = key.name_space,
            .kind = key.kind,
            .source_id = key.source_id,
            .render_scope = key.render_scope,
        };
        auto& service
            = settings_template_support::settings_theme_preview_cache_service();
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
        QtConcurrent::run(
            &settings_template_widget::render_theme_preview_face_image,
            source_id, element_id, target_bucket_px
        )
    );
}

void settings_template_widget::on_theme_preview_render_finished() {
    if (!active_theme_preview_render_key.has_value()) {
        return;
    }

    const raster_cache::entry_key key = *active_theme_preview_render_key;
    active_theme_preview_render_key.reset();
    const settings_template_support::theme_preview_scope_parse parsed
        = settings_template_support::parse_theme_preview_render_scope(
            key.render_scope
        );

    auto& service
        = settings_template_support::settings_theme_preview_cache_service();
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

QImage settings_template_widget::render_theme_preview_face_image(
    const QString& source_id, const QString& element_id, int target_bucket_px
) {
    if (target_bucket_px <= 0) {
        return {};
    }

    const QSize raster_size(target_bucket_px, target_bucket_px);
    return rasterize_card_face_with_fallback(
        source_id, element_id, raster_size
    );
}

void settings_template_widget::on_theme_preview_cache_updated(
    const raster_cache::entry_key& key
) {
    if (key.name_space != raster_cache::cache_namespace::settings
        || key.kind != raster_cache::resource_kind::card_sheet_faces) {
        return;
    }
    const settings_template_support::theme_preview_scope_parse parsed
        = settings_template_support::parse_theme_preview_render_scope(
            key.render_scope
        );
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

void settings_template_widget::clear_displayed_theme_entries() {
    clear_displayed_theme_preview_entries(displayed_theme_preview_entries);
    clear_displayed_theme_preview_entries(displayed_weights_preview_entries);
}

void settings_template_widget::mark_preview_refresh_pending(
    bool theme_preview, bool weights_preview
) {
    if (theme_preview) {
        theme_preview_needs_refresh = true;
    }
    if (weights_preview) {
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

void settings_template_widget::update_weights_carousel(int suit_index) {
    if (weights_carousel == nullptr) {
        return;
    }
    active_weights_preview_suit_index = suit_index;
    active_preview_element_ids.clear();
    warming_preview_element_ids.clear();
    prune_pending_theme_preview_queue();
    clear_displayed_theme_preview_entries(displayed_weights_preview_entries);
    int strategy_index = strategy_list_widget != nullptr
        ? strategy_list_widget->currentRow()
        : -1;
    if (strategy_index < 0 || strategy_index >= strategies.size()) {
        return;
    }
    const QSize card_size = settings_template_support::preview_card_size();
    weights_carousel->set_card_size(card_size);
    weights_carousel->set_card_accessible_name_provider([this, suit_index](
                                                            int card_index
                                                        ) {
        QString card_name = card_label_from_index(suit_index * 13 + card_index);
        const int selected_strategy = strategy_list_widget != nullptr
            ? strategy_list_widget->currentRow()
            : -1;
        if (selected_strategy < 0 || selected_strategy >= strategies.size()
            || card_index < 0
            || card_index >= strategies.at(selected_strategy).weights.size()) {
            return card_name;
        }
        return QStringLiteral("%1, count value %2")
            .arg(
                card_name,
                QString::number(
                    strategies.at(selected_strategy).weights.at(card_index)
                )
            );
    });
    weights_carousel->set_card_provider(
        13,
        std::bind_front(
            &settings_template_widget::request_active_weighted_preview_card,
            this
        )
    );
}

QPixmap settings_template_widget::request_active_weighted_preview_card(
    int card_index, const QSize& size
) {
    return request_weighted_preview_card(
        card_index, active_weights_preview_suit_index, size
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
        card_index, suit_index, size, displayed_weights_preview_entries
    );
    if (!base_face.has_value()) {
        return {};
    }
    return settings_template_support::build_weighted_card_preview(
        *base_face, card_index, suit_index, size,
        strategies[strategy_index].weights
    );
}

void settings_template_widget::note_displayed_theme_preview_entry(
    const raster_cache::entry_key& key,
    QSet<raster_cache::entry_key>& tracked_keys
) {
    tracked_keys.insert(key);
}

void settings_template_widget::clear_displayed_theme_preview_entries(
    QSet<raster_cache::entry_key>& tracked_keys
) {
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

    const settings_template_support::theme_preview_scope_parse parsed
        = settings_template_support::parse_theme_preview_render_scope(
            key.render_scope
        );
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
            && active_preview_element_ids.contains(parsed.element_id);
    }
    if (parsed.generation_id == warming_theme_preview_generation_id) {
        return key.source_id == warming_theme_preview_source_id
            && key.target_bucket_px == warming_theme_preview_bucket_px
            && warming_preview_element_ids.contains(parsed.element_id);
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
