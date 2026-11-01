#include "shell/mobile_shell.hpp"
#include <shared_ui/qt/touch_layouts.hpp>
#include <shared_ui/qt/touch_play.hpp>

#include "arch/str_label.hpp"
#include "shell/main_window.hpp"
#include "table/table.hpp"
#include "table/touch_text.hpp"

#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace shared_ui::qt;

namespace {
constexpr int footer_height = 76;
constexpr int tray_height = 196;
constexpr int event_ms = 4000;

QString shown(const QString& draft) {
    QString text = draft;
    if (text.startsWith(u'-'))
        text[0] = QChar(0x2212);
    return text;
}

std::optional<std::int64_t> parsed(const QString& draft) {
    bool valid = false;
    const auto value = draft.toLongLong(&valid);
    return valid ? std::optional<std::int64_t>(value) : std::nullopt;
}

// Caption over value, as the HUD prints its readouts.
QWidget* readout(QWidget* parent, QLabel*& value, const QString& caption) {
    auto* box = new QWidget(parent);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    auto* label
        = new touch_label(caption, touch_type::caption, touch_ink::muted, box);
    label->set_custom_font(
        touch_font(touch_tokens_for(*label), 9, QFont::Medium, true, 0.16)
    );
    value = new touch_label({}, touch_type::value, touch_ink::text, box);
    column->addStretch();
    column->addWidget(label);
    column->addWidget(value);
    column->addStretch();
    return box;
}

touch_button*
icon_command(QWidget* parent, touch_icon icon, const QString& name, int side) {
    auto* button = new touch_button({}, touch_button_style::icon, parent);
    button->set_icon(icon);
    button->set_height(side);
    button->setFixedSize(side, side);
    button->setAccessibleName(name);
    return button;
}
} // namespace

