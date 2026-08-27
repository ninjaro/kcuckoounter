#include "table/table_slot.hpp"

#include "arch/android_ui.hpp"
#include "arch/asset_locator.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/card_widget.hpp"
#include "table/infinity_spinbox.hpp"
#include "table/settings_template.hpp"
#include "table/slot_settings.hpp"

#include <QBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QString>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>

static QVector<int> weights_for_strategy_slug(const QString& strategy_slug) {
    const strategy_catalog& repository = strategy_repository();
    if (!repository.is_valid()) {
        return {};
    }
    for (const strategy_data& strategy : repository.strategies) {
        if (strategy.slug == strategy_slug) {
            return strategy.weights;
        }
    }
    return {};
}

table_slot::table_slot(BaseWidget* parent)
    : BaseWidget(parent)
    , current_phase(slot_phase::paused)
    , card_widget_internal(new card_widget(this))
    , overlay_widget(nullptr)
    , settings_bar_widget(nullptr)
    , swap_bar_widget(nullptr)
    , overlay_layout(nullptr)
    , swap_layout(nullptr)
    , infinity_check_box(nullptr)
    , deck_count_spin_box(nullptr)
    , strategy_combo_box(nullptr)
    , info_button(nullptr)
    , show_card_indexing(nullptr)
    , show_strategy_name(nullptr)
    , training_check_box(nullptr)
    , swap_button(nullptr)
    , settings_button(nullptr)
    , copy_button(nullptr)
    , copy_all_button(nullptr)
    , quiz_bar_widget(nullptr)
    , quiz_layout(nullptr)
    , quiz_prompt_widget(nullptr)
    , quiz_feedback_widget(nullptr)
    , quiz_weight_label(nullptr)
    , quiz_spin_box(nullptr)
    , quiz_answer_button(nullptr)
    , quiz_skip_button(nullptr)
    , quiz_feedback_label(nullptr)
    , quiz_continue_button(nullptr)
    , settings_overlay_visible(true)
    , is_rotated(false)
    , use_dialog_for_settings(false)
    , deck_count_minimum(1)
    , quiz_prompt_active(false)
    , quiz_feedback_active(false)
    , quiz_continue_visible(false)
    , allow_skipping_flag(true)
    , last_quiz_input_value(0) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    card_widget_internal->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Expanding
    );
    QObject::connect(
        card_widget_internal, &card_widget::rasterization_busy_changed, this,
        &table_slot::rasterization_busy_changed
    );
    setup_overlay();
    card_widget_internal->show();
}

table_slot::~table_slot() = default;

void table_slot::set_swap_selected(bool selected) {
    if (card_widget_internal == nullptr) {
        return;
    }
    card_widget_internal->set_swap_selected(selected);
    update_action_button_state();
}

bool table_slot::swap_selected() const {
    return card_widget_internal != nullptr
        && card_widget_internal->swap_selected();
}

void table_slot::set_rotated(bool rotated) {
    if (is_rotated == rotated) {
        return;
    }

    is_rotated = rotated;
    update_overlay_layout();
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_slot_rotated(rotated);
    }
    updateGeometry();
    update();
}

void table_slot::set_allow_skipping(bool allow) {
    if (allow_skipping_flag == allow) {
        return;
    }
    allow_skipping_flag = allow;
    update_quiz_controls_visibility();
}

table_slot_session_state table_slot::capture_session_state() const {
    table_slot_session_state state;
    if (card_widget_internal != nullptr) {
        state.card = card_widget_internal->capture_session_state();
    }
    state.paused = current_phase == slot_phase::paused;
    state.infinity_enabled = is_infinity_enabled();
    state.deck_count
        = deck_count_spin_box != nullptr ? deck_count_spin_box->value() : 1;
    if (strategy_combo_box != nullptr) {
        state.strategy_slug = strategy_combo_box->currentData().toString();
        state.strategy_id
            = strategy_combo_box->currentData(Qt::UserRole + 1).toInt();
    }
    state.show_card_indexing
        = show_card_indexing != nullptr && show_card_indexing->isChecked();
    state.show_strategy_name
        = show_strategy_name != nullptr && show_strategy_name->isChecked();
    state.training_mode
        = training_check_box != nullptr && training_check_box->isChecked();
    state.quiz_prompt_active = quiz_prompt_active;
    state.quiz_feedback_active = quiz_feedback_active;
    state.quiz_continue_visible = quiz_continue_visible;
    state.quiz_input_value
        = quiz_spin_box != nullptr ? quiz_spin_box->value() : 0;
    if (quiz_feedback_label != nullptr) {
        state.quiz_feedback_text = quiz_feedback_label->text();
    }
    return state;
}

