#include "table/settings_template.hpp"
#include "table/settings_template_support.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "image/card_preview_carousel.hpp"
#include "settings/preferences.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/table.hpp"

#include <QImage>
#include <utility>

settings_shared_state::settings_shared_state(QObject* parent)
    : QObject(parent)
    , default_suit_value(load_default_suit_preference())
    , table_color_index_value(settings_template_support::theme_index_from_color(
          theme_settings::base_color()
      )) { }

void settings_shared_state::set_default_suit(int index) {
    if (index < 0 || index > 3 || index == default_suit_value) {
        return;
    }
    default_suit_value = index;
    (void)save_default_suit_preference(index);
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
    , theme_suit(0)
    , weights_suit(0)
    , preview_images(this)
    , preview_refresh_scheduled(false)
    , theme_preview_needs_refresh(false)
    , weights_preview_needs_refresh(false) {
    connect(
        &preview_images, &card_image_consumer::images_changed, this,
        &settings_template_widget::preview_images_changed, Qt::QueuedConnection
    );
    connect(
        &current_card_sheet_metadata(), &card_sheet_metadata::geometry_changed,
        this,
        [this] {
            update_theme_carousel(theme_suit);
            update_weights_carousel(weights_suit);
        },
        Qt::QueuedConnection
    );
    setup_ui(selected_strategy);
}

settings_template_widget::~settings_template_widget() = default;

void settings_template_widget::setup_ui(const QString& selected_strategy) {
    if (tab_kind == settings_tab_kind::appearance) {
        setup_appearance_ui();
        return;
    }
    setup_strategy_ui(selected_strategy);
}
