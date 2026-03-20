#include "table/slot_settings.hpp"
#include "arch/android_ui.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "settings/strategy_data.hpp"
#include "table/infinity_spinbox.hpp"

#include <QHBoxLayout>
#include <QLabel>

slot_settings::slot_settings(BaseWidget* parent, bool include_info_button)
    : BaseWidget(parent)
    , infinity_check_box_internal(nullptr)
    , deck_count_spin_box_internal(nullptr)
    , strategy_combo_box_internal(nullptr)
    , show_card_indexing_internal(nullptr)
    , show_strategy_name_internal(nullptr)
    , training_check_box_internal(nullptr)
    , info_button_internal(nullptr) {
    preload_card_sheet();
    setup_ui(include_info_button);
}

slot_settings::~slot_settings() = default;

QSize slot_settings::minimum_settings_size() {
    static const QSize cached_size = build_minimum_settings_size();
    return cached_size;
}

QSize slot_settings::build_minimum_settings_size() {
    slot_settings temp_widget(nullptr, true);
    const int extra_margin = 16;
    QSize size = temp_widget.sizeHint();
    size.rwidth() += extra_margin;
    size.rheight() += extra_margin;
    return size;
}

void slot_settings::setup_ui(bool include_info_button) {
    auto settings_layout = new BaseVBoxLayout(this);
    settings_layout->setContentsMargins(8, 8, 8, 4);
    settings_layout->setSpacing(4);

    infinity_check_box_internal
        = new BaseCheckBox(str_label("Infinite deck"), this);
    infinity_check_box_internal->setToolTip(
        str_label("Repeat cards endlessly instead of finishing a deck")
    );
    android_ui::apply_check_box_style(infinity_check_box_internal);
    auto infinity_layout = new QHBoxLayout;
    infinity_layout->setContentsMargins(0, 0, 0, 0);
    infinity_layout->setSpacing(4);
    infinity_layout->addWidget(infinity_check_box_internal);
    infinity_layout->addStretch();
    settings_layout->addLayout(infinity_layout);

    auto decks_layout = new QHBoxLayout;
    decks_layout->setContentsMargins(0, 0, 0, 0);
    decks_layout->setSpacing(4);
    const QString decks_tooltip = str_label("Number of card decks in play");
    auto decks_label = new QLabel(str_label("Deck count"), this);
    decks_label->setToolTip(decks_tooltip);
    auto decks_spin_box_internal = new infinity_spinbox(this);
    decks_spin_box_internal->setMinimum(1);
    decks_spin_box_internal->setMaximum(16);
    decks_spin_box_internal->setSingleStep(1);
    decks_spin_box_internal->setValue(4);
    decks_spin_box_internal->setToolTip(decks_tooltip);
    android_ui::apply_spin_box_style(decks_spin_box_internal);
    deck_count_spin_box_internal = decks_spin_box_internal;
    decks_layout->addWidget(decks_label);
    decks_layout->addWidget(deck_count_spin_box_internal);
    settings_layout->addLayout(decks_layout);

    auto strategy_layout = new QHBoxLayout;
    strategy_layout->setContentsMargins(0, 0, 0, 0);
    strategy_layout->setSpacing(4);
    const QString strategy_tooltip = str_label("Strategy used for weights");
    auto strategy_label = new QLabel(str_label("Weight strategy"), this);
    strategy_label->setToolTip(strategy_tooltip);
    strategy_combo_box_internal = new BaseComboBox(this);
    strategy_combo_box_internal->setToolTip(strategy_tooltip);
    android_ui::apply_combo_box_style(strategy_combo_box_internal);
    const QVector<strategy_data> strategies = load_strategies();
    if (strategies.isEmpty()) {
        strategy_combo_box_internal->addItem(str_label("Default strategy"));
    } else {
        for (const strategy_data& strategy : strategies) {
            strategy_combo_box_internal->addItem(strategy.name);
        }
    }
    strategy_layout->addWidget(strategy_label);
    strategy_layout->addWidget(strategy_combo_box_internal, 1);

    if (include_info_button) {
        info_button_internal = android_ui::create_button(this);
        info_button_internal->setText(str_label("Info"));
        info_button_internal->setIcon(
            icon_loader::themed(
                { "info-symbolic", "dialog-information-symbolic",
                  "dialog-information", "help-about", "info" },
                QStyle::SP_MessageBoxInformation
            )
        );
        android_ui::apply_button_style(
            info_button_internal, android_button_profile::inline_info
        );
        strategy_layout->addWidget(info_button_internal);
    }

    settings_layout->addLayout(strategy_layout);

    show_card_indexing_internal
        = new BaseCheckBox(str_label("Show card indices"), this);
    show_card_indexing_internal->setToolTip(
        str_label("Display the card index number on each card")
    );
    android_ui::apply_check_box_style(show_card_indexing_internal);
    show_strategy_name_internal
        = new BaseCheckBox(str_label("Show strategy name"), this);
    show_strategy_name_internal->setToolTip(
        str_label("Display the current strategy name on cards")
    );
    android_ui::apply_check_box_style(show_strategy_name_internal);
    training_check_box_internal
        = new BaseCheckBox(str_label("Training mode"), this);
    training_check_box_internal->setToolTip(
        str_label("Practice without updating your score")
    );
    android_ui::apply_check_box_style(training_check_box_internal);
    training_check_box_internal->setObjectName(
        QStringLiteral("training_check_box")
    );

    settings_layout->addWidget(show_card_indexing_internal);
    settings_layout->addWidget(show_strategy_name_internal);
    settings_layout->addWidget(training_check_box_internal);
}

BaseCheckBox* slot_settings::infinity_check_box() const {
    return infinity_check_box_internal;
}

BaseSpinBox* slot_settings::deck_count_spin_box() const {
    return deck_count_spin_box_internal;
}

BaseComboBox* slot_settings::strategy_combo_box() const {
    return strategy_combo_box_internal;
}

BaseCheckBox* slot_settings::show_card_indexing() const {
    return show_card_indexing_internal;
}

BaseCheckBox* slot_settings::show_strategy_name() const {
    return show_strategy_name_internal;
}

BaseCheckBox* slot_settings::training_check_box() const {
    return training_check_box_internal;
}

BasePushButton* slot_settings::info_button() const {
    return info_button_internal;
}
