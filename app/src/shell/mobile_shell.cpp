#include "shell/mobile_shell.hpp"
#include <shared_ui/qt/presentation.hpp>
#include <shared_ui/qt/touch_layouts.hpp>
#include <shared_ui/qt/touch_play.hpp>

#include "arch/str_label.hpp"
#include "settings/gameplay_drill.hpp"
#include "settings/strategy_data.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_layout.hpp"
#include "table/table.hpp"
#include "table/touch_text.hpp"

#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QResizeEvent>
#include <QSettings>
#include <QStackedLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

using namespace shared_ui::qt;

namespace {
// Fixed bars of the table page; the table gets the rest.
constexpr int hud_height = 56;
constexpr int compact_hud_height = 44;
constexpr int footer_height = 76;
constexpr int tray_height = 196;

QString drill_summary(const gameplay_drill& drill) {
    QStringList strategies;
    for (const auto& deck : drill.decks) {
        const auto name = touch_text::short_strategy(
            touch_text::strategy_name(deck.configuration.strategy_slug)
        );
        if (!strategies.contains(name))
            strategies.append(name);
    }
    QStringList parts {
        drill.decks.size() == 1
            ? str_label("1 deck")
            : str_label("%1 decks")
                  .arg(static_cast<qulonglong>(drill.decks.size())),
        strategies.join(QStringLiteral(" + ")),
    };
    if (std::ranges::any_of(drill.decks, [](const drill_deck& deck) {
            return deck.configuration.infinite;
        }))
        parts.append(str_label("endless"));
    if (std::ranges::any_of(drill.decks, [](const drill_deck& deck) {
            return deck.configuration.training;
        }))
        parts.append(str_label("Training"));
    parts.append(
        drill.configuration.failure == gameplay::failure_policy::lives
            ? str_label("Lives %1").arg(drill.configuration.initial_lives)
            : str_label("Block")
    );
    parts.append(
        drill.configuration.source == gameplay::quiz_source::physical_joker
            ? str_label("Jokers")
            : str_label("Virtual")
    );
    return parts.join(QStringLiteral(" · "));
}
} // namespace

mobile_shell::mobile_shell(main_window& owner)
    : QWidget(&owner)
    , window(owner) {
    setObjectName(QStringLiteral("mobile_shell"));
    apply_presentation(
        *this, { material::felt, platform_profile::android, {} }
    );
    pages = new QStackedLayout(this);
    pages->setContentsMargins(0, 0, 0, 0);
    build_home();
    build_setup();
    build_strategy();
    build_table();
    build_result();
    build_progress();
    build_deck_sheet();
    build_quiz();
    build_pause();
    build_name_sheet();
    pages->addWidget(home_page);
    pages->addWidget(setup_page);
    pages->addWidget(strategy_page);
    pages->addWidget(table_page);
    pages->addWidget(result_page);
    pages->addWidget(progress_page);
    show_page(page::home);
}

QWidget* mobile_shell::table_host() const { return table_area; }

void mobile_shell::attach_table(table& target) {
    view = &target;
    target.set_touch_presentation(true);
    connect(
        &target, &table::gameplay_runtime_updated, this, &mobile_shell::refresh
    );
    connect(&target, &table::gameplay_session_changed, this, [this] {
        batch_id = 0;
        batch_decks.clear();
        drafts.clear();
        focused.reset();
        seen_phase.reset();
        seen_status.clear();
        refresh();
    });
    connect(
        &target, &table::gameplay_corrections_changed, this,
        &mobile_shell::refresh
    );
    connect(
        &target, &table::gameplay_deck_tapped, this, &mobile_shell::deck_tapped
    );
    connect(
        &target, &table::gameplay_quiz_resolved, this,
        [this](const gameplay::quiz_answer& answer) {
            if (answer.timed_out)
                show_event(
                    str_label("Time's up"),
                    str_label(
                        "The numbers you had entered were checked as answers. "
                        "Nothing was skipped."
                    )
                );
        }
    );
}

QSize mobile_shell::table_extent() const {
    const int top = landscape ? compact_hud_height : hud_height;
    int bottom = landscape ? 0 : footer_height;
    if (!landscape && view && view->active_gameplay_session()
        && !view->active_gameplay_session()->configuration().global_quiz_pause)
        bottom = tray_height;
    int side = 0;
    if (landscape && quiz_side && quiz_side->isVisibleTo(this))
        side = quiz_side->width();
    if (landscape)
        bottom = footer_height;
    return { std::max(1, width() - side - 12),
             std::max(1, height() - top - bottom - 12) };
}

