#include "table/slot_settings.hpp"
#include "arch/android_ui.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "table/infinity_spinbox.hpp"
#include <shared_ui/qt/presentation.hpp>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

slot_settings::slot_settings(
    BaseWidget* parent, bool include_info_button, layout style
)
    : BaseWidget(parent)
    , infinity_check_box_internal(nullptr)
    , deck_count_spin_box_internal(nullptr)
    , strategy_combo_box_internal(nullptr)
    , show_card_indexing_internal(nullptr)
    , show_strategy_name_internal(nullptr)
    , training_check_box_internal(nullptr)
    , info_button_internal(nullptr) {
    setup_ui(include_info_button, style);
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

void slot_settings::setup_ui(bool include_info_button, layout style) {
    const bool kit = style == layout::kit;
    auto settings_layout = new BaseVBoxLayout(this);
    settings_layout->setContentsMargins(
        kit ? QMargins() : QMargins(8, 8, 8, 4)
    );
    // The kit spaces rows 13px; 10px keeps the card inside a 1440×900
    // four-slot table between the piping's notch and legends.
    settings_layout->setSpacing(kit ? 10 : 4);

    infinity_check_box_internal
        = new BaseCheckBox(kit ? QString() : str_label("Infinite deck"), this);
    infinity_check_box_internal->setToolTip(
        str_label("Repeat cards endlessly instead of finishing a deck")
    );
    android_ui::apply_check_box_style(infinity_check_box_internal);
    auto infinity_layout = new QHBoxLayout;
    infinity_layout->setContentsMargins(0, 0, 0, 0);
    infinity_layout->setSpacing(4);
    if (kit) {
        // The switch carries no text; its label sits at the row's start.
        infinity_check_box_internal->setObjectName(
            QStringLiteral("kit_switch")
        );
        shared_ui::qt::set_presentation_role(
            *infinity_check_box_internal, QStringLiteral("switch")
        );
        infinity_check_box_internal->setAccessibleName(
            str_label("Infinite deck")
        );
        auto* infinity_label = new QLabel(str_label("Infinite deck"), this);
        infinity_label->setBuddy(infinity_check_box_internal);
        infinity_layout->addWidget(infinity_label);
        infinity_layout->addStretch();
        infinity_layout->addWidget(infinity_check_box_internal);
    } else {
        infinity_layout->addWidget(infinity_check_box_internal);
        infinity_layout->addStretch();
    }
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
    decks_label->setBuddy(deck_count_spin_box_internal);
    deck_count_spin_box_internal->setAccessibleName(str_label("Deck count"));
    decks_layout->addWidget(decks_label);
    if (kit) {
        // − n + stepper: the spin box's own buttons, drawn at either end.
        deck_count_spin_box_internal->setObjectName(
            QStringLiteral("kit_stepper")
        );
        shared_ui::qt::set_presentation_role(
            *deck_count_spin_box_internal, QStringLiteral("stepper")
        );
        deck_count_spin_box_internal->setAlignment(Qt::AlignCenter);
        deck_count_spin_box_internal->setFixedWidth(100);
        decks_layout->addStretch();
    }
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
    strategy_combo_box_internal->setAccessibleName(
        str_label("Weight strategy")
    );
    strategy_label->setBuddy(strategy_combo_box_internal);
    android_ui::apply_combo_box_style(strategy_combo_box_internal);
    const strategy_catalog& repository = strategy_repository();
    if (!repository.is_valid()) {
        strategy_combo_box_internal->addItem(
            str_label("Strategies unavailable")
        );
        strategy_combo_box_internal->setEnabled(false);
        strategy_combo_box_internal->setToolTip(
            str_label("Strategy data could not be loaded: %1")
                .arg(repository.diagnostic_summary())
        );
    } else {
        for (const strategy_data& strategy : repository.strategies) {
            strategy_combo_box_internal->addItem(strategy.name, strategy.slug);
            const int item_index = strategy_combo_box_internal->count() - 1;
            strategy_combo_box_internal->setItemData(
                item_index, strategy.id, Qt::UserRole + 1
            );
        }

        const trainer_preferences preferences = load_trainer_preferences();
        for (int index = 0; index < strategy_combo_box_internal->count();
             ++index) {
            if (strategy_combo_box_internal->itemData(index).toString()
                != preferences.preferred_strategy_slug) {
                continue;
            }
            strategy_combo_box_internal->setCurrentIndex(index);
            break;
        }
    }
    // Kit: the label above its row, 6px apart.
    auto* strategy_block = new QVBoxLayout;
    strategy_block->setContentsMargins(0, 0, 0, 0);
    strategy_block->setSpacing(6);
    if (kit) {
        strategy_layout->setSpacing(8);
        strategy_block->addWidget(strategy_label);
    } else {
        strategy_layout->addWidget(strategy_label);
    }
    strategy_layout->addWidget(strategy_combo_box_internal, 1);

    if (include_info_button) {
        info_button_internal = android_ui::create_button(this);
        info_button_internal->setText(str_label("Info"));
        info_button_internal->setAccessibleName(
            str_label("Strategy information")
        );
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
        if (kit) {
            info_button_internal->setObjectName(
                QStringLiteral("kit_info_button")
            );
            shared_ui::qt::set_presentation_role(
                *info_button_internal, QStringLiteral("info")
            );
            info_button_internal->setText(QStringLiteral("i"));
            info_button_internal->setIcon(QIcon());
            info_button_internal->setFixedSize(32, 32);
        }
        strategy_layout->addWidget(info_button_internal);
    }

    strategy_block->addLayout(strategy_layout);
    settings_layout->addLayout(strategy_block);

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

    if (kit) {
        auto* rule = new QFrame(this);
        rule->setObjectName(QStringLiteral("kit_rule"));
        shared_ui::qt::set_presentation_role(*rule, QStringLiteral("rule"));
        rule->setFixedHeight(1);
        settings_layout->addWidget(rule);
        auto* options = new QVBoxLayout;
        options->setContentsMargins(0, 0, 0, 0);
        options->setSpacing(7);
        options->addWidget(show_card_indexing_internal);
        options->addWidget(show_strategy_name_internal);
        options->addWidget(training_check_box_internal);
        settings_layout->addLayout(options);
        return;
    }
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
