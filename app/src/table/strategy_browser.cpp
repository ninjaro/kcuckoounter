#include "table/strategy_browser.hpp"
#include "table/settings_template_support.hpp"
#include <shared_ui/qt/presentation.hpp>
#include <shared_ui/qt/status_badge.hpp>

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "settings/preferences.hpp"
#include "settings/theme_palette.hpp"
#include "table/table.hpp"

#include <QAbstractItemView>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace strategy_browser_support {
enum strategy_browser_role {
    slug_role = Qt::UserRole + 1,
    search_role,
    detail_role,
    number_role,
    level_role
};

// Count level as the catalogue's tags define it: the largest absolute tag.
// Point counts stop at level 4; larger tags (Thorp's ten count) are ratio
// counts with no level, shown as a dash.
int strategy_level(const strategy_data& strategy) {
    int level = 0;
    for (const int weight : strategy.weights)
        level = std::max(level, std::abs(weight));
    return level <= 4 ? level : -1;
}

// D-STRAT-1 list rows: the name, with its level as a quiet right-hand tag.
class kit_strategy_delegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(
        const QStyleOptionViewItem& option, const QModelIndex& index
    ) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), 30));
        return size;
    }

    void paint(
        QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index
    ) const override {
        QStyledItemDelegate::paint(painter, option, index);
        const int level = index.data(level_role).toInt();
        if (level == 0)
            return;
        const shared_ui::qt::material_tokens m = shared_ui::qt::material_for(
            *qobject_cast<QWidget*>(parent()), shared_ui::qt::material::felt
        );
        QFont tag(m.mono_family);
        tag.setPixelSize(10);
        painter->save();
        painter->setFont(tag);
        painter->setPen(
            option.state & QStyle::State_Selected ? m.accent : m.quiet
        );
        painter->drawText(
            option.rect.adjusted(0, 0, -14, 0),
            Qt::AlignRight | Qt::AlignVCenter,
            level > 0 ? QStringLiteral("L%1").arg(level)
                      : QStringLiteral("\u2013")
        );
        painter->restore();
    }
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

