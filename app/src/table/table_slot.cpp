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

#include <QApplication>
#include <QBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QResizeEvent>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QString>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <utility>

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

drill_slot_preferences table_slot::capture_drill_settings() const {
    return { deck_count_spin_box->value(), is_infinity_enabled(),
             strategy_combo_box->currentData().toString(),
             is_training_enabled() };
}

void table_slot::apply_drill_settings(const drill_slot_preferences& settings) {
    finish_settings_editor(false);
    const QSignalBlocker infinity_blocker(infinity_check_box);
    const QSignalBlocker deck_blocker(deck_count_spin_box);
    const QSignalBlocker strategy_blocker(strategy_combo_box);
    const QSignalBlocker training_blocker(training_check_box);
    infinity_check_box->setChecked(settings.infinity_enabled);
    deck_count_spin_box->setValue(settings.deck_count);
    strategy_combo_box->setCurrentIndex(
        strategy_combo_box->findData(settings.strategy_slug)
    );
    training_check_box->setChecked(settings.training_mode);
    update_infinity_state(infinity_check_box, deck_count_spin_box);
    sync_card_display_settings();
    update_lockable_settings();
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

    finish_settings_editor(false);
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
    finish_settings_editor(false);
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
    finish_settings_editor(false);
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
    if (!paused)
        finish_settings_editor(false);
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
            && !quiz_prompt_active
            && settings_style == slot_settings_style::classic;
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

    update_action_presentation();
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
    update_compact_controls();
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

void table_slot::set_frame_style(slot_frame_style style) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_frame_style(style);
    }
}

void table_slot::set_quiz_presentation(
    quiz_answer_style answer, quiz_feedback_style feedback
) {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    if (answer_style == answer && feedback_style == feedback) {
        return;
    }
    answer_style = answer;
    feedback_style = feedback;
    update_quiz_presentation();
#else
    Q_UNUSED(answer);
    Q_UNUSED(feedback);
#endif
}

void table_slot::set_action_style(slot_action_style style) {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    if (action_style == style) {
        return;
    }
    const auto* focused = QApplication::focusWidget();
    const bool had_focus
        = focused != nullptr && swap_bar_widget->isAncestorOf(focused);
    action_style = style;
    QToolTip::hideText();
    update_action_presentation();
    update_overlay_layout();
    if (had_focus && !controls_dialog && compact_controls_button->isVisible()) {
        compact_controls_button->setFocus(Qt::OtherFocusReason);
    }
#else
    Q_UNUSED(style);
#endif
}

void table_slot::set_settings_style(slot_settings_style style) {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    if (settings_style == style)
        return;
    settings_style = style;
    set_paused(current_phase == slot_phase::paused);
    place_settings_editor();
#else
    Q_UNUSED(style);
#endif
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

#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    // A packed slot must never inherit the controls' minimum size. If they
    // cannot fit, expose the same widgets in a transient desktop window.
    overlay_layout->setSizeConstraint(QLayout::SetNoConstraint);
    overlay_widget->installEventFilter(this);
    compact_controls_button = new QToolButton(this);
    compact_controls_button->setObjectName(
        QStringLiteral("compact_slot_controls")
    );
    compact_controls_button->setFocusPolicy(Qt::StrongFocus);
    compact_controls_button->hide();
    connect(
        compact_controls_button, &QToolButton::clicked, this,
        &table_slot::show_compact_controls
    );
#endif

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
    settings_button->setObjectName(QStringLiteral("slot_details_button"));
    swap_button->setObjectName(QStringLiteral("slot_swap_button"));
    copy_button->setObjectName(QStringLiteral("slot_copy_button"));
    copy_all_button->setObjectName(QStringLiteral("slot_copy_all_button"));
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    for (auto* button :
         { settings_button, swap_button, copy_button, copy_all_button }) {
        button->installEventFilter(this);
    }
#endif

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
    setup_quiz_chips();
    if (quiz_chip_widget != nullptr) {
        quiz_prompt_layout->addWidget(quiz_chip_widget);
    }
    quiz_prompt_layout->addLayout(quiz_prompt_button_layout);

#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    quiz_feedback_heading
        = new QLabel(str_label("ACCUMULATED COUNT"), quiz_feedback_widget);
    quiz_feedback_heading->setObjectName(QStringLiteral("quiz_feedback_stamp"));
    quiz_feedback_heading->setWordWrap(true);
    quiz_feedback_heading->setAlignment(Qt::AlignCenter);
    quiz_feedback_layout->addWidget(quiz_feedback_heading);
#endif
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
    update_action_presentation();
    update_settings_button_state();
}

