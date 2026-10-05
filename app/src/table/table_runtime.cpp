#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "packing/layout/equal_rectangles.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_settings.hpp"
#include "table/card_widget.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include <QApplication>
#include <QDialog>
#include <QPointer>
#include <QString>

#include <algorithm>
#include <utility>

void table::stop_gameplay_clock() {
    gameplay_timer.stop();
    gameplay_elapsed.invalidate();
    gameplay_sampled_ms = 0;
}

bool table::start_gameplay_runtime(std::uint64_t dealing_seed) {
    if (!gameplay_owner || gameplay_runtime_attached || !gameplay_layout
        || slot_widgets.size() != gameplay_owner->size()
        || !has_usable_gameplay_layout()
        || !gameplay_owner->start(dealing_seed))
        return false;
    gameplay_runtime_attached = true;
    gameplay_pick_elapsed_ms = 0;
    gameplay_elapsed.start();
    gameplay_timer.start();
    publish_gameplay_runtime({});
    return true;
}

bool table::synchronize_gameplay_clock(
    std::vector<gameplay::quiz_answer>& answers
) {
    if (!gameplay_runtime_attached || !gameplay_owner)
        return false;
    if (gameplay_owner->phase() != gameplay::session_phase::running)
        return true;
    // Subtract cumulative samples instead of restarting on each key/tick:
    // repeated sub-millisecond fractions must not progressively slow the clock.
    // Consume the whole observed interval even on rejection/clipped timeout.
    const auto now = gameplay_elapsed.isValid() ? gameplay_elapsed.elapsed()
                                                : gameplay_sampled_ms;
    const auto delta = now - gameplay_sampled_ms;
    gameplay_sampled_ms = now;
    return advance_gameplay_runtime(
        static_cast<std::uint64_t>(std::max(qint64 { 0 }, delta)), answers
    );
}

bool table::advance_gameplay_runtime(
    std::uint64_t elapsed_ms, std::vector<gameplay::quiz_answer>& answers
) {
    if (!gameplay_runtime_attached || !gameplay_owner
        || gameplay_owner->phase() != gameplay::session_phase::running)
        return false;
    answers.reserve(gameplay_owner->size());
    const auto accrue = [this, &answers](std::uint64_t duration) {
        auto step = gameplay_owner->advance_time(duration);
        if (!step.accepted) {
            gameplay_runtime_failure = str_label(
                "Target gameplay clock was rejected; session paused."
            );
            (void)gameplay_owner->pause();
            stop_gameplay_clock();
            return false;
        }
        answers.insert(answers.end(), step.answers.begin(), step.answers.end());
        if (gameplay_owner->phase() != gameplay::session_phase::running)
            stop_gameplay_clock();
        return true;
    };
    const auto batch = gameplay_owner->current_quiz_batch();
    const bool frozen = batch && batch->remaining_ms;
    const auto has_eligible_deck = [this] {
        for (std::size_t index = 0; index < gameplay_owner->size(); ++index) {
            const auto* deck = gameplay_owner->deck({ index });
            if (deck->status == gameplay::deck_status::active && !deck->quiz)
                return true;
        }
        return false;
    };
    // Countdown is domain-owned; pending non-global questions still accrue
    // answer time even when every deck is waiting. Neither consumes pacing.
    if (frozen || !has_eligible_deck())
        return accrue(elapsed_ms);

    const auto interval
        = static_cast<std::uint64_t>(gameplay_owner->pick_interval_ms());
    const auto until_deal
        = interval - std::min(interval, gameplay_pick_elapsed_ms);
    // The delivered slice precedes the newly displayed card/question. Never
    // invent earlier reveal times or backdate a quiz opened after a GUI stall.
    if (!accrue(elapsed_ms))
        return false;
    if (elapsed_ms < until_deal) {
        gameplay_pick_elapsed_ms += elapsed_ms;
        return true;
    }

    const auto step = gameplay_owner->deal_step();
    if (step.status != gameplay::dealing_step_status::advanced
        && step.status != gameplay::dealing_step_status::quiz_blocked
        && step.status != gameplay::dealing_step_status::no_eligible_decks) {
        gameplay_runtime_failure
            = str_label("Target dealing was rejected (%1); session paused.")
                  .arg(static_cast<int>(step.status));
        (void)gameplay_owner->pause();
        stop_gameplay_clock();
        return false;
    }
    gameplay_pick_elapsed_ms = 0;
    if (gameplay_owner->phase() != gameplay::session_phase::running) {
        stop_gameplay_clock();
        return true;
    }
    // One delivery can show at most one step. Missed whole opportunities are
    // discarded; normal sub-interval jitter keeps the cadence remainder.
    const auto remainder = elapsed_ms - until_deal;
    const auto& next_batch = gameplay_owner->current_quiz_batch();
    const bool may_deal_again
        = !(next_batch && next_batch->remaining_ms) && has_eligible_deck();
    if (may_deal_again && remainder < interval)
        gameplay_pick_elapsed_ms = remainder;
    return true;
}

