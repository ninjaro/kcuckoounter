#include "main_window.hpp"

#include "table/settings_template.hpp"
#include "table/table.hpp"

#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "monitor/resource_monitor.hpp"

#include <QAbstractButton>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QToolBar>

#include <algorithm>

main_window::main_window(BaseWidget* parent)
    : BaseMainWindow(parent)
    , table_slots_count(nullptr)
    , quiz_type(nullptr)
    , wait_for_answers(nullptr)
    , allow_skipping(nullptr)
    , dealing_mode(nullptr)
    , continue_button(nullptr)
    , table_widget(nullptr)
    , setup_dialog(nullptr)
    , settings_dialog(nullptr)
    , setup_widget(nullptr)
    , clock_timer(nullptr)
    , clock_label(nullptr)
    , status_label(nullptr)
    , pickup_interval_label(nullptr)
    , raster_progress(nullptr)
    , speed_slider(nullptr)
    , new_game_action(nullptr)
    , start_pause_action(nullptr)
    , finish_action(nullptr)
    , settings_action(nullptr)
#ifndef NDEBUG
    , export_debug_snapshot_action(nullptr)
    , realistic_cadence_mode_action(nullptr)
    , instrumented_cadence_mode_action(nullptr)
    , toggle_resource_monitor_action(nullptr)
    , resource_monitor_window(nullptr)
    , resource_monitor_tabs(nullptr)
    , resource_monitor_live_text(nullptr)
    , resource_monitor_timeline_text(nullptr)
    , resource_monitor_diagnostics_text(nullptr)
#endif
    , quiz_started(false)
    , quiz_paused(false)
    , quiz_finished(false)
    , rasterization_busy(false)
    , score_correct(0)
    , score_total(0)
    , debug_telemetry_collector(nullptr) {
    setup_ui();
}

main_window::~main_window() = default;

void main_window::refresh_clock_label() const {
    if (clock_timer != nullptr && clock_label != nullptr) {
        clock_label->setText(clock_timer->time_string_hh_mm_ss());
    }
}

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