void table_slot::update_action_presentation() {
    if (settings_button == nullptr || copy_all_button == nullptr) {
        return;
    }
    const bool running_with_deck = current_phase == slot_phase::running
        && card_widget_internal->has_cards();
    const QString copy_label = current_copy_action == copy_action::cancel
        ? str_label("Cancel")
        : current_copy_action == copy_action::apply ? str_label("Set")
                                                    : str_label("Copy");
    const std::array<std::pair<BasePushButton*, QString>, 4> actions {
        { { settings_button,
            running_with_deck ? str_label("Info") : str_label("Details") },
          { swap_button, str_label("Swap") },
          { copy_button, copy_label },
          { copy_all_button, str_label("Copy all") } }
    };
    copy_button->setToolTip(
        current_copy_action == copy_action::cancel
            ? str_label("Cancel copying settings")
            : current_copy_action == copy_action::apply
            ? str_label("Apply the selected slot's settings to this slot")
            : str_label("Copy settings from this slot")
    );
    for (const auto& [button, label] : actions) {
        button->setAccessibleName(label);
        // Native themes may supply the same glyph for Copy and Copy all.
        // Keep the scope explicit even in the otherwise icon-only rail.
        button->setText(
            action_style == slot_action_style::rail
                ? (button == copy_all_button ? str_label("All") : QString())
                : label
        );
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
        button->setStyleSheet(
            action_style == slot_action_style::rail
                ? QStringLiteral(
                      "QPushButton { min-width: 24px; min-height: 24px; "
                      "padding: 4px; } QPushButton:focus { border-style: "
                      "dashed; }"
                  )
                : action_style == slot_action_style::pills
                ? QStringLiteral(
                      "QPushButton { border-radius: 12px; padding: 6px 12px; } "
                      "QPushButton:focus { border-style: dashed; }"
                  )
                : QString()
        );
#endif
    }
}

void table_slot::setup_quiz_chips() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    quiz_spin_box->installEventFilter(this);
    quiz_spin_box->findChild<QLineEdit*>()->installEventFilter(this);
    quiz_chip_widget = new BaseWidget(quiz_prompt_widget);
    quiz_chip_widget->setObjectName(QStringLiteral("quiz_chip_stepper"));
    auto* row = new QHBoxLayout(quiz_chip_widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    row->addStretch();
    QWidget* previous = quiz_spin_box;
    for (const int step : { -2, -1, 1, 2 }) {
        const QString text = step > 0 ? str_label("+%1").arg(step)
                                      : str_label("−%1").arg(-step);
        auto* button = new QPushButton(text, quiz_chip_widget);
        button->setObjectName(QStringLiteral("quiz_chip_%1").arg(step));
        button->setAutoDefault(false);
        button->setAccessibleName(
            step > 0 ? str_label("Add %1 to answer").arg(step)
                     : str_label("Subtract %1 from answer").arg(-step)
        );
        button->setToolTip(button->accessibleName());
        button->setStyleSheet(QStringLiteral(
            "QPushButton { border-radius: 18px; min-width: 36px; min-height: "
            "36px; padding: 0px; font-weight: 600; }"
        ));
        button->installEventFilter(this);
        row->addWidget(button);
        QWidget::setTabOrder(previous, button);
        previous = button;
        connect(button, &QPushButton::clicked, this, [this, step] {
            if (!quiz_prompt_active || quiz_feedback_active) {
                return;
            }
            quiz_spin_box->interpretText();
            quiz_spin_box->setValue(quiz_spin_box->value() + step);
        });
        connect(
            quiz_spin_box, &QSpinBox::valueChanged, button,
            [this, button, step](int value) {
                button->setEnabled(
                    step > 0 ? value < quiz_spin_box->maximum()
                             : value > quiz_spin_box->minimum()
                );
            }
        );
    }
    QWidget::setTabOrder(previous, quiz_answer_button);
    QWidget::setTabOrder(quiz_answer_button, quiz_skip_button);
    QWidget::setTabOrder(quiz_skip_button, quiz_continue_button);
    row->addStretch();
#endif
}

