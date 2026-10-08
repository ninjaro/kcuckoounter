#include "shell/main_window.hpp"

#include "card_helpers/card_sheet.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"

#include "arch/android_ui.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/gameplay_drill.hpp"
#include "settings/gameplay_preferences.hpp"
#include "settings/preferences.hpp"
#include "settings/session_checkpoint.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "settings/training_progress.hpp"
#include "shell/gameplay_reservation.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPointer>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>

#include <algorithm>

namespace main_window_setup {

packing::orientation_constraint gameplay_orientation(const table& view) {
    switch (view.current_card_orientation()) {
    case card_orientation_mode::horizontal:
        return packing::orientation_constraint::horizontal_only;
    case card_orientation_mode::vertical:
        return packing::orientation_constraint::vertical_only;
    case card_orientation_mode::automatic:
        return packing::orientation_constraint::allow_rotation;
    }
    return packing::orientation_constraint::allow_rotation;
}

packing::extent gameplay_card_extent() {
    const auto [long_side, short_side] = card_sheet_ratio();
    return { static_cast<double>(long_side), static_cast<double>(short_side) };
}

} // namespace main_window_setup

using namespace main_window_setup;

void main_window::close_gameplay_setup_dialog() {
    delete gameplay_setup_dialog.data();
    gameplay_setup_dialog = nullptr;
    gameplay_setup_fields = nullptr;
    gameplay_slot_count = nullptr;
    gameplay_setup_layout = nullptr;
    gameplay_setup_error = nullptr;
    gameplay_geometry_timer = nullptr;
    gameplay_geometry_ready = false;
}

void main_window::rebuild_gameplay_setup_fields() {
    delete gameplay_setup_fields.data();
    gameplay_setup_fields = nullptr;
    auto* owner = table_widget->active_gameplay_session();
    if (!owner || !gameplay_setup_layout)
        return;
    gameplay_setup_fields
        = new gameplay::setup_widget(*owner, gameplay_setup_dialog);
    gameplay_setup_fields->setObjectName(
        QStringLiteral("gameplay_global_setup")
    );
    gameplay_setup_layout->addWidget(gameplay_setup_fields);
    connect(
        gameplay_setup_fields, &gameplay::setup_widget::configuration_changed,
        this, [this] {
            table_widget->refresh_gameplay_session();
            refresh_gameplay_setup();
        }
    );
    refresh_gameplay_setup();
}

void main_window::refresh_gameplay_setup() {
    auto* owner = table_widget->active_gameplay_session();
    if (!owner || !gameplay_setup_fields)
        return;
    if (owner->phase() == gameplay::session_phase::setup) {
        gameplay_geometry_ready = gameplay_setup_fields->update_table_geometry(
            { static_cast<double>(table_widget->width()),
              static_cast<double>(table_widget->height()) },
            gameplay_card_extent(), gameplay_orientation(*table_widget)
        );
    }
    gameplay_setup_fields->refresh();
    const QSignalBlocker blocker(gameplay_slot_count);
    gameplay_slot_count->setValue(static_cast<int>(owner->size()));
    gameplay_slot_count->setEnabled(
        owner->phase() == gameplay::session_phase::setup
    );
}

