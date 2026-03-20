#include "shell/main_window.hpp"

#include "table/settings_template.hpp"
#include "table/table.hpp"

#include "arch/android_ui.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "monitor/resource_monitor.hpp"

#include <QAbstractButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QSlider>
#include <QStringList>
#include <QTabWidget>

#include <algorithm>
#include <functional>

main_window::main_window(BaseWidget* parent)
    : BaseMainWindow(parent)
    , table_slots_count(nullptr)
    , quiz_type(nullptr)
    , wait_for_answers(nullptr)
    , allow_skipping(nullptr)
    , dealing_mode(nullptr)
    , continue_button(nullptr)
    , primary_toolbar(nullptr)
    , table_widget(nullptr)
    , setup_dialog(nullptr)
    , settings_dialog(nullptr)
    , appearance_settings_widget(nullptr)
    , setup_widget(nullptr)
    , clock_timer(nullptr)
    , kde_clock(nullptr)
    , clock_label(nullptr)
    , status_label(nullptr)
    , pickup_interval_label(nullptr)
    , raster_progress(nullptr)
    , speed_slider(nullptr)
    , new_game_action(nullptr)
    , start_pause_action(nullptr)
    , finish_action(nullptr)
    , highscores_action(nullptr)
    , settings_action(nullptr)
    , export_debug_snapshot_action(nullptr)
    , export_process_memory_report_action(nullptr)
    , add_monitor_marker_action(nullptr)
    , realistic_cadence_mode_action(nullptr)
    , instrumented_cadence_mode_action(nullptr)
    , toggle_debug_broadcaster_action(nullptr)
    , quiz_started(false)
    , quiz_paused(false)
    , quiz_finished(false)
    , rasterization_busy(false)
    , score_correct(0)
    , score_total(0)
    , debug_telemetry_collector(nullptr) {
    setup_ui();
}

main_window::~main_window() { }

void main_window::update_start_pause_action(bool paused) const {
    if (start_pause_action == nullptr) {
        return;
    }

    if (paused) {
        start_pause_action->setText(str_label("Resume"));
        start_pause_action->setIcon(
            icon_loader::themed(
                { "media-playback-start", "media-playback-play", "play" },
                QStyle::SP_MediaPlay
            )
        );
        return;
    }

    start_pause_action->setText(str_label("Pause"));
    start_pause_action->setIcon(
        icon_loader::themed(
            { "media-playback-pause", "media-playback-stop", "pause" },
            QStyle::SP_MediaPause
        )
    );
}

void main_window::setup_game_actions() {
    new_game_action = new BaseAction(str_label("New game"), this);
    register_shell_action(new_game_action, QStringLiteral("game_new"));
    new_game_action->setIcon(
        icon_loader::themed(
            { "document-new", "list-add", "folder-new" }, QStyle::SP_FileIcon
        )
    );
    QObject::connect(
        new_game_action, &BaseAction::triggered, this,
        &main_window::on_new_game_triggered
    );

    start_pause_action = new BaseAction(str_label("Start"), this);
    register_shell_action(start_pause_action, QStringLiteral("game_start_pause"));
    start_pause_action->setIcon(
        icon_loader::themed(
            { "media-playback-start", "media-playback-play", "play" },
            QStyle::SP_MediaPlay
        )
    );
    start_pause_action->setEnabled(false);
    start_pause_action->setShortcut(QKeySequence(Qt::Key_P));
    QObject::connect(
        start_pause_action, &BaseAction::triggered, this,
        &main_window::on_start_pause_triggered
    );

    finish_action = new BaseAction(str_label("Finish"), this);
    register_shell_action(finish_action, QStringLiteral("game_finish"));
    finish_action->setIcon(
        icon_loader::themed(
            { "process-stop-symbolic", "process-stop", "dialog-close-symbolic",
              "dialog-close", "window-close" },
            QStyle::SP_DialogCloseButton
        )
    );
    finish_action->setEnabled(false);
    QObject::connect(
        finish_action, &BaseAction::triggered, this,
        &main_window::on_finish_triggered
    );

    settings_action = new BaseAction(str_label("Settings"), this);
    register_shell_action(settings_action, QStringLiteral("game_settings"));
    settings_action->setIcon(
        icon_loader::themed(
            { "preferences-system-symbolic", "settings-symbolic",
              "preferences-system", "configure", "settings" },
            QStyle::SP_FileDialogDetailedView
        )
    );
    QObject::connect(
        settings_action, &BaseAction::triggered, this,
        &main_window::on_settings_triggered
    );
}