void table_slot::update_quiz_presentation() {
    if (quiz_chip_widget == nullptr || quiz_feedback_heading == nullptr) {
        return;
    }
    const bool chips = answer_style == quiz_answer_style::chips;
    const auto* focused = QApplication::focusWidget();
    const bool chip_had_focus
        = focused != nullptr && quiz_chip_widget->isAncestorOf(focused);
    const bool controls_had_focus
        = focused != nullptr && overlay_widget->isAncestorOf(focused);
    quiz_chip_widget->setVisible(chips);
    quiz_spin_box->setButtonSymbols(
        chips ? QAbstractSpinBox::NoButtons : QAbstractSpinBox::UpDownArrows
    );
    if (!chips && chip_had_focus && quiz_prompt_active
        && !quiz_feedback_active) {
        quiz_spin_box->setFocus(Qt::OtherFocusReason);
    }
    const bool stamp = feedback_style == quiz_feedback_style::stamp;
    quiz_feedback_heading->setVisible(stamp);
    quiz_feedback_heading->setStyleSheet(QStringLiteral("font-weight: 700;"));
    quiz_feedback_label->setStyleSheet(
        stamp ? QStringLiteral(
                    "QLabel { font-weight: 600; border: 2px solid %1; "
                    "border-radius: 4px; padding: 8px; }"
                )
                    .arg(theme_settings::slot_border_color().name())
              : QString()
    );
    // Only presentation hints change. Invalidate them before checking whether
    // the controls still fit inline; neither table packing nor card demand
    // changes.
    quiz_prompt_widget->updateGeometry();
    quiz_feedback_widget->updateGeometry();
    quiz_bar_widget->updateGeometry();
    update_compact_controls();
    if (controls_had_focus && !controls_dialog
        && compact_controls_button != nullptr
        && compact_controls_button->isVisible() && quiz_prompt_active) {
        compact_controls_button->setFocus(Qt::OtherFocusReason);
    }
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
    apply_palette_to_widget(
        settings_editor_panel, panel_color, accent_color, input_color
    );
    update_quiz_presentation();
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
            " QFrame#quiz_bar_frame, QFrame#slot_settings_editor {"
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

    const bool rail = action_style == slot_action_style::rail;
    const bool horizontal_composition
        = !quiz_prompt_active && action_style != slot_action_style::classic
        ? rail
        : is_rotated;
    overlay_layout->setDirection(
        horizontal_composition ? QBoxLayout::LeftToRight
                               : QBoxLayout::TopToBottom
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
            rail || (action_style == slot_action_style::classic && is_rotated)
                ? QBoxLayout::TopToBottom
                : QBoxLayout::LeftToRight
        );
    }
    update_compact_controls();
}

bool table_slot::eventFilter(QObject* watched, QEvent* event) {
    if (watched == settings_editor_panel
        && event->type() == QEvent::LayoutRequest) {
        place_settings_editor();
    }
    if (action_style == slot_action_style::rail) {
        auto* button = qobject_cast<BasePushButton*>(watched);
        if (button != nullptr && button->parentWidget() == swap_bar_widget) {
            // A keyboard focus label must not widen the rail and change fit.
            if (event->type() == QEvent::FocusIn) {
                QToolTip::showText(
                    button->mapToGlobal(QPoint(button->width(), 0)),
                    button->toolTip(), button
                );
            } else if (
                event->type() == QEvent::FocusOut
                || event->type() == QEvent::Hide
            ) {
                QToolTip::hideText();
            }
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (quiz_spin_box != nullptr
                && (watched == quiz_spin_box
                    || watched == quiz_spin_box->findChild<QLineEdit*>())) {
                if (!key->isAutoRepeat()) {
                    on_quiz_answer_button_clicked();
                }
                return true; // Do not also activate a dialog default button.
            }
            auto* button = qobject_cast<QPushButton*>(watched);
            if (button != nullptr && quiz_chip_widget != nullptr
                && quiz_chip_widget->isAncestorOf(button)) {
                if (!key->isAutoRepeat()) {
                    button->click();
                }
                return true;
            }
        }
    }
    if (watched == overlay_widget && event->type() == QEvent::LayoutRequest) {
        update_compact_controls();
    }
    return BaseWidget::eventFilter(watched, event);
}

