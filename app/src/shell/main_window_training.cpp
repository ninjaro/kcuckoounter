#include "shell/main_window.hpp"

#include "card_helpers/card_sheet.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"

#include "arch/android_ui.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/session_checkpoint.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "settings/training_progress.hpp"

#include <QAbstractButton>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QMessageBox>
#include <QPointer>
#include <QScrollArea>
#include <QTabWidget>

void main_window::open_setup_dialog() {
    if (table_widget && table_widget->active_gameplay_session())
        return;
    if (setup_dialog != nullptr) {
#if defined(Q_OS_ANDROID)
        setup_dialog->setWindowState(Qt::WindowMaximized);
#endif
        setup_dialog->open();
    }
}

void main_window::on_setup_dialog_rejected() {
    if (table_widget && table_widget->active_gameplay_session())
        return;
    if (start_pause_action != nullptr && !quiz_started) {
        start_pause_action->setEnabled(true);
    }
    update_status_text();
}

void main_window::on_continue_button_clicked() {
    if (table_widget && table_widget->active_gameplay_session())
        return;
    int slot_count = table_slots_count->value();

    if (table_widget != nullptr) {
        table_widget->set_slot_count(slot_count);
        table_widget->show();
        table_widget->schedule_card_preload();
    }

    if (setup_dialog != nullptr) {
        setup_dialog->accept();
    }

    quiz_started = false;
    quiz_paused = false;
    quiz_finished = false;
    score_correct = 0;
    score_total = 0;
    mobile_checkpoint_restored = false;
    mobile_lifecycle_paused = false;
    clear_mobile_session_checkpoint();

    if (clock_timer != nullptr) {
        clock_timer->reset();
        refresh_clock_label();
    }
    update_status_text();

    if (start_pause_action != nullptr) {
        start_pause_action->setText(str_label("Start"));
        start_pause_action->setIcon(
            icon_loader::themed(
                { "media-playback-start", "media-playback-play", "play" },
                QStyle::SP_MediaPlay
            )
        );
        start_pause_action->setEnabled(true);
    }
    if (finish_action != nullptr) {
        finish_action->setEnabled(false);
    }
}

void main_window::on_new_game_triggered() {
    if (const auto* owner = table_widget->active_gameplay_session()) {
        if (owner->phase() == gameplay::session_phase::running
            || owner->phase() == gameplay::session_phase::paused) {
            on_finish_triggered();
            return;
        }
        table_widget->clear_gameplay_session();
        table_widget->set_slot_count(table_slots_count->value());
        reset_game_state(true);
        return;
    }
    if (quiz_started) {
        on_finish_triggered();
        return;
    }
    if (setup_dialog != nullptr) {
#if defined(Q_OS_ANDROID)
        setup_dialog->setWindowState(Qt::WindowMaximized);
#endif
        setup_dialog->show();
        setup_dialog->raise();
        setup_dialog->activateWindow();
    }
}

void main_window::on_start_pause_triggered() {
    if (table_widget == nullptr) {
        return;
    }

    if (const auto* owner = table_widget->active_gameplay_session()) {
        switch (owner->phase()) {
        case gameplay::session_phase::setup:
            (void)start_gameplay_from_ui();
            break;
        case gameplay::session_phase::running:
            (void)table_widget->pause_gameplay_runtime();
            break;
        case gameplay::session_phase::paused:
            (void)table_widget->resume_gameplay_runtime();
            break;
        case gameplay::session_phase::finished:
            break;
        }
        return;
    }

    if (!quiz_started) {
        table_widget->prepare_cards_for_start();
        start_quiz_from_ui();
        return;
    }

    if (!quiz_paused) {
        table_widget->set_paused(true);
        quiz_paused = true;

        if (start_pause_action != nullptr) {
            update_start_pause_action(true);
        }

        if (clock_timer != nullptr) {
            clock_timer->pause();
            refresh_clock_label();
        }
        update_status_text();
        mobile_checkpoint_restored = false;
        mobile_lifecycle_paused = false;
        persist_mobile_session_checkpoint(false);
    } else {
        table_widget->set_paused(false);
        quiz_paused = false;

        if (start_pause_action != nullptr) {
            update_start_pause_action(false);
        }

        if (clock_timer != nullptr) {
            clock_timer->start(true);
            refresh_clock_label();
        }
        update_status_text();
        mobile_checkpoint_restored = false;
        mobile_lifecycle_paused = false;
        persist_mobile_session_checkpoint(true);
    }
}

