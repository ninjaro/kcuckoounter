#include "shell/main_window.hpp"
#include <shared_ui/qt/presentation.hpp>

#include "card_helpers/card_sheet.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"

#include "arch/android_ui.hpp"
#include "arch/asset_locator.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/session_checkpoint.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/training_progress.hpp"
#include "shell/gameplay_reservation.hpp"

#include <shared_ui/qt/status_strip.hpp>
#include <shared_ui/qt/toolbar_presentation.hpp>

#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QToolButton>

#include <algorithm>

namespace {

#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
// D-NEW-1 segmented control: a view of an existing combo box, which stays
// the model (and the only object other code reads or persists).
QWidget* kit_segmented_field(QComboBox* combo, QWidget* parent) {
    auto* field = new QWidget(parent);
    auto* stack = new QVBoxLayout(field);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(0);
    combo->setParent(field);
    stack->addWidget(combo);
    auto* track = new QFrame(field);
    track->setObjectName(QStringLiteral("kit_segmented"));
    shared_ui::qt::set_presentation_role(*track, QStringLiteral("segmented"));
    auto* row = new QHBoxLayout(track);
    row->setContentsMargins(3, 3, 3, 3);
    row->setSpacing(2);
    auto* group = new QButtonGroup(track);
    group->setExclusive(true);
    for (int i = 0; i < combo->count(); ++i) {
        auto* option = new QPushButton(combo->itemText(i), track);
        option->setCheckable(true);
        option->setAutoDefault(false);
        option->setFocusPolicy(Qt::TabFocus);
        option->setChecked(i == combo->currentIndex());
        group->addButton(option, i);
        row->addWidget(option, 1);
    }
    QObject::connect(
        group, &QButtonGroup::idClicked, combo, &QComboBox::setCurrentIndex
    );
    QObject::connect(
        combo, &QComboBox::currentIndexChanged, group, [group](int index) {
            if (auto* option = group->button(index))
                option->setChecked(true);
        }
    );
    track->hide();
    stack->addWidget(track);
    return field;
}

void show_kit_segmented(QComboBox* combo, bool segmented) {
    auto* field = combo->parentWidget();
    auto* track = field->findChild<QFrame*>(
        QStringLiteral("kit_segmented"), Qt::FindDirectChildrenOnly
    );
    if (track == nullptr)
        return;
    combo->setVisible(!segmented);
    track->setVisible(segmented);
}
#endif

QString pickup_interval_text(int interval_ms) {
#ifdef KC_KDE
    return i18n("Pickup interval: %1 ms", interval_ms);
#else
    return str_label("Pickup interval: %1 ms").arg(interval_ms);
#endif
}

#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
struct lives_presentation {
    QString text;
    QList<shared_ui::qt::status_field> fields;
};

lives_presentation gameplay_lives_text(
    const gameplay::session& owner, desktop_hud_style style, int default_suit
) {
    if (owner.configuration().failure != gameplay::failure_policy::lives)
        return {};
    const int remaining = owner.remaining_lives();
    const int lost = owner.configuration().initial_lives - remaining;
    // Validated session limits this projection to 12 glyphs, even when a
    // large Multi batch records more errors than the initial life count.
    static const QStringList filled { QStringLiteral("♣"), QStringLiteral("♦"),
                                      QStringLiteral("♥"),
                                      QStringLiteral("♠") };
    static const QStringList outlined { QStringLiteral("♧"),
                                        QStringLiteral("♢"),
                                        QStringLiteral("♡"),
                                        QStringLiteral("♤") };
    const bool classic = style == desktop_hud_style::classic;
    const auto live_symbols
        = (classic ? QStringLiteral("♥") : filled.at(default_suit))
              .repeated(remaining);
    const auto lost_symbols
        = (classic ? QStringLiteral("♠") : outlined.at(default_suit))
              .repeated(lost);
#ifdef KC_KDE
    const auto text = i18n(
        "Remaining lives: %1 %2  Lost lives: %3 %4", remaining, live_symbols,
        lost, lost_symbols
    );
#else
    const auto text = str_label("Remaining lives: %1 %2  Lost lives: %3 %4")
                          .arg(remaining)
                          .arg(live_symbols)
                          .arg(lost)
                          .arg(lost_symbols);
#endif
    return { text,
             { { str_label("Remaining lives"),
                 QString::number(remaining) + u' ' + live_symbols,
                 {},
                 {} },
               { str_label("Lost lives"),
                 QString::number(lost) + u' ' + lost_symbols,
                 {},
                 {} } } };
}
#endif

} // namespace

