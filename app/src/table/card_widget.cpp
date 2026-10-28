#include "table/card_widget.hpp"
#include "settings/preferences.hpp"
#include "settings/theme_palette.hpp"
#include <shared_ui/qt/presentation.hpp>

#include "arch/asset_locator.hpp"
#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include <QEvent>
#include <QImage>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

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
    , swap_selected_flag(false)
    , random_gen()
    , card_rotation_deg(0.0)
    , card_offset(0.0, 0.0)
    , slot_rotated(false)
    , show_card_indexing_flag(false)
    , show_strategy_name_flag(false)
    , strategy_name()
    , selection_timer(std::make_unique<QTimer>())
    , selection_phase(0.0)
    , discard_history()
    , highlight_duration_ms(0)
    , highlight_remaining_ms(0)
    , highlight_active(false)
    , hide_cards_flag(false)
    , table_marking(application_image_service())
    , card_sheet_source(card_sheet_source_path())
    , card_images()
    , card_face_size()
    , displayed_card_images()
    , rasterizing(false)
    , presented_image_mode(false) {
    auto presentation = shared_ui::qt::presentation_for(*this);
    if (parent == nullptr) {
        presentation.context.felt_surface
            = theme_palette_registry::option(
                  load_table_appearance_preferences().palette
            )
                  .base_color();
        shared_ui::qt::apply_presentation(*this, presentation);
    }
    selection_timer->setInterval(45);
    QObject::connect(
        selection_timer.get(), &QTimer::timeout, this,
        &card_widget::update_selection_pulse
    );
    setAccessibleName(str_label("Playing card"));
    update_accessible_description();
    QObject::connect(
        &table_marking, &shared_ui::qt::raster_image_binding::changed, this,
        qOverload<>(&card_widget::update)
    );
    connect(&marking_source, &image_source_request::finished, this, [this] {
        auto target = table_marking.target();
        target.source = marking_source.source();
        table_marking.set_target(target);
    });
    marking_source_path
        = bundled_asset_path(QStringLiteral("brand/cuckoo.svg"));
    marking_source.load(marking_source_path);
}

card_widget::~card_widget() = default;

bool card_widget::event(QEvent* event) {
    const bool intent_changed = event
        && (event->type() == QEvent::Show || event->type() == QEvent::Hide
            || event->type() == QEvent::DevicePixelRatioChange
            || event->type() == QEvent::ScreenChangeInternal);
    const QPointer<card_widget> lifetime(this);
    const bool accepted = BaseWidget::event(event);
    if (lifetime && intent_changed)
        schedule_card_preparation();
    return accepted;
}

bool card_widget::bind_gameplay_deck(
    const gameplay::session& owner, gameplay::deck_id id
) {
    if (!owner.deck(id)
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
    presented_physical_cards = 0;
    hide_cards_flag = false;
    reset_presentation();
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
    return -1;
}

bool card_widget::display_running() const {
    if (const auto* deck = gameplay_deck()) {
        return deck->status == gameplay::deck_status::completed
            || (deck->status == gameplay::deck_status::active
                && gameplay_owner->phase() == gameplay::session_phase::running);
    }
    return false;
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
        if (!selection_timer->isActive()) {
            selection_timer->start();
        }
    } else {
        selection_timer->stop();
        selection_phase = 0.0;
    }
    update();
}

bool card_widget::swap_selected() const { return swap_selected_flag; }

void card_widget::set_slot_rotated(bool rotated) {
    if (slot_rotated == rotated) {
        return;
    }

    slot_rotated = rotated;
    schedule_card_preparation();
    update();
}

void card_widget::set_gameplay_layout_rotation(std::optional<qreal> degrees) {
    if (!gameplay_owner
        || (degrees
            && (!std::isfinite(*degrees) || *degrees < 0.0 || *degrees > 90.0))
        || gameplay_layout_rotation_deg == degrees)
        return;
    gameplay_layout_rotation_deg = degrees;
    schedule_card_preparation();
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

void card_widget::set_slot_number(int number) {
    if (slot_number == number)
        return;
    slot_number = number;
    if (frame_style == slot_frame_style::piping)
        update();
}

void card_widget::set_verdict_glow(bool enabled) {
    if (verdict_glow == enabled)
        return;
    verdict_glow = enabled;
    if (frame_style == slot_frame_style::piping)
        update();
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

void card_widget::set_strategy_name(const QString& name) {
    if (strategy_name == name) {
        return;
    }

    strategy_name = name;
    update();
}

void card_widget::set_table_marking_source(const QString& source) {
    if (marking_source_path == source)
        return;
    marking_source_path = source;
    const QPointer<card_widget> lifetime(this);
    auto target = table_marking.target();
    target.source = {};
    table_marking.set_target(target);
    if (!lifetime || marking_source_path != source)
        return;
    marking_source.load(source);
    if (lifetime && marking_source_path == source)
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

bool card_widget::has_cards() const {
    const auto* deck = gameplay_deck();
    return deck && !deck->stream.cards.empty();
}

bool card_widget::has_current_card() const { return display_card_index() >= 0; }

bool card_widget::is_deck_exhausted() const {
    const auto* deck = gameplay_deck();
    return deck && deck->status != gameplay::deck_status::active;
}

void card_widget::reset_presentation() {
    if (gameplay_owner)
        return;
    discard_history.clear();
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
    schedule_card_preparation();
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