bool table_slot::restore_session_state(const table_slot_session_state& state) {
    if (!is_session_state_valid(state) || card_widget_internal == nullptr
        || infinity_check_box == nullptr || deck_count_spin_box == nullptr
        || strategy_combo_box == nullptr || show_card_indexing == nullptr
        || show_strategy_name == nullptr || training_check_box == nullptr
        || quiz_spin_box == nullptr
        || state.deck_count < deck_count_spin_box->minimum()
        || state.deck_count > deck_count_spin_box->maximum()) {
        return false;
    }

    int strategy_index = -1;
    for (int index = 0; index < strategy_combo_box->count(); ++index) {
        if (strategy_combo_box->itemData(index).toString()
            == state.strategy_slug) {
            strategy_index = index;
            break;
        }
    }
    if (strategy_index < 0) {
        for (int index = 0; index < strategy_combo_box->count(); ++index) {
            if (strategy_combo_box->itemData(index, Qt::UserRole + 1).toInt()
                == state.strategy_id) {
                strategy_index = index;
                break;
            }
        }
    }
    if (strategy_index < 0 && strategy_combo_box->isEnabled()) {
        return false;
    }

    const QSignalBlocker infinity_blocker(infinity_check_box);
    const QSignalBlocker deck_blocker(deck_count_spin_box);
    const QSignalBlocker strategy_blocker(strategy_combo_box);
    const QSignalBlocker indexing_blocker(show_card_indexing);
    const QSignalBlocker name_blocker(show_strategy_name);
    const QSignalBlocker training_blocker(training_check_box);
    infinity_check_box->setChecked(state.infinity_enabled);
    deck_count_spin_box->setValue(state.deck_count);
    if (strategy_index >= 0) {
        strategy_combo_box->setCurrentIndex(strategy_index);
    }
    show_card_indexing->setChecked(state.show_card_indexing);
    show_strategy_name->setChecked(state.show_strategy_name);
    training_check_box->setChecked(state.training_mode);
    update_infinity_state(infinity_check_box, deck_count_spin_box);

    if (!card_widget_internal->restore_session_state(state.card)) {
        return false;
    }
    sync_card_display_settings();

    current_phase = slot_phase::paused;
    quiz_prompt_active = state.quiz_prompt_active;
    quiz_feedback_active = state.quiz_feedback_active;
    quiz_continue_visible = state.quiz_continue_visible;
    last_quiz_input_value = state.quiz_input_value;
    quiz_spin_box->setValue(state.quiz_input_value);
    if (quiz_feedback_label != nullptr) {
        quiz_feedback_label->setText(state.quiz_feedback_text);
    }
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_hide_cards(quiz_prompt_active);
        card_widget_internal->set_table_marking_source(bundled_asset_path(
            quiz_prompt_active ? QStringLiteral("mad.svg")
                               : QStringLiteral("cuckoo.svg")
        ));
    }
    update_overlay_layout();
    update_quiz_controls_visibility();
    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->setVisible(quiz_prompt_active);
    }
    set_paused(true);
    return true;
}

bool table_slot::is_session_state_valid(const table_slot_session_state& state) {
    if (state.infinity_enabled != state.card.infinity_enabled
        || state.deck_count != state.card.decks_count || state.deck_count < 1
        || state.deck_count > 16 || state.quiz_input_value < -9999
        || state.quiz_input_value > 9999
        || state.quiz_feedback_text.size() > 2048
        || state.strategy_slug.size() > 128
        || (state.quiz_feedback_active && !state.quiz_prompt_active)
        || !card_widget::can_restore_session_state(state.card)) {
        return false;
    }

    const strategy_catalog& repository = strategy_repository();
    if (!repository.is_valid()) {
        return state.strategy_slug.isEmpty() && state.strategy_id == 0;
    }
    return std::ranges::any_of(
        repository.strategies, [&state](const strategy_data& strategy) {
            return (!state.strategy_slug.isEmpty()
                    && strategy.slug == state.strategy_slug)
                || (state.strategy_id > 0 && strategy.id == state.strategy_id);
        }
    );
}

void table_slot::start_quiz(int quiz_type_index) {
    int decks_count = 1;
    if (deck_count_spin_box != nullptr) {
        decks_count = deck_count_spin_box->value();
    }
    if (decks_count <= 0) {
        decks_count = 1;
    }

    const bool is_infinity = is_infinity_enabled();
    if (card_widget_internal != nullptr) {
        card_widget_internal->start_quiz(
            quiz_type_index, decks_count, is_infinity
        );
    }

    quiz_prompt_active = false;
    quiz_feedback_active = false;
    quiz_continue_visible = false;
    last_quiz_input_value = 0;
    update_overlay_layout();
    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->hide();
    }
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_hide_cards(false);
        card_widget_internal->set_table_marking_source(
            bundled_asset_path(QStringLiteral("cuckoo.svg"))
        );
    }

    sync_card_display_settings();
    set_paused(false);
}

void table_slot::clear_quiz() {
    if (card_widget_internal != nullptr) {
        card_widget_internal->clear_quiz();
        card_widget_internal->set_hide_cards(false);
        card_widget_internal->set_table_marking_source(
            bundled_asset_path(QStringLiteral("cuckoo.svg"))
        );
    }
    quiz_prompt_active = false;
    quiz_feedback_active = false;
    quiz_continue_visible = false;
    last_quiz_input_value = 0;
    update_overlay_layout();
    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->hide();
    }
    set_paused(true);
}

void table_slot::set_paused(bool paused) {
    const slot_phase new_phase
        = paused ? slot_phase::paused : slot_phase::running;
    current_phase = new_phase;

    const bool has_deck
        = card_widget_internal != nullptr && card_widget_internal->has_cards();
    const bool show_overlay = paused || !has_deck || quiz_prompt_active;

    if (overlay_widget != nullptr) {
        overlay_widget->setVisible(show_overlay);
    }

    if (!has_deck) {
        settings_overlay_visible = true;
    }

    if (settings_bar_widget != nullptr) {
        const bool can_show_settings_inline = show_overlay
            && settings_overlay_visible && !use_dialog_for_settings
            && !quiz_prompt_active;
        settings_bar_widget->setVisible(can_show_settings_inline);
    }

    if (swap_bar_widget != nullptr) {
        swap_bar_widget->setVisible(show_overlay && !quiz_prompt_active);
    }

    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->setVisible(show_overlay && quiz_prompt_active);
    }

    if (swap_button != nullptr) {
        swap_button->setEnabled(show_overlay);
    }

    if (settings_button != nullptr) {
        const bool running_with_deck
            = (current_phase == slot_phase::running) && has_deck;
        settings_button->setText(
            running_with_deck ? str_label("Info") : str_label("Details")
        );
    }
    update_settings_button_state();

    if (card_widget_internal != nullptr) {
        card_widget_internal->set_running(current_phase == slot_phase::running);
    }
    if (deck_count_spin_box != nullptr) {
        if (paused) {
            if (has_deck) {
                const int current_value = deck_count_spin_box->value();
                deck_count_spin_box->setMinimum(
                    std::max(deck_count_minimum, current_value)
                );
            } else {
                deck_count_spin_box->setMinimum(deck_count_minimum);
            }
        } else {
            deck_count_spin_box->setMinimum(deck_count_minimum);
        }
    }
    update_lockable_settings();
    update();
}