BaseWidget* main_window::create_desktop_status_surface() {
    desktop_status = new shared_ui::qt::status_strip(this);
    desktop_status->setObjectName(QStringLiteral("desktop_status_surface"));
    status_label = desktop_status->status_label();
    pickup_interval_label = desktop_status->readout_label();
    speed_slider = desktop_status->control_slider();
    raster_progress = desktop_status->progress_bar();
    clock_label = desktop_status->trailing_label();
    raster_progress->setRange(0, 0);
    raster_progress->setAccessibleName(str_label("Preparing card images"));
    speed_slider->setRange(
        trainer_preferences::minimum_pickup_interval_ms,
        trainer_preferences::maximum_pickup_interval_ms
    );
    speed_slider->setValue(300);
    speed_slider->setTickInterval(100);
#ifdef KC_KDE
    speed_slider->setMaximumWidth(160);
#else
    speed_slider->setMaximumWidth(180);
#endif
    speed_slider->setAccessibleName(str_label("Card pickup interval (ms)"));
    speed_slider->setToolTip(str_label("Card pickup interval (ms)"));
    clock_label->setText(str_label("00:00:00"));
    clock_label->setObjectName(QStringLiteral("session_clock"));
#ifndef KC_KDE
    clock_label->hide();
#endif
    return desktop_status;
}

main_window::main_window(BaseWidget* parent)
    : BaseMainWindow(parent)
    , table_slots_count(nullptr)
    , quiz_type(nullptr)
    , wait_for_answers(nullptr)
    , allow_skipping(nullptr)
    , dealing_mode(nullptr)
    , continue_button(nullptr)
    , primary_toolbar(nullptr)
    , game_menu(nullptr)
    , settings_menu(nullptr)
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
    , progress_action(nullptr)
    , settings_action(nullptr)
    , quiz_started(false)
    , quiz_paused(false)
    , quiz_finished(false)
    , rasterization_busy(false)
    , score_correct(0)
    , score_total(0)
    , mobile_checkpoint_restored(false)
    , mobile_lifecycle_paused(false)
    , last_mobile_checkpoint_elapsed_ms(0) {
    setup_ui();
    connect(
        &current_card_sheet_metadata(), &card_sheet_metadata::geometry_changed,
        this, &main_window::refresh_gameplay_setup, Qt::QueuedConnection
    );
}

main_window::~main_window() {
    close_gameplay_review_dialog();
    close_gameplay_setup_dialog(); // borrowing view dies before table owner
    if (quiz_started) {
        persist_mobile_session_checkpoint(
            !quiz_paused || mobile_lifecycle_paused
        );
    }
    persist_setup_preferences();
    persist_desktop_shell_state();
    // Base-widget teardown hides the still-live table. Demand withdrawal can
    // emit a busy change then, after this derived receiver has been destroyed.
    if (table_widget)
        QObject::disconnect(table_widget, nullptr, this, nullptr);
}

void main_window::update_start_pause_action(bool paused) const {
    if (start_pause_action == nullptr) {
        return;
    }

    shared_ui::qt::set_toolbar_icon(
        *start_pause_action,
        paused ? shared_ui::qt::toolbar_icon::play
               : shared_ui::qt::toolbar_icon::pause
    );
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
    new_game_action->setShortcut(QKeySequence::New);
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
    shared_ui::qt::set_toolbar_icon(
        *start_pause_action, shared_ui::qt::toolbar_icon::play
    );
    register_shell_action(
        start_pause_action, QStringLiteral("game_start_pause")
    );
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
    settings_action->setShortcut(QKeySequence::Preferences);
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

#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    new_gameplay_action
        = new BaseAction(str_label("New gameplay session…"), this);
    new_gameplay_action->setObjectName(QStringLiteral("game_new_gameplay"));
    new_gameplay_action->setToolTip(str_label(
        "New gameplay model with identified recovery. Finish and save the "
        "current game first."
    ));
    register_shell_action(
        new_gameplay_action, QStringLiteral("game_new_gameplay")
    );
    connect(
        new_gameplay_action, &QAction::triggered, this,
        &main_window::on_new_gameplay_triggered
    );

    gameplay_review_action
        = new BaseAction(str_label("Review corrections…"), this);
    gameplay_review_action->setObjectName(
        QStringLiteral("game_review_corrections")
    );
    gameplay_review_action->setEnabled(false);
    gameplay_review_action->setToolTip(str_label(
        "Review retained per-deck corrections without pausing or answering"
    ));
    register_shell_action(
        gameplay_review_action, QStringLiteral("game_review_corrections")
    );
    connect(
        gameplay_review_action, &QAction::triggered, this,
        &main_window::on_gameplay_review_triggered
    );

    saved_drills_action = new BaseAction(str_label("Saved drills…"), this);
    saved_drills_action->setObjectName(QStringLiteral("game_saved_drills"));
    saved_drills_action->setShortcut(
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N)
    );
    register_shell_action(
        saved_drills_action, QStringLiteral("game_saved_drills")
    );
    connect(
        saved_drills_action, &QAction::triggered, this,
        &main_window::on_saved_drills_triggered
    );
#endif

    gameplay_retry_action
        = new BaseAction(str_label("Retry gameplay storage"), this);
    gameplay_retry_action->setObjectName(QStringLiteral("game_retry_storage"));
    gameplay_retry_action->setEnabled(false);
    register_shell_action(
        gameplay_retry_action, QStringLiteral("game_retry_storage")
    );
    connect(gameplay_retry_action, &QAction::triggered, this, [this] {
        retry_gameplay_from_ui();
    });
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    gameplay_retry_action->setVisible(false);
#endif