// D-STRAT-1 detail page on the felt: title, tags and provenance, the
// thirteen-rank tag strip as tiles, then description, notes and sources
// beside the catalogue metrics (values only; Phase 3.6 keeps them unscaled).
QString strategy_detail_kit_html(
    const strategy_data& strategy, const QWidget* context
) {
    const auto text
        = [](const QString& value) { return value.toHtmlEscaped(); };
    const shared_ui::qt::material_tokens m
        = shared_ui::qt::material_for(*context, shared_ui::qt::material::felt);
    const QString mono = m.mono_family;
    const auto label = [&](const QString& title) {
        return QStringLiteral(
                   "<p style=\"font-family:'%1'; font-size:10px; "
                   "letter-spacing:2px; color:%2; margin:14px 0 6px 0;\">%3"
                   "</p>"
        )
            .arg(mono, m.quiet.name(), text(title.toUpper()));
    };
    QStringList pills;
    const auto pill = [&](const QString& value, const QColor& fg,
                          const QColor& bg) {
        pills.append(QStringLiteral(
                         "<td bgcolor=\"%1\" style=\"font-family:'%2'; "
                         "font-size:10px; letter-spacing:1px; color:%3;\">"
                         "&nbsp;%4&nbsp;</td>"
        )
                         .arg(bg.name(), mono, fg.name(), text(value)));
    };
    const auto positive = shared_ui::qt::status_colors_for(
        shared_ui::qt::status_kind::positive, m
    );
    const auto warning = shared_ui::qt::status_colors_for(
        shared_ui::qt::status_kind::warning, m
    );
    const auto neutral = shared_ui::qt::status_colors_for(
        shared_ui::qt::status_kind::neutral, m
    );
    pill(
        strategy.balance ? str_label("BALANCED") : str_label("UNBALANCED"),
        positive.ink, positive.fill
    );
    if (strategy.ace_neutral)
        pill(str_label("ACE-NEUTRAL"), warning.ink, warning.fill);
    if (const int level = strategy_level(strategy); level > 0)
        pill(str_label("LEVEL %1").arg(level), neutral.ink, neutral.fill);
    QStringList games;
    for (QString game : strategy.games) {
        game.remove(QStringLiteral("blackjack_"));
        games.append(game.replace(QLatin1Char('_'), QLatin1Char(' ')));
    }
    QStringList provenance;
    if (!strategy.date.isEmpty())
        provenance.append(strategy.date);
    if (!strategy.authors.isEmpty())
        provenance.append(strategy.authors.join(QStringLiteral(" & ")));
    QStringList scope;
    if (strategy.min_decks > 0)
        scope.append(
            strategy.min_decks == 1
                ? str_label("min 1 deck")
                : str_label("min %1 decks").arg(strategy.min_decks)
        );
    if (!games.isEmpty())
        scope.append(games.join(QStringLiteral(", ")));
    QString html
        = QStringLiteral(
              "<table width=\"100%\" cellspacing=\"0\" "
              "cellpadding=\"0\"><tr><td valign=\"top\">"
              "<p style=\"font-size:26px; font-weight:600; color:%1; "
              "margin:0 0 8px 0;\">%2</p>"
              "<table cellspacing=\"0\" cellpadding=\"3\"><tr>%3"
              "</tr></table></td>"
              "<td align=\"right\" valign=\"top\" "
              "style=\"font-family:'%4'; font-size:11px; "
              "color:%5;\">%6<br>%7</td></tr></table>"
        )
              .arg(
                  m.card.name(), text(strategy.name),
                  pills.join(QStringLiteral("<td>&nbsp;</td>")), mono,
                  m.quiet.name(), text(provenance.join(QStringLiteral(" · "))),
                  text(scope.join(QStringLiteral(" · ")))
              );
    // Qt rich text gives an empty cell a full line, so a `height="1"` row on
    // a coloured table paints a band; a cell border is the kit's 1px rule.
    html += QStringLiteral(
                "<table width=\"100%\" cellspacing=\"0\" "
                "cellpadding=\"0\" style=\"margin-top:14px; "
                "border-collapse:collapse;\"><tr><td "
                "style=\"border-bottom:1px solid %1; "
                "font-size:1px;\"></td></tr></table>"
    )
                .arg(m.line.name());
    html += label(str_label("Tag per rank"));
    const QStringList ranks { QStringLiteral("A"), QStringLiteral("2"),
                              QStringLiteral("3"), QStringLiteral("4"),
                              QStringLiteral("5"), QStringLiteral("6"),
                              QStringLiteral("7"), QStringLiteral("8"),
                              QStringLiteral("9"), QStringLiteral("10"),
                              QStringLiteral("J"), QStringLiteral("Q"),
                              QStringLiteral("K") };
    html += QStringLiteral(
        "<table width=\"100%\" cellspacing=\"4\" "
        "cellpadding=\"7\"><tr>"
    );
    for (const auto& rank : ranks)
        html += QStringLiteral(
                    "<td align=\"center\" style=\"font-family:'Noto Serif', "
                    "Georgia, serif; font-size:16px; color:%1;\">%2</td>"
        )
                    .arg(m.text.name(), rank);
    html += QStringLiteral("</tr><tr>");
    for (qsizetype i = 0; i < ranks.size(); ++i) {
        if (i >= strategy.weights.size()) {
            html += QStringLiteral("<td align=\"center\">—</td>");
            continue;
        }
        const int weight = strategy.weights[i];
        const QString value = weight > 0 ? QStringLiteral("+%1").arg(weight)
            : weight < 0 ? QStringLiteral("\u2212%1").arg(-weight)
                         : QStringLiteral("0");
        html
            += QStringLiteral(
                   "<td align=\"center\" bgcolor=\"%1\" "
                   "style=\"font-family:'%2'; font-size:13px; "
                   "font-weight:600; color:%3;\">%4</td>"
            )
                   .arg(
                       (weight > 0       ? m.success_fill
                            : weight < 0 ? m.negative_fill
                                         : neutral.fill)
                           .name(),
                       mono, (weight == 0 ? m.muted : QColor(Qt::white)).name(),
                       value
                   );
    }
    html += QStringLiteral("</tr></table>");
    // Two columns: reading on the left, the catalogue's numbers on the right.
    QString left
        = QStringLiteral("<p style=\"font-size:13.5px; color:%1;\">%2</p>")
              .arg(m.text.name(), text(strategy.description));
    if (!strategy.unique_fields.isEmpty()) {
        left += label(str_label("Notes"));
        for (auto it = strategy.unique_fields.cbegin();
             it != strategy.unique_fields.cend(); ++it)
            left += QStringLiteral(
                        "<p style=\"font-size:12.5px; color:%1;\">"
                        "<b>%2:</b> %3</p>"
            )
                        .arg(
                            m.muted.name(),
                            text(
                                settings_template_support::format_key_label(
                                    it.key()
                                )
                            ),
                            text(it.value())
                        );
    }
    left += label(str_label("References"));
    if (strategy.references.isEmpty())
        left += QStringLiteral("<p style=\"color:%1;\">%2</p>")
                    .arg(m.muted.name(), text(str_label("Not provided")));
    int number = 0;
    for (const auto& ref : strategy.references) {
        QString line
            = QStringLiteral("[%1] %2").arg(++number).arg(text(ref.citation));
        const QUrl url(ref.url);
        if (url.isValid()
            && (url.scheme() == QStringLiteral("https")
                || url.scheme() == QStringLiteral("http")))
            line += QStringLiteral(" <a style=\"color:%1;\" href=\"%2\">%3</a>")
                        .arg(
                            m.accent.name(),
                            text(url.toString(QUrl::FullyEncoded)),
                            text(url.host())
                        );
        if (!ref.accessed.isEmpty())
            line += QStringLiteral(" (%1 %2)")
                        .arg(text(str_label("accessed")), text(ref.accessed));
        left += QStringLiteral(
                    "<p style=\"font-size:12.5px; color:%1; "
                    "margin:0 0 4px 0;\">%2</p>"
        )
                    .arg(m.muted.name(), line);
    }
    QString right = label(str_label("Effectiveness"))
        + QStringLiteral(
                        "<table width=\"100%\" cellspacing=\"0\" "
                        "cellpadding=\"5\">"
        );
    for (const auto& key : comparison_metric_keys()) {
        const auto value = strategy.metrics.constFind(key);
        const bool known
            = value != strategy.metrics.cend() && std::isfinite(value.value());
        const QString shown = !known ? str_label("not provided")
            : key == QStringLiteral("ease_of_use")
            ? QStringLiteral("%1<span style=\"color:%2;\">/10</span>")
                  .arg(QString::number(value.value()), m.quiet.name())
            : QString::number(value.value(), 'f', 2);
        right += QStringLiteral(
                     "<tr><td style=\"font-size:13px; color:%1; "
                     "border-bottom:1px solid %2;\">%3</td>"
                     "<td align=\"right\" style=\"font-family:'%4'; "
                     "font-size:13px; color:%5;\">%6</td></tr>"
        )
                     .arg(
                         m.text.name(), m.line.name(),
                         text(settings_template_support::format_key_label(key)),
                         mono, known ? m.card.name() : m.quiet.name(), shown
                     );
    }
    right += QStringLiteral("</table>");
    html += QStringLiteral(
                "<table width=\"100%\" cellspacing=\"0\" "
                "cellpadding=\"0\" style=\"margin-top:16px;\"><tr>"
                "<td width=\"56%\" valign=\"top\" "
                "style=\"padding-right:18px;\">%1</td><td width=\"44%\" "
                "valign=\"top\">%2</td></tr></table>"
    )
                .arg(left, right);
    return html;
}