void main_window::setup_ui() {
    setup_platform_shell();

    auto central_widget = new BaseWidget(this);
    auto main_layout = new BaseVBoxLayout;

    setup_dialog = new QDialog(this);
    setup_dialog->setWindowTitle(str_label("New game"));
    setup_dialog->setModal(false);
    setup_dialog->setWindowModality(Qt::NonModal);

    setup_widget = new BaseWidget(setup_dialog);
    auto setup_layout = new BaseVBoxLayout;

    auto form_layout = new BaseFormLayout;

    table_slots_count = new BaseSpinBox(setup_widget);
    table_slots_count->setMinimum(1);
    table_slots_count->setMaximum(16);
    table_slots_count->setValue(4);
    android_ui::apply_spin_box_style(table_slots_count);

    quiz_type = new BaseComboBox(setup_widget);
    quiz_type->addItems(
        QStringList() << str_label("Single question")
                      << str_label("Multi question")
    );
    android_ui::apply_combo_box_style(quiz_type);

    wait_for_answers
        = new BaseCheckBox(str_label("Pause for answers"), setup_widget);
    wait_for_answers->setToolTip(
        str_label("Pause the game while waiting for quiz answers")
    );
    android_ui::apply_check_box_style(wait_for_answers);

    allow_skipping
        = new BaseCheckBox(str_label("Allow skipping questions"), setup_widget);
    allow_skipping->setChecked(true);
    allow_skipping->setToolTip(
        str_label("Enable the skip button during quizzes")
    );
    android_ui::apply_check_box_style(allow_skipping);

    dealing_mode = new BaseComboBox(setup_widget);
    dealing_mode->addItems(
        QStringList() << str_label("Sequential") << str_label("Random")
                      << str_label("Simultaneous")
    );
    android_ui::apply_combo_box_style(dealing_mode);

    form_layout->addRow(str_label("Table slots"), table_slots_count);
    form_layout->addRow(str_label("Quiz mode"), quiz_type);
    form_layout->addRow(wait_for_answers);
    form_layout->addRow(allow_skipping);
    form_layout->addRow(str_label("Dealing mode"), dealing_mode);

    continue_button = android_ui::create_button(setup_widget);
    continue_button->setText(str_label("Continue"));
    android_ui::apply_button_style(
        continue_button, android_button_profile::primary
    );

    setup_layout->addLayout(form_layout);
    setup_layout->addWidget(continue_button);
    setup_layout->addStretch();
    setup_widget->setLayout(setup_layout);

    auto dialog_layout = new BaseVBoxLayout;
    dialog_layout->addWidget(setup_widget);
    setup_dialog->setLayout(dialog_layout);

    table_widget = new table(central_widget);

    setup_game_actions();
    setup_debug_monitoring();

    main_layout->addWidget(table_widget, 1);

    central_widget->setLayout(main_layout);
    setCentralWidget(central_widget);

    finalize_platform_shell();
    setup_status_surface(main_layout);

    clock_timer = new BaseClock(this);
    if (table_widget != nullptr && clock_timer != nullptr) {
        QObject::connect(
            clock_timer, &BaseClock::ticked, table_widget, &table::on_clock_tick
        );
        QObject::connect(
            clock_timer, &BaseClock::ticked, this, &main_window::on_clock_ticked
        );
    }
    if (table_widget != nullptr) {
        QObject::connect(
            table_widget, &table::rasterization_busy_changed, this,
            &main_window::on_table_rasterization_busy_changed
        );
        QObject::connect(
            table_widget, &table::game_over, this,
            &main_window::show_game_over_dialog
        );
        QObject::connect(
            table_widget, &table::dialog_opened, this,
            &main_window::pause_for_dialog
        );
        QObject::connect(
            table_widget, &table::score_adjusted, this,
            &main_window::on_table_score_adjusted
        );
    }
    if (table_widget != nullptr && speed_slider != nullptr) {
        table_widget->set_pick_interval(speed_slider->value());
    }

    setWindowTitle(str_label("kcuckoounter"));

    QObject::connect(
        continue_button, &BasePushButton::clicked, this,
        &main_window::on_continue_button_clicked
    );

    if (speed_slider != nullptr) {
        QObject::connect(
            speed_slider, &QSlider::valueChanged, this,
            &main_window::on_speed_slider_value_changed
        );
    }

    if (dealing_mode != nullptr) {
        QObject::connect(
            dealing_mode, &BaseComboBox::currentIndexChanged, this,
            &main_window::on_dealing_mode_changed
        );
        if (table_widget != nullptr) {
            table_widget->set_dealing_mode(dealing_mode->currentIndex());
        }
    }

    if (quiz_type != nullptr && wait_for_answers != nullptr) {
        QObject::connect(
            quiz_type, &BaseComboBox::currentIndexChanged, this,
            &main_window::on_quiz_type_changed
        );
        on_quiz_type_changed(quiz_type->currentIndex());
    }

    if (table_slots_count != nullptr) {
        QObject::connect(
            table_slots_count, &BaseSpinBox::valueChanged, this,
            &main_window::on_table_slot_count_changed
        );
    }

    if (table_widget != nullptr && table_slots_count != nullptr) {
        table_widget->set_slot_count(table_slots_count->value());
        table_widget->show();
        table_widget->schedule_card_preload();
    }

    if (pickup_interval_label != nullptr && speed_slider != nullptr) {
        pickup_interval_label->setText(
            str_label("Pickup interval: %1 ms").arg(speed_slider->value())
        );
    }
    refresh_clock_label();

    if (setup_dialog != nullptr) {
        time_interface::single_shot(
            0, setup_dialog, std::bind_front(&main_window::open_setup_dialog, this)
        );
        QObject::connect(
            setup_dialog, &QDialog::rejected, this,
            &main_window::on_setup_dialog_rejected
        );
    }
}