#if defined(Q_OS_ANDROID)
    progress_action = new BaseAction(str_label("Progress"), this);
    register_shell_action(progress_action, QStringLiteral("game_progress"));
    progress_action->setToolTip(
        str_label("Show completed games and training accuracy")
    );
    progress_action->setIcon(
        icon_loader::themed(
            { "view-statistics", "office-chart-line", "games-highscores" },
            QStyle::SP_FileDialogInfoView
        )
    );
    QObject::connect(
        progress_action, &BaseAction::triggered, this,
        &main_window::on_progress_triggered
    );
#endif
}

void main_window::setup_ui() {
    const trainer_preferences preferences = load_trainer_preferences();
    auto presentation = shared_ui::qt::presentation_for(*this);
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    presentation.profile = shared_ui::qt::platform_profile::android;
#elif defined(KC_KDE)
    presentation.profile = shared_ui::qt::platform_profile::desktop_kde;
#endif
    presentation.context.felt_surface
        = theme_palette_registry::option(preferences.palette).base_color();
    shared_ui::qt::apply_presentation(*this, presentation);

    setup_platform_shell();

    auto central_widget = new BaseWidget(this);
    auto main_layout = new BaseVBoxLayout;

    setup_dialog = new QDialog(this);
    setup_dialog->setWindowTitle(str_label("New game"));
    setup_dialog->setModal(false);
    setup_dialog->setWindowModality(Qt::NonModal);

    setup_widget = new BaseWidget(setup_dialog);
    auto setup_layout = new BaseVBoxLayout;
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    setup_dialog->setObjectName(QStringLiteral("setup_dialog"));
    // D-NEW-1 card heading; the native title bar keeps the window title.
    setup_heading = new QLabel(str_label("New game"), setup_widget);
    setup_heading->setObjectName(QStringLiteral("kit_card_heading"));
    shared_ui::qt::set_presentation_role(
        *setup_heading, QStringLiteral("heading")
    );
    setup_heading->hide();
    setup_layout->addWidget(setup_heading);
#endif

    auto form_layout = new BaseFormLayout;

    table_slots_count = new BaseSpinBox(setup_widget);
    table_slots_count->setMinimum(1);
    table_slots_count->setMaximum(16);
    table_slots_count->setValue(preferences.slot_count);
    android_ui::apply_spin_box_style(table_slots_count);

    quiz_type = new BaseComboBox(setup_widget);
    quiz_type->addItems(
        QStringList() << str_label("Single question")
                      << str_label("Multi question")
    );
    quiz_type->setCurrentIndex(preferences.quiz_type);
    android_ui::apply_combo_box_style(quiz_type);

    wait_for_answers
        = new BaseCheckBox(str_label("Pause for answers"), setup_widget);
    wait_for_answers->setToolTip(
        str_label("Pause the game while waiting for quiz answers")
    );
    wait_for_answers->setChecked(preferences.wait_for_answers);
    android_ui::apply_check_box_style(wait_for_answers);

    allow_skipping
        = new BaseCheckBox(str_label("Allow skipping questions"), setup_widget);
    allow_skipping->setChecked(preferences.allow_skipping);
    allow_skipping->setToolTip(
        str_label("Enable the skip button during quizzes")
    );
    android_ui::apply_check_box_style(allow_skipping);

    dealing_mode = new BaseComboBox(setup_widget);
    dealing_mode->addItems(
        QStringList() << str_label("Sequential") << str_label("Random")
                      << str_label("Simultaneous")
    );
    dealing_mode->setCurrentIndex(preferences.dealing_mode);
    android_ui::apply_combo_box_style(dealing_mode);

    form_layout->addRow(str_label("Table slots"), table_slots_count);
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    // D-NEW-1: the slot count drawn as this window's actual packed table.
    slot_preview = new QLabel(setup_widget);
    slot_preview->setObjectName(QStringLiteral("setup_slot_preview"));
    slot_preview->setAlignment(Qt::AlignCenter);
    slot_preview->setAccessibleName(str_label("Table slot preview"));
    form_layout->addRow(slot_preview);
    connect(
        table_slots_count, &BaseSpinBox::valueChanged, this,
        &main_window::refresh_slot_preview
    );
#endif
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    form_layout->addRow(
        str_label("Quiz mode"), kit_segmented_field(quiz_type, setup_widget)
    );
#else
    form_layout->addRow(str_label("Quiz mode"), quiz_type);
#endif
    form_layout->addRow(wait_for_answers);
    form_layout->addRow(allow_skipping);
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    form_layout->addRow(
        str_label("Dealing mode"),
        kit_segmented_field(dealing_mode, setup_widget)
    );
#else
    form_layout->addRow(str_label("Dealing mode"), dealing_mode);
#endif

    continue_button = android_ui::create_button(setup_widget);
    continue_button->setText(str_label("Continue"));
    continue_button->setProperty("kit_primary", true);
    shared_ui::qt::set_presentation_role(
        *continue_button, QStringLiteral("primary")
    );
    android_ui::apply_button_style(
        continue_button, android_button_profile::primary
    );

    setup_layout->addLayout(form_layout);
    setup_layout->addWidget(continue_button);
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    auto* gameplay_button
        = new QPushButton(str_label("New gameplay session…"), setup_widget);
    gameplay_button->setObjectName(QStringLiteral("setup_new_gameplay"));
    connect(
        gameplay_button, &QPushButton::clicked, this,
        &main_window::on_new_gameplay_triggered
    );
    setup_layout->addWidget(gameplay_button);
    auto* drills_button
        = new QPushButton(str_label("Saved drills…"), setup_widget);
    drills_button->setObjectName(QStringLiteral("setup_saved_drills"));
    connect(
        drills_button, &QPushButton::clicked, this,
        &main_window::on_saved_drills_triggered
    );
    setup_layout->addWidget(drills_button);
#endif
    setup_layout->addStretch();
    setup_widget->setLayout(setup_layout);

    auto dialog_layout = new BaseVBoxLayout;
    dialog_layout->addWidget(setup_widget);
    setup_dialog->setLayout(dialog_layout);

    table_widget = new table(central_widget);
    table_widget->set_card_orientation(preferences.card_orientation);

    setup_game_actions();

    main_layout->addWidget(table_widget, 1);

    central_widget->setLayout(main_layout);
    setCentralWidget(central_widget);

    finalize_platform_shell();
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    // Stable host identity after KDE has finalized the native actions.
    if (primary_toolbar)
        if (auto* button = primary_toolbar->widgetForAction(start_pause_action))
            button->setObjectName(QStringLiteral("game_start_pause_button"));
#endif
    setup_status_surface(main_layout);
    status_label->setObjectName(QStringLiteral("session_status"));
    pickup_interval_label->setObjectName(
        QStringLiteral("pickup_interval_readout")
    );
    shared_ui::qt::set_presentation_role(
        *pickup_interval_label, QStringLiteral("muted-value")
    );
    speed_slider->setObjectName(QStringLiteral("pickup_interval_slider"));
    raster_progress->setObjectName(QStringLiteral("raster_progress"));
    restore_desktop_shell_state();
    if (speed_slider != nullptr) {
        speed_slider->setValue(preferences.pickup_interval_ms);
    }

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
        connect(table_widget, &table::gameplay_runtime_updated, this, [this] {
            if (!gameplay_storage_busy && gameplay_binding
                && gameplay_binding->completion()
                && !gameplay_completion_attempted)
                (void)complete_gameplay_from_ui();
        });
        connect(table_widget, &table::gameplay_session_changed, this, [this] {
            if (gameplay_storage_busy || !gameplay_binding)
                return;
            if (gameplay_binding->completion()) {
                if (!gameplay_completion_attempted)
                    (void)complete_gameplay_from_ui();
            } else if (
                gameplay_binding->sequence()
                && !gameplay_binding->acknowledged()
                && !gameplay_binding->owns(*table_widget)
            ) {
                report_gameplay_storage(str_label(
                    "The identified scene was retired. Its checkpoint is "
                    "preserved for recovery."
                ));
            }
        });
        const auto refresh_target_hud = [this] {
            if (const auto* owner = table_widget->active_gameplay_session()) {
                if (speed_slider) {
                    const QSignalBlocker blocker(speed_slider);
                    speed_slider->setValue(owner->pick_interval_ms());
                    speed_slider->setEnabled(
                        owner->phase() != gameplay::session_phase::finished
                    );
                }
                if (pickup_interval_label)
                    set_pickup_readout(owner->pick_interval_ms());
            } else if (speed_slider) {
                const QSignalBlocker blocker(speed_slider);
                speed_slider->setValue(table_widget->pick_interval());
                speed_slider->setEnabled(true);
                if (pickup_interval_label)
                    set_pickup_readout(speed_slider->value());
            }
            refresh_clock_label();
            update_status_text();
            refresh_gameplay_actions();
        };
        connect(
            table_widget, &table::gameplay_runtime_updated, this,
            refresh_target_hud
        );
        connect(
            table_widget, &table::gameplay_session_changed, this,
            refresh_target_hud
        );
        table_widget->installEventFilter(this);
        connect(
            table_widget, &table::gameplay_session_about_to_change, this,
            [this] {
                close_gameplay_review_dialog();
                delete gameplay_setup_fields.data();
                gameplay_setup_fields = nullptr;
            }
        );
        connect(table_widget, &table::gameplay_session_changed, this, [this] {
            refresh_gameplay_review();
            if (!table_widget->active_gameplay_session())
                close_gameplay_setup_dialog();
            else if (gameplay_setup_dialog)
                rebuild_gameplay_setup_fields();
        });
        connect(table_widget, &table::gameplay_layout_changed, this, [this] {
            refresh_gameplay_review();
            if (gameplay_geometry_timer)
                gameplay_geometry_timer->start();
        });
        connect(
            table_widget, &table::gameplay_preparation_required, this,
            &main_window::refresh_gameplay_setup
        );
        connect(
            table_widget, &table::gameplay_runtime_failed, this,
            &main_window::report_gameplay_error
        );
        connect(
            table_widget, &table::gameplay_corrections_changed, this,
            &main_window::refresh_gameplay_review
        );
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
        QObject::connect(
            speed_slider, &QSlider::sliderReleased, this,
            &main_window::persist_setup_preferences
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

    if (wait_for_answers != nullptr) {
        QObject::connect(
            wait_for_answers, &BaseCheckBox::toggled, this,
            [this](bool) { persist_setup_preferences(); }
        );
    }
    if (allow_skipping != nullptr) {
        QObject::connect(
            allow_skipping, &BaseCheckBox::toggled, this,
            [this](bool) { persist_setup_preferences(); }
        );
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
        set_pickup_readout(speed_slider->value());
    }
    refresh_clock_label();

    apply_desktop_presentation();

    if (setup_dialog != nullptr) {
        time_interface::single_shot(0, this, [this]() {
            const QPointer<main_window> guard(this);
            if (!gameplay_recovery_checked)
                (void)recover_gameplay_from_ui();
            if (!guard)
                return;
            if (!mobile_checkpoint_restored) {
                open_setup_dialog();
            }
        });
        QObject::connect(
            setup_dialog, &QDialog::rejected, this,
            &main_window::on_setup_dialog_rejected
        );
    }

#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QObject::connect(
        qApp, &QGuiApplication::applicationStateChanged, this,
        &main_window::on_application_state_changed
    );
#endif
}