void main_window::on_finish_triggered() {
    if (const auto* owner = table_widget->active_gameplay_session()) {
        const auto phase = owner->phase();
        if (phase != gameplay::session_phase::running
            && phase != gameplay::session_phase::paused)
            return;
        const bool was_running = phase == gameplay::session_phase::running;
        bool replaced = false;
        const auto replacement = connect(
            table_widget, &table::gameplay_session_about_to_change, this,
            [&replaced] { replaced = true; }
        );
        const QPointer<main_window> guard(this);
        const bool paused
            = !was_running || table_widget->pause_gameplay_runtime();
        if (!guard)
            return;
        if (!paused || replaced) {
            disconnect(replacement);
            return;
        }
        const auto answer = QMessageBox::question(
            this, str_label("Finish"), str_label("Do you want to finish?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No
        );
        if (!guard)
            return;
        disconnect(replacement);
        if (replaced)
            return; // a modal event loop must not finish a replacement scene
        if (answer == QMessageBox::Yes)
            (void)table_widget->finish_gameplay_runtime();
        else if (was_running)
            (void)table_widget->resume_gameplay_runtime();
        return;
    }
    const bool restore_running_quiz
        = table_widget != nullptr && quiz_started && !quiz_paused;
    if (restore_running_quiz) {
        table_widget->set_paused(true);
        quiz_paused = true;
        if (start_pause_action != nullptr) {
            update_start_pause_action(true);
        }
        if (clock_timer != nullptr) {
            clock_timer->pause();
            refresh_clock_label();
        }
        update_status_text();
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, str_label("Finish"), str_label("Do you want to finish?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No
    );
    if (answer != QMessageBox::Yes) {
        if (restore_running_quiz) {
            table_widget->set_paused(false);
            quiz_paused = false;
            if (start_pause_action != nullptr) {
                update_start_pause_action(false);
            }
            if (clock_timer != nullptr) {
                clock_timer->start(true);
                refresh_clock_label();
            }
            update_status_text();
        }
        return;
    }

    show_game_over_dialog();
}

void main_window::start_quiz_from_ui() {
    if (table_widget && table_widget->active_gameplay_session())
        return;
    if (table_widget == nullptr || quiz_started) {
        return;
    }

    int quiz_type_index = 0;
    if (quiz_type != nullptr) {
        quiz_type_index = quiz_type->currentIndex();
    }

    bool wait_answers = false;
    if (wait_for_answers != nullptr) {
        wait_answers = wait_for_answers->isChecked();
    }
    if (quiz_type_index == 1) {
        wait_answers = true;
    }

    if (table_widget != nullptr && allow_skipping != nullptr) {
        table_widget->set_allow_skipping(allow_skipping->isChecked());
    }
    table_widget->start_quiz(quiz_type_index, wait_answers);

    quiz_started = true;
    quiz_paused = wait_answers;

    if (start_pause_action != nullptr) {
        update_start_pause_action(quiz_paused);
    }
    if (finish_action != nullptr) {
        finish_action->setEnabled(true);
    }

    if (clock_timer != nullptr) {
        clock_timer->reset();
        if (!quiz_paused) {
            clock_timer->start(true);
        }
        refresh_clock_label();
    }
    update_status_text();
    persist_mobile_session_checkpoint(!quiz_paused);
    refresh_gameplay_actions();
}

void main_window::on_settings_triggered() {
    pause_for_dialog();

    if (settings_dialog != nullptr) {
#if defined(Q_OS_ANDROID)
        settings_dialog->setWindowState(Qt::WindowMaximized);
#endif
        settings_dialog->show();
        settings_dialog->raise();
        settings_dialog->activateWindow();
        return;
    }

    settings_dialog = new QDialog(this, Qt::Window);
    settings_dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    settings_dialog->setModal(false);
    settings_dialog->setWindowModality(Qt::NonModal);
    settings_dialog->setWindowTitle(str_label("Settings"));

    auto dialog_layout = new QVBoxLayout(settings_dialog);

    auto tab_widget = new QTabWidget(settings_dialog);
    auto shared_state = new settings_shared_state(settings_dialog);
    connect(
        shared_state, &settings_shared_state::default_suit_changed, this,
        [this](int index) {
            gameplay_default_suit = index;
            update_status_text();
        }
    );
    appearance_settings_widget = new settings_template_widget(
        settings_tab_kind::appearance, tab_widget, QString(), table_widget,
        shared_state
    );
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    auto* appearance_scroll = new QScrollArea(tab_widget);
    appearance_scroll->setWidgetResizable(true);
    appearance_scroll->setFrameShape(QFrame::NoFrame);
    appearance_scroll->setWidget(appearance_settings_widget);
    tab_widget->addTab(appearance_scroll, str_label("Appearance"));
#else
    tab_widget->addTab(appearance_settings_widget, str_label("Appearance"));
#endif
    connect(
        appearance_settings_widget,
        &settings_template_widget::desktop_presentation_applied, this,
        &main_window::apply_desktop_presentation
    );
    tab_widget->addTab(
        new settings_template_widget(
            settings_tab_kind::strategies, tab_widget, QString(), nullptr,
            shared_state
        ),
        str_label("Strategies")
    );
    dialog_layout->addWidget(tab_widget);

    auto button_box = new QDialogButtonBox(settings_dialog);
    auto save_button
        = button_box->addButton(str_label("Save"), QDialogButtonBox::ApplyRole);
    auto cancel_button = button_box->addButton(
        str_label("Cancel"), QDialogButtonBox::ResetRole
    );
    auto close_button = button_box->addButton(
        str_label("Save and close"), QDialogButtonBox::AcceptRole
    );
    QObject::connect(
        save_button, &QAbstractButton::clicked, appearance_settings_widget,
        &settings_template_widget::apply_theme_settings
    );
    QObject::connect(
        cancel_button, &QAbstractButton::clicked, appearance_settings_widget,
        &settings_template_widget::reset_theme_selection
    );
    QObject::connect(
        close_button, &QAbstractButton::clicked, this,
        &main_window::on_settings_commit_requested
    );
    dialog_layout->addWidget(button_box);

    QObject::connect(
        settings_dialog, &QDialog::finished, this,
        &main_window::on_settings_dialog_finished
    );

#if defined(Q_OS_ANDROID)
    settings_dialog->setWindowState(Qt::WindowMaximized);
#else
    settings_dialog->resize(820, 620);
#endif
    settings_dialog->show();
    settings_dialog->raise();
    settings_dialog->activateWindow();
}

void main_window::on_show_highscores_triggered() {
    if (table_widget->active_gameplay_session())
        return;
    show_platform_highscores();
}

void main_window::on_progress_triggered() {
    const training_progress progress = load_training_progress();
    const int accuracy = progress.answered_questions > 0
        ? static_cast<int>(
              (progress.correct_answers * 100 + progress.answered_questions / 2)
              / progress.answered_questions
          )
        : 0;
    const int best_accuracy = progress.best_answered > 0
        ? (progress.best_correct * 100 + progress.best_answered / 2)
            / progress.best_answered
        : 0;
    QMessageBox::information(
        this, str_label("Training progress"),
        str_label(
            "Completed games: %1\nQuestions: %2\nCorrect: %3 (%4%)\n"
            "Best game: %5/%6 (%7%)"
        )
            .arg(progress.completed_sessions)
            .arg(progress.answered_questions)
            .arg(progress.correct_answers)
            .arg(accuracy)
            .arg(progress.best_correct)
            .arg(progress.best_answered)
            .arg(best_accuracy)
    );
}

void main_window::on_settings_commit_requested() {
    if (appearance_settings_widget != nullptr
        && !appearance_settings_widget->apply_theme_settings()) {
        return;
    }
    if (settings_dialog != nullptr) {
        settings_dialog->close();
    }
}

void main_window::on_settings_dialog_finished(int result) {
    Q_UNUSED(result);

    settings_dialog = nullptr;
    appearance_settings_widget = nullptr;
}

void main_window::pause_for_dialog() {
    if (table_widget && table_widget->active_gameplay_session()) {
        if (table_widget->active_gameplay_session()->phase()
            == gameplay::session_phase::running)
            (void)table_widget->pause_gameplay_runtime();
        return;
    }
    if (table_widget == nullptr || !quiz_started || quiz_paused) {
        return;
    }

    table_widget->set_paused(true);
    quiz_paused = true;

    if (start_pause_action != nullptr) {
        update_start_pause_action(true);
    }

    if (clock_timer != nullptr) {
        clock_timer->pause();
        refresh_clock_label();
    }
    update_status_text();
}

void main_window::reset_game_state(bool show_setup_dialog, bool mark_finished) {
    if (table_widget && table_widget->active_gameplay_session())
        return;
    if (show_setup_dialog && setup_dialog != nullptr) {
#if defined(Q_OS_ANDROID)
        setup_dialog->setWindowState(Qt::WindowMaximized);
#endif
        setup_dialog->show();
        setup_dialog->raise();
        setup_dialog->activateWindow();
    }

    if (table_widget != nullptr) {
        table_widget->clear_quiz();
        table_widget->set_paused(true);
    }

    quiz_started = false;
    quiz_paused = false;
    quiz_finished = mark_finished;
    score_correct = 0;
    score_total = 0;
    mobile_checkpoint_restored = false;
    mobile_lifecycle_paused = false;
    clear_mobile_session_checkpoint();

    if (clock_timer != nullptr) {
        clock_timer->reset();
        refresh_clock_label();
    }
    update_status_text();

    if (start_pause_action != nullptr) {
        start_pause_action->setText(str_label("Start"));
        start_pause_action->setIcon(
            icon_loader::themed(
                { "media-playback-start", "media-playback-play", "play" },
                QStyle::SP_MediaPlay
            )
        );
        start_pause_action->setEnabled(!show_setup_dialog);
    }
    if (finish_action != nullptr) {
        finish_action->setEnabled(false);
    }
    refresh_gameplay_actions();
}

void main_window::show_game_over_dialog() {
    if (table_widget && table_widget->active_gameplay_session())
        return;
    if (clock_timer != nullptr) {
        clock_timer->pause();
    }
    clear_mobile_session_checkpoint();
    const QString score_text
        = str_label("Score: %1/%2").arg(score_correct).arg(score_total);
    if (score_total > 0) {
        record_training_result(
            {
                .correct = score_correct,
                .answered = score_total,
                .elapsed_ms
                = clock_timer != nullptr ? clock_timer->elapsed_time_ms() : 0,
                .completed_at_utc_ms = QDateTime::currentMSecsSinceEpoch(),
            }
        );
    }
    QMessageBox::information(
        this, str_label("Game over"), str_label("Game over\n%1").arg(score_text)
    );
    record_platform_score();
    reset_game_state(true, true);
}
