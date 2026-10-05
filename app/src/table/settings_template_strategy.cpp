#include "table/settings_template.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "image/card_preview_carousel.hpp"
#include "settings/preferences.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/table.hpp"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QtConcurrent/QtConcurrent>

#include "table/settings_template_support.hpp"

#include <QTableWidget>

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
    suit_combo_box->setObjectName(QStringLiteral("default_suit"));
    suit_combo_box->setAccessibleName(str_label("Default suit"));
    suit_combo_box->setToolTip(str_label(
        "Saved immediately for previews and Instrument lives. Does not change "
        "gameplay."
    ));
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