void main_window::apply_desktop_presentation() {
    auto window_presentation = shared_ui::qt::presentation_for(*this);
    window_presentation.context.felt_surface
        = theme_palette_registry::option(load_trainer_preferences().palette)
              .base_color();
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    const auto value = load_desktop_ui_preferences();
    gameplay_hud_style = value.hud();
    gameplay_default_suit = load_default_suit_preference();
    // Material first: slots read it while restyling their overlays below.
    felt_chrome = value.chrome() == desktop_chrome_style::felt;
    window_presentation.value = felt_chrome ? shared_ui::qt::material::felt
                                            : shared_ui::qt::material::system;
    shared_ui::qt::apply_presentation(*this, window_presentation);
    if (table_widget != nullptr) {
        shared_ui::qt::apply_presentation(*table_widget, window_presentation);
        table_widget->set_frame_style(value.frame());
        table_widget->set_action_style(value.actions());
        table_widget->set_settings_style(value.settings_surface());
        table_widget->set_quiz_presentation(value.answer(), value.feedback());
        table_widget->refresh_material();
    }
    if (pickup_interval_label != nullptr) {
        pickup_interval_label->setVisible(value.show_speed_readout());
    }
    if (desktop_status != nullptr) {
        const bool instruments = value.hud() == desktop_hud_style::instruments;
        auto presentation
            = shared_ui::qt::presentation_for(*desktop_status->parentWidget());
        if (instruments)
            presentation.value = shared_ui::qt::material::felt;
        shared_ui::qt::apply_presentation(
            *desktop_status, presentation,
            instruments ? shared_ui::qt::presentation_surface::instruments
                        : shared_ui::qt::presentation_surface::window
        );
        desktop_status->set_variant(
            instruments ? shared_ui::qt::status_strip_variant::instruments
                        : shared_ui::qt::status_strip_variant::classic
        );
#ifndef KC_KDE
        clock_label->setVisible(instruments);
#endif
    }
    apply_kit_one_bar();
    refresh_slot_preview();
    if (primary_toolbar != nullptr) {
        shared_ui::qt::set_toolbar_presentation(
            *primary_toolbar,
            value.toolbar() == desktop_toolbar_style::compact
                ? shared_ui::qt::toolbar_variant::compact
                : shared_ui::qt::toolbar_variant::labelled,
            start_pause_action
        );
    }
    update_status_text();
#else
    shared_ui::qt::apply_presentation(*this, window_presentation);
    if (table_widget != nullptr)
        shared_ui::qt::apply_presentation(*table_widget, window_presentation);
#endif
}

