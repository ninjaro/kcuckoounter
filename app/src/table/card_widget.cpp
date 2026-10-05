#include "table/card_widget.hpp"

#include "arch/asset_locator.hpp"
#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "settings/theme_settings.hpp"
#include <QFutureWatcher>
#include <QImage>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

int card_widget::total_cards_for_quiz_type(int quiz_type_index) {
    if (quiz_type_index == 1) {
        return 54;
    }
    return 52;
}

QString card_widget::weight_text_for_value(std::int64_t weight) {
    if (weight >= 0) {
#ifdef KC_KDE
        return i18n("+%1", static_cast<qlonglong>(weight));
#else
        return str_label("+%1").arg(static_cast<qlonglong>(weight));
#endif
    }
    return QString::number(weight);
}

int card_widget::rank_index_from_card_index(int card_index) {
    if (card_index < 0 || card_index >= 52) {
        return -1;
    }
    return card_index % 13;
}

card_widget::card_widget(BaseWidget* parent)
    : BaseWidget(parent)
    , running(false)
    , swap_selected_flag(false)
    , picker()
    , random_gen()
    , card_rotation_deg(0.0)
    , card_offset(0.0, 0.0)
    , slot_rotated(false)
    , show_card_indexing_flag(false)
    , show_strategy_name_flag(false)
    , training_mode_flag(false)
    , strategy_name()
    , strategy_weights()
    , cards_per_deck(0)
    , decks_count(0)
    , infinity_enabled(false)
    , selection_timer(std::make_unique<time_interface>())
    , selection_phase(0.0)
    , discard_history()
    , highlight_duration_ms(0)
    , highlight_remaining_ms(0)
    , highlight_active(false)
    , hide_cards_flag(false)
    , table_marking(bundled_asset_path(QStringLiteral("cuckoo.svg")))
    , card_sheet_source(card_sheet_source_path())
    , card_sheet_renderer()
    , selected_card_face()
    , selected_card_face_index(-1)
    , card_face_size()
    , card_faces_rasterized()
    , card_face_raster_size()
    , picks_since_rasterize(0)
    , rasterize_watcher()
    , raster_task_size()
    , raster_task_source()
    , pending_raster_size()
    , rasterizing(false)
    , shared_card_faces_active(false)
    , shared_card_faces_mode(false) {
    selection_timer->set_interval(45);
    QObject::connect(
        selection_timer.get(), &time_interface::timeout, this,
        &card_widget::update_selection_pulse
    );
    setAccessibleName(str_label("Playing card"));
    update_accessible_description();
    QObject::connect(
        &rasterize_watcher, &QFutureWatcher<QVector<QImage>>::finished, this,
        &card_widget::on_rasterization_finished
    );

    card_sheet_renderer.load(card_sheet_source);
}

card_widget::~card_widget() = default;

bool card_widget::bind_gameplay_deck(
    const gameplay::session& owner, gameplay::deck_id id
) {
    if (!owner.deck(id) || picker.has_cards()
        || (gameplay_owner && (gameplay_owner != &owner || gameplay_id != id)))
        return false;
    gameplay_owner = &owner;
    gameplay_id = id;
    refresh_gameplay_deck();
    return true;
}

void card_widget::refresh_gameplay_deck() {
    const auto* deck = gameplay_deck();
    if (!deck)
        return;
    const auto exposure = deck->dealt_physical_cards;
    if (exposure != presented_physical_cards) {
        if (exposure < presented_physical_cards)
            discard_history.clear();
        else if (presented_physical_cards > 0)
            record_discard();
        // One transform for the newly presented face, not a replay of missed
        // frames or an owned card-history copy. Same-state refresh is inert.
        presented_physical_cards = exposure;
        if (picks_since_rasterize < std::numeric_limits<int>::max())
            ++picks_since_rasterize;
        update_card_jitter();
    }
    update_accessible_description();
    update();
}

void card_widget::unbind_gameplay_deck() {
    if (!gameplay_owner)
        return;
    gameplay_owner = nullptr;
    gameplay_layout_rotation_deg.reset();
    gameplay_paint_external = false;
    presented_physical_cards = 0;
    hide_cards_flag = false;
    clear_quiz();
}