void main_window::open_gameplay_setup_dialog() {
    if (!table_widget->active_gameplay_session())
        return;
    if (!gameplay_setup_dialog) {
        auto* dialog = new QDialog(this);
        gameplay_setup_dialog = dialog;
        dialog->setObjectName(QStringLiteral("gameplay_setup_dialog"));
        dialog->setWindowTitle(str_label("New gameplay session"));
        dialog->setModal(false);
        auto* layout = new QVBoxLayout(dialog);
        auto* scroll = new QScrollArea(dialog);
        scroll->setWidgetResizable(true);
        auto* content = new QWidget(scroll);
        gameplay_setup_layout = new QVBoxLayout(content);
        auto* notice = new QLabel(
            str_label(
                "Start saves global defaults and reserves this session for "
                "recovery. Pause and normal close save its continuation; "
                "recovery always opens Paused. "
                "Saved drills store named configuration, not progress. "
                "Finish and natural endings save separate verified results. "
                "Legacy data is unchanged. "
                "Configure each deck using "
                "Details on "
                "the table, then Start. Closing this form keeps accepted "
                "settings."
            ),
            content
        );
        notice->setWordWrap(true);
        gameplay_setup_layout->addWidget(notice);
        auto* count_row = new QFormLayout;
        gameplay_slot_count = new BaseSpinBox(content);
        gameplay_slot_count->setObjectName(
            QStringLiteral("gameplay_slot_count")
        );
        gameplay_slot_count->setAccessibleName(
            str_label("Gameplay table slots")
        );
        gameplay_slot_count->setRange(
            1, static_cast<int>(gameplay::desktop_slot_technical_cap)
        );
        gameplay_slot_count->setKeyboardTracking(false);
        count_row->addRow(str_label("Table slots"), gameplay_slot_count);
        gameplay_setup_layout->addLayout(count_row);
        gameplay_setup_error = new QLabel(content);
        gameplay_setup_error->setObjectName(
            QStringLiteral("gameplay_setup_error")
        );
        gameplay_setup_error->setWordWrap(true);
        gameplay_setup_error->setTextFormat(Qt::PlainText);
        gameplay_setup_layout->addWidget(gameplay_setup_error);
        scroll->setWidget(content);
        layout->addWidget(scroll);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        auto* drills = buttons->addButton(
            str_label("Saved drills…"), QDialogButtonBox::ActionRole
        );
        drills->setObjectName(QStringLiteral("gameplay_saved_drills"));
        drills->setAutoDefault(false);
        connect(
            drills, &QPushButton::clicked, this,
            &main_window::on_saved_drills_triggered
        );
        auto* proceed = buttons->addButton(
            str_label("Continue"), QDialogButtonBox::AcceptRole
        );
        proceed->setObjectName(QStringLiteral("gameplay_setup_continue"));
        connect(proceed, &QPushButton::clicked, dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        layout->addWidget(buttons);
        connect(
            gameplay_slot_count, &QSpinBox::valueChanged, this,
            &main_window::on_gameplay_roster_changed
        );
        gameplay_geometry_timer = new QTimer(dialog);
        gameplay_geometry_timer->setSingleShot(true);
        gameplay_geometry_timer->setInterval(0);
        connect(
            gameplay_geometry_timer, &QTimer::timeout, this,
            &main_window::refresh_gameplay_setup
        );
        rebuild_gameplay_setup_fields();
        dialog->resize(480, 600);
    }
    refresh_gameplay_setup();
    gameplay_setup_dialog->show();
    gameplay_setup_dialog->raise();
    gameplay_setup_dialog->activateWindow();
}

void main_window::report_gameplay_error(const QString& message) {
    if (table_widget->active_gameplay_session()) {
        const QPointer<main_window> guard(this);
        open_gameplay_setup_dialog();
        if (guard && gameplay_setup_error)
            gameplay_setup_error->setText(message);
    } else if (status_label) {
        status_label->setText(message);
    }
}

void main_window::refresh_gameplay_actions() {
    const auto* owner = table_widget->active_gameplay_session();
    const bool retained = gameplay_binding && gameplay_binding->sequence()
        && !gameplay_binding->acknowledged();
    if (gameplay_retry_action)
        gameplay_retry_action->setEnabled(
            !gameplay_storage_busy && !gameplay_storage_error.isEmpty()
        );
    if (new_gameplay_action)
        new_gameplay_action->setEnabled(
            !gameplay_storage_busy && !retained
            && (!gameplay_recovery_checked || gameplay_recovery_ready)
            && (owner ? owner->phase() == gameplay::session_phase::setup
                        || owner->phase() == gameplay::session_phase::finished
                      : !quiz_started)
        );
    if (saved_drills_action)
        saved_drills_action->setEnabled(!gameplay_storage_busy);
    if (highscores_action)
        highscores_action->setEnabled(!owner);
    if (!owner)
        return;
    const auto phase = owner->phase();
    if (phase == gameplay::session_phase::setup) {
        start_pause_action->setText(str_label("Start"));
        start_pause_action->setIcon(
            icon_loader::themed(
                { "media-playback-start", "media-playback-play", "play" },
                QStyle::SP_MediaPlay
            )
        );
    } else {
        update_start_pause_action(phase == gameplay::session_phase::paused);
    }
    start_pause_action->setEnabled(
        !gameplay_storage_busy
        && (!gameplay_recovery_checked || gameplay_recovery_ready)
        && (phase != gameplay::session_phase::setup || !retained)
        && phase != gameplay::session_phase::finished
    );
    finish_action->setEnabled(
        !gameplay_storage_busy
        && (phase == gameplay::session_phase::running
            || phase == gameplay::session_phase::paused)
    );
    if (gameplay_setup_dialog && gameplay_setup_dialog->isVisible())
        refresh_gameplay_setup();
}

void main_window::on_new_gameplay_triggered() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    return;
#else
    const QPointer<main_window> guard(this);
    if (!gameplay_storage_ready() || !guard || !gameplay_retirement_allowed()
        || !guard)
        return;
    auto* current = table_widget->active_gameplay_session();
    if ((!current && quiz_started)
        || (current
            && (current->phase() == gameplay::session_phase::running
                || current->phase() == gameplay::session_phase::paused)))
        return; // Finish first; never implicitly replace live gameplay.
    if (!current || current->phase() == gameplay::session_phase::finished) {
        const auto recommendation = gameplay::recommend_slots(
            { static_cast<double>(table_widget->width()),
              static_cast<double>(table_widget->height()) },
            gameplay_card_extent(), gameplay_orientation(*table_widget)
        );
        if (!recommendation) {
            report_gameplay_error(str_label(
                "A usable table is required for a new gameplay session."
            ));
            return;
        }
        std::optional<gameplay::session> next;
        if (current) {
            std::vector<gameplay::deck_configuration> decks;
            for (std::size_t id = 0; id < current->size(); ++id)
                decks.push_back(current->deck({ id })->configuration);
            next = gameplay::session::create(
                current->configuration(), decks, recommendation->bounds
            );
            if (next) {
                (void)next->set_pick_interval_ms(current->pick_interval_ms());
                for (std::size_t id = 0; id < current->size(); ++id)
                    if (current->deck({ id })->show_count)
                        (void)next->set_show_count({ id }, true);
            }
        } else {
            QSettings storage(
                QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
            );
            gameplay_preferences_service defaults_service(
                storage, recommendation->bounds
            );
            QString error;
            const auto defaults = defaults_service.load(&error);
            if (!defaults) {
                report_gameplay_error(error);
                return;
            }
            const auto count = defaults->slot_count;
            auto bounds = recommendation->bounds;
            // Keep saved M/N even on a smaller window. Setup immediately
            // refreshes actual bounds and Start still requires explicit
            // consent.
            bounds.recommended = std::max(count, bounds.recommended);
            next = gameplay::create_fresh_session(
                strategy_repository(), defaults->configuration,
                std::vector<gameplay::deck_configuration>(
                    count, { "hi_lo", 1, false, false }
                ),
                bounds
            );
            if (next)
                (void)next->set_pick_interval_ms(defaults->pickup_interval_ms);
        }
        if (!next
            || !table_widget->install_gameplay_session(
                std::make_unique<gameplay::session>(std::move(*next))
            )) {
            report_gameplay_error(str_label(
                "Could not create the gameplay roster. The current table is "
                "unchanged."
            ));
            return;
        }
        if (gameplay_setup_error)
            gameplay_setup_error->clear();
    }
    if (setup_dialog)
        setup_dialog->hide();
    open_gameplay_setup_dialog();
#endif
}