void main_window::apply_kit_one_bar() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    // Table and HUD run edge to edge under the felt chrome (D-TOP-1/D-HUD-1);
    // Classic keeps the platform layout margins it started with.
    if (auto* layout = centralWidget() ? centralWidget()->layout() : nullptr) {
        if (!classic_central_spacing)
            classic_central_spacing.emplace(
                layout->contentsMargins(), layout->spacing()
            );
        layout->setContentsMargins(
            felt_chrome ? QMargins() : classic_central_spacing->first
        );
        layout->setSpacing(felt_chrome ? 0 : classic_central_spacing->second);
    }
    if (primary_toolbar == nullptr)
        return;
    // Brand first in the toolbar: a native macOS menu bar has no corner.
    // It exists only under the felt chrome so the native toolbar keeps
    // exactly its own commands.
    if (!felt_chrome && kit_brand_action != nullptr) {
        // The toolbar's widget action owns the brand widget.
        primary_toolbar->removeAction(kit_brand_action);
        kit_brand_action->deleteLater();
        kit_brand_action = nullptr;
        kit_wordmark = nullptr;
    }
    if (felt_chrome && kit_brand_action == nullptr) {
        auto* brand = new QWidget(primary_toolbar);
        brand->setObjectName(QStringLiteral("kit_brand"));
        auto* row = new QHBoxLayout(brand);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        auto* icon = new QLabel(brand);
        icon->setObjectName(QStringLiteral("kit_wordmark_icon"));
        shared_ui::qt::set_presentation_role(
            *icon, QStringLiteral("brand-icon")
        );
        const QString cuckoo
            = bundled_asset_path(QStringLiteral("brand/cuckoo.svg"));
        if (!cuckoo.isEmpty())
            icon->setPixmap(QIcon(cuckoo).pixmap(QSize(20, 20)));
        kit_wordmark = new QLabel(str_label("KCUCKOOUNTER"), brand);
        kit_wordmark->setObjectName(QStringLiteral("kit_wordmark"));
        shared_ui::qt::set_presentation_role(
            *kit_wordmark, QStringLiteral("wordmark")
        );
        kit_wordmark->setAccessibleName(str_label("kcuckoounter"));
        row->addWidget(icon);
        row->addWidget(kit_wordmark);
        const auto actions = primary_toolbar->actions();
        kit_brand_action = actions.isEmpty()
            ? primary_toolbar->addWidget(brand)
            : primary_toolbar->insertWidget(actions.constFirst(), brand);
    }