void table_slot::update_compact_controls() {
    if (compact_controls_button == nullptr || overlay_layout == nullptr
        || quiz_bar_widget == nullptr || swap_bar_widget == nullptr) {
        return;
    }
    if (settings_editor_panel) {
        compact_controls_button->hide();
        overlay_widget->hide();
        return;
    }
    const bool controls_visible = current_phase == slot_phase::paused
        || !card_widget_internal->has_cards() || quiz_prompt_active;
    if (quiz_prompt_active) {
        // A stacked page's minimum hint allows wrapped text to be clipped.
        // Reserve its natural height-for-width before either centering it or
        // deciding to host it. Derive this from layout hints, never the old
        // explicit minimum, so shorter feedback/styles can shrink again.
        QSize quiz_size = quiz_bar_widget->sizeHint().expandedTo(
            quiz_bar_widget->minimumSizeHint()
        );
        quiz_size.setHeight(
            std::max(
                quiz_size.height(),
                quiz_bar_widget->heightForWidth(quiz_size.width())
            )
        );
        quiz_bar_widget->setMinimumSize(quiz_size);
    }
    // Read the active surfaces, not the outer layout's QWidgetItem cache:
    // that cache can still describe a hidden bar while restoring a question.
    const auto minimum = [](const QWidget* widget) {
        return widget->minimumSizeHint().expandedTo(widget->minimumSize());
    };
    QSize required
        = minimum(quiz_prompt_active ? quiz_bar_widget : swap_bar_widget);
    if (!quiz_prompt_active && settings_overlay_visible
        && !use_dialog_for_settings
        && settings_style == slot_settings_style::classic) {
        const QSize settings = minimum(settings_bar_widget);
        required = overlay_layout->direction() == QBoxLayout::LeftToRight
            ? QSize(
                  required.width() + settings.width()
                      + overlay_layout->spacing(),
                  std::max(required.height(), settings.height())
              )
            : QSize(
                  std::max(required.width(), settings.width()),
                  required.height() + settings.height()
                      + overlay_layout->spacing()
              );
    }
    const auto margins = overlay_layout->contentsMargins();
    required += QSize(
        margins.left() + margins.right(), margins.top() + margins.bottom()
    );
    const bool compact
        = required.width() > width() || required.height() > height();
    compact_controls_button->setText(
        quiz_prompt_active ? QStringLiteral("?") : QStringLiteral("…")
    );
    const QString description = quiz_prompt_active
        ? (quiz_feedback_active
               ? str_label("Show answer feedback")
               : str_label("Answer accumulated-count question"))
        : str_label("Show slot actions");
    compact_controls_button->setAccessibleName(description);
    compact_controls_button->setToolTip(description);
    const QSize button_size
        = compact_controls_button->sizeHint().boundedTo(size());
    compact_controls_button->resize(button_size);
    compact_controls_button->move(
        std::max(0, (width() - button_size.width()) / 2),
        std::max(0, height() - button_size.height() - 2)
    );
    compact_controls_button->setVisible(
        controls_visible && (compact || controls_dialog)
    );
    compact_controls_button->raise();
    if (controls_dialog) {
        if (!controls_visible) {
            controls_dialog->close();
        }
        return;
    }
    overlay_widget->setVisible(controls_visible && !compact);
}

void table_slot::show_compact_controls() {
    if (controls_dialog) {
        controls_dialog->raise();
        controls_dialog->activateWindow();
        return;
    }
    auto* dialog = new QDialog(this);
    controls_dialog = dialog;
    dialog->setObjectName(QStringLiteral("slot_controls_dialog"));
    dialog->setWindowTitle(str_label("Card controls"));
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(overlay_widget);
    overlay_widget->show();
    auto* close_buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(
        close_buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject
    );
    layout->addWidget(close_buttons);
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        dialog->layout()->removeWidget(overlay_widget);
        overlay_widget->setParent(this);
        overlay_widget->setGeometry(rect());
        controls_dialog = nullptr;
        dialog->deleteLater();
        update_compact_controls();
        if (compact_controls_button->isVisible()) {
            compact_controls_button->setFocus(Qt::OtherFocusReason);
        }
    });
    dialog->show();
    if (quiz_prompt_active && !quiz_feedback_active) {
        quiz_spin_box->setFocus(Qt::OtherFocusReason);
    } else if (quiz_prompt_active && quiz_continue_visible) {
        quiz_continue_button->setFocus(Qt::OtherFocusReason);
    } else if (quiz_prompt_active) {
        close_buttons->button(QDialogButtonBox::Close)
            ->setFocus(Qt::OtherFocusReason);
    } else {
        settings_button->setFocus(Qt::OtherFocusReason);
    }
}

