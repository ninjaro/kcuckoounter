#include "shell/mobile_shell.hpp"
#include <shared_ui/qt/touch_layouts.hpp>

#include "arch/str_label.hpp"
#include "settings/gameplay_progress.hpp"
#include "shell/main_window.hpp"
#include "table/table.hpp"
#include "table/touch_text.hpp"

#include <QDateTime>
#include <QLabel>
#include <QRegularExpression>
#include <QSettings>
#include <QStackedLayout>
#include <QVBoxLayout>

#include <algorithm>

using namespace shared_ui::qt;

namespace {
QString reason_tag(gameplay::session_end_reason reason) {
    switch (reason) {
    case gameplay::session_end_reason::manual_finish:
        return str_label("FINISHED BY YOU");
    case gameplay::session_end_reason::lives_exhausted:
        return str_label("NO LIVES LEFT");
    case gameplay::session_end_reason::no_live_decks:
        return str_label("ALL DECKS DONE");
    }
    return {};
}

QString status_tag(const gameplay::deck_result& deck) {
    switch (deck.status) {
    case gameplay::deck_status::active:
        return str_label("PLAYING AT FINISH");
    case gameplay::deck_status::completed:
        return str_label("SHOE FINISHED");
    case gameplay::deck_status::failed:
        return str_label("FAILED");
    }
    return {};
}

QString plural(std::uint64_t value, const QString& one, const QString& many) {
    return (value == 1 ? one : many).arg(static_cast<qulonglong>(value));
}

QString figures(const gameplay::session_statistics& stats) {
    return QStringList {
        str_label("%1 pts").arg(static_cast<qulonglong>(stats.verified_cards)),
        plural(stats.errors, str_label("%1 error"), str_label("%1 errors")),
        plural(stats.skips, str_label("%1 skip"), str_label("%1 skips")),
    }
        .join(QStringLiteral(" · "));
}

// "Seq 1/M · Multi · Jokers · Lives 3" for history rows.
QString context(const gameplay::session_result& result) {
    QStringList parts {
        result.decks.size() == 1
            ? str_label("1 deck")
            : str_label("%1 decks")
                  .arg(static_cast<qulonglong>(result.decks.size())),
    };
    auto dealing = touch_text::dealing_summary(result.configuration);
    dealing.replace(str_label("Sequential"), str_label("Seq"));
    parts.append(dealing);
    parts.append(
        result.configuration.scope == gameplay::quiz_scope::multi
            ? str_label("Multi")
            : str_label("Single")
    );
    parts.append(
        result.configuration.source == gameplay::quiz_source::physical_joker
            ? str_label("Jokers")
            : str_label("Virtual")
    );
    parts.append(
        result.configuration.failure == gameplay::failure_policy::lives
            ? str_label("Lives %1").arg(result.configuration.initial_lives)
            : str_label("Block")
    );
    return parts.join(QStringLiteral(" · "));
}

QString when(std::int64_t utc_ms) {
    const auto at = QDateTime::fromMSecsSinceEpoch(utc_ms).toLocalTime();
    const auto days = at.date().daysTo(QDate::currentDate());
    const auto time = at.time().toString(QStringLiteral("HH:mm"));
    if (days == 0)
        return str_label("Today, %1").arg(time);
    if (days == 1)
        return str_label("Yesterday, %1").arg(time);
    if (days < 7)
        return QLocale().toString(at.date(), QStringLiteral("ddd"))
            + QStringLiteral(", ") + time;
    return QLocale().toString(at.date(), QLocale::ShortFormat)
        + QStringLiteral(", ") + time;
}
} // namespace

