#include "table/settings_template.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "image/card_preview_carousel.hpp"
#include "settings/preferences.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/table.hpp"

#include <QButtonGroup>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QtConcurrent/QtConcurrent>

#include "table/settings_template_support.hpp"

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
    suit_combo_box->setObjectName(QStringLiteral("default_suit"));
    suit_combo_box->setAccessibleName(str_label("Default suit"));
    suit_combo_box->setToolTip(str_label(
        "Saved immediately for previews and Instrument lives. Does not change "
        "gameplay."
    ));
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

    prune_preview_queue();
    clear_displayed_theme_entries();
    mark_preview_refresh_pending(true, true);
    flush_preview_refresh();
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

    prune_preview_queue();
    clear_displayed_theme_entries();
    mark_preview_refresh_pending(true, true);
    flush_preview_refresh();
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

void settings_template_widget::update_suit_selection(int index) {
    if (suit_combo_box != nullptr && suit_combo_box->currentIndex() != index) {
        suit_combo_box->setCurrentIndex(index);
    }
    shared_state->set_default_suit(index);
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