void table_slot::update_settings_button_state(bool dialog_open) {
    if (settings_button == nullptr) {
        return;
    }
    if (settings_editor_panel
        || settings_style != slot_settings_style::classic) {
        settings_button->setChecked(settings_editor_panel != nullptr);
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

    if (overlay_widget != nullptr && !controls_dialog) {
        overlay_widget->setGeometry(rect());
    }
    place_settings_editor();

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
        update_compact_controls();
        return;
    }

    use_dialog_for_settings = new_use_dialog_for_settings;

    if (use_dialog_for_settings) {
        settings_overlay_visible = false;
        settings_bar_widget->hide();
    } else {
        if (overlay_widget != nullptr && overlay_widget->isVisible()) {
            settings_bar_widget->setVisible(
                settings_overlay_visible
                && settings_style == slot_settings_style::classic
            );
        }
    }
    update_settings_button_state();
    update_compact_controls();
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

void table_slot::populate_settings_editor(slot_settings* editor) {
    auto* infinite = editor->infinity_check_box();
    auto* decks = editor->deck_count_spin_box();
    auto* strategy = editor->strategy_combo_box();
    infinite->setChecked(infinity_check_box->isChecked());
    decks->setRange(
        deck_count_spin_box->minimum(), deck_count_spin_box->maximum()
    );
    decks->setSingleStep(deck_count_spin_box->singleStep());
    decks->setValue(deck_count_spin_box->value());
    strategy->clear();
    for (int i = 0; i < strategy_combo_box->count(); ++i) {
        strategy->addItem(
            strategy_combo_box->itemText(i), strategy_combo_box->itemData(i)
        );
        strategy->setItemData(
            i, strategy_combo_box->itemData(i, Qt::UserRole + 1),
            Qt::UserRole + 1
        );
    }
    strategy->setCurrentIndex(strategy_combo_box->currentIndex());
    strategy->setEnabled(strategy_combo_box->isEnabled());
    editor->show_card_indexing()->setChecked(show_card_indexing->isChecked());
    editor->show_strategy_name()->setChecked(show_strategy_name->isChecked());
    editor->training_check_box()->setChecked(training_check_box->isChecked());
    const bool locked = card_widget_internal->has_cards()
        && current_phase == slot_phase::paused;
    infinite->setEnabled(!(locked && infinite->isChecked()));
    editor->training_check_box()->setEnabled(
        !(locked && editor->training_check_box()->isChecked())
    );
    update_infinity_state(infinite, decks);
    connect(infinite, &BaseCheckBox::toggled, editor, [infinite, decks](bool) {
        update_infinity_state(infinite, decks);
    });
}

void table_slot::apply_settings_editor(const slot_settings* editor) {
    editor->deck_count_spin_box()->interpretText();
    infinity_check_box->setChecked(editor->infinity_check_box()->isChecked());
    deck_count_spin_box->setValue(editor->deck_count_spin_box()->value());
    strategy_combo_box->setCurrentIndex(
        editor->strategy_combo_box()->currentIndex()
    );
    show_card_indexing->setChecked(editor->show_card_indexing()->isChecked());
    show_strategy_name->setChecked(editor->show_strategy_name()->isChecked());
    training_check_box->setChecked(editor->training_check_box()->isChecked());
    sync_card_display_settings();
}

void table_slot::open_settings_editor() {
    if (quiz_prompt_active
        || (current_phase == slot_phase::running
            && card_widget_internal->has_cards())) {
        return;
    }
    if (controls_dialog)
        controls_dialog->close();
    // Reuse the desktop shell's pause policy; never auto-resume.
    emit dialog_opened();
    auto* panel = new QFrame(this);
    settings_editor_panel = panel;
    panel->setObjectName(QStringLiteral("slot_settings_editor"));
    panel->setAccessibleName(str_label("Card settings"));
    panel->installEventFilter(this);
    auto* layout = new QVBoxLayout(panel);
    auto* heading
        = new QLabel(str_label("Card settings — changes apply with OK"), panel);
    heading->setToolTip(str_label(
        "Cancel or Escape discards this draft. Resuming play, restarting, "
        "restoring a session or copying settings here also discards unapplied "
        "edits."
    ));
    heading->setWordWrap(true);
    layout->addWidget(heading);
    settings_editor_fields = new slot_settings(panel, true);
    populate_settings_editor(settings_editor_fields);
    layout->addWidget(settings_editor_fields);
    connect(
        settings_editor_fields->info_button(), &BasePushButton::clicked, this,
        [this] {
            if (settings_editor_fields)
                show_template_dialog(
                    str_label("Strategy details"),
                    settings_editor_fields->strategy_combo_box()->currentText()
                );
        }
    );
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, panel
    );
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        finish_settings_editor(true);
    });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] {
        finish_settings_editor(false);
    });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), panel);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] {
        finish_settings_editor(false);
    });
    update_overlay_palette();
    update_settings_button_state();
    update_compact_controls();
    place_settings_editor();
    for (QWidget* field :
         { static_cast<QWidget*>(settings_editor_fields->infinity_check_box()),
           static_cast<QWidget*>(settings_editor_fields->deck_count_spin_box()),
           static_cast<QWidget*>(settings_editor_fields->strategy_combo_box()),
           static_cast<QWidget*>(
               settings_editor_fields->show_card_indexing()
           ) }) {
        if (field->isEnabled()) {
            field->setFocus(Qt::OtherFocusReason);
            break;
        }
    }
}

