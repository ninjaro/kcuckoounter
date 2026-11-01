#include "shell/mobile_shell.hpp"
#include <shared_ui/qt/touch_layouts.hpp>

#include "arch/str_label.hpp"
#include "settings/gameplay_drill.hpp"
#include "settings/strategy_data.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_layout.hpp"
#include "table/table.hpp"
#include "table/touch_text.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScroller>
#include <QSettings>
#include <QSlider>
#include <QStackedLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace shared_ui::qt;

namespace {
QWidget* footer_bar(QWidget* parent, QHBoxLayout*& row) {
    auto* bar = new touch_panel(touch_surface::footer, parent);
    row = new QHBoxLayout(bar);
    row->setContentsMargins(16, 12, 16, 20);
    row->setSpacing(10);
    return bar;
}

QWidget* scroll_page(QWidget* parent, QVBoxLayout*& body) {
    auto* content = new QWidget;
    body = new QVBoxLayout(content);
    body->setContentsMargins(16, 0, 16, 16);
    body->setSpacing(14);
    return new touch_scroll(content, parent);
}

// The search field of the strategy list: an icon and a plain line edit on
// the kit's translucent field.
QLineEdit* search_field(QWidget* parent, touch_panel*& field) {
    field = new touch_panel(touch_surface::locked, parent);
    field->setFixedHeight(48);
    auto* row = new QHBoxLayout(field);
    row->setContentsMargins(10, 0, 14, 0);
    row->setSpacing(6);
    auto* icon = new touch_button({}, touch_button_style::bare_icon, field);
    icon->set_icon(touch_icon::search);
    icon->setFixedSize(28, 28);
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    icon->setFocusPolicy(Qt::NoFocus);
    auto* edit = new QLineEdit(field);
    const auto tokens = touch_tokens_for(*field);
    edit->setFont(touch_font(tokens, 15));
    edit->setFrame(false);
    edit->setStyleSheet(
        QStringLiteral(
            "QLineEdit { background: transparent; border: 0; color: %1; }"
        )
            .arg(tokens.text.name())
    );
    QPalette colours = edit->palette();
    colours.setColor(QPalette::PlaceholderText, tokens.muted);
    edit->setPalette(colours);
    row->addWidget(icon);
    row->addWidget(edit, 1);
    return edit;
}
} // namespace