void mobile_shell::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const bool wide = width() > height();
    if (wide != landscape) {
        landscape = wide;
        if (hud)
            hud->set_compact(wide);
        refresh_quiz();
    }
    // Hidden pages do not lay out; the table keeps a real extent anyway so
    // recommendations and packing work from any screen.
    if (view && current != page::table)
        view->resize(table_extent());
    place_banners();
}

void mobile_shell::show_page(const page target) {
    if (target != page::table && pause_sheet)
        pause_sheet->close_sheet();
    if (target != page::table && quiz_sheet)
        quiz_sheet->close_sheet();
    if (target != page::setup && deck_sheet && current != page::strategy)
        deck_sheet->close_sheet();
    current = target;
    switch (target) {
    case page::home:
        pages->setCurrentWidget(home_page);
        break;
    case page::setup:
        pages->setCurrentWidget(setup_page);
        break;
    case page::strategy:
        pages->setCurrentWidget(strategy_page);
        break;
    case page::table:
        pages->setCurrentWidget(table_page);
        break;
    case page::result:
        pages->setCurrentWidget(result_page);
        break;
    case page::progress:
        pages->setCurrentWidget(progress_page);
        break;
    }
    refresh();
}

void mobile_shell::show_setup() {
    if (setup_steps) {
        setup_steps->set_current(0);
        setup_stack->setCurrentIndex(0);
    }
    if (setup_error)
        setup_error->clear();
    show_page(page::setup);
}

void mobile_shell::show_table() {
    set_mode(table_mode::play);
    show_page(page::table);
}

void mobile_shell::report_setup_error(const QString& message) {
    if (setup_error) {
        setup_error->setText(message);
        setup_error->setVisible(!message.isEmpty());
    }
    if (!message.isEmpty() && current != page::setup)
        show_page(page::setup);
}

QLabel* mobile_shell::setup_message() const { return setup_error; }

void mobile_shell::go_back() {
    for (auto* sheet : { name_sheet, deck_sheet, finish_sheet })
        if (sheet && sheet->is_open()) {
            sheet->close_sheet();
            return;
        }
    switch (current) {
    case page::home:
        return;
    case page::setup:
        if (setup_stack->currentIndex() == 1) {
            setup_steps->set_current(0);
            setup_stack->setCurrentIndex(0);
            refresh_setup();
            return;
        }
        show_page(page::home);
        return;
    case page::strategy:
        if (strategy_for_deck) {
            show_page(page::setup);
            deck_sheet->open();
            return;
        }
        show_page(page::home);
        return;
    case page::table: {
        if (mode != table_mode::play) {
            set_mode(table_mode::play);
            return;
        }
        if (keypad_requested) {
            keypad_requested = false;
            refresh_quiz();
            return;
        }
        const auto* owner = view ? view->active_gameplay_session() : nullptr;
        if (owner && owner->phase() == gameplay::session_phase::running) {
            pause_or_resume();
            return;
        }
        show_page(page::home);
        return;
    }
    case page::result:
    case page::progress:
        show_page(page::home);
        return;
    }
}