void main_window::setup_ui() {
    auto toolbar = new BaseToolBar(str_label("Main"), this);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addToolBar(toolbar);

    new_game_action = toolbar->addAction(str_label("New game"));
    new_game_action->setIcon(
        icon_loader::themed(
            { "document-new", "list-add", "folder-new" }, QStyle::SP_FileIcon
        )
    );
    QObject::connect(
        new_game_action, &BaseAction::triggered, this,
        &main_window::on_new_game_triggered
    );

    start_pause_action = toolbar->addAction(str_label("Start"));
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

    finish_action = toolbar->addAction(str_label("Finish"));
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

    settings_action = toolbar->addAction(str_label("Settings"));
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

#ifndef NDEBUG
    toolbar->addSeparator();

    toggle_resource_monitor_action = toolbar->addAction(str_label("Monitor"));
    toggle_resource_monitor_action->setIcon(
        icon_loader::themed(
            { "utilities-system-monitor", "org.kde.plasma.systemmonitor",
              "view-statistics", "monitor" },
            QStyle::SP_ComputerIcon
        )
    );
    toggle_resource_monitor_action->setToolTip(
        str_label("Open the debug resource monitor window")
    );
    toggle_resource_monitor_action->setCheckable(true);
    toggle_resource_monitor_action->setChecked(false);
    QObject::connect(
        toggle_resource_monitor_action, &BaseAction::triggered, this,
        &main_window::on_toggle_resource_monitor_triggered
    );
#endif

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

    quiz_type = new BaseComboBox(setup_widget);
    quiz_type->addItems(
        QStringList() << str_label("Single question")
                      << str_label("Multi question")
    );

    wait_for_answers
        = new BaseCheckBox(str_label("Pause for answers"), setup_widget);
    wait_for_answers->setToolTip(
        str_label("Pause the game while waiting for quiz answers")
    );

    allow_skipping
        = new BaseCheckBox(str_label("Allow skipping questions"), setup_widget);
    allow_skipping->setChecked(true);
    allow_skipping->setToolTip(
        str_label("Enable the skip button during quizzes")
    );

    dealing_mode = new BaseComboBox(setup_widget);
    dealing_mode->addItems(
        QStringList() << str_label("Sequential") << str_label("Random")
                      << str_label("Simultaneous")
    );

    form_layout->addRow(str_label("Table slots"), table_slots_count);
    form_layout->addRow(str_label("Quiz mode"), quiz_type);
    form_layout->addRow(wait_for_answers);
    form_layout->addRow(allow_skipping);
    form_layout->addRow(str_label("Dealing mode"), dealing_mode);

    continue_button = new BasePushButton(setup_widget);
    continue_button->setText(str_label("Continue"));

    setup_layout->addLayout(form_layout);
    setup_layout->addWidget(continue_button);
    setup_layout->addStretch();
    setup_widget->setLayout(setup_layout);

    auto dialog_layout = new BaseVBoxLayout;
    dialog_layout->addWidget(setup_widget);
    setup_dialog->setLayout(dialog_layout);

    table_widget = new table(central_widget);

#ifndef NDEBUG
    debug_telemetry_collector = new resource_monitor(this);
    if (table_widget != nullptr) {
        debug_telemetry_collector->attach_cache_service(
            table_widget->shared_raster_cache_service()
        );
    }
    sync_debug_cadence_mode_actions();

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        dump_debug_telemetry_on_exit();
    });

    resource_monitor_window = new QDialog(this, Qt::Window);
    resource_monitor_window->setWindowTitle(str_label("Resource monitor"));
    resource_monitor_window->setModal(false);
    resource_monitor_window->resize(760, 520);

    auto monitor_menu_bar = new QMenuBar(resource_monitor_window);
    auto monitor_menu = monitor_menu_bar->addMenu(str_label("Monitor"));

    export_debug_snapshot_action
        = monitor_menu->addAction(str_label("Export debug snapshot"));
    export_debug_snapshot_action->setToolTip(
        str_label("Export cache/resource telemetry snapshot to JSON")
    );
    QObject::connect(
        export_debug_snapshot_action, &BaseAction::triggered, this,
        &main_window::on_export_debug_snapshot_triggered
    );

    auto cadence_action_group = new QActionGroup(this);
    cadence_action_group->setExclusive(true);
    auto debug_mode_menu = monitor_menu->addMenu(str_label("Debug mode"));

    realistic_cadence_mode_action
        = debug_mode_menu->addAction(str_label("Realistic"));
    realistic_cadence_mode_action->setCheckable(true);
    cadence_action_group->addAction(realistic_cadence_mode_action);
    QObject::connect(
        realistic_cadence_mode_action, &BaseAction::triggered, this,
        &main_window::on_set_realistic_cadence_mode_triggered
    );

    instrumented_cadence_mode_action
        = debug_mode_menu->addAction(str_label("Instrumented"));
    instrumented_cadence_mode_action->setCheckable(true);
    cadence_action_group->addAction(instrumented_cadence_mode_action);
    QObject::connect(
        instrumented_cadence_mode_action, &BaseAction::triggered, this,
        &main_window::on_set_instrumented_cadence_mode_triggered
    );

    auto monitor_layout = new BaseVBoxLayout;
    monitor_layout->setMenuBar(monitor_menu_bar);
    resource_monitor_tabs = new QTabWidget(resource_monitor_window);

    resource_monitor_live_text = new QPlainTextEdit(resource_monitor_window);
    resource_monitor_live_text->setReadOnly(true);
    resource_monitor_live_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    resource_monitor_tabs->addTab(
        resource_monitor_live_text, str_label("Live")
    );

    resource_monitor_timeline_text
        = new QPlainTextEdit(resource_monitor_window);
    resource_monitor_timeline_text->setReadOnly(true);
    resource_monitor_timeline_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    resource_monitor_tabs->addTab(
        resource_monitor_timeline_text, str_label("Timeline")
    );

    resource_monitor_diagnostics_text
        = new QPlainTextEdit(resource_monitor_window);
    resource_monitor_diagnostics_text->setReadOnly(true);
    resource_monitor_diagnostics_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    resource_monitor_tabs->addTab(
        resource_monitor_diagnostics_text, str_label("Diagnostics")
    );

    monitor_layout->addWidget(resource_monitor_tabs);
    resource_monitor_window->setLayout(monitor_layout);
    resource_monitor_window->hide();

    QObject::connect(
        resource_monitor_window, &QDialog::finished, this,
        [this](int) { on_resource_monitor_visibility_changed(false); }
    );

    QObject::connect(
        debug_telemetry_collector, &resource_monitor::cache_snapshot_collected,
        this, [this](const resource_monitor::cache_timeline_entry&) {
            refresh_resource_monitor_view();
        }
    );
    QObject::connect(
        debug_telemetry_collector, &resource_monitor::event_recorded, this,
        [this](const resource_monitor::event_timeline_entry&) {
            refresh_resource_monitor_view();
        }
    );

    refresh_resource_monitor_view();