void mobile_shell::build_setup() {
    setup_page = new touch_panel(touch_surface::ground, this);
    setup_page->setObjectName(QStringLiteral("mobile_setup"));
    auto* column = new QVBoxLayout(setup_page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto* bar = new touch_top_bar(str_label("New game"), setup_page);
    auto* drill_button = new touch_button(
        str_label("Drills"), touch_button_style::outline_accent, bar
    );
    drill_button->setObjectName(QStringLiteral("mobile_drills"));
    connect(drill_button, &QAbstractButton::clicked, this, [this] {
        window.on_saved_drills_triggered();
    });
    bar->set_trailing(drill_button);
    connect(bar, &touch_top_bar::back_requested, this, [this] {
        if (setup_stack->currentIndex() == 1) {
            setup_steps->set_current(0);
            setup_stack->setCurrentIndex(0);
            refresh_setup();
            return;
        }
        show_page(page::home);
    });
    column->addWidget(bar);
    setup_steps = new touch_choice(
        { str_label("1 · RULES"), str_label("2 · DECKS") },
        touch_choice_style::steps, setup_page
    );
    setup_steps->setObjectName(QStringLiteral("mobile_setup_steps"));
    auto* steps_row = new QHBoxLayout;
    steps_row->setContentsMargins(16, 0, 16, 12);
    steps_row->addWidget(setup_steps);
    column->addLayout(steps_row);
    setup_stack = new QStackedLayout;
    column->addLayout(setup_stack, 1);

    // Rules
    QVBoxLayout* rules = nullptr;
    setup_stack->addWidget(scroll_page(setup_page, rules));
    auto* mistakes = new touch_section(str_label("Mistakes"));
    penalty = new touch_choice({ str_label("Lives"), str_label("Block") });
    penalty->setObjectName(QStringLiteral("mobile_penalty"));
    mistakes->add_row(new touch_setting_row(str_label("Penalty"), penalty));
    lives = new touch_stepper;
    lives->setObjectName(QStringLiteral("mobile_lives"));
    lives->set_range(1, 12);
    lives->set_value_width(44);
    lives->set_accessible_names(
        str_label("Fewer lives"), str_label("More lives")
    );
    lives_row = new touch_setting_row(str_label("Lives for the table"), lives);
    mistakes->add_row(lives_row);
    lives_hint = mistakes->add_hint({});
    rules->addWidget(mistakes);

    auto* questions = new touch_section(str_label("Questions"));
    source = new touch_choice({ str_label("Jokers"), str_label("Virtual") });
    source->setObjectName(QStringLiteral("mobile_source"));
    questions->add_row(new touch_setting_row(str_label("Asked by"), source));
    scope = new touch_choice({ str_label("Single"), str_label("Multi") });
    scope->setObjectName(QStringLiteral("mobile_scope"));
    questions->add_row(new touch_setting_row(str_label("Who answers"), scope));
    freeze = new touch_switch;
    freeze->setObjectName(QStringLiteral("mobile_freeze"));
    freeze->setAccessibleName(str_label("Freeze table while answering"));
    freeze_row = new touch_setting_row(
        str_label("Freeze table while answering"), freeze
    );
    questions->add_row(freeze_row);
    skip = new touch_switch;
    skip->setObjectName(QStringLiteral("mobile_allow_skip"));
    skip->setAccessibleName(str_label("Allow Skip"));
    questions->add_row(new touch_setting_row(str_label("Allow Skip"), skip));
    questions->add_hint(str_label("Question timing is automatic and "
                                  "unpredictable; there is nothing to tune."));
    rules->addWidget(questions);

    auto* dealing_section = new touch_section(str_label("Dealing"));
    dealing = new touch_radio_list;
    dealing->setObjectName(QStringLiteral("mobile_dealing"));
    dealing->add_option(
        str_label("Sequential, one deck at a time"), str_label("1/M")
    );
    dealing->add_option(
        str_label("Sequential, several at a time"), str_label("N/M")
    );
    dealing->add_option(str_label("Random deck"));
    dealing->add_option(str_label("All decks together"));
    per_step = new touch_stepper;
    per_step->setObjectName(QStringLiteral("mobile_per_step"));
    per_step->set_value_width(44);
    per_step->set_accessible_names(
        str_label("Fewer decks per step"), str_label("More decks per step")
    );
    auto* per_step_row
        = new touch_setting_row(str_label("Decks per step"), per_step);
    per_step_row->set_note(str_label("N need not divide the number of decks"));
    dealing->set_detail(1, per_step_row);
    dealing_section->add_row(dealing);
    rules->addWidget(dealing_section);
    rules->addStretch();

    // Decks
    QVBoxLayout* decks = nullptr;
    setup_stack->addWidget(scroll_page(setup_page, decks));
    auto* table_section = new touch_section(str_label("Table"));
    deck_count = new touch_stepper;
    deck_count->setObjectName(QStringLiteral("mobile_deck_count"));
    deck_count->set_accessible_names(
        str_label("Fewer decks"), str_label("More decks")
    );
    decks_row
        = new touch_setting_row(str_label("Decks on the table"), deck_count);
    decks_row->set_note_mono(true);
    table_section->add_row(decks_row);
    allow_more = new touch_switch;
    allow_more->setObjectName(QStringLiteral("mobile_allow_more"));
    allow_more->setAccessibleName(str_label("Allow more than recommended"));
    auto* more_row = new touch_setting_row(
        str_label("Allow more than recommended"), allow_more
    );
    more_row->set_note(str_label(
        "Cards get smaller than is comfortable to read on this screen."
    ));
    table_section->add_row(more_row);
    setup_interval = new touch_stepper;
    setup_interval->setObjectName(QStringLiteral("mobile_setup_interval"));
    setup_interval->set_range(
        gameplay::session::minimum_pick_interval_ms,
        gameplay::session::maximum_pick_interval_ms
    );
    setup_interval->set_step(100);
    setup_interval->set_formatter([](double value) {
        return touch_text::interval(static_cast<int>(std::lround(value)));
    });
    setup_interval->set_accessible_names(
        str_label("Faster"), str_label("Slower")
    );
    table_section->add_row(
        new touch_setting_row(str_label("Next card every"), setup_interval)
    );
    decks->addWidget(table_section);
    auto* measured = new touch_label(
        str_label(
            "Measured from this screen and orientation. Turning the phone "
            "recalculates it."
        ),
        touch_type::note, touch_ink::muted
    );
    measured->setWordWrap(true);
    decks->addWidget(measured);
    auto* deck_head = new QHBoxLayout;
    deck_head->addWidget(new touch_label(
        str_label("Decks"), touch_type::caption, touch_ink::muted
    ));
    deck_head->addStretch();
    deck_head->addWidget(new touch_label(
        str_label("Tap a deck to change it"), touch_type::small,
        touch_ink::muted
    ));
    decks->addLayout(deck_head);
    deck_cards = new QVBoxLayout;
    deck_cards->setSpacing(14);
    decks->addLayout(deck_cards);
    auto* training_note = new touch_label(
        str_label(
            "Training decks are dealt and asked like the others but earn no "
            "points and never cost a life."
        ),
        touch_type::note, touch_ink::muted
    );
    training_note->setWordWrap(true);
    decks->addWidget(training_note);
    decks->addStretch();

    setup_error = new touch_label(
        {}, touch_type::small, touch_ink::negative, setup_page
    );
    setup_error->setObjectName(QStringLiteral("gameplay_setup_error"));
    setup_error->setWordWrap(true);
    setup_error->setContentsMargins(16, 6, 16, 6);
    setup_error->hide();
    column->addWidget(setup_error);

    QHBoxLayout* footer_row = nullptr;
    column->addWidget(footer_bar(setup_page, footer_row));
    auto* save = new touch_button(
        str_label("Save as drill"), touch_button_style::outline, setup_page
    );
    save->setObjectName(QStringLiteral("mobile_save_drill"));
    connect(save, &QAbstractButton::clicked, this, &mobile_shell::save_drill);
    setup_next = new touch_button(
        str_label("Next: decks"), touch_button_style::primary, setup_page
    );
    setup_next->setObjectName(QStringLiteral("mobile_setup_next"));
    footer_row->addWidget(save);
    footer_row->addWidget(setup_next, 1);

    connect(
        setup_steps, &touch_choice::current_changed, this,
        [this](int index) {
            setup_stack->setCurrentIndex(index);
            refresh_setup();
        }
    );
    connect(setup_next, &QAbstractButton::clicked, this, [this] {
        if (setup_stack->currentIndex() == 0) {
            setup_steps->set_current(1);
            setup_stack->setCurrentIndex(1);
            refresh_setup();
            return;
        }
        if (setup_error)
            setup_error->hide();
        (void)window.start_gameplay_from_ui();
    });
    connect(
        penalty, &touch_choice::current_changed, this,
        &mobile_shell::apply_rules
    );
    connect(
        source, &touch_choice::current_changed, this, &mobile_shell::apply_rules
    );
    connect(
        scope, &touch_choice::current_changed, this, &mobile_shell::apply_rules
    );
    connect(
        lives, &touch_stepper::value_changed, this, &mobile_shell::apply_rules
    );
    connect(
        per_step, &touch_stepper::value_changed, this,
        &mobile_shell::apply_rules
    );
    connect(
        freeze, &QAbstractButton::toggled, this, &mobile_shell::apply_rules
    );
    connect(skip, &QAbstractButton::toggled, this, &mobile_shell::apply_rules);
    connect(
        allow_more, &QAbstractButton::toggled, this, &mobile_shell::apply_rules
    );
    connect(
        dealing, &touch_radio_list::current_changed, this,
        &mobile_shell::apply_rules
    );
    connect(
        deck_count, &touch_stepper::value_changed, this,
        [this](double value) {
            if (refreshing_setup)
                return;
            window.on_gameplay_roster_changed(static_cast<int>(std::lround(value
            )));
            refresh_setup();
        }
    );
    connect(
        setup_interval, &touch_stepper::value_changed, this,
        [this](double value) {
            if (refreshing_setup || !window.speed_slider)
                return;
            window.speed_slider->setValue(static_cast<int>(std::lround(value)));
            refresh_setup();
        }
    );
}

void mobile_shell::refresh_setup() {
    auto* owner = view ? view->active_gameplay_session() : nullptr;
    if (!owner)
        return;
    refreshing_setup = true;
    const auto& settings = owner->configuration();
    const bool editable = owner->phase() == gameplay::session_phase::setup;
    const bool uses_lives = settings.failure == gameplay::failure_policy::lives;
    penalty->set_current(uses_lives ? 0 : 1);
    lives->set_value(settings.initial_lives);
    lives_row->setVisible(uses_lives);
    lives_hint->setText(
        uses_lives
            ? str_label("1 to 12. Every wrong answer costs one shared life. "
                        "Training decks never lose lives.")
            : str_label("A wrong answer stops only that deck; the others keep "
                        "playing. Training decks are never blocked.")
    );
    source->set_current(
        settings.source == gameplay::quiz_source::physical_joker ? 0 : 1
    );
    scope->set_current(settings.scope == gameplay::quiz_scope::multi ? 1 : 0);
    {
        const QSignalBlocker block_freeze(freeze);
        const QSignalBlocker block_skip(skip);
        const QSignalBlocker block_more(allow_more);
        freeze->setChecked(settings.global_quiz_pause);
        skip->setChecked(settings.allow_skip);
        allow_more->setChecked(settings.allow_extra_slots);
    }
    freeze->setEnabled(editable && !uses_lives);
    freeze_row->set_note(
        uses_lives ? str_label("Always on with Lives")
                   : (settings.global_quiz_pause
                          ? str_label("Answer time starts at 60 s")
                          : str_label("Other decks keep dealing; no timer")),
        uses_lives ? touch_ink::accent : touch_ink::muted
    );
    int option = 3;
    if (settings.dealing == gameplay::dealing_mode::sequential)
        option = settings.sequential_count <= 1 ? 0 : 1;
    else if (settings.dealing == gameplay::dealing_mode::random)
        option = 2;
    dealing->set_current(option);
    per_step->set_range(
        2, std::max<double>(2, static_cast<double>(owner->size()))
    );
    per_step->set_value(
        static_cast<double>(std::max<std::size_t>(2, settings.sequential_count))
    );
    per_step->setEnabled(owner->size() >= 2);

    const auto bounds = owner->slot_constraints();
    decks_row->set_note(
        str_label("Recommended here: %1")
            .arg(static_cast<qulonglong>(bounds.recommended)),
        touch_ink::accent
    );
    const auto upper = settings.allow_extra_slots
        ? bounds.technical_cap
        : std::max(bounds.recommended, owner->size());
    deck_count->set_range(1, static_cast<double>(upper));
    deck_count->set_value(static_cast<double>(owner->size()));
    setup_interval->set_value(owner->pick_interval_ms());

    // Deck cards are updated in place: a tap must not delete its own card.
    while (deck_cards->count() < static_cast<int>(owner->size())) {
        const auto index = static_cast<std::size_t>(deck_cards->count());
        auto* card = new touch_deck_card;
        card->setObjectName(QStringLiteral("mobile_deck_%1").arg(index));
        connect(card, &QAbstractButton::clicked, this, [this, index] {
            open_deck_sheet({ index });
        });
        deck_cards->addWidget(card);
    }
    while (deck_cards->count() > static_cast<int>(owner->size())) {
        auto* item = deck_cards->takeAt(deck_cards->count() - 1);
        item->widget()->hide();
        item->widget()->deleteLater();
        delete item;
    }
    for (std::size_t index = 0; index < owner->size(); ++index) {
        const auto* deck = owner->deck({ index });
        auto* card = static_cast<touch_deck_card*>(
            deck_cards->itemAt(static_cast<int>(index))->widget()
        );
        QStringList solid;
        QStringList outline;
        if (deck->configuration.training)
            solid.append(str_label("TRAINING"));
        if (deck->configuration.training && deck->show_count)
            outline.append(str_label("SHOWS COUNT"));
        card->set_deck(
            QString::number(static_cast<qulonglong>(index + 1)),
            touch_text::strategy_name(deck->configuration.strategy_slug),
            touch_text::shoe_summary(deck->configuration), solid, outline
        );
        card->setEnabled(editable);
    }
    for (auto* control : std::initializer_list<QWidget*> {
             penalty, lives, source, scope, skip, dealing, deck_count,
             allow_more, setup_interval })
        control->setEnabled(editable);
    setup_next->setText(
        setup_stack->currentIndex() == 0 ? str_label("Next: decks")
                                         : str_label("Start")
    );
    setup_steps->set_current(setup_stack->currentIndex());
    refreshing_setup = false;
}

void mobile_shell::apply_rules() {
    if (refreshing_setup)
        return;
    auto* owner = view ? view->active_gameplay_session() : nullptr;
    if (!owner || owner->phase() != gameplay::session_phase::setup)
        return;
    auto candidate = owner->configuration();
    candidate.failure = penalty->current() == 0
        ? gameplay::failure_policy::lives
        : gameplay::failure_policy::block;
    if (candidate.failure == gameplay::failure_policy::lives)
        candidate.initial_lives = static_cast<int>(std::lround(lives->value()));
    candidate.source = source->current() == 0
        ? gameplay::quiz_source::physical_joker
        : gameplay::quiz_source::virtual_interrupt;
    candidate.scope = scope->current() == 1 ? gameplay::quiz_scope::multi
                                            : gameplay::quiz_scope::single;
    // Lives always freezes the table while answering (US-090).
    candidate.global_quiz_pause
        = candidate.failure == gameplay::failure_policy::lives
        || freeze->isChecked();
    candidate.allow_skip = skip->isChecked();
    candidate.allow_extra_slots = allow_more->isChecked();
    switch (dealing->current()) {
    case 0:
        candidate.dealing = gameplay::dealing_mode::sequential;
        candidate.sequential_count = 1;
        break;
    case 1:
        candidate.dealing = gameplay::dealing_mode::sequential;
        candidate.sequential_count = std::min<std::size_t>(
            owner->size(),
            static_cast<std::size_t>(std::lround(per_step->value()))
        );
        break;
    case 2:
        candidate.dealing = gameplay::dealing_mode::random;
        break;
    default:
        candidate.dealing = gameplay::dealing_mode::simultaneous;
        break;
    }
    if (candidate != owner->configuration() && owner->configure(candidate)) {
        view->refresh_gameplay_session();
        window.refresh_gameplay_setup();
    }
    refresh_setup();
}

void mobile_shell::save_drill() {
    if (!view || !view->active_gameplay_session())
        return;
    name_field->setText(str_label("Drill %1").arg(drills.size() + 1));
    name_field->selectAll();
    name_sheet->findChild<QLabel*>(QStringLiteral("mobile_name_error"))
        ->clear();
    name_sheet->open();
    name_field->setFocus(Qt::OtherFocusReason);
}

void mobile_shell::build_name_sheet() {
    name_sheet = new touch_sheet(*this, touch_surface::sheet_paper);
    name_sheet->setObjectName(QStringLiteral("mobile_name_sheet"));
    name_sheet->set_dismissible(true);
    connect(
        name_sheet, &touch_sheet::dismissed, name_sheet,
        &touch_sheet::close_sheet
    );
    auto* content = new QWidget;
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(20, 0, 20, 20);
    column->setSpacing(12);
    column->addWidget(new touch_label(
        str_label("Save as drill"), touch_type::heading, touch_ink::paper_ink
    ));
    auto* note = new touch_label(
        str_label(
            "A drill keeps these rules and decks, not cards, answers or time."
        ),
        touch_type::small, touch_ink::paper_muted
    );
    note->setWordWrap(true);
    column->addWidget(note);
    auto* field = new touch_panel(touch_surface::locked, content);
    field->setFixedHeight(52);
    auto* field_row = new QHBoxLayout(field);
    field_row->setContentsMargins(14, 0, 14, 0);
    name_field = new QLineEdit(field);
    name_field->setObjectName(QStringLiteral("mobile_drill_name"));
    name_field->setMaxLength(gameplay_drill_service::maximum_name_length);
    name_field->setFrame(false);
    const auto tokens = touch_tokens_for(*content);
    name_field->setFont(touch_font(tokens, 16));
    name_field->setStyleSheet(
        QStringLiteral(
            "QLineEdit { background: transparent; border: 0; color: %1; }"
        )
            .arg(tokens.paper_ink.name())
    );
    field_row->addWidget(name_field);
    column->addWidget(field);
    auto* error
        = new touch_label({}, touch_type::small, touch_ink::danger, content);
    error->setObjectName(QStringLiteral("mobile_name_error"));
    error->setWordWrap(true);
    column->addWidget(error);
    auto* cancel = new touch_button(
        str_label("Cancel"), touch_button_style::outline_ink, content
    );
    auto* save = new touch_button(
        str_label("Save"), touch_button_style::dark, content
    );
    save->setObjectName(QStringLiteral("mobile_drill_save"));
    column->addLayout(touch_row_layout({ cancel, save }));
    name_sheet->set_content(content);
    connect(
        cancel, &QAbstractButton::clicked, name_sheet, &touch_sheet::close_sheet
    );
    connect(save, &QAbstractButton::clicked, this, [this, error] {
        const auto* owner = view ? view->active_gameplay_session() : nullptr;
        if (!owner)
            return;
        const auto cap = gameplay::slot_technical_cap;
        const auto drill = capture_gameplay_drill(
            *owner, name_field->text().trimmed(), { cap, cap }
        );
        QSettings storage(
            QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
        );
        gameplay_drill_service service(storage, { cap, cap });
        QString message;
        auto saved = service.load(&message);
        if (!drill || !saved) {
            error->setText(
                message.isEmpty() ? str_label("Give the drill a name.")
                                  : message
            );
            return;
        }
        saved->append(*drill);
        if (!service.save(*saved, &message)) {
            error->setText(message);
            return;
        }
        drills = *saved;
        name_sheet->close_sheet();
        refresh();
    });
}

void mobile_shell::build_deck_sheet() {
    deck_sheet = new touch_sheet(*this, touch_surface::sheet_paper);
    deck_sheet->setObjectName(QStringLiteral("mobile_deck_sheet"));
    deck_sheet->set_dismissible(true);
    connect(
        deck_sheet, &touch_sheet::dismissed, deck_sheet,
        &touch_sheet::close_sheet
    );
    auto* content = new QWidget;
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(20, 0, 20, 20);
    column->setSpacing(0);
    auto* head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 6);
    sheet_title = new touch_label(
        {}, touch_type::heading, touch_ink::paper_ink, content
    );
    head->addWidget(sheet_title);
    head->addStretch();
    auto* staged = new touch_label(
        str_label("changes apply on OK"), touch_type::mono,
        touch_ink::paper_muted, content
    );
    head->addWidget(staged, 0, Qt::AlignBottom);
    column->addLayout(head);

    auto* strategy_value = new QWidget(content);
    auto* strategy_row = new QHBoxLayout(strategy_value);
    strategy_row->setContentsMargins(0, 0, 0, 0);
    strategy_row->setSpacing(6);
    sheet_strategy_name = new touch_label(
        {}, touch_type::item, touch_ink::paper_ink, strategy_value
    );
    auto* chevron
        = new touch_button({}, touch_button_style::bare_icon, strategy_value);
    chevron->set_icon(touch_icon::chevron);
    chevron->setFixedSize(20, 20);
    chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
    chevron->setFocusPolicy(Qt::NoFocus);
    chevron->set_on_paper(true);
    strategy_row->addWidget(sheet_strategy_name);
    strategy_row->addWidget(chevron);
    sheet_strategy
        = new touch_setting_row(str_label("Strategy"), strategy_value, content);
    sheet_strategy->setObjectName(QStringLiteral("mobile_sheet_strategy"));
    sheet_strategy->set_note_mono(true);
    sheet_strategy->set_on_paper(true);
    sheet_strategy->set_clickable(true);
    column->addWidget(sheet_strategy);
    column->addWidget(new touch_divider(true, content));
    sheet_shoe = new touch_choice(
        { str_label("Ends"), str_label("Endless") }, touch_choice_style::paper
    );
    sheet_shoe->setObjectName(QStringLiteral("mobile_sheet_shoe"));
    auto* shoe_row
        = new touch_setting_row(str_label("Shoe"), sheet_shoe, content);
    shoe_row->set_on_paper(true);
    column->addWidget(shoe_row);
    column->addWidget(new touch_divider(true, content));
    sheet_count = new touch_stepper;
    sheet_count->setObjectName(QStringLiteral("mobile_sheet_decks"));
    sheet_count->set_on_paper(true);
    sheet_count->set_value_width(40);
    sheet_count->set_range(1, 16);
    sheet_count->set_accessible_names(
        str_label("Fewer decks in the shoe"),
        str_label("More decks in the shoe")
    );
    sheet_count_row = new touch_setting_row(
        str_label("Decks in the shoe"), sheet_count, content
    );
    sheet_count_row->set_on_paper(true);
    column->addWidget(sheet_count_row);
    auto* hint_panel = new touch_panel(touch_surface::note_accent, content);
    auto* hint_row = new QHBoxLayout(hint_panel);
    hint_row->setContentsMargins(12, 10, 12, 10);
    hint_row->setSpacing(10);
    auto* info
        = new touch_button({}, touch_button_style::bare_icon, hint_panel);
    info->set_icon(touch_icon::info);
    info->setFixedSize(20, 20);
    info->setAttribute(Qt::WA_TransparentForMouseEvents);
    info->setFocusPolicy(Qt::NoFocus);
    info->set_on_paper(true);
    sheet_hint = new touch_label(
        {}, touch_type::small, touch_ink::paper_ink, hint_panel
    );
    sheet_hint->setWordWrap(true);
    hint_row->addWidget(info, 0, Qt::AlignTop);
    hint_row->addWidget(sheet_hint, 1);
    column->addWidget(hint_panel);
    column->addSpacing(6);
    sheet_training = new touch_switch;
    sheet_training->setObjectName(QStringLiteral("mobile_sheet_training"));
    sheet_training->setAccessibleName(str_label("Training"));
    sheet_training->set_on_paper(true);
    auto* training_row
        = new touch_setting_row(str_label("Training"), sheet_training, content);
    training_row->set_on_paper(true);
    training_row->set_note(
        str_label("No points, no lost lives, never blocked"),
        touch_ink::paper_muted
    );
    column->addWidget(training_row);
    column->addWidget(new touch_divider(true, content));
    sheet_show = new touch_switch;
    sheet_show->setObjectName(QStringLiteral("mobile_sheet_show_count"));
    sheet_show->setAccessibleName(str_label("Show the count"));
    sheet_show->set_on_paper(true);
    sheet_show_row = new touch_setting_row(
        str_label("Show the count"), sheet_show, content
    );
    sheet_show_row->set_on_paper(true);
    sheet_show_row->set_note(
        str_label("Training decks only"), touch_ink::paper_muted
    );
    column->addWidget(sheet_show_row);
    column->addWidget(new touch_divider(true, content));
    sheet_copy = new touch_button(
        str_label("Copy these settings to every deck"),
        touch_button_style::link, content
    );
    sheet_copy->setObjectName(QStringLiteral("mobile_sheet_copy"));
    column->addSpacing(6);
    column->addWidget(sheet_copy);
    column->addSpacing(8);
    auto* cancel = new touch_button(
        str_label("Cancel"), touch_button_style::outline_ink, content
    );
    cancel->setObjectName(QStringLiteral("mobile_sheet_cancel"));
    auto* ok
        = new touch_button(str_label("OK"), touch_button_style::dark, content);
    ok->setObjectName(QStringLiteral("mobile_sheet_ok"));
    column->addLayout(touch_row_layout({ cancel, ok }));
    deck_sheet->set_content(content);

    connect(sheet_strategy, &touch_setting_row::clicked, this, [this] {
        if (!sheet_live)
            open_strategy(true);
    });
    connect(
        sheet_shoe, &touch_choice::current_changed, this,
        [this](int index) {
            sheet_draft.infinite = index == 1;
            refresh_deck_sheet();
        }
    );
    connect(
        sheet_count, &touch_stepper::value_changed, this,
        [this](double value) {
            sheet_draft.deck_count
                = static_cast<std::size_t>(std::lround(value));
            refresh_deck_sheet();
        }
    );
    connect(sheet_training, &QAbstractButton::toggled, this, [this](bool on) {
        sheet_draft.training = on;
        if (!on)
            sheet_show_count = false;
        refresh_deck_sheet();
    });
    connect(sheet_show, &QAbstractButton::toggled, this, [this](bool on) {
        sheet_show_count = on;
        refresh_deck_sheet();
    });
    connect(sheet_copy, &QAbstractButton::clicked, this, [this] {
        apply_deck_sheet(true);
    });
    connect(cancel, &QAbstractButton::clicked, this, [this] {
        deck_sheet->close_sheet();
        sheet_deck.reset();
    });
    connect(ok, &QAbstractButton::clicked, this, [this] {
        apply_deck_sheet(false);
    });
}