QString
strategy_detail_html(const strategy_data& strategy, const QWidget* context) {
    if (shared_ui::qt::presentation_for(*context).value
        == shared_ui::qt::material::felt)
        return strategy_detail_kit_html(strategy, context);
    const auto text
        = [](const QString& value) { return value.toHtmlEscaped(); };
    const auto absent = str_label("Not provided");
    // D-STRAT-1 on the felt: gold section labels and a coloured weight strip.
    const bool felt = shared_ui::qt::presentation_for(*context).value
        == shared_ui::qt::material::felt;
    const shared_ui::qt::material_tokens material
        = shared_ui::qt::material_for(*context, shared_ui::qt::material::felt);
    const QString heading = felt
        ? QStringLiteral(
              "<h3 style='color:%1; font-family:\"%2\"; "
              "font-size:small; font-weight:600'>%3</h3>"
          )
              .arg(material.accent.name(), material.mono_family)
        : QStringLiteral("<h3>%1</h3>");
    const auto section = [&](const QString& title) {
        return felt ? heading.arg(text(title))
                    : QStringLiteral("<h3>%1</h3>").arg(text(title));
    };
    QString html = QStringLiteral("<h2>%1</h2><p>%2</p>")
                       .arg(text(strategy.name), text(strategy.description));
    html += section(str_label("Count value by rank"));
    html += felt ? QStringLiteral(
                       "<table border='0' cellspacing='3' "
                       "cellpadding='6'><tr>"
                   )
                 : QStringLiteral(
                       "<table border='1' cellspacing='0' "
                       "cellpadding='4'><tr>"
                   );
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
        QString cell = QStringLiteral("<td align='center'>");
        if (felt && i < strategy.weights.size()) {
            const int weight = strategy.weights[i];
            cell
                = QStringLiteral(
                      "<td align='center' bgcolor='%1' "
                      "style='color:%2; font-family:\"%3\"; "
                      "font-weight:600'>"
                )
                      .arg(
                          (weight > 0       ? material.success_fill
                               : weight < 0 ? material.negative_fill
                                            : material.inset)
                              .name(),
                          (weight == 0 ? material.muted : material.card).name(),
                          material.mono_family
                      );
        }
        html += cell
            + QStringLiteral("<p style='white-space:pre; margin:0'>%1</p></td>")
                  .arg(text(value));
    }
    html += QStringLiteral("</tr></table>") + section(str_label("General"))
        + QStringLiteral("<table cellpadding='3'>");
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
    html += QStringLiteral("</table>") + section(str_label("Metrics"))
        + QStringLiteral("<table cellpadding='3'>");
    for (const auto& key : comparison_metric_keys()) {
        const auto value = strategy.metrics.constFind(key);
        field(
            settings_template_support::format_key_label(key),
            value != strategy.metrics.cend() && std::isfinite(value.value())
                ? QString::number(value.value())
                : absent
        );
    }
    html += QStringLiteral("</table>")
        + section(str_label("Notes / Unique fields"));
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
    html += section(str_label("References"));
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
    shared_ui::qt::set_presentation_role(*search, QStringLiteral("search"));
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
    shared_ui::qt::set_presentation_role(*list, QStringLiteral("navigation"));
    list->setAccessibleName(str_label("Available strategies"));
    list->setMinimumWidth(90);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto* details = new QTextBrowser(split);
    details->setObjectName(QStringLiteral("strategy_browser_details"));
    shared_ui::qt::set_presentation_role(*details, QStringLiteral("document"));
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
            name->setData(strategy_detail_html(strategy, browser), detail_role);
            name->setData(strategy_level(strategy), level_role);
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
    if (shared_ui::qt::presentation_for(*browser).value
        == shared_ui::qt::material::felt) {
        // D-STRAT-1: search inside the list's column, level tags on rows.
        search_label->hide();
        search->setPlaceholderText(str_label("Search"));
        list->setItemDelegate(new kit_strategy_delegate(list));
        details->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        state->setObjectName(QStringLiteral("strategy_search_status"));
        state->setProperty("kit_caption", true);
        shared_ui::qt::set_presentation_role(*state, QStringLiteral("caption"));
        note->setProperty("kit_note", true);
        shared_ui::qt::set_presentation_role(*note, QStringLiteral("note"));
        split->setSizes({ 230, 620 });
    }
    select_slug(selected_slug);
    refresh_status();
    return browser;
}
