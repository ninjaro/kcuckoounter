#include "table/strategy_browser.hpp"
#include "table/settings_template_support.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "image/card_preview_carousel.hpp"
#include "settings/preferences.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/table.hpp"

#include <QAbstractItemView>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

#include <cmath>

namespace strategy_browser_support {
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
} // namespace strategy_browser_support

using namespace strategy_browser_support;

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