void table::publish_gameplay_runtime(
    const std::vector<gameplay::quiz_answer>& answers
) {
    const QPointer<table> guard(this);
    const auto revision = gameplay_scene_revision;
    const bool active_quiz_host
        = std::ranges::any_of(slot_widgets, [](const auto* slot) {
              return slot->controls_dialog
                  && slot->controls_dialog == QApplication::activeWindow();
          });
    const auto diagnostic = std::exchange(gameplay_runtime_failure, {});
    // Install correction before a refresh can close its compact prompt host.
    for (const auto& answer : answers) {
        slot_widgets[answer.owner.value]->show_gameplay_answer(answer);
        if (!guard || gameplay_scene_revision != revision)
            return;
    }
    refresh_gameplay_session();
    if (!guard || gameplay_scene_revision != revision)
        return;
    for (const auto& answer : answers) {
        emit gameplay_quiz_resolved(answer);
        if (!guard || gameplay_scene_revision != revision)
            return;
    }
    if (!answers.empty()) {
        emit gameplay_corrections_changed();
        if (!guard || gameplay_scene_revision != revision)
            return;
    }
    if (!diagnostic.isEmpty()) {
        emit gameplay_runtime_failed(diagnostic);
        if (!guard || gameplay_scene_revision != revision)
            return;
    }
    const auto batch = gameplay_owner->current_quiz_batch();
    if (batch && gameplay_owner->phase() == gameplay::session_phase::running) {
        if (!answers.empty())
            (void)focus_gameplay_quiz(answers.back().owner);
        else if (gameplay_presented_batch != batch->id)
            (void)focus_gameplay_quiz();
        if (!guard || gameplay_scene_revision != revision)
            return;
        gameplay_presented_batch = batch->id;
    } else if (!batch) {
        gameplay_presented_batch.reset();
    }
    // A resolved compact prompt can close during refresh. Restore only focus
    // we actually owned; a background batch must not activate the application.
    if (active_quiz_host && !QApplication::activeWindow())
        window()->activateWindow();
    if (!guard || gameplay_scene_revision != revision)
        return;
    emit gameplay_runtime_updated();
}

bool table::focus_gameplay_quiz(
    std::optional<gameplay::deck_id> after, bool reverse
) {
    if (!gameplay_runtime_attached || !gameplay_owner
        || gameplay_owner->phase() != gameplay::session_phase::running)
        return false;
    const auto id = gameplay_owner->quiz_focus(after, reverse);
    if (!id)
        return false;
    const QPointer<table> guard(this);
    const auto revision = gameplay_scene_revision;
    bool return_focus = false;
    for (auto* slot : slot_widgets) {
        if (slot->gameplay_id == *id || !slot->controls_dialog)
            continue;
        return_focus |= slot->controls_dialog == QApplication::activeWindow();
        // Keep the correction/draft in its slot, not a competing focus window.
        slot->controls_dialog->close();
        if (!guard || gameplay_scene_revision != revision)
            return false;
    }
    if (return_focus)
        window()->activateWindow();
    if (!guard || gameplay_scene_revision != revision)
        return false;
    slot_widgets[id->value]->focus_gameplay_input();
    return true;
}