void table_slot::place_settings_editor() {
    if (!settings_editor_panel || settings_editor_host)
        return;
    auto* panel = settings_editor_panel.data();
    panel->ensurePolished();
    const QSize needed = panel->sizeHint().expandedTo(panel->minimumSizeHint());
    const QRect available = rect().adjusted(8, 8, -8, -8);
    if (needed.width() > available.width()
        || needed.height() > available.height()) {
        QPointer<QWidget> focused = panel->focusWidget();
        auto* host = new QDialog(this);
        settings_editor_host = host;
        host->setObjectName(QStringLiteral("slot_settings_host"));
        host->setWindowTitle(str_label("Card settings"));
        auto* layout = new QVBoxLayout(host);
        // Move the draft, never recreate it on resize.
        layout->addWidget(panel);
        connect(host, &QDialog::rejected, this, [this] {
            finish_settings_editor(false);
        });
        panel->show();
        host->show();
        if (focused)
            focused->setFocus(Qt::OtherFocusReason);
        return;
    }
    const int x = settings_style == slot_settings_style::drawer
        ? available.right() - needed.width() + 1
        : available.x() + (available.width() - needed.width()) / 2;
    const int y = settings_style == slot_settings_style::sill
        ? available.bottom() - needed.height() + 1
        : available.y() + (available.height() - needed.height()) / 2;
    panel->setGeometry(QRect(QPoint(x, y), needed));
    panel->show();
    panel->raise();
}

void table_slot::finish_settings_editor(bool apply) {
    if (!settings_editor_panel)
        return;
    auto* panel = settings_editor_panel.data();
    auto* fields = settings_editor_fields;
    auto* host = settings_editor_host.data();
    settings_editor_panel = nullptr;
    settings_editor_fields = nullptr;
    settings_editor_host = nullptr;
    panel->hide();
    if (host) {
        disconnect(host, nullptr, this, nullptr);
        host->close();
        host->deleteLater();
    }
    if (apply)
        apply_settings_editor(fields);
    panel->deleteLater();
    set_paused(current_phase == slot_phase::paused);
    if (compact_controls_button && compact_controls_button->isVisible()) {
        compact_controls_button->setFocus(Qt::OtherFocusReason);
    } else if (settings_button->isVisible()) {
        settings_button->setFocus(Qt::OtherFocusReason);
    }
}

void table_slot::on_settings_button_clicked() {
    if (settings_editor_panel) {
        finish_settings_editor(false);
        return;
    }
    if (settings_style != slot_settings_style::classic) {
        open_settings_editor();
        return;
    }
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

        populate_settings_editor(dialog_settings_widget);

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
            apply_settings_editor(dialog_settings_widget);
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
    if (!quiz_prompt_active || quiz_feedback_active || quiz_spin_box == nullptr
        || card_widget_internal == nullptr) {
        return;
    }

    quiz_spin_box->interpretText();
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
    if (!quiz_prompt_active || quiz_feedback_active || !allow_skipping_flag
        || quiz_spin_box == nullptr || card_widget_internal == nullptr) {
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
    if (compact_controls_button != nullptr
        && compact_controls_button->isVisible()) {
        compact_controls_button->setFocus(Qt::OtherFocusReason);
    }
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
    update_compact_controls();
}

void table_slot::apply_settings_from(const table_slot& source) {
    finish_settings_editor(false);
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

void table_slot::set_copy_action(copy_action action) {
    current_copy_action = action;
    update_action_presentation();
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

    const bool is_copy_mode = current_copy_action == copy_action::cancel;
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