const gameplay::deck_state* card_widget::gameplay_deck() const {
    return gameplay_owner ? gameplay_owner->deck(gameplay_id) : nullptr;
}

int card_widget::display_card_index() const {
    if (const auto* deck = gameplay_deck()) {
        return deck->next_card > 0
                && deck->next_card <= deck->stream.cards.size()
            ? static_cast<int>(deck->stream.cards[deck->next_card - 1])
            : -1;
    }
    return picker.current_card_index();
}

bool card_widget::display_running() const {
    if (const auto* deck = gameplay_deck()) {
        return deck->status == gameplay::deck_status::completed
            || (deck->status == gameplay::deck_status::active
                && gameplay_owner->phase() == gameplay::session_phase::running);
    }
    return running;
}

bool card_widget::display_hidden() const {
    if (const auto* deck = gameplay_deck())
        return deck->status == gameplay::deck_status::active
            && (hide_cards_flag || deck->quiz.has_value());
    return hide_cards_flag;
}

QStringList card_widget::gameplay_extra_lines() const {
    QStringList lines;
    const auto* deck = gameplay_deck();
    if (!deck)
        return lines;
    if (show_strategy_name_flag && !strategy_name.isEmpty())
        lines.append(strategy_name);
    if (deck->status == gameplay::deck_status::completed) {
#ifdef KC_KDE
        lines.append(i18n(
            "Expected final count: %1",
            static_cast<qlonglong>(deck->running_count)
        ));
#else
        lines.append(str_label("Expected final count: %1")
                         .arg(static_cast<qlonglong>(deck->running_count)));
#endif
    } else if (
        deck->status == gameplay::deck_status::active
        && deck->configuration.training && deck->show_count
    ) {
        lines.append(weight_text_for_value(deck->running_count));
    }
    return lines;
}

void card_widget::set_swap_selected(bool selected) {
    if (swap_selected_flag == selected) {
        return;
    }

    swap_selected_flag = selected;
    if (swap_selected_flag) {
        if (!selection_timer->is_active()) {
            selection_timer->start();
        }
    } else {
        selection_timer->stop();
        selection_phase = 0.0;
    }
    update();
}

bool card_widget::swap_selected() const { return swap_selected_flag; }

void card_widget::start_quiz(
    int quiz_type_index, int requested_decks_count, bool infinity_enabled_flag
) {
    if (gameplay_owner)
        return;
    const int total_per_deck = total_cards_for_quiz_type(quiz_type_index);
    if (requested_decks_count <= 0) {
        requested_decks_count = 1;
    }

    picker.setup(total_per_deck, requested_decks_count, infinity_enabled_flag);
    cards_per_deck = total_per_deck;
    decks_count = requested_decks_count;
    infinity_enabled = infinity_enabled_flag;
    discard_history.clear();
    picks_since_rasterize = 0;
    update_card_jitter();
    update_accessible_description();
    update();
}

void card_widget::set_infinity(bool enabled) {
    if (gameplay_owner)
        return;
    picker.set_infinity(enabled);
    infinity_enabled = enabled;
    update();
}

void card_widget::set_running(bool new_running) {
    if (gameplay_owner)
        return;
    if (running == new_running) {
        return;
    }

    running = new_running;
    update_accessible_description();
    update();
}

void card_widget::set_slot_rotated(bool rotated) {
    if (slot_rotated == rotated) {
        return;
    }

    slot_rotated = rotated;
    update();
}

void card_widget::set_gameplay_layout_rotation(std::optional<qreal> degrees) {
    if (!gameplay_owner
        || (degrees
            && (!std::isfinite(*degrees) || *degrees < 0.0 || *degrees > 90.0))
        || gameplay_layout_rotation_deg == degrees)
        return;
    gameplay_layout_rotation_deg = degrees;
    if (!degrees)
        update_table_marking(); // Final geometry only, not every motion frame.
    update();
}