void table_slot::advance_card() {
    if (current_phase != slot_phase::running || quiz_prompt_active) {
        return;
    }

    if (card_widget_internal != nullptr) {
        card_widget_internal->advance_card();
        const int current_position = card_widget_internal->current_position();
        if (current_position >= 0 && (current_position + 1) % 30 == 0) {
            show_quiz_prompt();
        }
    }
}

void table_slot::trigger_highlight(int duration_ms) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->trigger_highlight(duration_ms);
    }
}

void table_slot::tick_highlight(int delta_ms) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->tick_highlight(delta_ms);
    }
}

void table_slot::prepare_card_faces() {
    if (card_widget_internal != nullptr) {
        card_widget_internal->prepare_card_faces();
    }
}

int table_slot::card_face_need_short_px() const {
    if (card_widget_internal == nullptr) {
        return 0;
    }

    return card_widget_internal->card_face_target_short_px();
}

void table_slot::set_shared_card_faces(
    const QVector<QImage>& face_images, const QSize& raster_size
) {
    if (card_widget_internal == nullptr) {
        return;
    }

    card_widget_internal->set_shared_card_faces(face_images, raster_size);
}

void table_slot::clear_shared_card_faces() {
    if (card_widget_internal == nullptr) {
        return;
    }

    card_widget_internal->clear_shared_card_faces();
}

bool table_slot::has_shared_card_faces() const {
    if (card_widget_internal == nullptr) {
        return false;
    }

    return card_widget_internal->has_shared_card_faces();
}

void table_slot::set_shared_card_faces_mode(bool enabled) {
    if (card_widget_internal == nullptr) {
        return;
    }

    card_widget_internal->set_shared_card_faces_mode(enabled);
}

void table_slot::apply_theme() {
    if (card_widget_internal != nullptr) {
        card_widget_internal->sync_card_sheet_source();
        card_widget_internal->update();
    }
    update_overlay_palette();
    update();
}