void main_window::on_clock_ticked(qint64 elapsed_ms, qint64 delta_ms) {
    Q_UNUSED(elapsed_ms);
    Q_UNUSED(delta_ms);

    refresh_clock_label();
    update_status_text();
}

void main_window::on_table_rasterization_busy_changed(bool busy) {
    rasterization_busy = busy;
    if (raster_progress != nullptr) {
        raster_progress->setVisible(busy);
    }
    update_status_text();
}

void main_window::on_table_score_adjusted(int correct_delta, int total_delta) {
    score_correct = std::max(0, score_correct + correct_delta);
    score_total = std::max(0, score_total + total_delta);
    update_status_text();
}

void main_window::on_speed_slider_value_changed(int value) {
    if (table_widget != nullptr) {
        table_widget->set_pick_interval(value);
    }
    if (pickup_interval_label != nullptr) {
        pickup_interval_label->setText(
            str_label("Pickup interval: %1 ms").arg(value)
        );
    }
}

void main_window::on_dealing_mode_changed(int index) {
    if (table_widget != nullptr) {
        table_widget->set_dealing_mode(index);
    }
}

void main_window::on_quiz_type_changed(int index) {
    if (wait_for_answers == nullptr) {
        return;
    }

    const bool is_multi_question = index == 1;
    wait_for_answers->setChecked(is_multi_question);
    wait_for_answers->setEnabled(!is_multi_question);
}

void main_window::on_table_slot_count_changed(int value) {
    if (table_widget == nullptr) {
        return;
    }

    table_widget->set_slot_count(value);
    table_widget->schedule_card_preload();
}

void main_window::open_setup_dialog() {
    if (setup_dialog != nullptr) {
        setup_dialog->open();
    }
}