bool main_window::load_gameplay_drill(const gameplay_drill& drill) {
    const QPointer<main_window> guard(this);
    if (!gameplay_storage_ready() || !guard || !gameplay_retirement_allowed()
        || !guard)
        return false;
    const auto* owner = table_widget->active_gameplay_session();
    if ((!owner && quiz_started)
        || (owner && owner->phase() != gameplay::session_phase::setup
            && owner->phase() != gameplay::session_phase::finished))
        return false;
    const auto recommendation = gameplay::recommend_slots(
        { static_cast<double>(table_widget->width()),
          static_cast<double>(table_widget->height()) },
        gameplay_card_extent(), gameplay_orientation(*table_widget)
    );
    if (!recommendation)
        return false;
    auto next = create_drill_session(
        drill, strategy_repository(), recommendation->bounds
    );
    if (!next
        || !table_widget->install_gameplay_session(
            std::make_unique<gameplay::session>(std::move(*next))
        ))
        return false;
    if (setup_dialog)
        setup_dialog->hide();
    open_gameplay_setup_dialog();
    if (gameplay_setup_error)
        gameplay_setup_error->clear();
    return true;
}

void main_window::on_gameplay_roster_changed(int count) {
    const QPointer<main_window> guard(this);
    if (!gameplay_retirement_allowed()) {
        if (guard)
            refresh_gameplay_setup();
        return;
    }
    auto* current = table_widget->active_gameplay_session();
    if (!current || current->phase() != gameplay::session_phase::setup
        || count < 1 || static_cast<std::size_t>(count) == current->size()) {
        refresh_gameplay_setup();
        return;
    }
    refresh_gameplay_setup();
    const auto old_n = current->configuration().sequential_count;
    auto next = gameplay::resize_setup_roster(
        *current, static_cast<std::size_t>(count), strategy_repository(),
        { "hi_lo", 1, false, false }, current->slot_constraints()
    );
    if (!next
        || !table_widget->install_gameplay_session(
            std::make_unique<gameplay::session>(std::move(*next))
        )) {
        report_gameplay_error(str_label(
            "Could not resize the roster. Existing deck choices are unchanged."
        ));
        refresh_gameplay_setup();
        return;
    }
    gameplay_setup_error->setText(
        old_n > static_cast<std::size_t>(count)
            ? str_label("Sequential N was reduced to the new table size.")
            : QString()
    );
}