void table_slot::setup_overlay() {
    overlay_widget = new BaseWidget(this);

    overlay_layout = new QBoxLayout(QBoxLayout::TopToBottom, overlay_widget);
    overlay_layout->setContentsMargins(2, 2, 2, 2);
    overlay_layout->setSpacing(2);

    auto settings_frame = new QFrame(overlay_widget);
    settings_frame->setObjectName(QStringLiteral("settings_bar_frame"));
    auto settings_frame_layout = new QVBoxLayout(settings_frame);
    settings_frame_layout->setContentsMargins(2, 2, 2, 2);
    settings_frame_layout->setSpacing(0);

    auto settings_widget_internal = new slot_settings(settings_frame, true);
    settings_frame_layout->addWidget(settings_widget_internal);
    settings_bar_widget = settings_frame;
    settings_bar_widget->setSizePolicy(
        QSizePolicy::Minimum, QSizePolicy::Fixed
    );

    infinity_check_box = settings_widget_internal->infinity_check_box();
    deck_count_spin_box = settings_widget_internal->deck_count_spin_box();
    if (deck_count_spin_box != nullptr) {
        deck_count_minimum = deck_count_spin_box->minimum();
    }
    strategy_combo_box = settings_widget_internal->strategy_combo_box();
    show_card_indexing = settings_widget_internal->show_card_indexing();
    show_strategy_name = settings_widget_internal->show_strategy_name();
    training_check_box = settings_widget_internal->training_check_box();
    info_button = settings_widget_internal->info_button();

    swap_bar_widget = new QFrame(overlay_widget);
    swap_bar_widget->setObjectName(QStringLiteral("swap_bar_frame"));
    swap_bar_widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    swap_layout = new QBoxLayout(QBoxLayout::LeftToRight, swap_bar_widget);
    swap_layout->setContentsMargins(4, 4, 4, 4);
    swap_layout->setSpacing(2);

    quiz_bar_widget = new QFrame(overlay_widget);
    quiz_bar_widget->setObjectName(QStringLiteral("quiz_bar_frame"));
    quiz_bar_widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    quiz_layout = new QStackedLayout(quiz_bar_widget);
    quiz_layout->setContentsMargins(4, 4, 4, 4);
    quiz_layout->setSpacing(2);

    settings_button = android_ui::create_button(swap_bar_widget);
    settings_button->setText(str_label("Details"));
    settings_button->setCheckable(true);
    swap_button = android_ui::create_button(swap_bar_widget);
    swap_button->setText(str_label("Swap"));
    swap_button->setCheckable(true);
    copy_button = android_ui::create_button(swap_bar_widget);
    copy_button->setText(str_label("Copy"));
    copy_button->setCheckable(true);
    copy_all_button = android_ui::create_button(swap_bar_widget);
    copy_all_button->setText(str_label("Copy all"));

    quiz_prompt_widget = new BaseWidget(quiz_bar_widget);
    auto quiz_prompt_layout = new QVBoxLayout(quiz_prompt_widget);
    quiz_prompt_layout->setContentsMargins(0, 0, 0, 0);
    quiz_prompt_layout->setSpacing(2);
    auto quiz_prompt_row_layout = new QHBoxLayout();
    quiz_prompt_row_layout->setContentsMargins(0, 0, 0, 0);
    quiz_prompt_row_layout->setSpacing(2);
    auto quiz_prompt_button_layout = new QHBoxLayout();
    quiz_prompt_button_layout->setContentsMargins(0, 0, 0, 0);
    quiz_prompt_button_layout->setSpacing(2);

    quiz_feedback_widget = new BaseWidget(quiz_bar_widget);
    auto quiz_feedback_layout = new QVBoxLayout(quiz_feedback_widget);
    quiz_feedback_layout->setContentsMargins(0, 0, 0, 0);
    quiz_feedback_layout->setSpacing(2);

    quiz_weight_label = new QLabel(str_label("Weight"), quiz_bar_widget);
    quiz_spin_box = new BaseSpinBox(quiz_bar_widget);
    quiz_weight_label->setBuddy(quiz_spin_box);
    quiz_spin_box->setAccessibleName(str_label("Card-count answer"));
    quiz_spin_box->setObjectName(QStringLiteral("quiz_spin_box"));
    quiz_spin_box->setRange(-9999, 9999);
    quiz_spin_box->setValue(0);
    quiz_spin_box->setToolTip(
        str_label("Enter the total weight for the current cards")
    );
    android_ui::apply_spin_box_style(quiz_spin_box);
    quiz_answer_button = android_ui::create_button(quiz_bar_widget);
    quiz_answer_button->setObjectName(QStringLiteral("quiz_answer_button"));
    quiz_answer_button->setText(str_label("Check"));
    quiz_answer_button->setToolTip(str_label("Check your answer"));
    quiz_skip_button = android_ui::create_button(quiz_bar_widget);
    quiz_skip_button->setObjectName(QStringLiteral("quiz_skip_button"));
    quiz_skip_button->setText(str_label("Skip"));
    quiz_skip_button->setToolTip(str_label("Skip this question"));
    quiz_feedback_label = new QLabel(quiz_bar_widget);
    quiz_feedback_label->setAccessibleName(str_label("Answer feedback"));
    quiz_feedback_label->setObjectName(QStringLiteral("quiz_feedback_label"));
    quiz_feedback_label->setWordWrap(true);
    quiz_feedback_label->setVisible(false);
    quiz_continue_button = android_ui::create_button(quiz_bar_widget);
    quiz_continue_button->setObjectName(QStringLiteral("quiz_continue_button"));
    quiz_continue_button->setText(str_label("Continue"));
    quiz_continue_button->setToolTip(str_label("Continue to the next card"));
    quiz_continue_button->setVisible(false);

    settings_button->setIcon(
        icon_loader::themed(
            { "document-properties", "view-list-details", "document-preview",
              "dialog-information" },
            QStyle::SP_FileDialogContentsView
        )
    );
    swap_button->setIcon(
        icon_loader::themed(
            { "view-refresh", "reload", "object-rotate-right" },
            QStyle::SP_BrowserReload
        )
    );
    copy_button->setIcon(
        icon_loader::themed(
            { "edit-copy", "copy", "document-duplicate" },
            QStyle::SP_FileDialogNewFolder
        )
    );
    copy_all_button->setIcon(
        icon_loader::themed(
            { "edit-copy", "copy", "document-duplicate" },
            QStyle::SP_FileDialogNewFolder
        )
    );
    settings_button->setToolTip(str_label("Show card details and settings"));
    swap_button->setToolTip(str_label("Swap this slot with another"));
    copy_button->setToolTip(str_label("Copy settings from this slot"));
    copy_all_button->setToolTip(str_label("Copy settings to all slots"));
    android_ui::apply_button_style(
        settings_button, android_button_profile::compact_action
    );
    android_ui::apply_button_style(
        swap_button, android_button_profile::compact_action
    );
    android_ui::apply_button_style(
        copy_button, android_button_profile::compact_action
    );
    android_ui::apply_button_style(
        copy_all_button, android_button_profile::compact_action
    );
    android_ui::apply_button_style(
        quiz_answer_button, android_button_profile::quiz_action
    );
    android_ui::apply_button_style(
        quiz_skip_button, android_button_profile::quiz_action
    );
    android_ui::apply_button_style(
        quiz_continue_button, android_button_profile::quiz_action
    );

    swap_layout->addWidget(settings_button);
    swap_layout->addWidget(swap_button);
    swap_layout->addWidget(copy_button);
    swap_layout->addWidget(copy_all_button);
    swap_layout->addStretch();

    quiz_prompt_row_layout->addWidget(quiz_weight_label);
    quiz_prompt_row_layout->addWidget(quiz_spin_box);
    quiz_prompt_row_layout->addStretch();
    quiz_prompt_button_layout->addStretch();
    quiz_prompt_button_layout->addWidget(quiz_answer_button);
    quiz_prompt_button_layout->addWidget(quiz_skip_button);
    quiz_prompt_button_layout->addStretch();
    quiz_prompt_layout->addLayout(quiz_prompt_row_layout);
    quiz_prompt_layout->addLayout(quiz_prompt_button_layout);

    quiz_feedback_layout->addWidget(quiz_feedback_label);
    quiz_feedback_layout->addWidget(quiz_continue_button, 0, Qt::AlignCenter);

    quiz_layout->addWidget(quiz_prompt_widget);
    quiz_layout->addWidget(quiz_feedback_widget);

    update_overlay_layout();

    update_overlay_palette();
    update_quiz_controls_visibility();

    overlay_widget->show();
    settings_bar_widget->show();
    swap_bar_widget->show();
    quiz_bar_widget->hide();
    overlay_widget->raise();

    QObject::connect(
        infinity_check_box, &BaseCheckBox::toggled, this,
        &table_slot::on_infinity_toggled
    );
    QObject::connect(
        swap_button, &BasePushButton::clicked, this,
        &table_slot::on_swap_button_clicked
    );
    QObject::connect(
        settings_button, &BasePushButton::clicked, this,
        &table_slot::on_settings_button_clicked
    );
    if (info_button != nullptr) {
        QObject::connect(
            info_button, &BasePushButton::clicked, this,
            &table_slot::on_info_button_clicked
        );
    }
    QObject::connect(
        copy_button, &BasePushButton::clicked, this,
        &table_slot::on_copy_button_clicked
    );
    QObject::connect(
        copy_all_button, &BasePushButton::clicked, this,
        &table_slot::on_copy_all_button_clicked
    );
    QObject::connect(
        quiz_answer_button, &BasePushButton::clicked, this,
        &table_slot::on_quiz_answer_button_clicked
    );
    QObject::connect(
        quiz_skip_button, &BasePushButton::clicked, this,
        &table_slot::on_quiz_skip_button_clicked
    );
    QObject::connect(
        quiz_continue_button, &BasePushButton::clicked, this,
        &table_slot::on_quiz_continue_button_clicked
    );
    QObject::connect(
        show_card_indexing, &BaseCheckBox::toggled, this,
        &table_slot::on_show_card_indexing_toggled
    );
    QObject::connect(
        show_strategy_name, &BaseCheckBox::toggled, this,
        &table_slot::on_show_strategy_name_toggled
    );
    QObject::connect(
        training_check_box, &BaseCheckBox::toggled, this,
        &table_slot::on_training_check_box_toggled
    );
    QObject::connect(
        strategy_combo_box, &BaseComboBox::currentTextChanged, this,
        &table_slot::on_strategy_name_changed
    );

    sync_card_display_settings();
    update_settings_button_state();
}

