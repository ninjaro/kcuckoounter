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
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
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
#include <QTimer>
#include <QToolButton>

#include <algorithm>

namespace {

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
        gameplay::session::minimum_pick_interval_ms,
        gameplay::session::maximum_pick_interval_ms
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
    , primary_toolbar(nullptr)
    , game_menu(nullptr)
    , settings_menu(nullptr)
    , table_widget(nullptr)
    , settings_dialog(nullptr)
    , appearance_settings_widget(nullptr)
    , clock_label(nullptr)
    , status_label(nullptr)
    , pickup_interval_label(nullptr)
    , raster_progress(nullptr)
    , speed_slider(nullptr)
    , new_game_action(nullptr)
    , start_pause_action(nullptr)
    , finish_action(nullptr)
    , progress_action(nullptr)
    , settings_action(nullptr)
    , rasterization_busy(false) {
    setup_ui();
    connect(
        &current_card_sheet_metadata(), &card_sheet_metadata::geometry_changed,
        this, &main_window::refresh_gameplay_setup, Qt::QueuedConnection
    );
}

main_window::~main_window() {
    close_gameplay_review_dialog();
    close_gameplay_setup_dialog(); // borrowing view dies before table owner
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    persist_desktop_shell_state();
#endif
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
    new_game_action->setObjectName(QStringLiteral("game_new"));
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

    progress_action = new BaseAction(str_label("Progress"), this);
    register_shell_action(progress_action, QStringLiteral("game_progress"));
    progress_action->setToolTip(str_label("Show completed gameplay results"));
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
}

void main_window::setup_ui() {
    const table_appearance_preferences preferences
        = load_table_appearance_preferences();
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
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    restore_desktop_shell_state();
#endif
    if (speed_slider != nullptr) {
        speed_slider->setValue(300);
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
                speed_slider->setEnabled(false);
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
    }

    setWindowTitle(str_label("kcuckoounter"));

    if (speed_slider != nullptr) {
        QObject::connect(
            speed_slider, &QSlider::valueChanged, this,
            &main_window::on_speed_slider_value_changed
        );
    }

    if (pickup_interval_label != nullptr && speed_slider != nullptr) {
        set_pickup_readout(speed_slider->value());
    }
    refresh_clock_label();

    apply_desktop_presentation();

    QTimer::singleShot(0, this, [this] {
        const QPointer<main_window> guard(this);
        if (!gameplay_recovery_checked)
            (void)recover_gameplay_from_ui();
        if (guard && gameplay_recovery_ready
            && !table_widget->active_gameplay_session())
            on_new_game_triggered();
    });

#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QObject::connect(
        qApp, &QGuiApplication::applicationStateChanged, this,
        &main_window::apply_mobile_state
    );
#endif
}

void main_window::apply_desktop_presentation() {
    auto window_presentation = shared_ui::qt::presentation_for(*this);
    window_presentation.context.felt_surface
        = theme_palette_registry::option(
              load_table_appearance_preferences().palette
        )
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
    window_presentation.profile = shared_ui::qt::platform_profile::android;
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

bool main_window::eventFilter(QObject* watched, QEvent* event) {
    if (watched == table_widget && event->type() == QEvent::Resize
        && gameplay_geometry_timer)
        gameplay_geometry_timer->start();
    return BaseMainWindow::eventFilter(watched, event);
}

void main_window::on_table_rasterization_busy_changed(bool busy) {
    rasterization_busy = busy;
    if (raster_progress != nullptr) {
        raster_progress->setVisible(busy);
    }
    update_status_text();
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
    if (pickup_interval_label != nullptr) {
        set_pickup_readout(value);
    }
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

    status_label->setText(
        gameplay_storage_error.isEmpty()
            ? str_label("Choose New game to configure a session.")
            : gameplay_storage_error
    );
}