void mobile_shell::open_deck_sheet(gameplay::deck_id id) {
    const auto* owner = view ? view->active_gameplay_session() : nullptr;
    if (!owner || !owner->deck(id))
        return;
    sheet_deck = id;
    sheet_draft = owner->deck(id)->configuration;
    sheet_show_count = owner->deck(id)->show_count;
    sheet_live = owner->phase() != gameplay::session_phase::setup;
    refresh_deck_sheet();
    deck_sheet->open();
}

void mobile_shell::refresh_deck_sheet() {
    const auto* owner = view ? view->active_gameplay_session() : nullptr;
    if (!owner || !sheet_deck || !owner->deck(*sheet_deck)) {
        deck_sheet->close_sheet();
        return;
    }
    sheet_title->setText(
        str_label("Deck %1").arg(static_cast<qulonglong>(sheet_deck->value + 1))
    );
    const auto* strategy = touch_text::strategy(sheet_draft.strategy_slug);
    sheet_strategy_name->setText(
        touch_text::strategy_name(sheet_draft.strategy_slug)
    );
    if (strategy) {
        const int level = touch_text::strategy_level(*strategy);
        sheet_strategy->set_note(
            (level > 0 ? str_label("Level %1").arg(level)
                       : str_label("Ratio count"))
                + QStringLiteral(" · ")
                + (strategy->balance ? str_label("balanced")
                                     : str_label("unbalanced")),
            touch_ink::paper_muted
        );
        QString hint;
        if (strategy->min_decks > 1)
            hint = str_label(
                       "%1 is usually played with %2 decks or more. Fewer is "
                       "fine for practice. "
            )
                       .arg(touch_text::short_strategy(strategy->name))
                       .arg(strategy->min_decks);
        hint += str_label("The starting count is set by the strategy.");
        sheet_hint->setText(hint);
    }
    {
        const QSignalBlocker block_shoe(sheet_shoe);
        const QSignalBlocker block_count(sheet_count);
        const QSignalBlocker block_training(sheet_training);
        const QSignalBlocker block_show(sheet_show);
        sheet_shoe->set_current(sheet_draft.infinite ? 1 : 0);
        sheet_count->set_value(static_cast<double>(sheet_draft.deck_count));
        sheet_training->setChecked(sheet_draft.training);
        sheet_show->setChecked(sheet_draft.training && sheet_show_count);
    }
    // Configuration is fixed once the session starts; only a Training deck's
    // Show the count may change while paused (US-145).
    sheet_shoe->setEnabled(!sheet_live);
    sheet_count->setEnabled(!sheet_live);
    sheet_training->setEnabled(!sheet_live);
    sheet_strategy->set_clickable(!sheet_live);
    sheet_strategy->set_dimmed(sheet_live);
    const bool can_show = sheet_draft.training
        && (!sheet_live || owner->phase() == gameplay::session_phase::paused);
    sheet_show->setEnabled(can_show);
    sheet_show_row->set_dimmed(!sheet_draft.training);
    sheet_copy->setVisible(!sheet_live && owner->size() > 1);
}