void table_slot::update_overlay_palette() {
    const QColor base_color = theme_settings::base_color();
    const theme_palette_option& palette_option = theme_palette_registry::option(
        theme_palette_registry::id_from_color(base_color)
    );
    const QColor panel_color = palette_option.panel_color();
    const QColor accent_color = theme_settings::slot_border_color();
    const QColor input_color = palette_option.input_color();
    apply_palette_to_widget(
        settings_bar_widget, panel_color, accent_color, input_color
    );
    apply_palette_to_widget(
        swap_bar_widget, panel_color, accent_color, input_color
    );
    apply_palette_to_widget(
        quiz_bar_widget, panel_color, accent_color, input_color
    );
}

void table_slot::apply_palette_to_widget(
    BaseWidget* widget, const QColor& panel_color, const QColor& accent_color,
    const QColor& input_color
) {
    if (widget == nullptr) {
        return;
    }

    const QString accent_hex = accent_color.name();
    const QString input_hex = input_color.name();
    const QString panel_hex = panel_color.name(QColor::HexArgb);

    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, panel_color);
    palette.setColor(QPalette::WindowText, accent_color);
    palette.setColor(QPalette::ButtonText, accent_color);
    palette.setColor(QPalette::Text, accent_color);
    palette.setColor(QPalette::Button, panel_color);
    palette.setColor(QPalette::Base, input_color);
    widget->setStyleSheet(
        QString(
            "QLabel { color: %1; background-color: transparent; }"
            "QCheckBox, QComboBox, QSpinBox { color: %1; }"
            "QComboBox, QSpinBox { background-color: %2; }"
            "QPushButton, QToolButton {"
            " background-color: %3;"
            " color: %1;"
            " border: 1px solid %1;"
            " border-radius: 4px;"
            " padding: 2px 6px;"
            "}"
            "QPushButton:checked, QToolButton:checked {"
            " background-color: %1;"
            " color: %3;"
            "}"
            "QFrame#settings_bar_frame, QFrame#swap_bar_frame,"
            " QFrame#quiz_bar_frame {"
            " background-color: %3;"
            " border: 1px solid %1;"
            " border-radius: 6px;"
            "}"
            "QFrame { background-color: %3; }"
        )
            .arg(accent_hex, input_hex, panel_hex)
    );
    widget->setPalette(palette);
    widget->setAutoFillBackground(true);
}

void table_slot::update_overlay_layout() {
    if (overlay_layout == nullptr || settings_bar_widget == nullptr
        || swap_bar_widget == nullptr || quiz_bar_widget == nullptr) {
        return;
    }

    while (overlay_layout->count() > 0) {
        delete overlay_layout->takeAt(0);
    }

    overlay_layout->setDirection(
        is_rotated ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom
    );

    if (quiz_prompt_active) {
        overlay_layout->addStretch();
        overlay_layout->addWidget(quiz_bar_widget, 0, Qt::AlignCenter);
        overlay_layout->addStretch();
    } else {
        overlay_layout->addWidget(settings_bar_widget, 0, Qt::AlignCenter);
        overlay_layout->addStretch();
        overlay_layout->addWidget(swap_bar_widget, 0, Qt::AlignCenter);
        overlay_layout->addWidget(quiz_bar_widget, 0, Qt::AlignCenter);
    }

    if (swap_layout != nullptr) {
        swap_layout->setDirection(
            is_rotated ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight
        );
    }
}

void table_slot::update_settings_button_state(bool dialog_open) {
    if (settings_button == nullptr) {
        return;
    }
    if (use_dialog_for_settings) {
        settings_button->setChecked(dialog_open);
        return;
    }
    settings_button->setChecked(settings_overlay_visible);
}

void table_slot::update_lockable_settings() {
    const bool has_deck
        = card_widget_internal != nullptr && card_widget_internal->has_cards();
    const bool paused = current_phase == slot_phase::paused;
    const bool lock_infinity = has_deck && paused
        && infinity_check_box != nullptr && infinity_check_box->isChecked();
    const bool lock_training = has_deck && paused
        && training_check_box != nullptr && training_check_box->isChecked();
    if (infinity_check_box != nullptr) {
        infinity_check_box->setEnabled(!lock_infinity);
    }
    if (training_check_box != nullptr) {
        training_check_box->setEnabled(!lock_training);
    }
}

