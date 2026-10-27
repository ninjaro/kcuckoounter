#include "table/settings_template.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "image/card_preview_carousel.hpp"
#include "settings/preferences.hpp"
#include "settings/theme_palette.hpp"
#include "table/table.hpp"
#include <shared_ui/qt/presentation.hpp>

#include <QButtonGroup>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>

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
    const table_appearance_preferences preferences
        = load_table_appearance_preferences();
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

    update_preview_demands();
    mark_preview_refresh_pending(true, true);
    flush_preview_refresh();
}

bool settings_template_widget::commit_theme_settings(
    const shared_ui::qt::appearance_state* submitted
) {
    if (theme_combo_box == nullptr || shared_state == nullptr) {
        return false;
    }

    if (submitted != nullptr
        && !save_desktop_ui_preferences(
            desktop_ui_preferences::from_appearance(*submitted)
        ))
        return false;

    const QColor base_color = settings_template_support::theme_color_from_label(
        theme_combo_box->currentText()
    );
    const QString selected_theme_source = selected_theme_source_id();
    const table_appearance_preferences previous
        = load_table_appearance_preferences();
    const QColor previous_color = table_widget != nullptr
        ? shared_ui::qt::presentation_for(*table_widget).context.felt_surface
        : theme_palette_registry::option(previous.palette).base_color();
    const bool color_changed = previous_color != base_color;
    const bool source_changed
        = card_sheet_source_path() != selected_theme_source;
    const bool theme_changed = color_changed || source_changed;
    if (source_changed)
        set_card_sheet_source_path(selected_theme_source);
    shared_state->set_table_color_index(theme_combo_box->currentIndex());
    table_appearance_preferences preferences = previous;
    preferences.palette = theme_palette_registry::id_from_color(base_color);
    preferences.card_orientation
        = settings_template_support::orientation_from_index(
            orientation_combo_box != nullptr
                ? orientation_combo_box->currentIndex()
                : 0
        );
    save_table_appearance_preferences(previous, preferences);
    if (table_widget != nullptr) {
        auto presentation = shared_ui::qt::presentation_for(*table_widget);
        presentation.context.felt_surface = base_color;
        if (submitted != nullptr)
            presentation.value
                = desktop_ui_preferences::from_appearance(*submitted).chrome()
                    == desktop_chrome_style::felt
                ? shared_ui::qt::material::felt
                : shared_ui::qt::material::system;
        shared_ui::qt::apply_presentation(*table_widget, presentation);
        table_widget->set_card_orientation(preferences.card_orientation);
        if (theme_changed) {
            table_widget->apply_theme();
        }
        if (submitted != nullptr) {
            const auto components
                = desktop_ui_preferences::from_appearance(*submitted);
            table_widget->set_frame_style(components.frame());
            table_widget->set_action_style(components.actions());
            table_widget->set_settings_style(components.settings_surface());
            table_widget->set_quiz_presentation(
                components.answer(), components.feedback()
            );
            table_widget->refresh_material();
        }
    }
    emit desktop_presentation_applied();
    return true;
}

void settings_template_widget::reset_theme_selection() {
    if (appearance_model_)
        appearance_model_->revert();
    if (theme_combo_box == nullptr || shared_state == nullptr) {
        return;
    }

    theme_combo_box->setCurrentIndex(shared_state->table_color_index());
    if (orientation_combo_box != nullptr) {
        orientation_combo_box->setCurrentIndex(
            settings_template_support::orientation_index(
                load_table_appearance_preferences().card_orientation
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

    update_preview_demands();
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
