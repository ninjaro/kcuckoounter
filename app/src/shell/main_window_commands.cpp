#include "shell/main_window.hpp"
#include <shared_ui/qt/toolbar_presentation.hpp>

#include "card_helpers/card_sheet.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"

#include "arch/str_label.hpp"
#include "settings/gameplay_progress.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"

#include <QAbstractButton>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QMessageBox>
#include <QPointer>
#include <QScrollArea>
#include <QSettings>
#include <QStringList>
#include <QTabWidget>
#include <shared_ui/qt/appearance_editor.hpp>

void main_window::on_start_pause_triggered() {
    if (table_widget == nullptr || mobile_background) {
        return;
    }

    const QPointer<main_window> guard(this);
    if (!gameplay_recovery_checked)
        (void)recover_gameplay_from_ui();
    if (!guard || gameplay_storage_busy || mobile_background)
        return;

    if (const auto* owner = table_widget->active_gameplay_session()) {
        switch (owner->phase()) {
        case gameplay::session_phase::setup:
            (void)start_gameplay_from_ui();
            break;
        case gameplay::session_phase::running:
            pause_for_dialog();
            break;
        case gameplay::session_phase::paused:
            if (gameplay_storage_ready())
                (void)table_widget->resume_gameplay_runtime();
            break;
        case gameplay::session_phase::finished:
            break;
        }
        return;
    }

    on_new_game_triggered();
}

void main_window::on_finish_triggered() {
    if (gameplay_storage_busy)
        return;
    const auto pause_revision = mobile_pause_revision;
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
        else if (
            was_running && !mobile_background
            && pause_revision == mobile_pause_revision
        )
            (void)table_widget->resume_gameplay_runtime();
        return;
    }
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
    auto* editor_scroll = new QScrollArea(tab_widget);
    editor_scroll->setWidgetResizable(true);
    editor_scroll->setFrameShape(QFrame::NoFrame);
    editor_scroll->setWidget(appearance_settings_widget->desktop_editor());
    tab_widget->addTab(editor_scroll, str_label("Appearance"));
    auto* appearance_scroll = new QScrollArea(tab_widget);
    appearance_scroll->setWidgetResizable(true);
    appearance_scroll->setFrameShape(QFrame::NoFrame);
    appearance_scroll->setWidget(appearance_settings_widget);
    tab_widget->addTab(appearance_scroll, str_label("Table and cards"));
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
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    const auto show_host_commands = [tab_widget, button_box, editor_scroll] {
        button_box->setVisible(tab_widget->currentWidget() != editor_scroll);
    };
    connect(
        tab_widget, &QTabWidget::currentChanged, button_box, show_host_commands
    );
    show_host_commands();
#endif

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

void main_window::on_progress_triggered() {
    const QPointer<main_window> guard(this);
    pause_for_dialog();
    if (!guard)
        return;
    QSettings storage(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    const auto cap = gameplay::slot_technical_cap;
    QString error;
    const auto progress
        = gameplay_progress_service(storage, { cap, cap }).load(&error);
    if (!error.isEmpty()) {
        report_gameplay_storage(error);
        return;
    }
    const auto value = progress.value_or(gameplay_progress {});
    QMessageBox dialog(
        QMessageBox::Information, str_label("Gameplay progress"),
        str_label(
            "Completed sessions: %1\nVerified cards: %2\nErrors: %3\nSkips: %4"
        )
            .arg(value.completed_sessions)
            .arg(value.challenge.verified_cards)
            .arg(value.challenge.errors)
            .arg(value.challenge.skips),
        QMessageBox::Close, this
    );
    dialog.setObjectName(QStringLiteral("gameplay_progress_dialog"));
    QStringList recent;
    for (const auto& record : value.recent_results) {
        recent.append(
            str_label("%1 — Verified: %2  Errors: %3  Skips: %4  Play: %5 ms")
                .arg(
                    QDateTime::fromMSecsSinceEpoch(record.completed_at_utc_ms)
                        .toString(Qt::ISODate)
                )
                .arg(record.result.challenge.verified_cards)
                .arg(record.result.challenge.errors)
                .arg(record.result.challenge.skips)
                .arg(record.result.timing.play_time_ms)
        );
    }
    dialog.setDetailedText(recent.join(QLatin1Char('\n')));
    dialog.exec();
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
        const QPointer<main_window> guard(this);
        if (table_widget->active_gameplay_session()->phase()
            == gameplay::session_phase::running)
            (void)table_widget->pause_gameplay_runtime();
        if (guard)
            (void)save_gameplay_from_ui();
        return;
    }
}