void table_slot::resizeEvent(QResizeEvent* event) {
    BaseWidget::resizeEvent(event);

    if (card_widget_internal != nullptr) {
        card_widget_internal->setGeometry(rect());
    }

    if (overlay_widget != nullptr) {
        overlay_widget->setGeometry(rect());
    }

    if (settings_bar_widget == nullptr) {
        return;
    }

    const QSize settings_size_needed = slot_settings::minimum_settings_size();
    int width_needed = settings_size_needed.width();
    int height_needed = settings_size_needed.height();
    if (swap_bar_widget != nullptr) {
        height_needed += swap_bar_widget->sizeHint().height();
    }

    int available_width = width();
    int available_height = height();

    if (is_rotated && settings_bar_widget != nullptr) {
        const int max_settings_width
            = std::max(1, static_cast<int>(available_width * 0.33));
        settings_bar_widget->setMaximumWidth(max_settings_width);
    } else if (settings_bar_widget != nullptr) {
        settings_bar_widget->setMaximumWidth(QWIDGETSIZE_MAX);
    }

    bool new_use_dialog_for_settings
        = available_width < width_needed || available_height < height_needed;

    if (new_use_dialog_for_settings == use_dialog_for_settings) {
        return;
    }

    use_dialog_for_settings = new_use_dialog_for_settings;

    if (use_dialog_for_settings) {
        settings_overlay_visible = false;
        settings_bar_widget->hide();
    } else {
        if (overlay_widget != nullptr && overlay_widget->isVisible()) {
            settings_bar_widget->setVisible(settings_overlay_visible);
        }
    }
    update_settings_button_state();
}

void table_slot::on_infinity_toggled(bool checked) {
    Q_UNUSED(checked);

    update_infinity_state(infinity_check_box, deck_count_spin_box);

    const bool is_infinity = is_infinity_enabled();
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_infinity(is_infinity);
    }
    update_lockable_settings();
}

void table_slot::on_swap_button_clicked() { emit swap_clicked(this); }

void table_slot::on_settings_button_clicked() {
    if (use_dialog_for_settings) {
        if (infinity_check_box == nullptr || deck_count_spin_box == nullptr
            || strategy_combo_box == nullptr || show_card_indexing == nullptr
            || show_strategy_name == nullptr || training_check_box == nullptr) {
            return;
        }

        emit dialog_opened();

        QDialog dialog(this);
        dialog.setWindowTitle(str_label("Card details"));
        update_settings_button_state(true);

        auto dialog_layout = new QVBoxLayout(&dialog);
        auto dialog_settings_widget = new slot_settings(&dialog, false);
        dialog_layout->addWidget(dialog_settings_widget);

        auto dialog_infinity_check_box
            = dialog_settings_widget->infinity_check_box();
        auto dialog_deck_count_spin_box
            = dialog_settings_widget->deck_count_spin_box();
        auto dialog_strategy_combo_box
            = dialog_settings_widget->strategy_combo_box();
        auto dialog_show_card_indexing
            = dialog_settings_widget->show_card_indexing();
        auto dialog_show_strategy_name
            = dialog_settings_widget->show_strategy_name();
        auto dialog_training_check_box
            = dialog_settings_widget->training_check_box();

        dialog_infinity_check_box->setChecked(infinity_check_box->isChecked());
        const bool has_deck = card_widget_internal != nullptr
            && card_widget_internal->has_cards();
        const bool paused = current_phase == slot_phase::paused;

        dialog_deck_count_spin_box->setMinimum(deck_count_spin_box->minimum());
        dialog_deck_count_spin_box->setMaximum(deck_count_spin_box->maximum());
        dialog_deck_count_spin_box->setSingleStep(
            deck_count_spin_box->singleStep()
        );
        dialog_deck_count_spin_box->setValue(deck_count_spin_box->value());

        dialog_strategy_combo_box->clear();
        int strategies_count = strategy_combo_box->count();
        for (int i = 0; i < strategies_count; ++i) {
            dialog_strategy_combo_box->addItem(strategy_combo_box->itemText(i));
        }
        dialog_strategy_combo_box->setCurrentIndex(
            strategy_combo_box->currentIndex()
        );

        dialog_show_card_indexing->setChecked(show_card_indexing->isChecked());
        dialog_show_strategy_name->setChecked(show_strategy_name->isChecked());
        dialog_training_check_box->setChecked(training_check_box->isChecked());
        const bool lock_infinity
            = has_deck && paused && dialog_infinity_check_box->isChecked();
        const bool lock_training = has_deck && paused
            && dialog_training_check_box != nullptr
            && dialog_training_check_box->isChecked();
        dialog_infinity_check_box->setEnabled(!lock_infinity);
        if (dialog_training_check_box != nullptr) {
            dialog_training_check_box->setEnabled(!lock_training);
        }

        update_infinity_state(
            dialog_infinity_check_box, dialog_deck_count_spin_box
        );
        QObject::connect(
            dialog_infinity_check_box, &BaseCheckBox::toggled, this,
            [dialog_infinity_check_box, dialog_deck_count_spin_box](bool) {
                update_infinity_state(
                    dialog_infinity_check_box, dialog_deck_count_spin_box
                );
            }
        );

        auto button_box = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog
        );
        QObject::connect(
            button_box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept
        );
        QObject::connect(
            button_box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject
        );
        dialog_layout->addWidget(button_box);

#if defined(Q_OS_ANDROID)
        dialog.setWindowState(Qt::WindowMaximized);
#endif
        if (dialog.exec() == QDialog::Accepted) {
            infinity_check_box->setChecked(
                dialog_infinity_check_box->isChecked()
            );
            deck_count_spin_box->setValue(dialog_deck_count_spin_box->value());
            strategy_combo_box->setCurrentIndex(
                dialog_strategy_combo_box->currentIndex()
            );
            show_card_indexing->setChecked(
                dialog_show_card_indexing->isChecked()
            );
            show_strategy_name->setChecked(
                dialog_show_strategy_name->isChecked()
            );
            training_check_box->setChecked(
                dialog_training_check_box->isChecked()
            );
            sync_card_display_settings();
        }

        update_settings_button_state(false);
        return;
    }

    if (settings_bar_widget == nullptr) {
        return;
    }

    settings_overlay_visible = !settings_overlay_visible;
    settings_bar_widget->setVisible(settings_overlay_visible);
    update_settings_button_state();
}