bool main_window::start_gameplay_from_ui() {
    const QPointer<main_window> guard(this);
    if (!gameplay_storage_ready() || !guard || !gameplay_retirement_allowed()
        || !guard)
        return false;
    if (gameplay_slot_count)
        gameplay_slot_count->interpretText();
    if (!guard)
        return false;
    const auto scene = table_widget->gameplay_scene();
    auto* owner = table_widget->active_gameplay_session();
    if (!owner || owner->phase() != gameplay::session_phase::setup)
        return false;
    if (!gameplay_setup_fields)
        open_gameplay_setup_dialog();
    refresh_gameplay_setup();
    if (!gameplay_geometry_ready
        || !table_widget->has_usable_gameplay_layout()) {
        report_gameplay_error(
            str_label("A usable table is required before Start.")
        );
        return false;
    }
    if (table_widget->has_open_gameplay_settings()) {
        report_gameplay_error(
            str_label("Apply or close open deck Details before Start.")
        );
        return false;
    }
    if (owner->size() > owner->slot_constraints().recommended
        && !owner->configuration().allow_extra_slots) {
        report_gameplay_error(
            str_label("Confirm Allow more slots than recommended before Start.")
        );
        return false;
    }
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    gameplay_preferences_service defaults_service(
        storage, owner->slot_constraints()
    );
    QString error;
    if (!defaults_service.load(&error)) {
        report_gameplay_error(error);
        return false;
    }
    if (!gameplay::prepare_generated_session(
            *owner, strategy_repository(),
            QRandomGenerator::global()->generate64()
        )) {
        report_gameplay_error(str_label(
            "Preparation failed: check deck strategies and card counts against "
            "the generation budgets."
        ));
        return false;
    }
    if (!defaults_service.save(
            { owner->size(), owner->configuration(),
              owner->pick_interval_ms() },
            &error
        )) {
        report_gameplay_error(error);
        return false; // Do not start or overwrite unreadable/future defaults.
    }
    table_widget->refresh_gameplay_session();
    if (!guard || table_widget->gameplay_scene() != scene)
        return false;
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    const bool started = table_widget->start_gameplay_runtime(
#else
    const bool started = reserve_gameplay_from_ui(
#endif
        QRandomGenerator::global()->generate64()
    );
    if (!guard)
        return false;
    if (!started) {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
        report_gameplay_error(str_label(
            "The prepared session could not start. Check its setup and slot "
            "consent."
        ));
#endif
        return false;
    }
    refresh_gameplay_setup();
    if (gameplay_setup_error && gameplay_storage_error.isEmpty())
        gameplay_setup_error->clear();
    if (gameplay_setup_dialog && gameplay_storage_error.isEmpty())
        gameplay_setup_dialog->hide();
    return true;
}