void mobile_shell::apply_deck_sheet(bool every_deck) {
    auto* owner = view ? view->active_gameplay_session() : nullptr;
    if (!owner || !sheet_deck) {
        deck_sheet->close_sheet();
        return;
    }
    const bool setup = owner->phase() == gameplay::session_phase::setup;
    std::vector<gameplay::deck_id> targets { *sheet_deck };
    if (every_deck && setup) {
        targets.clear();
        for (std::size_t index = 0; index < owner->size(); ++index)
            targets.push_back({ index });
    }
    bool configured = false;
    for (const auto id : targets) {
        if (setup && owner->deck(id)->configuration != sheet_draft)
            configured = owner->configure_deck(id, sheet_draft) || configured;
        const bool show = sheet_draft.training && sheet_show_count;
        if (owner->deck(id)->show_count != show)
            (void)owner->set_show_count(id, show);
    }
    view->refresh_gameplay_session();
    if (configured)
        window.refresh_gameplay_setup();
    deck_sheet->close_sheet();
    sheet_deck.reset();
    refresh();
}

void mobile_shell::build_strategy() {
    strategy_page = new touch_panel(touch_surface::ground, this);
    strategy_page->setObjectName(QStringLiteral("mobile_strategy"));
    auto* column = new QVBoxLayout(strategy_page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    strategy_bar = new touch_top_bar({}, strategy_page);
    connect(strategy_bar, &touch_top_bar::back_requested, this, [this] {
        if (strategy_for_deck) {
            show_page(page::setup);
            deck_sheet->open();
            return;
        }
        show_page(page::home);
    });
    column->addWidget(strategy_bar);
    touch_panel* field = nullptr;
    strategy_search = search_field(strategy_page, field);
    strategy_search->setObjectName(QStringLiteral("mobile_strategy_search"));
    strategy_search->setPlaceholderText(str_label("Search strategies"));
    auto* field_row = new QHBoxLayout;
    field_row->setContentsMargins(16, 0, 16, 10);
    field_row->addWidget(field);
    column->addLayout(field_row);
    strategy_filter = new touch_choice(
        { str_label("All"), str_label("Level 1"), str_label("Level 2"),
          str_label("Level 3"), str_label("Balanced"),
          str_label("Unbalanced") },
        touch_choice_style::filters
    );
    strategy_filter->setObjectName(QStringLiteral("mobile_strategy_filter"));
    auto* filters = new QScrollArea(strategy_page);
    filters->setFrameShape(QFrame::NoFrame);
    filters->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filters->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filters->setStyleSheet(
        QStringLiteral("QScrollArea { background: transparent; }")
    );
    filters->viewport()->setAutoFillBackground(false);
    auto* filter_holder = new QWidget;
    auto* filter_row = new QHBoxLayout(filter_holder);
    filter_row->setContentsMargins(16, 0, 16, 14);
    filter_row->addWidget(strategy_filter);
    filter_row->addStretch();
    // Chips keep their natural width and scroll sideways on narrow phones.
    filters->setWidgetResizable(true);
    filters->setWidget(filter_holder);
    filter_holder->setAutoFillBackground(false);
    filters->setFixedHeight(strategy_filter->sizeHint().height() + 14);
    QScroller::grabGesture(filters->viewport(), QScroller::TouchGesture);
    column->addWidget(filters);
    auto* list = new QWidget;
    strategy_rows = new QVBoxLayout(list);
    strategy_rows->setContentsMargins(0, 0, 0, 12);
    strategy_rows->setSpacing(0);
    column->addWidget(new touch_scroll(list, strategy_page), 1);
    QHBoxLayout* footer_row = nullptr;
    auto* use_bar = footer_bar(strategy_page, footer_row);
    strategy_use = new touch_button({}, touch_button_style::primary, use_bar);
    strategy_use->setObjectName(QStringLiteral("mobile_strategy_use"));
    footer_row->addWidget(strategy_use);
    column->addWidget(use_bar);
    connect(
        strategy_search, &QLineEdit::textChanged, this,
        &mobile_shell::refresh_strategy
    );
    connect(
        strategy_filter, &touch_choice::current_changed, this,
        &mobile_shell::refresh_strategy
    );
    connect(strategy_use, &QAbstractButton::clicked, this, [this] {
        sheet_draft.strategy_slug = strategy_choice.toStdString();
        show_page(page::setup);
        deck_sheet->open();
        refresh_deck_sheet();
    });
}

void mobile_shell::open_strategy(bool for_deck) {
    strategy_for_deck = for_deck;
    if (for_deck)
        strategy_choice = QString::fromStdString(sheet_draft.strategy_slug);
    else if (strategy_choice.isEmpty()
             && !strategy_repository().strategies.isEmpty())
        strategy_choice = strategy_repository().strategies.front().slug;
    strategy_bar->set_title(
        for_deck && sheet_deck
            ? str_label("Strategy for deck %1")
                  .arg(static_cast<qulonglong>(sheet_deck->value + 1))
            : str_label("Strategies")
    );
    strategy_use->parentWidget()->setVisible(for_deck);
    deck_sheet->close_sheet();
    show_page(page::strategy);
}

void mobile_shell::refresh_strategy() {
    while (auto* item = strategy_rows->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const auto& catalog = strategy_repository();
    const auto query = strategy_search->text().trimmed();
    const int filter = strategy_filter->current();
    const auto accepts = [&](const strategy_data& strategy) {
        if (!query.isEmpty()
            && !strategy.name.contains(query, Qt::CaseInsensitive))
            return false;
        const int level = touch_text::strategy_level(strategy);
        switch (filter) {
        case 1:
        case 2:
        case 3:
            return level == filter;
        case 4:
            return strategy.balance;
        case 5:
            return !strategy.balance;
        default:
            return true;
        }
    };
    const auto tags = [](const strategy_data& strategy) {
        const int level = touch_text::strategy_level(strategy);
        return QStringList {
            level > 0 ? str_label("LEVEL %1").arg(level) : str_label("RATIO"),
            strategy.balance ? str_label("BALANCED") : str_label("UNBALANCED")
        };
    };
    const strategy_data* chosen = nullptr;
    for (const auto& strategy : catalog.strategies)
        if (strategy.slug == strategy_choice)
            chosen = &strategy;
    if (chosen && accepts(*chosen)) {
        auto* card = new touch_panel(touch_surface::paper_selected);
        card->setObjectName(QStringLiteral("mobile_strategy_selected"));
        auto* column = new QVBoxLayout(card);
        column->setContentsMargins(14, 14, 14, 14);
        column->setSpacing(12);
        auto* head = new QHBoxLayout;
        head->setSpacing(10);
        auto* name = new touch_label(
            touch_text::short_strategy(chosen->name), touch_type::lead,
            touch_ink::paper_ink, card
        );
        name->set_custom_font(
            touch_font(touch_tokens_for(*name), 20, QFont::Bold)
        );
        head->addWidget(name, 1);
        const auto chosen_tags = tags(*chosen);
        head->addWidget(
            new touch_tag(chosen_tags.at(0), touch_tag_style::ink, card)
        );
        head->addWidget(
            new touch_tag(chosen_tags.at(1), touch_tag_style::ink_outline, card)
        );
        column->addLayout(head);
        auto* grid = new touch_value_grid(card);
        QList<touch_grid_cell> cells;
        static const QStringList ranks {
            QStringLiteral("A"),  QStringLiteral("2"), QStringLiteral("3"),
            QStringLiteral("4"),  QStringLiteral("5"), QStringLiteral("6"),
            QStringLiteral("7"),  QStringLiteral("8"), QStringLiteral("9"),
            QStringLiteral("10"), QStringLiteral("J"), QStringLiteral("Q"),
            QStringLiteral("K")
        };
        // Kit order: 2 … 10, J, Q, K, A, then the Joker, which has no weight.
        for (int step = 1; step <= 13; ++step) {
            const int index = step % 13;
            const int weight = index < chosen->weights.size()
                ? chosen->weights.at(index)
                : 0;
            cells.append({ ranks.at(index), touch_signed(weight, true),
                           weight > 0 ? 1 : (weight < 0 ? -1 : 0), false });
        }
        cells.append({ str_label("Jk"), QStringLiteral("—"), 0, true });
        grid->set_cells(cells, 7);
        column->addWidget(grid);
        const auto start
            = chosen->initial_running_count_for(sheet_draft.deck_count)
                  .value_or(0);
        QString text = str_label("Starts at %1.")
                           .arg(touch_signed(static_cast<qint64>(start)));
        if (chosen->min_decks > 1)
            text += str_label(" Usually played with %1 decks or more.")
                        .arg(chosen->min_decks);
        text += str_label(" Jokers carry no weight: they only ask the question."
        );
        auto* note = new touch_label(
            text, touch_type::small, touch_ink::paper_muted, card
        );
        note->setWordWrap(true);
        column->addWidget(note);
        auto* holder = new QWidget;
        auto* holder_row = new QHBoxLayout(holder);
        holder_row->setContentsMargins(16, 0, 16, 10);
        holder_row->addWidget(card);
        strategy_rows->addWidget(holder);
    }
    int index = 0;
    for (const auto& strategy : catalog.strategies) {
        if (&strategy == chosen || !accepts(strategy))
            continue;
        auto* row
            = new touch_list_row(touch_text::short_strategy(strategy.name));
        row->setObjectName(QStringLiteral("mobile_strategy_%1").arg(index++));
        row->set_tags(tags(strategy), touch_tag_style::outline);
        const auto slug = strategy.slug;
        connect(row, &QAbstractButton::clicked, this, [this, slug] {
            strategy_choice = slug;
            refresh_strategy();
        });
        strategy_rows->addWidget(row);
    }
    strategy_rows->addStretch();
    if (chosen)
        strategy_use->setText(str_label("Use %1 for deck %2")
                                  .arg(touch_text::short_strategy(chosen->name))
                                  .arg(static_cast<qulonglong>(
                                      sheet_deck ? sheet_deck->value + 1 : 1
                                  )));
}