void table_slot::on_info_button_clicked() {
    const QString strategy_name = strategy_combo_box != nullptr
        ? strategy_combo_box->currentText()
        : QString();
    show_template_dialog(str_label("Strategy details"), strategy_name);
}

void table_slot::on_copy_button_clicked() { emit copy_clicked(this); }

void table_slot::on_copy_all_button_clicked() { emit copy_all_clicked(this); }

void table_slot::on_quiz_answer_button_clicked() {
    if (!quiz_prompt_active || quiz_spin_box == nullptr
        || card_widget_internal == nullptr) {
        return;
    }

    const int expected = card_widget_internal->current_total_weight();
    const int provided = quiz_spin_box->value();
    last_quiz_input_value = provided;
    const bool training_enabled = is_training_enabled();
    if (provided == expected) {
        if (!training_enabled) {
            emit score_adjusted(1, 0);
        }
        clear_quiz_prompt();
        return;
    }

    const QString message
        = str_label("You've set %1 while the correct answer is %2.")
              .arg(provided)
              .arg(expected);
    if (training_enabled) {
        show_quiz_feedback(message, true);
        return;
    }

    card_widget_internal->mark_deck_exhausted();
    show_quiz_feedback(message, false);
}

void table_slot::on_quiz_skip_button_clicked() {
    if (!quiz_prompt_active || quiz_spin_box == nullptr
        || card_widget_internal == nullptr) {
        return;
    }

    const int expected = card_widget_internal->current_total_weight();
    const int provided = quiz_spin_box->value();
    last_quiz_input_value = provided;
    if (!is_training_enabled()) {
        emit score_adjusted(0, -1);
    }

    show_quiz_feedback(
        str_label("You've set %1 while the correct answer is %2.")
            .arg(provided)
            .arg(expected),
        true
    );
}

void table_slot::on_quiz_continue_button_clicked() {
    if (!quiz_prompt_active) {
        return;
    }

    clear_quiz_prompt();
}

void table_slot::on_show_card_indexing_toggled(bool checked) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_show_card_indexing(checked);
    }
}

void table_slot::on_show_strategy_name_toggled(bool checked) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_show_strategy_name(checked);
    }
}

void table_slot::on_training_check_box_toggled(bool checked) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_training_mode(checked);
    }
    update_lockable_settings();
}

void table_slot::on_strategy_name_changed(const QString& text) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_strategy_name(text);
        update_strategy_weights();
    }

    if (strategy_combo_box == nullptr
        || strategy_combo_box->currentIndex() < 0) {
        return;
    }
    const int index = strategy_combo_box->currentIndex();
    trainer_preferences preferences = load_trainer_preferences();
    preferences.preferred_strategy_slug
        = strategy_combo_box->itemData(index).toString();
    preferences.preferred_strategy_id
        = strategy_combo_box->itemData(index, Qt::UserRole + 1).toInt();
    save_trainer_preferences(preferences);
}

void table_slot::update_infinity_state(
    BaseCheckBox* check_box, BaseSpinBox* spin_box
) {
    if (check_box == nullptr || spin_box == nullptr) {
        return;
    }

    auto internal_spin_box = dynamic_cast<infinity_spinbox*>(spin_box);
    if (!internal_spin_box) {
        return;
    }

    bool checked = check_box->isChecked();
    internal_spin_box->set_infinity_mode(checked);
    spin_box->setEnabled(!checked);
}

bool table_slot::is_infinity_enabled() const {
    return infinity_check_box != nullptr && infinity_check_box->isChecked();
}

bool table_slot::is_training_enabled() const {
    return training_check_box != nullptr && training_check_box->isChecked();
}

void table_slot::show_quiz_prompt() {
    if (quiz_prompt_active) {
        return;
    }
    quiz_prompt_active = true;
    quiz_feedback_active = false;
    quiz_continue_visible = false;
    update_overlay_layout();
    if (quiz_spin_box != nullptr) {
        quiz_spin_box->setValue(last_quiz_input_value);
    }
    update_quiz_controls_visibility();
    if (!isVisible()) {
        show();
    }
    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->show();
    }
    if (quiz_spin_box != nullptr) {
        quiz_spin_box->setFocus(Qt::OtherFocusReason);
    }
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_hide_cards(true);
        card_widget_internal->set_table_marking_source(
            bundled_asset_path(QStringLiteral("mad.svg"))
        );
    }
    if (!is_training_enabled()) {
        emit score_adjusted(0, 1);
    }
    set_paused(current_phase == slot_phase::paused);
}

void table_slot::clear_quiz_prompt() {
    if (!quiz_prompt_active) {
        return;
    }
    quiz_prompt_active = false;
    quiz_feedback_active = false;
    quiz_continue_visible = false;
    update_overlay_layout();
    update_quiz_controls_visibility();
    if (quiz_bar_widget != nullptr) {
        quiz_bar_widget->hide();
    }
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_hide_cards(false);
        card_widget_internal->set_table_marking_source(
            bundled_asset_path(QStringLiteral("cuckoo.svg"))
        );
    }
    set_paused(current_phase == slot_phase::paused);
}