QString table::gameplay_clock_text() const {
    if (!gameplay_owner)
        return {};
    const auto seconds = gameplay_owner->timing().play_time_ms / 1000;
    // QTime wraps at 24 hours and legacy KGameClock accepts only uint seconds.
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<qulonglong>(seconds / 3600), 2, 10, QLatin1Char('0'))
        .arg(
            static_cast<qulonglong>((seconds / 60) % 60), 2, 10,
            QLatin1Char('0')
        )
        .arg(static_cast<qulonglong>(seconds % 60), 2, 10, QLatin1Char('0'));
}

QString table::gameplay_status_text() const {
    if (!gameplay_owner)
        return {};
    QString phase;
    switch (gameplay_owner->phase()) {
    case gameplay::session_phase::setup:
        phase = str_label("Ready");
        break;
    case gameplay::session_phase::running:
        phase = str_label("Running");
        break;
    case gameplay::session_phase::paused:
        phase = str_label("Paused");
        break;
    case gameplay::session_phase::finished:
        phase = str_label("Finished");
        break;
    }
    const auto& stats = gameplay_owner->statistics();
    const auto verified = static_cast<qulonglong>(stats.verified_cards);
    const auto errors = static_cast<qulonglong>(stats.errors);
    const auto skips = static_cast<qulonglong>(stats.skips);
    const auto questions
        = static_cast<qulonglong>(gameplay_owner->unresolved_quizzes().size());
#ifdef KC_KDE
    QString text = i18n(
        "Status: %1  Verified: %2  Errors: %3  Skips: %4  Questions: %5  Time: "
        "%6",
        phase, verified, errors, skips, questions, gameplay_clock_text()
    );
#else
    QString text = str_label(
                       "Status: %1  Verified: %2  Errors: %3  Skips: %4  "
                       "Questions: %5  Time: %6"
    )
                       .arg(phase)
                       .arg(verified)
                       .arg(errors)
                       .arg(skips)
                       .arg(questions)
                       .arg(gameplay_clock_text());
#endif
    if (gameplay_owner->configuration().failure
        == gameplay::failure_policy::lives) {
#ifdef KC_KDE
        text += QStringLiteral("  ")
            + i18n("Lives: %1", gameplay_owner->remaining_lives());
#else
        text += QStringLiteral("  ")
            + str_label("Lives: %1").arg(gameplay_owner->remaining_lives());
#endif
    }
    const auto& batch = gameplay_owner->current_quiz_batch();
    if (batch && batch->remaining_ms
        && gameplay_owner->phase() != gameplay::session_phase::finished) {
        const auto seconds = static_cast<qulonglong>(
            *batch->remaining_ms / 1000 + (*batch->remaining_ms % 1000 != 0)
        );
#ifdef KC_KDE
        text += QStringLiteral("  ") + i18n("Quiz remaining: %1 s", seconds);
#else
        text += QStringLiteral("  ")
            + str_label("Quiz remaining: %1 s").arg(seconds);
#endif
    }
    return text;
}

void table::on_gameplay_clock_tick() {
    if (!gameplay_runtime_attached || !gameplay_owner
        || gameplay_owner->phase() != gameplay::session_phase::running) {
        stop_gameplay_clock();
        return;
    }
    std::vector<gameplay::quiz_answer> answers;
    (void)synchronize_gameplay_clock(answers);
    publish_gameplay_runtime(answers);
}

bool table::pause_gameplay_runtime() {
    if (!gameplay_runtime_attached || !gameplay_owner
        || gameplay_owner->phase() != gameplay::session_phase::running)
        return false;
    std::vector<gameplay::quiz_answer> answers;
    const bool accepted
        = synchronize_gameplay_clock(answers) && gameplay_owner->pause();
    stop_gameplay_clock();
    publish_gameplay_runtime(answers);
    return accepted;
}