void mobile_shell::build_table() {
    table_page = new touch_panel(touch_surface::table, this);
    table_page->setObjectName(QStringLiteral("mobile_table"));
    auto* column = new QVBoxLayout(table_page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    hud = new touch_hud(table_page);
    hud->setObjectName(QStringLiteral("mobile_hud"));
    column->addWidget(hud);
    table_row = new QHBoxLayout;
    table_row->setContentsMargins(0, 0, 0, 0);
    table_row->setSpacing(0);
    table_area = new QWidget(table_page);
    table_area->setObjectName(QStringLiteral("mobile_table_area"));
    auto* area = new QVBoxLayout(table_area);
    area->setContentsMargins(6, 6, 6, 6);
    table_row->addWidget(table_area, 1);
    column->addLayout(table_row, 1);

    footer = new touch_panel(touch_surface::bar, table_page);
    footer->setProperty("touch_edge", QStringLiteral("top"));
    footer->setObjectName(QStringLiteral("mobile_footer"));
    footer->setFixedHeight(footer_height);
    footer_stack = new QStackedLayout(footer);
    // 0: next card and Pause
    auto* normal = new QWidget(footer);
    auto* normal_row = new QHBoxLayout(normal);
    normal_row->setContentsMargins(16, 0, 16, 0);
    normal_row->setSpacing(12);
    normal_row->addWidget(
        readout(normal, footer_interval, str_label("Next card")), 1
    );
    footer_pause = new touch_button(
        str_label("Pause"), touch_button_style::paper, normal
    );
    footer_pause->setObjectName(QStringLiteral("mobile_pause"));
    footer_pause->set_icon(touch_icon::pause);
    footer_pause->setFixedWidth(180);
    connect(
        footer_pause, &QAbstractButton::clicked, this,
        &mobile_shell::pause_or_resume
    );
    normal_row->addWidget(footer_pause);
    footer_stack->addWidget(normal);
    // 1: the question tray when other decks keep dealing
    tray = new QWidget(footer);
    tray->setObjectName(QStringLiteral("mobile_question_tray"));
    auto* tray_column = new QVBoxLayout(tray);
    tray_column->setContentsMargins(16, 14, 16, 20);
    tray_column->setSpacing(12);
    auto* tray_head = new QHBoxLayout;
    tray_caption
        = new touch_label({}, touch_type::caption, touch_ink::soft, tray);
    tray_head->addWidget(tray_caption);
    tray_head->addStretch();
    tray_head->addWidget(new touch_label(
        str_label("Others keep dealing · no timer"), touch_type::note,
        touch_ink::muted, tray
    ));
    tray_column->addLayout(tray_head);
    tray_targets = new touch_target_strip(tray);
    tray_targets->set_compact(true);
    tray_column->addWidget(tray_targets);
    tray_nudge = new touch_nudge_row(tray);
    tray_nudge->setObjectName(QStringLiteral("mobile_tray_nudge"));
    tray_column->addWidget(tray_nudge);
    auto* tray_commands = new QHBoxLayout;
    tray_commands->setSpacing(8);
    auto* tray_pause
        = icon_command(tray, touch_icon::pause, str_label("Pause"), 56);
    auto* tray_keypad
        = icon_command(tray, touch_icon::keypad, str_label("Open keypad"), 56);
    tray_keypad->setObjectName(QStringLiteral("mobile_tray_keypad"));
    tray_skip = new touch_button(
        str_label("Skip"), touch_button_style::outline, tray
    );
    tray_skip->setObjectName(QStringLiteral("mobile_tray_skip"));
    tray_skip->set_height(56);
    tray_skip->setFixedWidth(84);
    tray_check = new touch_button(
        str_label("Check"), touch_button_style::primary, tray
    );
    tray_check->setObjectName(QStringLiteral("mobile_tray_check"));
    tray_check->set_height(56);
    tray_check->set_text_size(17);
    tray_commands->addWidget(tray_pause);
    tray_commands->addWidget(tray_keypad);
    tray_commands->addWidget(tray_skip);
    tray_commands->addWidget(tray_check, 1);
    tray_column->addLayout(tray_commands);
    footer_stack->addWidget(tray);
    connect(
        tray_pause, &QAbstractButton::clicked, this,
        &mobile_shell::pause_or_resume
    );
    connect(tray_keypad, &QAbstractButton::clicked, this, [this] {
        keypad_requested = true;
        refresh_quiz();
    });
    connect(
        tray_skip, &QAbstractButton::clicked, this, &mobile_shell::skip_focused
    );
    connect(
        tray_check, &QAbstractButton::clicked, this,
        &mobile_shell::check_focused
    );
    connect(tray_nudge, &touch_nudge_row::nudged, this, &mobile_shell::nudge);
    connect(tray_nudge, &touch_nudge_row::value_clicked, this, [this] {
        keypad_requested = true;
        refresh_quiz();
    });
    connect(
        tray_targets, &touch_target_strip::activated, this,
        [this](int index) {
            if (index >= 0 && index < static_cast<int>(batch_decks.size()))
                focus_deck(batch_decks[static_cast<std::size_t>(index)]);
        }
    );
    // 2: choosing decks for Swap or Details while paused
    auto* modes = new QWidget(footer);
    auto* mode_row = new QHBoxLayout(modes);
    mode_row->setContentsMargins(16, 0, 16, 0);
    mode_row->setSpacing(12);
    mode_hint = new touch_label({}, touch_type::small, touch_ink::text, modes);
    mode_hint->setWordWrap(true);
    mode_row->addWidget(mode_hint, 1);
    auto* done
        = new touch_button(str_label("Done"), touch_button_style::paper, modes);
    done->setObjectName(QStringLiteral("mobile_mode_done"));
    done->setFixedWidth(120);
    connect(done, &QAbstractButton::clicked, this, [this] {
        set_mode(table_mode::play);
    });
    mode_row->addWidget(done);
    footer_stack->addWidget(modes);
    column->addWidget(footer);

    banner_area = new QWidget(table_page);
    banner_area->setObjectName(QStringLiteral("mobile_banners"));
    banners = new QVBoxLayout(banner_area);
    banners->setContentsMargins(0, 0, 0, 0);
    banners->setSpacing(10);
    storage_banner = new touch_banner(banner_area);
    storage_banner->setObjectName(QStringLiteral("mobile_storage_banner"));
    storage_banner->set_message(
        str_label("Couldn't save the session"),
        str_label("Your last saved state is kept. You can keep playing."),
        str_label("Retry"), true
    );
    connect(storage_banner, &touch_banner::action_requested, this, [this] {
        window.retry_gameplay_from_ui();
        refresh();
    });
    event_banner = new touch_banner(banner_area);
    event_banner->setObjectName(QStringLiteral("mobile_event_banner"));
    connect(event_banner, &touch_banner::action_requested, this, [this] {
        restored_notice = false;
        event_banner->hide();
        pause_or_resume();
    });
    banners->addWidget(storage_banner);
    banners->addWidget(event_banner);
    storage_banner->hide();
    event_banner->hide();
    event_timer = new QTimer(this);
    event_timer->setSingleShot(true);
    event_timer->setInterval(event_ms);
    connect(event_timer, &QTimer::timeout, this, [this] {
        event_banner->hide();
        place_banners();
    });
}

void mobile_shell::place_banners() {
    if (!banner_area || !table_page)
        return;
    const bool any = storage_banner->isVisibleTo(banner_area)
        || event_banner->isVisibleTo(banner_area);
    banner_area->setVisible(any && current == page::table);
    if (!any)
        return;
    const int width = std::min(table_area->width() - 24, 520);
    const int height = banner_area->layout()->totalHeightForWidth(width);
    banner_area->setGeometry(
        table_area->x() + (table_area->width() - width) / 2, hud->height() + 12,
        width, std::max(0, height)
    );
    banner_area->raise();
}

void mobile_shell::show_event(const QString& title, const QString& text) {
    event_banner->set_message(title, text);
    event_banner->show();
    event_timer->start();
    place_banners();
}

void mobile_shell::pause_or_resume() {
    const QPointer<mobile_shell> guard(this);
    window.on_start_pause_triggered();
    if (!guard)
        return;
    if (view && view->active_gameplay_session()
        && view->active_gameplay_session()->phase()
            == gameplay::session_phase::running) {
        restored_notice = false;
        keypad_requested = false;
    }
    refresh();
}

void mobile_shell::set_mode(table_mode next) {
    if (mode == table_mode::swap && next != table_mode::swap && view)
        view->cancel_swap_choice();
    mode = next;
    if (mode_hint)
        mode_hint->setText(
            next == table_mode::swap
                ? str_label("Tap one deck, then another. The decks change "
                            "places; the dealing order stays.")
                : str_label("Tap a deck to see its details.")
        );
    refresh();
}

void mobile_shell::deck_tapped(int deck) {
    if (current != page::table || deck < 0)
        return;
    const gameplay::deck_id id { static_cast<std::size_t>(deck) };
    if (mode == table_mode::swap) {
        (void)view->choose_swap_deck(id);
        return;
    }
    if (mode == table_mode::details) {
        open_deck_sheet(id);
        return;
    }
    // While answering, tapping an asked deck makes it the current question.
    if (std::ranges::find(batch_decks, id) != batch_decks.end())
        focus_deck(id);
}

void mobile_shell::refresh_table() {
    auto* owner = view ? view->active_gameplay_session() : nullptr;
    if (!owner) {
        show_page(page::home);
        return;
    }
    const auto phase = owner->phase();
    if (phase == gameplay::session_phase::setup) {
        show_page(page::setup);
        return;
    }
    if (phase == gameplay::session_phase::finished) {
        show_page(page::result);
        return;
    }
    const auto& settings = owner->configuration();
    const auto& stats = owner->statistics();
    const auto& batch = owner->current_quiz_batch();
    const auto unresolved = owner->unresolved_quizzes();
    const bool frozen = batch && batch->remaining_ms && !unresolved.empty();
    const bool paused = phase == gameplay::session_phase::paused;
    QList<touch_hud_field> fields;
    if (settings.failure == gameplay::failure_policy::lives) {
        const int remaining = std::max(0, owner->remaining_lives());
        const int lost = std::max(0, settings.initial_lives - remaining);
        fields.append(
            { str_label("LIVES"),
              str_label("%1/%2").arg(remaining).arg(settings.initial_lives),
              remaining, lost,
              str_label("Lives: %1 remaining, %2 lost")
                  .arg(remaining)
                  .arg(lost) }
        );
    }
    const auto number = [](std::uint64_t value) {
        return QString::number(static_cast<qulonglong>(value));
    };
    fields.append({ str_label("ERRORS"), number(stats.errors), 0, 0, {} });
    if (frozen) {
        fields.append({ str_label("OPEN"), number(unresolved.size()), 0, 0, {} }
        );
    } else {
        fields.append({ str_label("SKIPS"), number(stats.skips), 0, 0, {} });
        fields.append(
            { str_label("POINTS"), number(stats.verified_cards), 0, 0, {} }
        );
    }
    if (landscape && frozen) {
        // The compact bar keeps points and drops skips.
        fields.removeIf([](const touch_hud_field& field) {
            return field.caption == QStringLiteral("OPEN");
        });
        fields.append(
            { str_label("POINTS"), number(stats.verified_cards), 0, 0, {} }
        );
    }
    hud->set_fields(fields);
    hud->set_title(
        frozen ? str_label("COUNT CHECK · TABLE FROZEN") : QString()
    );
    touch_hud_clock clock;
    if (frozen) {
        clock.value = touch_text::countdown(*batch->remaining_ms);
        clock.mode
            = paused ? touch_clock_mode::held : touch_clock_mode::countdown;
        clock.accessible = paused
            ? str_label("Answer time held at %1").arg(clock.value)
            : str_label("%1 left to answer").arg(clock.value);
    } else {
        clock.caption = str_label("ACTIVE");
        clock.value = touch_text::clock(owner->timing().play_time_ms);
        clock.mode = paused ? touch_clock_mode::held : touch_clock_mode::plain;
    }
    hud->set_clock(clock);
    footer_interval->setText(touch_text::interval(owner->pick_interval_ms()));
    footer->setFixedHeight(
        settings.global_quiz_pause || landscape ? footer_height : tray_height
    );
    footer_pause->setText(paused ? str_label("Resume") : str_label("Pause"));
    footer_pause->set_icon(paused ? touch_icon::play : touch_icon::pause);
    if (mode != table_mode::play && !paused)
        mode = table_mode::play;
    storage_banner->setVisible(!window.gameplay_storage_error.isEmpty());
    const bool no_lives = frozen
        && settings.failure == gameplay::failure_policy::lives
        && owner->remaining_lives() <= 0;
    if (no_lives) {
        event_timer->stop();
        event_banner->set_message(
            str_label("No lives left"),
            str_label("Finish this check. Every answer still counts, and the "
                      "session ends after it.")
        );
        event_banner->show();
    } else if (restored_notice && paused) {
        event_timer->stop();
        event_banner->set_message(
            str_label("Back where you left off"),
            str_label("The session is paused. Your answers and remaining time "
                      "are kept."),
            str_label("Resume")
        );
        event_banner->show();
    } else if (!event_timer->isActive()) {
        event_banner->hide();
    }
    refresh_quiz();
    refresh_pause();
    place_banners();
}

void mobile_shell::build_quiz() {
    quiz_sheet = new touch_sheet(*table_page, touch_surface::sheet_dark);
    quiz_sheet->setObjectName(QStringLiteral("mobile_quiz_sheet"));
    quiz_sheet->set_handle(false);
    quiz_sheet->set_scrim(false);
    auto* content = new QWidget;
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(16, 16, 16, 20);
    column->setSpacing(12);
    auto* head = new QHBoxLayout;
    head->setSpacing(12);
    auto* caption
        = new touch_label({}, touch_type::caption, touch_ink::soft, content);
    caption->setObjectName(QStringLiteral("mobile_quiz_caption"));
    caption->set_custom_font(
        touch_font(touch_tokens_for(*caption), 11, QFont::Medium, true, 0.16)
    );
    caption->setWordWrap(true);
    auto* rules
        = new touch_label({}, touch_type::mono, touch_ink::muted, content);
    rules->setObjectName(QStringLiteral("mobile_quiz_rules"));
    rules->set_custom_font(
        touch_font(touch_tokens_for(*rules), 11, QFont::Normal, true)
    );
    rules->setWordWrap(true);
    head->addWidget(caption, 1);
    head->addWidget(rules, 1);
    column->addLayout(head);
    quiz_targets = new touch_target_strip(content);
    quiz_targets->setObjectName(QStringLiteral("mobile_quiz_targets"));
    column->addWidget(quiz_targets);
    quiz_entry = new touch_entry(content);
    quiz_entry->setObjectName(QStringLiteral("mobile_quiz_entry"));
    column->addWidget(quiz_entry);
    quiz_keypad = new touch_keypad(content);
    quiz_keypad->setObjectName(QStringLiteral("mobile_quiz_keypad"));
    column->addWidget(quiz_keypad);
    auto* commands = new QHBoxLayout;
    commands->setSpacing(10);
    auto* pause
        = icon_command(content, touch_icon::pause, str_label("Pause"), 56);
    pause->setObjectName(QStringLiteral("mobile_quiz_pause"));
    quiz_skip = new touch_button(
        str_label("Skip"), touch_button_style::outline, content
    );
    quiz_skip->setObjectName(QStringLiteral("mobile_quiz_skip"));
    quiz_skip->set_height(56);
    quiz_skip->set_text_size(16);
    quiz_skip->setFixedWidth(96);
    quiz_check = new touch_button({}, touch_button_style::primary, content);
    quiz_check->setObjectName(QStringLiteral("mobile_quiz_check"));
    quiz_check->set_height(56);
    quiz_check->set_text_size(17);
    commands->addWidget(pause);
    commands->addWidget(quiz_skip);
    commands->addWidget(quiz_check, 1);
    column->addLayout(commands);
    quiz_sheet->set_content(content);

    quiz_side = new touch_panel(touch_surface::side_dark, table_page);
    quiz_side->setObjectName(QStringLiteral("mobile_quiz_side"));
    quiz_side->setFixedWidth(300);
    auto* side = new QVBoxLayout(quiz_side);
    side->setContentsMargins(12, 12, 12, 12);
    side->setSpacing(8);
    side_targets = new touch_target_strip(quiz_side);
    side_targets->set_compact(true);
    side->addWidget(side_targets);
    side_keypad = new touch_keypad(quiz_side);
    side_keypad->set_key_height(48);
    side_keypad->set_spacing(6);
    side->addWidget(side_keypad);
    auto* side_commands = new QHBoxLayout;
    side_commands->setSpacing(6);
    auto* side_pause
        = icon_command(quiz_side, touch_icon::pause, str_label("Pause"), 48);
    side_skip = new touch_button(
        str_label("Skip"), touch_button_style::outline, quiz_side
    );
    side_skip->set_height(48);
    side_skip->setFixedWidth(72);
    side_check = new touch_button({}, touch_button_style::primary, quiz_side);
    side_check->set_height(48);
    side_check->set_text_size(15);
    side_commands->addWidget(side_pause);
    side_commands->addWidget(side_skip);
    side_commands->addWidget(side_check, 1);
    side->addLayout(side_commands);
    side->addStretch();
    table_row->addWidget(quiz_side);
    quiz_side->hide();

    for (auto* keypad : { quiz_keypad, side_keypad }) {
        connect(
            keypad, &touch_keypad::digit_pressed, this,
            &mobile_shell::press_digit
        );
        connect(
            keypad, &touch_keypad::sign_pressed, this, &mobile_shell::press_sign
        );
        connect(
            keypad, &touch_keypad::delete_pressed, this,
            &mobile_shell::press_delete
        );
    }
    for (auto* strip : { quiz_targets, side_targets })
        connect(strip, &touch_target_strip::activated, this, [this](int index) {
            if (index >= 0 && index < static_cast<int>(batch_decks.size()))
                focus_deck(batch_decks[static_cast<std::size_t>(index)]);
        });
    for (auto* button : { pause, side_pause })
        connect(
            button, &QAbstractButton::clicked, this,
            &mobile_shell::pause_or_resume
        );
    for (auto* button : { quiz_skip, side_skip })
        connect(
            button, &QAbstractButton::clicked, this, &mobile_shell::skip_focused
        );
    for (auto* button : { quiz_check, side_check })
        connect(
            button, &QAbstractButton::clicked, this,
            &mobile_shell::check_focused
        );
}

QString mobile_shell::draft_for(gameplay::deck_id id) const {
    if (const auto found = drafts.find(id.value); found != drafts.end())
        return found->second;
    const auto* owner = view ? view->active_gameplay_session() : nullptr;
    const auto* deck = owner ? owner->deck(id) : nullptr;
    if (!deck)
        return {};
    return QString::number(static_cast<qlonglong>(
        deck->quiz ? deck->quiz->input : deck->latest_input
    ));
}

void mobile_shell::focus_deck(gameplay::deck_id id) {
    focused = id;
    refresh_quiz();
}

void mobile_shell::refresh_quiz() {
    auto* owner = view ? view->active_gameplay_session() : nullptr;
    if (!owner || !quiz_sheet || current != page::table) {
        if (quiz_sheet)
            quiz_sheet->close_sheet();
        return;
    }
    const auto phase = owner->phase();
    const bool running = phase == gameplay::session_phase::running;
    const auto& batch = owner->current_quiz_batch();
    const auto unresolved = owner->unresolved_quizzes();
    const bool frozen = batch && batch->remaining_ms && !unresolved.empty();
    const auto open = [&unresolved](gameplay::deck_id id) {
        return std::ranges::find(unresolved, id) != unresolved.end();
    };
    // Questions in table traversal order, the order focus moves in.
    std::vector<gameplay::deck_id> ordered;
    if (const auto first = owner->quiz_focus()) {
        auto at = *first;
        do {
            ordered.push_back(at);
            const auto next = owner->quiz_focus(at);
            if (!next || std::ranges::find(ordered, *next) != ordered.end())
                break;
            at = *next;
        } while (ordered.size() < owner->size());
    }
    if (frozen && batch->id != batch_id) {
        batch_id = batch->id;
        batch_decks = ordered;
        for (const auto id : ordered)
            drafts.erase(id.value);
        focused.reset();
    } else if (!frozen) {
        batch_id = 0;
        batch_decks = ordered;
    }
    if (!focused || !open(*focused)) {
        const auto next
            = focused ? owner->quiz_focus(*focused) : owner->quiz_focus();
        focused = next
            ? next
            : (ordered.empty() ? std::nullopt : std::optional(ordered.front()));
    }
    for (std::size_t index = 0; index < owner->size(); ++index) {
        const gameplay::deck_id id { index };
        const auto position = std::ranges::find(batch_decks, id);
        if (frozen && position != batch_decks.end())
            view->set_touch_ordinal(
                id,
                QString::number(
                    std::distance(batch_decks.begin(), position) + 1
                ),
                !open(id)
            );
        else
            view->set_touch_ordinal(id, {}, false);
        view->set_touch_dealing(
            id,
            !frozen && running && !unresolved.empty() && !open(id)
                && owner->deck(id)->status == gameplay::deck_status::active
        );
    }
    for (auto it = drafts.begin(); it != drafts.end();)
        it = open({ it->first }) ? std::next(it) : drafts.erase(it);

    const bool asking = running && focused.has_value();
    const bool side = asking && frozen && landscape;
    const bool sheet = asking && !side && (frozen || keypad_requested);
    if (!asking)
        keypad_requested = false;
    const bool tray_shown = asking && !frozen && !landscape;
    const bool side_was = quiz_side->isVisibleTo(table_page);
    quiz_side->setVisible(side);
    if (side != side_was && view)
        QTimer::singleShot(0, this, [this] { place_banners(); });
    if (mode == table_mode::play)
        footer_stack->setCurrentIndex(tray_shown ? 1 : 0);
    else
        footer_stack->setCurrentIndex(2);
    footer->setVisible(!side);

    QList<touch_target> targets;
    const auto* current_deck = focused ? owner->deck(*focused) : nullptr;
    for (std::size_t index = 0; index < batch_decks.size(); ++index) {
        const auto id = batch_decks[index];
        const auto* deck = owner->deck(id);
        touch_target target;
        target.index = QString::number(static_cast<qulonglong>(index + 1));
        target.title
            = str_label("Deck %1").arg(static_cast<qulonglong>(id.value + 1));
        target.detail
            = touch_text::short_strategy(
                  touch_text::strategy_name(deck->configuration.strategy_slug)
            )
                  .toUpper();
        if (deck->configuration.training)
            target.detail += str_label(" · TRAINING");
        target.value = shown(
            open(id)
                ? draft_for(id)
                : QString::number(static_cast<qlonglong>(deck->latest_input))
        );
        target.state = !open(id)
            ? touch_target_state::done
            : (focused && *focused == id ? touch_target_state::current
                                         : touch_target_state::open);
        target.accessible = str_label("Question %1, deck %2, value %3")
                                .arg(target.index)
                                .arg(static_cast<qulonglong>(id.value + 1))
                                .arg(target.value);
        targets.append(target);
    }
    const QString deck_number = focused
        ? QString::number(static_cast<qulonglong>(focused->value + 1))
        : QString();
    const QString draft = focused ? draft_for(*focused) : QString();
    const bool valid = parsed(draft).has_value();
    const bool can_skip = owner->configuration().allow_skip;
    if (auto* caption
        = quiz_sheet->findChild<QLabel*>(QStringLiteral("mobile_quiz_caption")))
        caption->setText(
            frozen ? str_label("COUNT CHECK · TABLE FROZEN")
                   : str_label("DECK %1 · RUNNING COUNT").arg(deck_number)
        );
    if (auto* rules
        = quiz_sheet->findChild<QLabel*>(QStringLiteral("mobile_quiz_rules")))
        rules->setText(
            frozen ? str_label("+20 s right · +10 wrong · +5 skip")
                   : str_label("Others keep dealing · no timer")
        );
    quiz_targets->set_targets(targets);
    quiz_targets->setVisible(frozen ? !targets.isEmpty() : targets.size() > 1);
    side_targets->set_targets(targets);
    tray_targets->set_targets(targets);
    tray_targets->setVisible(targets.size() > 1);
    quiz_entry->set_entry(
        str_label("DECK %1 · RUNNING COUNT").arg(deck_number),
        str_label("Starts from your last entry"), shown(draft)
    );
    tray_caption->setText(str_label("DECK %1 · RUNNING COUNT").arg(deck_number)
    );
    tray_nudge->set_value(shown(draft));
    const auto value = parsed(draft);
    for (const int step : { -2, -1, 1, 2 })
        tray_nudge->set_step_enabled(
            step,
            value
                && (step > 0 ? *value
                            <= std::numeric_limits<std::int64_t>::max() - step
                             : *value
                            >= std::numeric_limits<std::int64_t>::min() - step)
        );
    for (auto* skip_button : { quiz_skip, side_skip, tray_skip }) {
        skip_button->setVisible(can_skip);
        skip_button->setEnabled(asking);
    }
    for (auto* check : { quiz_check, side_check, tray_check })
        check->setEnabled(asking && valid);
    quiz_check->setText(
        frozen ? str_label("Check deck %1").arg(deck_number)
               : str_label("Check")
    );
    side_check->setText(str_label("Check deck %1").arg(deck_number));
    Q_UNUSED(current_deck);
    if (sheet) {
        quiz_sheet->open();
    } else {
        quiz_sheet->close_sheet();
    }
}

void mobile_shell::edit_draft(const QString& text) {
    if (!focused || !view)
        return;
    drafts[focused->value] = text;
    if (const auto value = parsed(text)) {
        if (const auto prompt = view->gameplay_quiz_prompt(*focused))
            (void)view->edit_gameplay_quiz(*prompt, *value);
    }
    refresh_quiz();
}

void mobile_shell::press_digit(int digit) {
    if (!focused)
        return;
    QString text = draft_for(*focused);
    if (text == QStringLiteral("0"))
        text.clear();
    else if (text == QStringLiteral("-0"))
        text = QStringLiteral("-");
    const auto digits = text.size() - (text.startsWith(u'-') ? 1 : 0);
    if (digits >= 18)
        return;
    edit_draft(text + QString::number(digit));
}

void mobile_shell::press_sign() {
    if (!focused)
        return;
    QString text = draft_for(*focused);
    if (text.startsWith(u'-'))
        text.remove(0, 1);
    else
        text.prepend(u'-');
    edit_draft(text);
}

void mobile_shell::press_delete() {
    if (!focused)
        return;
    QString text = draft_for(*focused);
    text.chop(1);
    edit_draft(text);
}

void mobile_shell::nudge(int delta) {
    if (!focused)
        return;
    const auto value = parsed(draft_for(*focused));
    if (!value
        || (delta > 0
            && *value > std::numeric_limits<std::int64_t>::max() - delta)
        || (delta < 0
            && *value < std::numeric_limits<std::int64_t>::min() - delta))
        return;
    edit_draft(QString::number(static_cast<qlonglong>(*value + delta)));
}

void mobile_shell::check_focused() {
    if (!focused || !view)
        return;
    const auto id = *focused;
    const auto value = parsed(draft_for(id));
    const auto prompt = view->gameplay_quiz_prompt(id);
    if (!value || !prompt)
        return;
    const QPointer<mobile_shell> guard(this);
    if (!view->edit_gameplay_quiz(*prompt, *value) || !guard)
        return;
    drafts.erase(id.value);
    (void)view->check_gameplay_quiz(*prompt);
    if (guard)
        refresh();
}

void mobile_shell::skip_focused() {
    if (!focused || !view)
        return;
    const auto id = *focused;
    if (const auto prompt = view->gameplay_quiz_prompt(id)) {
        const QPointer<mobile_shell> guard(this);
        drafts.erase(id.value);
        (void)view->skip_gameplay_quiz(*prompt);
        if (guard)
            refresh();
    }
}

void mobile_shell::build_pause() {
    pause_sheet = new touch_sheet(*table_page, touch_surface::sheet_paper);
    pause_sheet->setObjectName(QStringLiteral("mobile_pause_sheet"));
    pause_sheet->set_handle(false);
    pause_sheet->set_scrim(true);
    auto* content = new QWidget;
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(16, 20, 16, 20);
    column->setSpacing(14);
    auto* head = new QVBoxLayout;
    head->setSpacing(4);
    auto* title_row = new QHBoxLayout;
    title_row->addWidget(
        new touch_label(
            str_label("Paused"), touch_type::display, touch_ink::paper_ink,
            content
        ),
        1
    );
    // iOS has no system back; Home keeps the session paused.
    auto* leave = new touch_button(
        str_label("Home"), touch_button_style::link, content
    );
    leave->setObjectName(QStringLiteral("mobile_pause_home"));
    leave->set_height(44);
    connect(leave, &QAbstractButton::clicked, this, [this] {
        show_page(page::home);
    });
    title_row->addWidget(leave);
    head->addLayout(title_row);
    auto* explain = new touch_label(
        str_label(
            "Nothing is dealt and the answer time is held. Open questions keep "
            "their values and are locked until you resume."
        ),
        touch_type::small, touch_ink::paper_muted, content
    );
    explain->setWordWrap(true);
    head->addWidget(explain);
    column->addLayout(head);
    pause_locks = new QWidget(content);
    pause_lock_row = new QHBoxLayout(pause_locks);
    pause_lock_row->setContentsMargins(0, 0, 0, 0);
    pause_lock_row->setSpacing(8);
    column->addWidget(pause_locks);
    auto* resume = new touch_button(
        str_label("Resume"), touch_button_style::dark, content
    );
    resume->setObjectName(QStringLiteral("mobile_resume"));
    resume->set_icon(touch_icon::play);
    resume->set_height(60);
    resume->set_text_size(18);
    connect(
        resume, &QAbstractButton::clicked, this, &mobile_shell::pause_or_resume
    );
    column->addWidget(resume);
    auto* grid = new QGridLayout;
    grid->setSpacing(8);
    const auto tile
        = [content](const QString& text, touch_icon icon, const char* name) {
              auto* button
                  = new touch_button(text, touch_button_style::tile, content);
              button->set_icon(icon);
              button->setObjectName(QString::fromLatin1(name));
              button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
              return button;
          };
    auto* swap
        = tile(str_label("Swap decks"), touch_icon::swap, "mobile_pause_swap");
    auto* details = tile(
        str_label("Deck details"), touch_icon::details, "mobile_pause_details"
    );
    pause_corrections = tile(
        str_label("Corrections"), touch_icon::corrections,
        "mobile_pause_corrections"
    );
    auto* appearance = tile(
        str_label("Appearance"), touch_icon::appearance,
        "mobile_pause_appearance"
    );
    grid->addWidget(swap, 0, 0);
    grid->addWidget(details, 0, 1);
    grid->addWidget(pause_corrections, 1, 0);
    grid->addWidget(appearance, 1, 1);
    column->addLayout(grid);
    column->addWidget(new touch_divider(true, content));
    pause_interval = new touch_stepper;
    pause_interval->setObjectName(QStringLiteral("mobile_pause_interval"));
    pause_interval->set_on_paper(true);
    pause_interval->set_range(
        gameplay::session::minimum_pick_interval_ms,
        gameplay::session::maximum_pick_interval_ms
    );
    pause_interval->set_step(100);
    pause_interval->set_formatter([](double value) {
        return touch_text::interval(static_cast<int>(std::lround(value)));
    });
    pause_interval->set_accessible_names(
        str_label("Faster"), str_label("Slower")
    );
    auto* interval_row = new touch_setting_row(
        str_label("Next card every"), pause_interval, content
    );
    interval_row->set_on_paper(true);
    column->addWidget(interval_row);
    auto* finish = new touch_button(
        str_label("Finish session…"), touch_button_style::danger, content
    );
    finish->setObjectName(QStringLiteral("mobile_finish"));
    column->addWidget(finish);
    pause_sheet->set_content(content);

    connect(swap, &QAbstractButton::clicked, this, [this] {
        set_mode(table_mode::swap);
    });
    connect(details, &QAbstractButton::clicked, this, [this] {
        set_mode(table_mode::details);
    });
    connect(pause_corrections, &QAbstractButton::clicked, this, [this] {
        window.on_gameplay_review_triggered();
    });
    connect(appearance, &QAbstractButton::clicked, this, [this] {
        window.on_settings_triggered();
    });
    connect(
        pause_interval, &touch_stepper::value_changed, this,
        [this](double value) {
            if (window.speed_slider)
                window.speed_slider->setValue(static_cast<int>(std::lround(value
                )));
            refresh();
        }
    );

    finish_sheet = new touch_sheet(*table_page, touch_surface::sheet_paper);
    finish_sheet->setObjectName(QStringLiteral("mobile_finish_sheet"));
    finish_sheet->set_dismissible(true);
    connect(
        finish_sheet, &touch_sheet::dismissed, finish_sheet,
        &touch_sheet::close_sheet
    );
    auto* confirm = new QWidget;
    auto* confirm_column = new QVBoxLayout(confirm);
    confirm_column->setContentsMargins(20, 0, 20, 20);
    confirm_column->setSpacing(12);
    confirm_column->addWidget(new touch_label(
        str_label("Finish this session?"), touch_type::heading,
        touch_ink::paper_ink
    ));
    auto* consequence = new touch_label(
        str_label(
            "It is saved as a normal result with everything answered so far. "
            "Open questions are not scored."
        ),
        touch_type::small, touch_ink::paper_muted
    );
    consequence->setWordWrap(true);
    confirm_column->addWidget(consequence);
    auto* keep = new touch_button(
        str_label("Keep playing"), touch_button_style::outline_ink
    );
    auto* end
        = new touch_button(str_label("Finish"), touch_button_style::alert);
    end->setObjectName(QStringLiteral("mobile_finish_confirm"));
    end->set_height(52);
    end->set_text_size(16);
    confirm_column->addLayout(touch_row_layout({ keep, end }));
    finish_sheet->set_content(confirm);
    connect(finish, &QAbstractButton::clicked, this, [this] {
        finish_sheet->open();
    });
    connect(
        keep, &QAbstractButton::clicked, finish_sheet, &touch_sheet::close_sheet
    );
    connect(end, &QAbstractButton::clicked, this, [this] {
        finish_sheet->close_sheet();
        if (view && view->active_gameplay_session()
            && view->active_gameplay_session()->phase()
                == gameplay::session_phase::paused)
            (void)view->finish_gameplay_runtime();
        refresh();
    });
}

void mobile_shell::refresh_pause() {
    auto* owner = view ? view->active_gameplay_session() : nullptr;
    const bool paused = owner
        && owner->phase() == gameplay::session_phase::paused
        && current == page::table;
    if (!paused || mode != table_mode::play) {
        pause_sheet->close_sheet();
        if (!paused)
            finish_sheet->close_sheet();
        return;
    }
    while (auto* item = pause_lock_row->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    for (const auto id : owner->unresolved_quizzes()) {
        auto* lock = new touch_panel(touch_surface::locked, pause_locks);
        lock->setFixedHeight(48);
        auto* row = new QHBoxLayout(lock);
        row->setContentsMargins(10, 0, 10, 0);
        row->setSpacing(8);
        auto* icon = new touch_button({}, touch_button_style::bare_icon, lock);
        icon->set_icon(touch_icon::lock);
        icon->set_on_paper(true);
        icon->setFixedSize(16, 16);
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        icon->setFocusPolicy(Qt::NoFocus);
        row->addWidget(icon);
        row->addWidget(new touch_label(
            str_label("Deck %1 · %2")
                .arg(static_cast<qulonglong>(id.value + 1))
                .arg(shown(draft_for(id))),
            touch_type::mono_body, touch_ink::paper_muted, lock
        ));
        row->addStretch();
        pause_lock_row->addWidget(lock, 1);
    }
    pause_locks->setVisible(!owner->unresolved_quizzes().empty());
    const auto corrections = view->gameplay_corrections().size();
    pause_corrections->set_trailing(
        corrections ? QString::number(static_cast<qulonglong>(corrections))
                    : QString()
    );
    pause_corrections->setEnabled(corrections > 0);
    {
        const QSignalBlocker blocker(pause_interval);
        pause_interval->set_value(owner->pick_interval_ms());
    }
    if (!pause_sheet->is_open() && !finish_sheet->is_open())
        pause_sheet->open();
    else if (pause_sheet->is_open())
        pause_sheet->open(); // re-place after content changes
}