void table_slot::show_quiz_feedback(
    const QString& message, bool show_continue
) {
    if (quiz_feedback_label != nullptr) {
        quiz_feedback_label->setText(message);
    }
    quiz_feedback_active = true;
    quiz_continue_visible = show_continue;
    update_quiz_controls_visibility();
    if (!isVisible()) {
        show();
    }
    if (quiz_continue_button != nullptr) {
        quiz_continue_button->setVisible(show_continue);
        quiz_continue_button->setEnabled(show_continue);
    }
    set_paused(current_phase == slot_phase::paused);
}

void table_slot::update_quiz_controls_visibility() {
    if (quiz_layout != nullptr && quiz_prompt_widget != nullptr
        && quiz_feedback_widget != nullptr) {
        quiz_layout->setCurrentWidget(
            quiz_feedback_active ? quiz_feedback_widget : quiz_prompt_widget
        );
    }
    if (quiz_prompt_widget != nullptr) {
        quiz_prompt_widget->setVisible(!quiz_feedback_active);
    }
    if (quiz_feedback_widget != nullptr) {
        quiz_feedback_widget->setVisible(quiz_feedback_active);
    }
    if (quiz_spin_box != nullptr) {
        quiz_spin_box->setVisible(!quiz_feedback_active);
    }
    if (quiz_weight_label != nullptr) {
        quiz_weight_label->setVisible(!quiz_feedback_active);
    }
    if (quiz_answer_button != nullptr) {
        quiz_answer_button->setVisible(!quiz_feedback_active);
    }
    if (quiz_skip_button != nullptr) {
        quiz_skip_button->setVisible(
            !quiz_feedback_active && allow_skipping_flag
        );
        quiz_skip_button->setEnabled(
            !quiz_feedback_active && allow_skipping_flag
        );
    }
    if (quiz_feedback_label != nullptr) {
        quiz_feedback_label->setVisible(quiz_feedback_active);
    }
    if (quiz_continue_button != nullptr) {
        const bool show_continue
            = quiz_feedback_active && quiz_continue_visible;
        quiz_continue_button->setVisible(show_continue);
        quiz_continue_button->setEnabled(show_continue);
    }
}

void table_slot::apply_settings_from(const table_slot& source) {
    if (infinity_check_box == nullptr || deck_count_spin_box == nullptr
        || strategy_combo_box == nullptr || show_card_indexing == nullptr
        || show_strategy_name == nullptr || training_check_box == nullptr) {
        return;
    }

    if (source.infinity_check_box != nullptr) {
        infinity_check_box->setChecked(source.infinity_check_box->isChecked());
    }

    if (source.deck_count_spin_box != nullptr) {
        deck_count_spin_box->setValue(source.deck_count_spin_box->value());
    }

    if (source.strategy_combo_box != nullptr) {
        const int source_index = source.strategy_combo_box->currentIndex();
        if (source_index >= 0 && source_index < strategy_combo_box->count()) {
            strategy_combo_box->setCurrentIndex(source_index);
        }
    }

    if (source.show_card_indexing != nullptr) {
        show_card_indexing->setChecked(source.show_card_indexing->isChecked());
    }

    if (source.show_strategy_name != nullptr) {
        show_strategy_name->setChecked(source.show_strategy_name->isChecked());
    }

    if (source.training_check_box != nullptr) {
        training_check_box->setChecked(source.training_check_box->isChecked());
    }

    update_infinity_state(infinity_check_box, deck_count_spin_box);
    sync_card_display_settings();
    update_lockable_settings();
}

void table_slot::set_copy_button_text(const QString& text) {
    if (copy_button == nullptr) {
        return;
    }
    copy_button->setText(text);
    update_action_button_state();
}

bool table_slot::is_deck_exhausted() const {
    return card_widget_internal != nullptr
        && card_widget_internal->is_deck_exhausted();
}

bool table_slot::is_quiz_prompt_active() const { return quiz_prompt_active; }

void table_slot::sync_card_display_settings() {
    if (card_widget_internal == nullptr) {
        return;
    }

    if (show_card_indexing != nullptr) {
        card_widget_internal->set_show_card_indexing(
            show_card_indexing->isChecked()
        );
    }
    if (show_strategy_name != nullptr) {
        card_widget_internal->set_show_strategy_name(
            show_strategy_name->isChecked()
        );
    }
    if (training_check_box != nullptr) {
        card_widget_internal->set_training_mode(
            training_check_box->isChecked()
        );
    }
    if (strategy_combo_box != nullptr) {
        const QString strategy_name = strategy_combo_box->currentText();
        card_widget_internal->set_strategy_name(strategy_name);
        update_strategy_weights();
    }
}

void table_slot::update_action_button_state() {
    if (swap_button == nullptr || copy_button == nullptr) {
        return;
    }

    if (!swap_selected()) {
        swap_button->setChecked(false);
        copy_button->setChecked(false);
        return;
    }

    const bool is_copy_mode = copy_button->text() == str_label("Cancel");
    swap_button->setChecked(!is_copy_mode);
    copy_button->setChecked(is_copy_mode);
}

void table_slot::show_template_dialog(
    const QString& title, const QString& strategy_name
) {
    emit dialog_opened();

    QDialog dialog(this);
    dialog.setWindowTitle(title);

    auto dialog_layout = new QVBoxLayout(&dialog);
    dialog_layout->addWidget(new settings_template_widget(
        settings_tab_kind::strategies, &dialog, strategy_name
    ));

    auto button_box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(
        button_box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject
    );
    dialog_layout->addWidget(button_box);

#if defined(Q_OS_ANDROID)
    dialog.setWindowState(Qt::WindowMaximized);
#endif
    dialog.exec();
}

void table_slot::update_strategy_weights() {
    if (card_widget_internal == nullptr || strategy_combo_box == nullptr) {
        return;
    }
    card_widget_internal->set_strategy_weights(
        weights_for_strategy_slug(strategy_combo_box->currentData().toString())
    );
}
