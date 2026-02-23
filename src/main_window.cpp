#include "main_window.hpp"

#include "widget/settings_template.hpp"
#include "widget/table.hpp"

#include "helpers/icon_loader.hpp"
#include "helpers/str_label.hpp"

#include <QAbstractButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QSlider>
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
    , quiz_started(false)
    , quiz_paused(false)
    , quiz_finished(false)
    , rasterization_busy(false)
    , pending_start_after_rasterization(false)
    , score_correct(0)
    , score_total(0) {
    setup_ui();
}

main_window::~main_window() = default;

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

    auto central_widget = new BaseWidget(this);
    auto main_layout = new BaseVBoxLayout;

    setup_dialog = new QDialog(this);
    setup_dialog->setWindowTitle(str_label("New game"));
    setup_dialog->setModal(true);

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
                if (!busy && pending_start_after_rasterization) {
                    pending_start_after_rasterization = false;
                    if (start_pause_action != nullptr) {
                        start_pause_action->setEnabled(true);
                    }
                    start_quiz_from_ui();
                }
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
            if (start_pause_action != nullptr && !quiz_started
                && !pending_start_after_rasterization) {
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
    pending_start_after_rasterization = false;
    score_correct = 0;
    score_total = 0;

    if (clock_timer != nullptr) {
        clock_timer->reset();
        if (clock_label != nullptr) {
            clock_label->setText(clock_timer->time_string_hh_mm_ss());
        }
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
    if (quiz_started) {
        on_finish_triggered();
        return;
    }
    if (setup_dialog != nullptr) {
        setup_dialog->show();
        setup_dialog->raise();
        setup_dialog->activateWindow();
    }
}

void main_window::on_start_pause_triggered() {
    if (table_widget == nullptr) {
        return;
    }

    if (!quiz_started) {
        if (pending_start_after_rasterization) {
            return;
        }
        table_widget->prepare_cards_for_start();
        if (table_widget->is_rasterization_busy()) {
            pending_start_after_rasterization = true;
            if (start_pause_action != nullptr) {
                start_pause_action->setEnabled(false);
            }
            update_status_text();
            return;
        }

        start_quiz_from_ui();
        return;
    }

    if (!quiz_paused) {
        table_widget->set_paused(true);
        quiz_paused = true;

        if (start_pause_action != nullptr) {
            start_pause_action->setText(str_label("Resume"));
            start_pause_action->setIcon(
                icon_loader::themed(
                    { "media-playback-start", "media-playback-play", "play" },
                    QStyle::SP_MediaPlay
                )
            );
        }

        if (clock_timer != nullptr) {
            clock_timer->pause();
            if (clock_label != nullptr) {
                clock_label->setText(clock_timer->time_string_hh_mm_ss());
            }
        }
        update_status_text();
    } else {
        table_widget->set_paused(false);
        quiz_paused = false;

        if (start_pause_action != nullptr) {
            start_pause_action->setText(str_label("Pause"));
            start_pause_action->setIcon(
                icon_loader::themed(
                    { "media-playback-pause", "media-playback-stop", "pause" },
                    QStyle::SP_MediaPause
                )
            );
        }

        if (clock_timer != nullptr) {
            clock_timer->start(true);
            if (clock_label != nullptr) {
                clock_label->setText(clock_timer->time_string_hh_mm_ss());
            }
        }
        update_status_text();
    }
}

void main_window::on_finish_triggered() {
    if (table_widget != nullptr && quiz_started && !quiz_paused) {
        table_widget->set_paused(true);
        quiz_paused = true;
        if (start_pause_action != nullptr) {
            start_pause_action->setText(str_label("Resume"));
            start_pause_action->setIcon(
                icon_loader::themed(
                    { "media-playback-start", "media-playback-play", "play" },
                    QStyle::SP_MediaPlay
                )
            );
        }
        if (clock_timer != nullptr) {
            clock_timer->pause();
            if (clock_label != nullptr) {
                clock_label->setText(clock_timer->time_string_hh_mm_ss());
            }
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
        if (quiz_paused) {
            start_pause_action->setText(str_label("Resume"));
            start_pause_action->setIcon(
                icon_loader::themed(
                    { "media-playback-start", "media-playback-play", "play" },
                    QStyle::SP_MediaPlay
                )
            );
        } else {
            start_pause_action->setText(str_label("Pause"));
            start_pause_action->setIcon(
                icon_loader::themed(
                    { "media-playback-pause", "media-playback-stop", "pause" },
                    QStyle::SP_MediaPause
                )
            );
        }
    }
    if (finish_action != nullptr) {
        finish_action->setEnabled(true);
    }

    if (clock_timer != nullptr) {
        clock_timer->reset();
        if (!quiz_paused) {
            clock_timer->start(true);
        }
        if (clock_label != nullptr) {
            clock_label->setText(clock_timer->time_string_hh_mm_ss());
        }
    }
    update_status_text();
}

void main_window::on_settings_triggered() {
    pause_for_dialog();

    QDialog dialog(this);
    dialog.setWindowTitle(str_label("Settings"));

    auto dialog_layout = new QVBoxLayout(&dialog);

    auto tab_widget = new QTabWidget(&dialog);
    auto shared_state = new settings_shared_state(&dialog);
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

    auto button_box = new QDialogButtonBox(&dialog);
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
        close_button, &QAbstractButton::clicked, &dialog,
        [&dialog, appearance_widget]() {
            if (appearance_widget != nullptr) {
                appearance_widget->apply_theme_settings();
            }
            dialog.accept();
        }
    );
    dialog_layout->addWidget(button_box);

    dialog.exec();
}

void main_window::update_status_text() {
    if (status_label == nullptr) {
        return;
    }

    QStringList status_entries;
    if (rasterization_busy || pending_start_after_rasterization) {
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

    if (!quiz_started && !quiz_finished) {
        status_label->setText(str_label("Status: %1").arg(status_value));
        return;
    }

    status_label->setText(str_label("Status: %1  Score: %2/%3  Time: %4")
                              .arg(status_value)
                              .arg(score_correct)
                              .arg(score_total)
                              .arg(time_label));
}

void main_window::pause_for_dialog() {
    if (table_widget == nullptr || !quiz_started || quiz_paused) {
        return;
    }

    table_widget->set_paused(true);
    quiz_paused = true;

    if (start_pause_action != nullptr) {
        start_pause_action->setText(str_label("Resume"));
        start_pause_action->setIcon(
            icon_loader::themed(
                { "media-playback-start", "media-playback-play", "play" },
                QStyle::SP_MediaPlay
            )
        );
    }

    if (clock_timer != nullptr) {
        clock_timer->pause();
        if (clock_label != nullptr) {
            clock_label->setText(clock_timer->time_string_hh_mm_ss());
        }
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
    pending_start_after_rasterization = false;
    score_correct = 0;
    score_total = 0;

    if (clock_timer != nullptr) {
        clock_timer->reset();
        if (clock_label != nullptr) {
            clock_label->setText(clock_timer->time_string_hh_mm_ss());
        }
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