#endif

    main_layout->addWidget(table_widget, 1);

    central_widget->setLayout(main_layout);
    setCentralWidget(central_widget);

    if (auto window_status_bar = statusBar()) {
        status_label = new QLabel(this);
        status_label->setText(QString());
        window_status_bar->addPermanentWidget(status_label, 1);

        raster_progress = new QProgressBar(this);
        raster_progress->setTextVisible(false);
        raster_progress->setRange(0, 0);
        raster_progress->setVisible(false);
        raster_progress->setFixedWidth(120);
        window_status_bar->addPermanentWidget(raster_progress);

        pickup_interval_label = new QLabel(this);
        pickup_interval_label->setText(QString());
        window_status_bar->addPermanentWidget(pickup_interval_label);

        speed_slider = new QSlider(Qt::Horizontal, this);
        speed_slider->setRange(100, 1000);
        speed_slider->setValue(300);
        speed_slider->setToolTip(str_label("Card pickup interval (ms)"));
        window_status_bar->addPermanentWidget(speed_slider);

        clock_label = new QLabel(this);
        clock_label->setText(str_label("00:00:00"));
        window_status_bar->addPermanentWidget(clock_label);

        clock_timer = new BaseClock(this);
    }
    if (table_widget != nullptr && clock_timer != nullptr) {
        QObject::connect(
            clock_timer, &BaseClock::ticked, table_widget, &table::on_clock_tick
        );
        QObject::connect(
            clock_timer, &BaseClock::ticked, this, [this](qint64, qint64) {
                if (clock_label != nullptr && clock_timer != nullptr) {
                    clock_label->setText(clock_timer->time_string_hh_mm_ss());
                }
                update_status_text();
            }
        );
    }
    if (table_widget != nullptr) {
        QObject::connect(
            table_widget, &table::rasterization_busy_changed, this,
            [this](bool busy) {
                rasterization_busy = busy;
                if (raster_progress != nullptr) {
                    raster_progress->setVisible(busy);
                }
                update_status_text();
            }
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
            [this](int correct_delta, int total_delta) {
                score_correct = std::max(0, score_correct + correct_delta);
                score_total = std::max(0, score_total + total_delta);
                update_status_text();
            }
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
            speed_slider, &QSlider::valueChanged, this, [this](int value) {
                if (table_widget != nullptr) {
                    table_widget->set_pick_interval(value);
                }
                if (pickup_interval_label != nullptr) {
                    pickup_interval_label->setText(
                        str_label("Pickup interval: %1 ms").arg(value)
                    );
                }
            }
        );
    }

    if (dealing_mode != nullptr) {
        QObject::connect(
            dealing_mode, &BaseComboBox::currentIndexChanged, this,
            [this](int index) {
                if (table_widget != nullptr) {
                    table_widget->set_dealing_mode(index);
                }
            }
        );
        if (table_widget != nullptr) {
            table_widget->set_dealing_mode(dealing_mode->currentIndex());
        }
    }

    if (quiz_type != nullptr && wait_for_answers != nullptr) {
        QObject::connect(
            quiz_type, &BaseComboBox::currentIndexChanged, this,
            [this](int index) {
                if (wait_for_answers == nullptr) {
                    return;
                }
                const bool is_multi_question = index == 1;
                wait_for_answers->setChecked(is_multi_question);
                wait_for_answers->setEnabled(!is_multi_question);
            }
        );
        const int initial_quiz_type = quiz_type->currentIndex();
        const bool initial_multi_question = initial_quiz_type == 1;
        wait_for_answers->setChecked(initial_multi_question);
        wait_for_answers->setEnabled(!initial_multi_question);
    }

    if (table_slots_count != nullptr) {
        QObject::connect(
            table_slots_count, &BaseSpinBox::valueChanged, this,
            [this](int value) {
                if (table_widget != nullptr) {
                    table_widget->set_slot_count(value);
                    table_widget->schedule_card_preload();
                }
            }
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

    if (setup_dialog != nullptr) {
        time_interface::single_shot(0, setup_dialog, [this]() {
            if (setup_dialog != nullptr) {
                setup_dialog->open();
            }
        });
        QObject::connect(setup_dialog, &QDialog::rejected, this, [this]() {
#ifndef NDEBUG
            add_debug_marker(QStringLiteral("new_game_dialog_closed"));
#endif
            if (start_pause_action != nullptr && !quiz_started) {
                start_pause_action->setEnabled(true);
            }
            update_status_text();
        });
    }
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
#ifndef NDEBUG
    add_debug_marker(QStringLiteral("new_game_triggered"));
#endif
    if (quiz_started) {
        on_finish_triggered();
        return;
    }
    if (setup_dialog != nullptr) {
#ifndef NDEBUG
        add_debug_marker(QStringLiteral("new_game_dialog_opened"));
#endif
        setup_dialog->show();
        setup_dialog->raise();
        setup_dialog->activateWindow();
    }
}

void main_window::on_start_pause_triggered() {
#ifndef NDEBUG
    add_debug_marker(QStringLiteral("start_pause_triggered"));
#endif
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
#ifndef NDEBUG
    add_debug_marker(QStringLiteral("settings_opened"));
#endif
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
    auto appearance_widget = new settings_template_widget(
        settings_tab_kind::appearance, tab_widget, QString(), table_widget,
        shared_state
    );
    tab_widget->addTab(appearance_widget, str_label("Appearance"));
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
        save_button, &QAbstractButton::clicked, appearance_widget,
        &settings_template_widget::apply_theme_settings
    );
    QObject::connect(
        cancel_button, &QAbstractButton::clicked, appearance_widget,
        &settings_template_widget::reset_theme_selection
    );
    QObject::connect(
        close_button, &QAbstractButton::clicked, settings_dialog,
        [this, appearance_widget]() {
            if (appearance_widget != nullptr) {
                appearance_widget->apply_theme_settings();
            }
            if (settings_dialog != nullptr) {
                settings_dialog->close();
            }
        }
    );
    dialog_layout->addWidget(button_box);

    QObject::connect(settings_dialog, &QDialog::finished, this, [this](int) {
#ifndef NDEBUG
        add_debug_marker(QStringLiteral("settings_closed"));
#endif
        settings_dialog = nullptr;
    });

    settings_dialog->resize(820, 620);
    settings_dialog->show();
    settings_dialog->raise();
    settings_dialog->activateWindow();
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

    QString debug_mode_suffix;
#ifndef NDEBUG
    if (debug_telemetry_collector != nullptr) {
        const bool instrumented
            = debug_telemetry_collector->get_debug_cadence_mode()
            == resource_monitor::debug_cadence_mode::instrumented;
        debug_mode_suffix = str_label("  Debug cadence: %1")
                                .arg(
                                    instrumented ? str_label("instrumented")
                                                 : str_label("realistic")
                                );
    }
#endif

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

#ifndef NDEBUG
void main_window::on_export_debug_snapshot_triggered() {
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd_hhmmss")
    );
    const QString suggested_name
        = QStringLiteral("debug_snapshot_%1.json").arg(timestamp);
    const QString output_path = QFileDialog::getSaveFileName(
        this, str_label("Export debug snapshot"), suggested_name,
        str_label("JSON files (*.json)")
    );
    if (output_path.isEmpty()) {
        return;
    }

    add_debug_marker(QStringLiteral("manual_export_snapshot"));
    debug_telemetry_collector->export_debug_snapshot_async(output_path);
}