#ifndef KC_KDE
    // One bar where the menus are drawn inside the window: the same menus
    // move to the toolbar's end and keep their actions and shortcuts. KDE's
    // menu bar and a native (global) menu bar stay as they are.
    const bool merge_menus
        = felt_chrome && menuBar() != nullptr && !menuBar()->isNativeMenuBar();
    if (merge_menus && kit_menu_actions.isEmpty()) {
        auto* spacer = new QWidget(primary_toolbar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        kit_menu_actions.append(primary_toolbar->addWidget(spacer));
        for (QMenu* menu : { game_menu, settings_menu }) {
            if (menu == nullptr)
                continue;
            primary_toolbar->addAction(menu->menuAction());
            kit_menu_actions.append(menu->menuAction());
            if (auto* button = qobject_cast<QToolButton*>(
                    primary_toolbar->widgetForAction(menu->menuAction())
                )) {
                button->setObjectName(QStringLiteral("kit_menu_button"));
                shared_ui::qt::set_presentation_role(
                    *button, QStringLiteral("quiet-command")
                );
                button->setPopupMode(QToolButton::InstantPopup);
            }
        }
    } else if (!merge_menus && !kit_menu_actions.isEmpty()) {
        for (QAction* action : std::as_const(kit_menu_actions)) {
            primary_toolbar->removeAction(action);
            // Menu actions belong to their menus; only the spacer is ours.
            if (action->menu() == nullptr)
                action->deleteLater();
        }
        kit_menu_actions.clear();
    }
    if (menuBar() != nullptr && !menuBar()->isNativeMenuBar())
        menuBar()->setVisible(!merge_menus);
#endif
#endif
}