bool table::resume_gameplay_runtime() {
    if (!gameplay_runtime_attached || !gameplay_owner
        || !gameplay_owner->resume())
        return false;
    gameplay_elapsed.start();
    gameplay_timer.start();
    publish_gameplay_runtime({});
    return true;
}

bool table::finish_gameplay_runtime() {
    if (!gameplay_runtime_attached || !gameplay_owner
        || (gameplay_owner->phase() != gameplay::session_phase::running
            && gameplay_owner->phase() != gameplay::session_phase::paused))
        return false;
    std::vector<gameplay::quiz_answer> answers;
    const bool accepted
        = synchronize_gameplay_clock(answers) && gameplay_owner->finish();
    stop_gameplay_clock();
    publish_gameplay_runtime(answers);
    return accepted;
}

bool table::set_gameplay_pick_interval(int interval_ms) {
    if (!gameplay_runtime_attached || !gameplay_owner
        || interval_ms < gameplay::session::minimum_pick_interval_ms
        || interval_ms > gameplay::session::maximum_pick_interval_ms
        || gameplay_owner->phase() == gameplay::session_phase::finished)
        return false;
    std::vector<gameplay::quiz_answer> answers;
    const auto previous = gameplay_owner->pick_interval_ms();
    const bool accepted = synchronize_gameplay_clock(answers)
        && gameplay_owner->set_pick_interval_ms(interval_ms);
    // A changed manual speed starts a fresh interval; no unsigned subtraction
    // against an old, larger remainder and no immediate surprise reveal.
    if (accepted && previous != interval_ms)
        gameplay_pick_elapsed_ms = 0;
    publish_gameplay_runtime(answers);
    return accepted;
}

std::optional<table::gameplay_prompt_key>
table::gameplay_quiz_prompt(gameplay::deck_id id) const {
    if (!gameplay_runtime_attached || !gameplay_owner)
        return std::nullopt;
    const auto* deck = gameplay_owner->deck(id);
    if (!deck || !deck->quiz)
        return std::nullopt;
    return gameplay_prompt_key { id, deck->quiz->batch_id,
                                 deck->dealt_physical_cards,
                                 gameplay_scene_revision };
}

bool table::current_gameplay_prompt(const gameplay_prompt_key& prompt) const {
    if (!gameplay_runtime_attached || !gameplay_owner
        || gameplay_owner->phase() != gameplay::session_phase::running)
        return false;
    return gameplay_quiz_prompt(prompt.owner) == prompt;
}

bool table::edit_gameplay_quiz(
    const gameplay_prompt_key& prompt, std::int64_t input
) {
    if (!current_gameplay_prompt(prompt))
        return false;
    std::vector<gameplay::quiz_answer> answers;
    const bool accepted = synchronize_gameplay_clock(answers)
        && current_gameplay_prompt(prompt)
        && gameplay_owner->edit_quiz_input(prompt.owner, input);
    publish_gameplay_runtime(answers);
    return accepted;
}

bool table::answer_gameplay_quiz(const gameplay_prompt_key& prompt, bool skip) {
    if (!current_gameplay_prompt(prompt))
        return false;
    std::vector<gameplay::quiz_answer> answers;
    bool accepted = false;
    if (synchronize_gameplay_clock(answers)
        && current_gameplay_prompt(prompt)) {
        const auto answer = skip ? gameplay_owner->skip_quiz(prompt.owner)
                                 : gameplay_owner->check_quiz(prompt.owner);
        if (answer) {
            answers.push_back(*answer);
            accepted = true;
        }
    }
    publish_gameplay_runtime(answers);
    return accepted;
}

bool table::check_gameplay_quiz(const gameplay_prompt_key& prompt) {
    return answer_gameplay_quiz(prompt, false);
}

bool table::skip_gameplay_quiz(const gameplay_prompt_key& prompt) {
    return answer_gameplay_quiz(prompt, true);
}