void main_window::on_set_realistic_cadence_mode_triggered() {
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    debug_telemetry_collector->set_debug_cadence_mode(
        resource_monitor::debug_cadence_mode::realistic
    );
    add_debug_marker(QStringLiteral("cadence_mode_realistic"));
    sync_debug_cadence_mode_actions();
    update_status_text();
}

void main_window::on_set_instrumented_cadence_mode_triggered() {
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    debug_telemetry_collector->set_debug_cadence_mode(
        resource_monitor::debug_cadence_mode::instrumented
    );
    add_debug_marker(QStringLiteral("cadence_mode_instrumented"));
    sync_debug_cadence_mode_actions();
    update_status_text();
}

void main_window::add_debug_marker(const QString& label) const {
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    debug_telemetry_collector->add_manual_marker(label);
}

void main_window::sync_debug_cadence_mode_actions() const {
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    const bool is_instrumented
        = debug_telemetry_collector->get_debug_cadence_mode()
        == resource_monitor::debug_cadence_mode::instrumented;
    if (realistic_cadence_mode_action != nullptr) {
        realistic_cadence_mode_action->setChecked(!is_instrumented);
    }
    if (instrumented_cadence_mode_action != nullptr) {
        instrumented_cadence_mode_action->setChecked(is_instrumented);
    }
}