void main_window::refresh_slot_preview() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    if (slot_preview == nullptr || table_widget == nullptr)
        return;
    if (auto* form = setup_widget->findChild<QFormLayout*>()) {
        form->setRowVisible(slot_preview, felt_chrome);
        // Paper card: labels above full-width fields, segmented choices.
        form->setRowWrapPolicy(
            felt_chrome ? QFormLayout::WrapAllRows : QFormLayout::DontWrapRows
        );
        form->setVerticalSpacing(felt_chrome ? 10 : -1);
    }
    if (setup_heading != nullptr)
        setup_heading->setVisible(felt_chrome);
    for (QComboBox* combo : { static_cast<QComboBox*>(quiz_type),
                              static_cast<QComboBox*>(dealing_mode) })
        if (combo != nullptr)
            show_kit_segmented(combo, felt_chrome);
    if (setup_dialog != nullptr) {
        shared_ui::qt::apply_presentation(
            *setup_dialog, shared_ui::qt::presentation_for(*this),
            shared_ui::qt::presentation_surface::paper
        );
        // The kit's card is ~350px wide with 20px of paper around the form.
        setup_dialog->setMinimumWidth(felt_chrome ? 352 : 0);
        if (setup_dialog->layout() != nullptr) {
            if (!classic_setup_margins)
                classic_setup_margins
                    = setup_dialog->layout()->contentsMargins();
            setup_dialog->layout()->setContentsMargins(
                felt_chrome ? QMargins(12, 12, 12, 12) : *classic_setup_margins
            );
        }
    }
    if (!felt_chrome)
        return;
    const QSizeF area = table_widget->size().isEmpty()
        ? QSizeF(16.0, 10.0)
        : QSizeF(table_widget->size());
    const qreal scale = std::min(240.0 / area.width(), 120.0 / area.height());
    const QSizeF box = area * scale;
    const qreal ratio = slot_preview->devicePixelRatioF();
    QPixmap pixmap((box * ratio).toSize());
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // On the paper card: a pale tray with the packed slots in table green.
    painter.setPen(Qt::NoPen);
    painter.setBrush(
        shared_ui::qt::material_for(*this, shared_ui::qt::material::felt)
            .card_recess
    );
    painter.drawRoundedRect(QRectF(QPointF(), box), 6.0, 6.0);
    painter.setBrush(
        shared_ui::qt::presentation_for(*this).context.felt_surface
    );
    const auto packed
        = table_widget->preview_slot_layout(table_slots_count->value());
    for (const QRectF& slot : packed) {
        const QRectF scaled(
            slot.x() * scale, slot.y() * scale, slot.width() * scale,
            slot.height() * scale
        );
        painter.drawRoundedRect(
            scaled.adjusted(2.5, 2.5, -2.5, -2.5), 3.0, 3.0
        );
    }
    painter.end();
    slot_preview->setPixmap(pixmap);
#ifdef KC_KDE
    slot_preview->setToolTip(i18n(
        "%1 equal slots packed for the current window",
        static_cast<int>(packed.size())
    ));
#else
    slot_preview->setToolTip(
        str_label("%1 equal slots packed for the current window")
            .arg(packed.size())
    );
#endif
#endif
}

bool main_window::eventFilter(QObject* watched, QEvent* event) {
    if (watched == table_widget && event->type() == QEvent::Resize
        && gameplay_geometry_timer)
        gameplay_geometry_timer->start();
    return BaseMainWindow::eventFilter(watched, event);
}

void main_window::on_clock_ticked(qint64 elapsed_ms, qint64 delta_ms) {
    if (table_widget && table_widget->active_gameplay_session())
        return;
    Q_UNUSED(elapsed_ms);
    Q_UNUSED(delta_ms);

    refresh_clock_label();
    update_status_text();

#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    if (quiz_started && clock_timer != nullptr
        && clock_timer->elapsed_time_ms() - last_mobile_checkpoint_elapsed_ms
            >= 10000) {
        persist_mobile_session_checkpoint(!quiz_paused);
        last_mobile_checkpoint_elapsed_ms = clock_timer->elapsed_time_ms();
    }
#endif
}

void main_window::on_table_rasterization_busy_changed(bool busy) {
    rasterization_busy = busy;
    if (raster_progress != nullptr) {
        raster_progress->setVisible(busy);
    }
    update_status_text();
}

void main_window::on_table_score_adjusted(int correct_delta, int total_delta) {
    if (table_widget && table_widget->active_gameplay_session())
        return;
    score_correct = std::max(0, score_correct + correct_delta);
    score_total = std::max(0, score_total + total_delta);
    update_status_text();
    if (quiz_started) {
        persist_mobile_session_checkpoint(!quiz_paused);
    }
}

void main_window::on_speed_slider_value_changed(int value) {
    if (table_widget && table_widget->active_gameplay_session()) {
        const QPointer<main_window> guard(this);
        auto* owner = table_widget->active_gameplay_session();
        if (owner->phase() == gameplay::session_phase::setup) {
            (void)owner->set_pick_interval_ms(value);
            table_widget->refresh_gameplay_session();
        } else {
            (void)table_widget->set_gameplay_pick_interval(value);
        }
        if (!guard)
            return;
        owner = table_widget->active_gameplay_session();
        if (!owner) {
            update_status_text();
            return;
        }
        // Rejection and setup-only edits still project the accepted value.
        if (speed_slider) {
            const QSignalBlocker blocker(speed_slider);
            speed_slider->setValue(owner->pick_interval_ms());
        }
        if (pickup_interval_label)
            set_pickup_readout(owner->pick_interval_ms());
        update_status_text();
        return;
    }
    if (table_widget != nullptr) {
        table_widget->set_pick_interval(value);
    }
    if (pickup_interval_label != nullptr) {
        set_pickup_readout(value);
    }
}

void main_window::on_dealing_mode_changed(int index) {
    if (table_widget && table_widget->active_gameplay_session())
        return;
    if (table_widget != nullptr) {
        table_widget->set_dealing_mode(index);
    }
    persist_setup_preferences();
}

void main_window::on_quiz_type_changed(int index) {
    if (table_widget && table_widget->active_gameplay_session())
        return;
    if (wait_for_answers == nullptr) {
        return;
    }

    const bool is_multi_question = index == 1;
    if (is_multi_question) {
        wait_for_answers->setChecked(true);
    }
    wait_for_answers->setEnabled(!is_multi_question);
    persist_setup_preferences();
}