void card_widget::set_frame_style(slot_frame_style style) {
    if (frame_style == style) {
        return;
    }
    frame_style = style;
    update(); // No geometry, jitter, deck or raster-cache invalidation.
}

void card_widget::set_show_card_indexing(bool enabled) {
    if (show_card_indexing_flag == enabled) {
        return;
    }

    show_card_indexing_flag = enabled;
    update();
}

void card_widget::set_show_strategy_name(bool enabled) {
    if (show_strategy_name_flag == enabled) {
        return;
    }

    show_strategy_name_flag = enabled;
    update();
}

void card_widget::set_training_mode(bool enabled) {
    if (gameplay_owner)
        return;
    if (training_mode_flag == enabled) {
        return;
    }

    training_mode_flag = enabled;
    update();
}

void card_widget::set_strategy_name(const QString& name) {
    if (strategy_name == name) {
        return;
    }

    strategy_name = name;
    update();
}

void card_widget::set_strategy_weights(const QVector<int>& weights) {
    if (gameplay_owner)
        return;
    if (strategy_weights == weights) {
        return;
    }

    strategy_weights = weights;
    update();
}

void card_widget::set_table_marking_source(const QString& source) {
    table_marking.set_source(source);
    update_table_marking();
    update();
}

void card_widget::set_hide_cards(bool hide) {
    if (hide_cards_flag == hide) {
        return;
    }
    hide_cards_flag = hide;
    update_accessible_description();
    update();
}

void card_widget::advance_card() {
    if (gameplay_owner || !running) {
        return;
    }

    record_discard();
    picker.advance();
    update_accessible_description();
    ++picks_since_rasterize;
    update_card_jitter();
    update();
}

bool card_widget::has_cards() const {
    if (const auto* deck = gameplay_deck())
        return !deck->stream.cards.empty();
    return picker.has_cards();
}

bool card_widget::has_current_card() const { return display_card_index() >= 0; }

bool card_widget::is_deck_exhausted() const {
    if (const auto* deck = gameplay_deck())
        return deck->status != gameplay::deck_status::active;
    return picker.is_depleted();
}

void card_widget::mark_deck_exhausted() {
    if (gameplay_owner)
        return;
    picker.set_infinity(false);
    infinity_enabled = false;
    picker.mark_depleted();
    update_accessible_description();
    update();
}

int card_widget::current_position() const { return picker.current_position(); }

int card_widget::current_total_weight() const {
    return total_weight_for_picks();
}

card_session_state card_widget::capture_session_state() const {
    // Empty/invalid v1 state instead of serializing a target deck through the
    // legacy picker. The target host must use G8's eventual versioned codec.
    if (gameplay_owner)
        return {};
    return {
        .cards_per_deck = cards_per_deck,
        .decks_count = decks_count,
        .infinity_enabled = infinity_enabled,
        .deck = picker.deck_order(),
        .deck_position = picker.is_depleted() ? picker.total_cards()
                                              : picker.current_position(),
    };
}

bool card_widget::restore_session_state(const card_session_state& state) {
    if (gameplay_owner || !can_restore_session_state(state)
        || !picker.restore(
            state.deck, state.deck_position, state.infinity_enabled,
            state.cards_per_deck
        )) {
        return false;
    }

    cards_per_deck = state.cards_per_deck;
    decks_count = state.decks_count;
    infinity_enabled = state.infinity_enabled;
    discard_history.clear();
    picks_since_rasterize = 0;
    running = false;
    swap_selected_flag = false;
    highlight_duration_ms = 0;
    highlight_remaining_ms = 0;
    highlight_active = false;
    hide_cards_flag = false;
    selection_timer->stop();
    selection_phase = 0.0;
    update_card_jitter();
    update_accessible_description();
    update();
    return true;
}

bool card_widget::can_restore_session_state(const card_session_state& state) {
    return state.cards_per_deck >= 1 && state.decks_count >= 1
        && state.deck.size() == state.cards_per_deck * state.decks_count
        && card_picker::is_valid_restore(
               state.deck, state.deck_position, state.infinity_enabled,
               state.cards_per_deck
        );
}