void mobile_shell::build_result() {
    result_page = new touch_panel(touch_surface::ground, this);
    result_page->setObjectName(QStringLiteral("mobile_result"));
    auto* column = new QVBoxLayout(result_page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto* bar = new touch_top_bar(str_label("Result"), result_page);
    bar->set_back_visible(true, touch_icon::close);
    connect(bar, &touch_top_bar::back_requested, this, [this] {
        show_page(page::home);
    });
    column->addWidget(bar);
    auto* body = new QWidget;
    auto* inner = new QVBoxLayout(body);
    inner->setContentsMargins(16, 0, 16, 12);
    inner->setSpacing(12);
    result_reason
        = new touch_label({}, touch_type::caption, touch_ink::accent, body);
    result_reason->set_custom_font(touch_font(
        touch_tokens_for(*result_reason), 11, QFont::Medium, true, 0.16
    ));
    inner->addWidget(result_reason);
    result_points = new touch_stat_tile(true, body);
    result_points->setObjectName(QStringLiteral("mobile_result_points"));
    result_errors = new touch_stat_tile(true, body);
    inner->addLayout(touch_row_layout({ result_points, result_errors }, 10));
    result_skips = new touch_stat_tile(false, body);
    result_lives = new touch_stat_tile(false, body);
    result_answering = new touch_stat_tile(false, body);
    inner->addLayout(
        touch_row_layout({ result_skips, result_lives, result_answering }, 8)
    );
    result_decks = new QVBoxLayout;
    result_decks->setSpacing(0);
    inner->addLayout(result_decks);
    result_chips = new touch_tag_flow(body);
    inner->addWidget(result_chips);
    inner->addStretch();
    column->addWidget(new touch_scroll(body, result_page), 1);
    auto* actions = new QWidget(result_page);
    auto* footer_column = new QVBoxLayout(actions);
    footer_column->setContentsMargins(16, 12, 16, 20);
    footer_column->setSpacing(8);
    auto* again = new touch_button(
        str_label("Play again with this setup"), touch_button_style::primary,
        actions
    );
    again->setObjectName(QStringLiteral("mobile_play_again"));
    again->set_height(56);
    footer_column->addWidget(again);
    auto* save = new touch_button(
        str_label("Save as drill"), touch_button_style::outline, actions
    );
    auto* history = new touch_button(
        str_label("History"), touch_button_style::outline, actions
    );
    history->setObjectName(QStringLiteral("mobile_result_history"));
    footer_column->addLayout(touch_row_layout({ save, history }, 8));
    column->addWidget(actions);
    connect(again, &QAbstractButton::clicked, this, [this] {
        const QPointer<mobile_shell> guard(this);
        window.on_new_game_triggered();
        if (!guard || !view || !view->active_gameplay_session()
            || view->active_gameplay_session()->phase()
                != gameplay::session_phase::setup)
            return;
        // The same rules and decks start right away; anything that needs the
        // player (e.g. a smaller screen's recommendation) stays on setup.
        setup_steps->set_current(1);
        setup_stack->setCurrentIndex(1);
        if (!window.start_gameplay_from_ui() && guard)
            refresh_setup();
    });
    connect(save, &QAbstractButton::clicked, this, &mobile_shell::save_drill);
    connect(history, &QAbstractButton::clicked, this, [this] {
        show_page(page::progress);
    });
}

void mobile_shell::refresh_result() {
    const auto* owner = view ? view->active_gameplay_session() : nullptr;
    if (!owner || !owner->result()) {
        show_page(page::home);
        return;
    }
    const auto& result = *owner->result();
    result_reason->setText(
        reason_tag(result.reason) + QStringLiteral(" · ")
        + str_label("%1 ACTIVE")
              .arg(touch_text::clock(result.timing.play_time_ms))
    );
    result_points->set_values(
        str_label("Points"),
        QString::number(static_cast<qulonglong>(result.challenge.verified_cards)
        ),
        str_label("cards proven by right answers")
    );
    result_errors->set_values(
        str_label("Errors"),
        QString::number(static_cast<qulonglong>(result.challenge.errors)),
        str_label("wrong answers")
    );
    result_skips->set_values(
        str_label("Skips"),
        QString::number(static_cast<qulonglong>(result.challenge.skips))
    );
    if (result.configuration.failure == gameplay::failure_policy::lives)
        result_lives->set_values(
            str_label("Lives"),
            str_label("%1/%2")
                .arg(std::max(0, result.remaining_lives))
                .arg(result.configuration.initial_lives)
        );
    else
        result_lives->set_values(
            str_label("Failed"),
            QString::number(static_cast<qulonglong>(result.failed_decks))
        );
    result_answering->set_values(
        str_label("Answering"),
        touch_text::clock(result.timing.quiz_time_ms)
            .remove(QRegularExpression(QStringLiteral("^0")))
    );
    while (auto* item = result_decks->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (std::size_t index = 0; index < result.decks.size(); ++index) {
        const auto& deck = result.decks[index];
        auto* row = new touch_record_row;
        row->set_inset(0);
        row->setFocusPolicy(Qt::NoFocus);
        const auto title
            = str_label("Deck %1 · %2")
                  .arg(static_cast<qulonglong>(deck.id.value + 1))
                  .arg(touch_text::short_strategy(touch_text::strategy_name(
                      deck.configuration.strategy_slug
                  )));
        if (deck.configuration.training) {
            row->set_record(
                title, str_label("TRAINING"), touch_tag_style::outline,
                QStringList {
                    plural(
                        deck.statistics.errors, str_label("%1 wrong"),
                        str_label("%1 wrong")
                    ),
                    plural(
                        deck.statistics.skips, str_label("%1 skip"),
                        str_label("%1 skips")
                    ),
                }
                    .join(QStringLiteral(" · ")),
                str_label("Training is kept out of points, errors and lives."),
                false
            );
        } else {
            gameplay::session_statistics stats;
            stats.verified_cards = deck.statistics.verified_cards;
            stats.errors = deck.statistics.errors;
            stats.skips = deck.statistics.skips;
            row->set_record(
                title, status_tag(deck), touch_tag_style::paper, figures(stats),
                str_label("%1 cards dealt · %2 verified")
                    .arg(static_cast<qulonglong>(
                        deck.statistics.dealt_normal_cards
                    ))
                    .arg(static_cast<qulonglong>(deck.statistics.verified_cards)
                    )
            );
        }
        row->set_divider(index + 1 < result.decks.size());
        result_decks->addWidget(row);
    }
    auto chips
        = touch_text::setup_chips(result.configuration, result.decks.size());
    QStringList shoes;
    for (const auto& deck : result.decks) {
        const auto shoe = deck.configuration.infinite
            ? str_label("endless")
            : str_label("%1-deck").arg(
                  static_cast<qulonglong>(deck.configuration.deck_count)
              );
        if (!shoes.contains(shoe))
            shoes.append(shoe);
    }
    chips.append(shoes.join(QStringLiteral(" + ")));
    result_chips->set_tags(chips, touch_tag_style::chip);
}

void mobile_shell::build_progress() {
    progress_page = new touch_panel(touch_surface::ground, this);
    progress_page->setObjectName(QStringLiteral("mobile_progress"));
    auto* column = new QVBoxLayout(progress_page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto* bar = new touch_top_bar(str_label("Progress"), progress_page);
    connect(bar, &touch_top_bar::back_requested, this, [this] {
        show_page(page::home);
    });
    column->addWidget(bar);
    progress_kind = new touch_choice(
        { str_label("Challenge"), str_label("Training") },
        touch_choice_style::dark
    );
    progress_kind->setObjectName(QStringLiteral("mobile_progress_kind"));
    progress_kind->set_stretch(true);
    auto* kind_row = new QHBoxLayout;
    kind_row->setContentsMargins(16, 0, 16, 8);
    kind_row->addWidget(progress_kind);
    column->addLayout(kind_row);
    auto* totals = new QWidget(progress_page);
    progress_totals = new QHBoxLayout(totals);
    progress_totals->setContentsMargins(16, 0, 16, 0);
    progress_totals->setSpacing(8);
    column->addWidget(totals);
    column->addWidget(new touch_divider(false, progress_page));
    auto* body = new QWidget;
    auto* inner = new QVBoxLayout(body);
    inner->setContentsMargins(0, 0, 0, 12);
    inner->setSpacing(0);
    auto* head = new QHBoxLayout;
    head->setContentsMargins(16, 14, 16, 4);
    head->addWidget(new touch_label(
        str_label("Recent"), touch_type::caption, touch_ink::muted, body
    ));
    head->addStretch();
    head->addWidget(new touch_label(
        str_label("last 20 kept"), touch_type::note, touch_ink::muted, body
    ));
    inner->addLayout(head);
    progress_rows = new QVBoxLayout;
    progress_rows->setSpacing(0);
    inner->addLayout(progress_rows);
    inner->addStretch();
    column->addWidget(new touch_scroll(body, progress_page), 1);
    connect(
        progress_kind, &touch_choice::current_changed, this,
        &mobile_shell::refresh_progress
    );
}

void mobile_shell::refresh_progress() {
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    const auto cap = gameplay::slot_technical_cap;
    QString error;
    const auto value = gameplay_progress_service(storage, { cap, cap })
                           .load(&error)
                           .value_or(gameplay_progress {});
    const bool training = progress_kind->current() == 1;
    while (auto* item = progress_totals->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    while (auto* item = progress_rows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto total = [this](const QString& caption, const QString& figure) {
        auto* box = new QWidget;
        auto* column = new QVBoxLayout(box);
        column->setContentsMargins(0, 10, 0, 10);
        column->setSpacing(3);
        auto* label = new touch_label(
            caption, touch_type::caption_small, touch_ink::muted, box
        );
        auto* number
            = new touch_label(figure, touch_type::value, touch_ink::text, box);
        number->set_custom_font(
            touch_font(touch_tokens_for(*number), 20, QFont::Medium, true)
        );
        column->addWidget(label);
        column->addWidget(number);
        progress_totals->addWidget(box, 1);
    };
    const auto grouped = [](std::uint64_t number) {
        return QLocale().toString(static_cast<qulonglong>(number));
    };
    if (!training) {
        total(str_label("Sessions"), grouped(value.completed_sessions));
        total(str_label("Points"), grouped(value.challenge.verified_cards));
        total(str_label("Errors"), grouped(value.challenge.errors));
        total(str_label("Skips"), grouped(value.challenge.skips));
    } else {
        // Training stays out of challenge totals (US-146); sum what the
        // retained results recorded per Training deck.
        std::uint64_t sessions = 0;
        std::uint64_t errors = 0;
        std::uint64_t skips = 0;
        std::uint64_t cards = 0;
        for (const auto& record : value.recent_results) {
            bool any = false;
            for (const auto& deck : record.result.decks)
                if (deck.configuration.training) {
                    any = true;
                    errors += deck.statistics.errors;
                    skips += deck.statistics.skips;
                    cards += deck.statistics.dealt_normal_cards;
                }
            sessions += any;
        }
        total(str_label("Sessions"), grouped(sessions));
        total(str_label("Cards"), grouped(cards));
        total(str_label("Wrong"), grouped(errors));
        total(str_label("Skips"), grouped(skips));
    }
    int shown = 0;
    for (const auto& record : value.recent_results) {
        const auto& result = record.result;
        QString line;
        if (training) {
            std::uint64_t errors = 0;
            std::uint64_t skips = 0;
            std::size_t decks = 0;
            for (const auto& deck : result.decks)
                if (deck.configuration.training) {
                    ++decks;
                    errors += deck.statistics.errors;
                    skips += deck.statistics.skips;
                }
            if (decks == 0)
                continue;
            line = QStringList {
                plural(decks, str_label("%1 training deck"), str_label("%1 training decks")),
                plural(errors, str_label("%1 wrong"), str_label("%1 wrong")),
                plural(skips, str_label("%1 skip"), str_label("%1 skips")),
            }
                       .join(QStringLiteral(" · "));
        } else {
            line = figures(result.challenge);
        }
        auto* row = new touch_record_row;
        row->setObjectName(QStringLiteral("mobile_history_%1").arg(shown));
        row->set_record(
            when(record.completed_at_utc_ms), reason_tag(result.reason),
            touch_tag_style::outline, line, context(result)
        );
        progress_rows->addWidget(row);
        ++shown;
    }
    if (shown == 0) {
        auto* empty = new touch_label(
            training ? str_label("No Training decks in the kept results yet.")
                     : str_label("Finished sessions appear here."),
            touch_type::small, touch_ink::muted
        );
        empty->setContentsMargins(16, 12, 16, 12);
        empty->setWordWrap(true);
        progress_rows->addWidget(empty);
    }
}