void main_window::on_table_slot_count_changed(int value) {
    if (table_widget && table_widget->active_gameplay_session())
        return;
    persist_setup_preferences();
    if (table_widget == nullptr) {
        return;
    }

    table_widget->set_slot_count(value);
    table_widget->schedule_card_preload();
}

void main_window::set_pickup_readout(int interval_ms) const {
    if (!pickup_interval_label)
        return;
    const auto text = pickup_interval_text(interval_ms);
    if (desktop_status) {
#ifdef KC_KDE
        const auto value = i18n("%1 ms", interval_ms);
#else
        const auto value = str_label("%1 ms").arg(interval_ms);
#endif
        desktop_status->set_readout(
            text, { { str_label("Pickup"), value, {}, {} } }
        );
    } else {
        pickup_interval_label->setText(text);
    }
}

void main_window::update_status_text() {
    if (status_label == nullptr) {
        return;
    }
    const auto present = [this](
                             const QString& text,
                             const QList<shared_ui::qt::status_field>& fields
                         ) {
        if (desktop_status)
            desktop_status->set_status(text, fields);
        else
            status_label->setText(text);
    };
    if (!gameplay_storage_error.isEmpty()) {
        present(gameplay_storage_error, {});
        return;
    }
    if (table_widget && table_widget->active_gameplay_session()) {
        auto text = table_widget->gameplay_status_text();
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
        const auto lives = gameplay_lives_text(
            *table_widget->active_gameplay_session(), gameplay_hud_style,
            gameplay_default_suit
        );
        if (!lives.text.isEmpty())
            text += QStringLiteral("  ") + lives.text;
#endif
        if (rasterization_busy)
            text.prepend(str_label("Processing") + QStringLiteral(" / "));
        const auto& owner = *table_widget->active_gameplay_session();
        QString phase;
        switch (owner.phase()) {
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
        if (rasterization_busy)
            phase.prepend(str_label("Processing") + str_label(" / "));
        const auto number = [](auto value) {
            return QString::number(static_cast<qulonglong>(value));
        };
        const auto& stats = owner.statistics();
        QList<shared_ui::qt::status_field> fields {
            { str_label("Status"),
              phase,
              {},
              shared_ui::qt::status_kind::live },
            { str_label("Verified"), number(stats.verified_cards), {}, {} },
            { str_label("Errors"), number(stats.errors), {}, {} },
            { str_label("Skips"), number(stats.skips), {}, {} },
            { str_label("Questions"),
              number(owner.unresolved_quizzes().size()),
              {},
              {} }
        };
#ifdef KC_KDE
        fields.append(
            { str_label("Time"), table_widget->gameplay_clock_text(), {}, {} }
        );
#endif
        if (owner.configuration().failure == gameplay::failure_policy::lives)
            fields.append(
                { str_label("Lives"), number(owner.remaining_lives()), {}, {} }
            );
        const auto& batch = owner.current_quiz_batch();
        if (batch && batch->remaining_ms
            && owner.phase() != gameplay::session_phase::finished) {
            const auto seconds = *batch->remaining_ms / 1000
                + (*batch->remaining_ms % 1000 != 0);
#ifdef KC_KDE
            const auto remaining = i18n("%1 s", seconds);
#else
            const auto remaining = str_label("%1 s").arg(seconds);
#endif
            fields.append({ str_label("Quiz remaining"), remaining, {}, {} });
        }
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
        fields.append(lives.fields);
#endif
        present(text, fields);
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
    if (mobile_checkpoint_restored) {
        status_entries.append(str_label("Recovered safely"));
    } else if (mobile_lifecycle_paused) {
        status_entries.append(str_label("Paused in background"));
    }
    const QString status_value = status_entries.join(str_label(" / "));

    QString time_label = str_label("00:00");
    if (clock_timer != nullptr) {
        time_label = clock_timer->time_string_mm_ss();
    }

    QList<shared_ui::qt::status_field> fields {
        { str_label("Status"),
          status_value,
          {},
          shared_ui::qt::status_kind::live }
    };
    if (!quiz_started && !quiz_finished) {
#ifdef KC_KDE
        present(i18n("Status: %1", status_value), fields);
#else
        present(str_label("Status: %1").arg(status_value), fields);
#endif
        return;
    }
    fields.append(
        { str_label("Score"),
          QString::number(score_correct),
          QStringLiteral("/") + QString::number(score_total),
          {} }
    );
#ifdef KC_KDE
    fields.append({ str_label("Time"), time_label, {}, {} });
#endif

#ifdef KC_KDE
    present(
        i18n(
            "Status: %1  Score: %2/%3  Time: %4", status_value, score_correct,
            score_total, time_label
        ),
        fields
    );
#else
    present(
        str_label("Status: %1  Score: %2/%3  Time: %4")
            .arg(status_value)
            .arg(score_correct)
            .arg(score_total)
            .arg(time_label),
        fields
    );
#endif
}