void card_widget::clear_quiz() {
    if (gameplay_owner)
        return;
    picker.setup(0, 0, false);
    cards_per_deck = 0;
    decks_count = 0;
    infinity_enabled = false;
    discard_history.clear();
    picks_since_rasterize = 0;
    running = false;
    swap_selected_flag = false;
    highlight_duration_ms = 0;
    highlight_remaining_ms = 0;
    highlight_active = false;
    if (selection_timer) {
        selection_timer->stop();
    }
    selection_phase = 0.0;
    update_card_jitter();
    update_accessible_description();
    update();
}

void card_widget::update_accessible_description() {
    // Mirror paintEvent's visibility precedence, including the paused back.
    // A quiz prompt must not expose the hidden face to assistive technology.
    if (!has_cards()) {
        setAccessibleDescription(str_label("Empty card slot"));
    } else if (
        const auto* deck = gameplay_deck();
        deck && deck->status == gameplay::deck_status::failed
    ) {
        setAccessibleDescription(str_label("Failed deck. Card back."));
    } else if (display_hidden()) {
        setAccessibleDescription(str_label("Card hidden"));
    } else if (!display_running() || display_card_index() < 0) {
        setAccessibleDescription(str_label("Card back"));
    } else {
        setAccessibleDescription(
#ifdef KC_KDE
            i18n(
                "Current card: %1", card_label_from_index(display_card_index())
            )
#else
            str_label("Current card: %1")
                .arg(card_label_from_index(display_card_index()))
#endif
        );
    }
    const auto lines = gameplay_extra_lines();
    if (const auto* deck = gameplay_deck();
        deck && deck->status == gameplay::deck_status::completed)
        setAccessibleDescription(
            str_label("Completed deck.") + QLatin1Char('\n')
            + accessibleDescription()
        );
    if (has_cards() && !lines.isEmpty())
        setAccessibleDescription(
            accessibleDescription() + QLatin1Char('\n')
            + lines.join(QLatin1Char('\n'))
        );
}

void card_widget::trigger_highlight(int duration_ms) {
    if (duration_ms < 1) {
        duration_ms = 1;
    }
    highlight_duration_ms = duration_ms;
    highlight_remaining_ms = duration_ms;
    highlight_active = true;
    update();
}

void card_widget::tick_highlight(int delta_ms) {
    if (!highlight_active) {
        return;
    }
    if (highlight_duration_ms <= 0) {
        highlight_active = false;
        update();
        return;
    }
    highlight_remaining_ms -= delta_ms;
    if (highlight_remaining_ms <= 0) {
        highlight_active = false;
        highlight_remaining_ms = 0;
    }
    update();
}

void card_widget::record_discard() {
    if (!has_cards()) {
        return;
    }

    const int card_index = display_card_index();
    if (card_index < 0) {
        return;
    }

    discard_history.push_back({ card_rotation_deg, card_offset });
    while (discard_history.size() > 5) {
        discard_history.pop_front();
    }
}

int card_widget::total_weight_for_picks() const {
    if (strategy_weights.isEmpty()) {
        return 0;
    }

    const int current_position = picker.current_position();
    if (current_position < 0) {
        return 0;
    }

    int total_weight = 0;
    for (int position = 0; position <= current_position; ++position) {
        const int card_index = picker.card_index_at(position);
        const int rank_index = rank_index_from_card_index(card_index);
        if (rank_index >= 0 && rank_index < strategy_weights.size()) {
            total_weight += strategy_weights.at(rank_index);
        }
    }
    return total_weight;
}

qreal card_widget::highlight_strength() const {
    if (!highlight_active || highlight_duration_ms <= 0) {
        return 0.0;
    }
    const qreal remaining_ratio
        = highlight_remaining_ms / static_cast<qreal>(highlight_duration_ms);
    return std::clamp(remaining_ratio, 0.0, 1.0);
}

void card_widget::update_selection_pulse() {
    if (!swap_selected_flag) {
        return;
    }
    selection_phase += 0.35;
    if (selection_phase > 6.283) {
        selection_phase -= 6.283;
    }
    update();
}