void main_window::on_toggle_resource_monitor_triggered(bool checked) {
    if (resource_monitor_window == nullptr) {
        return;
    }

    if (checked) {
        resource_monitor_window->show();
        resource_monitor_window->raise();
        resource_monitor_window->activateWindow();
        on_resource_monitor_visibility_changed(true);
        return;
    }

    resource_monitor_window->hide();
    on_resource_monitor_visibility_changed(false);
}

void main_window::on_resource_monitor_visibility_changed(bool visible) {
    if (toggle_resource_monitor_action != nullptr) {
        toggle_resource_monitor_action->setChecked(visible);
    }

    if (visible) {
        refresh_resource_monitor_view();
    }
}

void main_window::refresh_resource_monitor_view() {
    if (resource_monitor_live_text == nullptr
        || resource_monitor_timeline_text == nullptr
        || resource_monitor_diagnostics_text == nullptr
        || debug_telemetry_collector == nullptr) {
        return;
    }

    QStringList live_lines;
    live_lines.append(str_label("Debug resource monitor (collector shell)"));
    live_lines.append(str_label("Cache timeline entries: %1")
                          .arg(debug_telemetry_collector->timeline_size()));
    live_lines.append(
        str_label("Event timeline entries: %1")
            .arg(debug_telemetry_collector->event_timeline_size())
    );

    if (!debug_telemetry_collector->has_cache_snapshot()) {
        live_lines.append(
            str_label("No cache snapshot has been collected yet.")
        );
        resource_monitor_live_text->setPlainText(
            live_lines.join(QLatin1Char('\n'))
        );
        resource_monitor_timeline_text->setPlainText(
            str_label("No timeline rows yet.")
        );
        resource_monitor_diagnostics_text->setPlainText(
            str_label("No diagnostics data yet.")
        );
        return;
    }

    const resource_monitor::cache_timeline_entry latest
        = debug_telemetry_collector->latest_cache_snapshot();
    const raster_cache::debug_snapshot& snapshot = latest.cache_snapshot;

    const bool instrumented
        = debug_telemetry_collector->get_debug_cadence_mode()
        == resource_monitor::debug_cadence_mode::instrumented;

    live_lines.append(str_label("Cadence mode: %1")
                          .arg(
                              instrumented ? str_label("instrumented")
                                           : str_label("realistic")
                          ));
    live_lines.append(str_label("Latest collector sequence: %1")
                          .arg(latest.collector_sequence));
    live_lines.append(str_label("Latest cache snapshot sequence: %1")
                          .arg(snapshot.snapshot_sequence));
    live_lines.append(str_label("Ready entries/images: %1 / %2")
                          .arg(snapshot.ready_entries)
                          .arg(snapshot.ready_images));
    live_lines.append(str_label("Ready bytes: %1").arg(snapshot.ready_bytes));
    live_lines.append(str_label("Displayed now entries/images: %1 / %2")
                          .arg(snapshot.displayed_ready_entries)
                          .arg(snapshot.displayed_ready_images));
    live_lines.append(str_label("Cached-only entries/images: %1 / %2")
                          .arg(snapshot.cached_only_ready_entries)
                          .arg(snapshot.cached_only_ready_images));
    live_lines.append(str_label("Size buckets: %1  Largest entries tracked: %2")
                          .arg(snapshot.unique_size_buckets)
                          .arg(snapshot.largest_entries.size()));

    resource_monitor_live_text->setPlainText(
        live_lines.join(QLatin1Char('\n'))
    );

    QStringList timeline_lines;
    timeline_lines.append(str_label("Latest timeline rows (up to 12):"));
    const QVector<resource_monitor::cache_timeline_entry> cache_rows
        = debug_telemetry_collector->cache_timeline();
    const QVector<resource_monitor::event_timeline_entry> event_rows
        = debug_telemetry_collector->event_timeline();

    const int cache_start = std::max(0, int(cache_rows.size() - 12));
    for (int index = cache_start; index < cache_rows.size(); ++index) {
        const auto& row = cache_rows.at(index);
        timeline_lines.append(
            str_label("[cache] seq=%1 snap=%2 ready=%3 bytes=%4")
                .arg(row.collector_sequence)
                .arg(row.cache_snapshot.snapshot_sequence)
                .arg(row.cache_snapshot.ready_entries)
                .arg(row.cache_snapshot.ready_bytes)
        );
    }

    const int event_start = std::max(0, int(event_rows.size() - 12));
    for (int index = event_start; index < event_rows.size(); ++index) {
        const auto& row = event_rows.at(index);
        const QString kind = row.kind
                == resource_monitor::event_timeline_entry::event_kind::
                    manual_marker
            ? QStringLiteral("marker")
            : QStringLiteral("cache");
        timeline_lines.append(str_label("[event] seq=%1 kind=%2 t=%3 label=%4")
                                  .arg(row.collector_sequence)
                                  .arg(kind)
                                  .arg(row.timestamp_ms)
                                  .arg(row.label));
    }
    resource_monitor_timeline_text->setPlainText(
        timeline_lines.join(QLatin1Char('\n'))
    );

    QStringList diagnostics_lines;
    diagnostics_lines.append(str_label("Recency-window diagnostics"));
    diagnostics_lines.append(
        str_label("Window size: %1 ms").arg(snapshot.displayed_entry_window_ms)
    );
    diagnostics_lines.append(str_label("Displayed-now entries: %1")
                                 .arg(snapshot.displayed_ready_entries));
    diagnostics_lines.append(str_label("Cached-only entries: %1")
                                 .arg(snapshot.cached_only_ready_entries));
    const QString recency_quality = snapshot.ready_entries <= 0
        ? str_label("no-ready-entries")
        : (snapshot.displayed_entry_coverage_percent >= 80
               ? str_label("strong")
               : (snapshot.displayed_entry_coverage_percent >= 40
                      ? str_label("moderate")
                      : str_label("sparse")));
    diagnostics_lines.append(str_label("Displayed recency coverage: %1% (%2)")
                                 .arg(snapshot.displayed_entry_coverage_percent)
                                 .arg(recency_quality));
    diagnostics_lines.append(str_label("Stage timing summary"));
    diagnostics_lines.append(
        str_label("- raster_lifecycle: avg %1 ms, max %2 ms, samples=%3")
            .arg(snapshot.raster_timing_avg_ms)
            .arg(snapshot.raster_timing_max_ms)
            .arg(snapshot.raster_timing_samples)
    );
    diagnostics_lines.append(
        str_label("- coalesced_wait: avg %1 ms, max %2 ms, samples=%3")
            .arg(snapshot.coalesced_wait_avg_ms)
            .arg(snapshot.coalesced_wait_max_ms)
            .arg(snapshot.coalesced_wait_samples)
    );
    diagnostics_lines.append(str_label("Top expensive tasks: %1")
                                 .arg(snapshot.top_expensive_tasks.size()));
    for (const auto& expensive : snapshot.top_expensive_tasks) {
        const QString stage = expensive.stage
                == raster_cache::debug_snapshot::timing_stage::coalesced_wait
            ? QStringLiteral("coalesced_wait")
            : QStringLiteral("raster_lifecycle");
        diagnostics_lines.append(
            str_label("- %1 | %2ms avg / %3ms max | samples=%4")
                .arg(stage)
                .arg(expensive.avg_elapsed_ms)
                .arg(expensive.max_elapsed_ms)
                .arg(expensive.completed_samples)
        );
    }
    resource_monitor_diagnostics_text->setPlainText(
        diagnostics_lines.join(QLatin1Char('\n'))
    );
}

void main_window::dump_debug_telemetry_on_exit() const {
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    const QString base_dir
        = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base_dir.isEmpty()) {
        return;
    }

    QDir dir(base_dir);
    if (!dir.mkpath(QStringLiteral("."))) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd_hhmmss")
    );
    const QString output_path = dir.filePath(
        QStringLiteral("debug_snapshot_exit_%1.json").arg(timestamp)
    );

    QString error_message;
    const bool success = debug_telemetry_collector->export_debug_snapshot_sync(
        output_path, &error_message
    );
    if (!success) {
        qWarning() << "Unable to export debug snapshot on exit:"
                   << error_message;
    }
}

#endif

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
    reset_game_state(true, true);
}