void main_window::on_setup_dialog_rejected() {
    add_debug_marker(QStringLiteral("new_game_dialog_closed"));
    if (start_pause_action != nullptr && !quiz_started) {
        start_pause_action->setEnabled(true);
    }
    update_status_text();
}

void main_window::on_continue_button_clicked() {
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
    add_debug_marker(QStringLiteral("new_game_triggered"));
    if (quiz_started) {
        on_finish_triggered();
        return;
    }
    if (setup_dialog != nullptr) {
        add_debug_marker(QStringLiteral("new_game_dialog_opened"));
        setup_dialog->show();
        setup_dialog->raise();
        setup_dialog->activateWindow();
    }
}

void main_window::on_start_pause_triggered() {
    add_debug_marker(QStringLiteral("start_pause_triggered"));
    if (table_widget == nullptr) {
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
    }
}

void main_window::on_finish_triggered() {
    if (table_widget != nullptr && quiz_started && !quiz_paused) {
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
        return;
    }

    show_game_over_dialog();
}

void main_window::start_quiz_from_ui() {
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
}

void main_window::on_settings_triggered() {
    add_debug_marker(QStringLiteral("settings_opened"));
    pause_for_dialog();

    if (settings_dialog != nullptr) {
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
    appearance_settings_widget = new settings_template_widget(
        settings_tab_kind::appearance, tab_widget, QString(), table_widget,
        shared_state
    );
    tab_widget->addTab(appearance_settings_widget, str_label("Appearance"));
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
        &main_window::on_settings_save_and_close_requested
    );
    dialog_layout->addWidget(button_box);

    QObject::connect(
        settings_dialog, &QDialog::finished, this,
        &main_window::on_settings_dialog_finished
    );

    settings_dialog->resize(820, 620);
    settings_dialog->show();
    settings_dialog->raise();
    settings_dialog->activateWindow();
}

void main_window::on_show_highscores_triggered() { show_platform_highscores(); }

void main_window::on_settings_save_and_close_requested() {
    if (appearance_settings_widget != nullptr) {
        appearance_settings_widget->apply_theme_settings();
    }
    if (settings_dialog != nullptr) {
        settings_dialog->close();
    }
}

void main_window::on_settings_dialog_finished(int result) {
    Q_UNUSED(result);

    add_debug_marker(QStringLiteral("settings_closed"));
    settings_dialog = nullptr;
    appearance_settings_widget = nullptr;
}

void main_window::update_status_text() {
    if (status_label == nullptr) {
        return;
    }

    QStringList status_entries;
    if (rasterization_busy) {
        status_entries.append(str_label("Processing"));
    }

    if (quiz_finished) {
        status_entries.append(str_label("Finished"));
    } else if (!quiz_started) {
        status_entries.append(str_label("Ready"));
    } else if (quiz_paused) {
        status_entries.append(str_label("Paused"));
    } else {
        status_entries.append(str_label("Running"));
    }
    const QString status_value = status_entries.join(str_label(" / "));

    QString time_label = str_label("00:00");
    if (clock_timer != nullptr) {
        time_label = clock_timer->time_string_mm_ss();
    }

    const QString debug_mode_suffix = debug_status_suffix();

    if (!quiz_started && !quiz_finished) {
        status_label->setText(
            str_label("Status: %1%2").arg(status_value, debug_mode_suffix)
        );
        return;
    }

    status_label->setText(str_label("Status: %1  Score: %2/%3  Time: %4%5")
                              .arg(status_value)
                              .arg(score_correct)
                              .arg(score_total)
                              .arg(time_label)
                              .arg(debug_mode_suffix));
}

void main_window::pause_for_dialog() {
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
    if (show_setup_dialog && setup_dialog != nullptr) {
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
}

void main_window::show_game_over_dialog() {
    const QString score_text
        = str_label("Score: %1/%2").arg(score_correct).arg(score_total);
    QMessageBox::information(
        this, str_label("Game over"), str_label("Game over\n%1").arg(score_text)
    );
    record_platform_score();
    reset_game_state(true, true);
}