void mobile_shell::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Back || event->key() == Qt::Key_Escape) {
        go_back();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void mobile_shell::observe_session() {
    const auto* owner = view ? view->active_gameplay_session() : nullptr;
    if (!owner) {
        seen_phase.reset();
        return;
    }
    const auto phase = owner->phase();
    if (seen_phase && *seen_phase != gameplay::session_phase::finished
        && phase == gameplay::session_phase::finished && current == page::table)
        show_page(page::result);
    if (seen_status.size() == owner->size()) {
        for (std::size_t index = 0; index < owner->size(); ++index) {
            const auto status = owner->deck({ index })->status;
            if (seen_status[index] == gameplay::deck_status::active
                && status == gameplay::deck_status::completed
                && phase != gameplay::session_phase::finished) {
                std::size_t live = 0;
                for (std::size_t other = 0; other < owner->size(); ++other)
                    live += owner->deck({ other })->status
                        == gameplay::deck_status::active;
                show_event(
                    str_label("Shoe finished on deck %1")
                        .arg(static_cast<qulonglong>(index + 1)),
                    str_label("Final count %1. %2")
                        .arg(touch_signed(static_cast<qint64>(
                            owner->deck({ index })->running_count
                        )))
                        .arg(
                            live == 1 ? str_label("One deck keeps going.")
                                      : str_label("%1 decks keep going.")
                                            .arg(static_cast<qulonglong>(live))
                        )
                );
            }
        }
    }
    seen_status.clear();
    for (std::size_t index = 0; index < owner->size(); ++index)
        seen_status.push_back(owner->deck({ index })->status);
    seen_phase = phase;
}

void mobile_shell::refresh() {
    if (!view)
        return;
    observe_session();
    switch (current) {
    case page::home:
        refresh_home();
        break;
    case page::setup:
        refresh_setup();
        break;
    case page::strategy:
        refresh_strategy();
        break;
    case page::table:
        refresh_table();
        break;
    case page::result:
        refresh_result();
        break;
    case page::progress:
        refresh_progress();
        break;
    }
    if (deck_sheet && deck_sheet->is_open())
        refresh_deck_sheet();
}

void mobile_shell::build_home() {
    home_page = new touch_panel(touch_surface::table, this);
    home_page->setObjectName(QStringLiteral("mobile_home"));
    auto* column = new QVBoxLayout(home_page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto* bar = new touch_top_bar({}, home_page);
    bar->set_back_visible(false);
    bar->set_wordmark(str_label("k"), str_label("cuckoo"), str_label("unter"));
    auto* appearance = new touch_button({}, touch_button_style::bare_icon, bar);
    appearance->set_icon(touch_icon::sun);
    appearance->setFixedSize(48, 48);
    appearance->setAccessibleName(str_label("Appearance"));
    appearance->setObjectName(QStringLiteral("mobile_appearance"));
    connect(appearance, &QAbstractButton::clicked, this, [this] {
        window.on_settings_triggered();
    });
    bar->set_trailing(appearance);
    column->addWidget(bar);

    auto* body = new QWidget;
    home_body = new QVBoxLayout(body);
    home_body->setContentsMargins(0, 18, 0, 24);
    home_body->setSpacing(18);
    // Cards are inset; list rows run edge to edge.
    auto* inset = new QVBoxLayout;
    inset->setContentsMargins(16, 0, 16, 0);
    inset->setSpacing(18);
    home_body->addLayout(inset);

    home_session = new touch_panel(touch_surface::paper, body);
    home_session->setObjectName(QStringLiteral("mobile_paused_session"));
    auto* shadow = new QGraphicsDropShadowEffect(home_session);
    shadow->setBlurRadius(30);
    shadow->setOffset(0, 14);
    shadow->setColor(QColor(0, 0, 0, 90));
    home_session->setGraphicsEffect(shadow);
    auto* card = new QVBoxLayout(home_session);
    card->setContentsMargins(16, 16, 16, 16);
    card->setSpacing(12);
    auto* head = new QHBoxLayout;
    auto* caption = new touch_label(
        str_label("Paused session"), touch_type::caption,
        touch_ink::paper_caption, home_session
    );
    caption->set_custom_font(
        touch_font(touch_tokens_for(*caption), 11, QFont::DemiBold, true, 0.16)
    );
    home_session_time = new touch_label(
        {}, touch_type::mono, touch_ink::paper_muted, home_session
    );
    head->addWidget(caption);
    head->addStretch();
    head->addWidget(home_session_time);
    card->addLayout(head);
    home_session_title = new touch_label(
        {}, touch_type::lead, touch_ink::paper_ink, home_session
    );
    home_session_title->setWordWrap(true);
    card->addWidget(home_session_title);
    home_session_chips = new touch_tag_flow(home_session);
    card->addWidget(home_session_chips);
    auto* note = new touch_label(
        str_label(
            "Opens paused, exactly where you left it. Press Resume on the "
            "table when you are ready."
        ),
        touch_type::small, touch_ink::paper_muted, home_session
    );
    note->setWordWrap(true);
    card->addWidget(note);
    auto* resume = new touch_button(
        str_label("Continue"), touch_button_style::dark, home_session
    );
    resume->setObjectName(QStringLiteral("mobile_continue"));
    connect(resume, &QAbstractButton::clicked, this, [this] {
        restored_notice = true;
        show_table();
    });
    card->addWidget(resume);
    inset->addWidget(home_session);

    auto* start = new touch_button(
        str_label("New game"), touch_button_style::primary, body
    );
    start->setObjectName(QStringLiteral("mobile_new_game"));
    start->set_icon(touch_icon::plus);
    start->set_height(60);
    start->set_text_size(17);
    connect(start, &QAbstractButton::clicked, this, [this] {
        window.on_new_game_triggered();
    });
    inset->addWidget(start);

    home_drills = new QWidget(body);
    auto* drills_column = new QVBoxLayout(home_drills);
    drills_column->setContentsMargins(0, 0, 0, 0);
    drills_column->setSpacing(0);
    auto* drills_head = new QHBoxLayout;
    drills_head->setContentsMargins(16, 4, 16, 8);
    drills_head->addWidget(new touch_label(
        str_label("Saved drills"), touch_type::caption, touch_ink::muted,
        home_drills
    ));
    drills_head->addStretch();
    home_drill_count
        = new touch_label({}, touch_type::mono, touch_ink::muted, home_drills);
    drills_head->addWidget(home_drill_count);
    drills_column->addLayout(drills_head);
    home_drill_rows = new QVBoxLayout;
    home_drill_rows->setContentsMargins(0, 0, 0, 0);
    home_drill_rows->setSpacing(0);
    drills_column->addLayout(home_drill_rows);
    home_body->addWidget(home_drills);

    auto* navigation = new QVBoxLayout;
    navigation->setContentsMargins(0, 0, 0, 0);
    navigation->setSpacing(0);
    auto* progress
        = new touch_list_row(str_label("Progress and history"), body);
    progress->setObjectName(QStringLiteral("mobile_progress"));
    progress->set_icon(touch_icon::chart);
    progress->set_chevron(true);
    progress->set_title_type(touch_type::item_plain);
    connect(progress, &QAbstractButton::clicked, this, [this] {
        show_page(page::progress);
    });
    auto* strategies = new touch_list_row(str_label("Strategies"), body);
    strategies->setObjectName(QStringLiteral("mobile_strategies"));
    strategies->set_icon(touch_icon::book);
    strategies->set_chevron(true);
    strategies->set_title_type(touch_type::item_plain);
    connect(strategies, &QAbstractButton::clicked, this, [this] {
        open_strategy(false);
    });
    navigation->addWidget(progress);
    navigation->addWidget(strategies);
    home_body->addLayout(navigation);
    home_body->addStretch();
    column->addWidget(new touch_scroll(body, home_page), 1);
}

void mobile_shell::refresh_home() {
    const auto* owner = view ? view->active_gameplay_session() : nullptr;
    const bool live = owner
        && (owner->phase() == gameplay::session_phase::running
            || owner->phase() == gameplay::session_phase::paused);
    home_session->setVisible(live);
    if (live) {
        QStringList names;
        for (std::size_t index = 0; index < owner->size(); ++index) {
            const auto name = touch_text::strategy_name(
                owner->deck({ index })->configuration.strategy_slug
            );
            if (!names.contains(name))
                names.append(name);
        }
        const auto strategies = names.size() == 2
            ? names.join(str_label(" and "))
            : names.join(QStringLiteral(", "));
        home_session_title->setText(
            (owner->size() == 1
                 ? str_label("1 deck")
                 : str_label("%1 decks")
                       .arg(static_cast<qulonglong>(owner->size())))
            + QStringLiteral(" · ") + strategies
        );
        home_session_time->setText(
            str_label("active %1")
                .arg(touch_text::clock(owner->timing().play_time_ms))
        );
        QStringList chips;
        const auto& settings = owner->configuration();
        if (settings.failure == gameplay::failure_policy::lives)
            chips.append(str_label("Lives %1 of %2")
                             .arg(owner->remaining_lives())
                             .arg(settings.initial_lives));
        else
            chips.append(str_label("Block"));
        chips.append(
            settings.scope == gameplay::quiz_scope::multi ? str_label("Multi")
                                                          : str_label("Single")
        );
        chips.append(touch_text::dealing_summary(settings));
        const auto open = owner->unresolved_quizzes().size();
        if (open == 1)
            chips.append(str_label("1 question open"));
        else if (open > 1)
            chips.append(str_label("%1 questions open")
                             .arg(static_cast<qulonglong>(open)));
        home_session_chips->set_tags(chips, touch_tag_style::chip_paper);
    }

    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    const auto cap = gameplay::slot_technical_cap;
    gameplay_drill_service service(storage, { cap, cap });
    drills = service.load().value_or(QVector<gameplay_drill> {});
    while (auto* item = home_drill_rows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    home_drill_count->setText(str_label("%1 of %2")
                                  .arg(drills.size())
                                  .arg(gameplay_drill_service::maximum_drills));
    for (int index = 0; index < drills.size(); ++index) {
        const auto drill = drills.at(index);
        auto* row = new touch_list_row(drill.name, home_drills);
        row->setObjectName(QStringLiteral("mobile_drill_%1").arg(index));
        row->set_subtitle(drill_summary(drill));
        row->set_trailing_text(str_label("Load"));
        row->setEnabled(!live);
        connect(row, &QAbstractButton::clicked, this, [this, drill] {
            if (!window.load_gameplay_drill(drill))
                report_setup_error(str_label(
                    "This drill can no longer be loaded. The current session "
                    "is unchanged."
                ));
        });
        home_drill_rows->addWidget(row);
    }
    home_drills->setVisible(!drills.isEmpty());
}
