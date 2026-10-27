#include "table/table_slot.hpp"
#include <shared_ui/qt/adaptive_surface.hpp>
#include <shared_ui/qt/editor_panel.hpp>
#include <shared_ui/qt/presentation.hpp>

#include "arch/android_ui.hpp"
#include "arch/asset_locator.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "table/card_widget.hpp"
#include "table/gameplay_setup.hpp"
#include "table/settings_template.hpp"
#include <QToolButton>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QShortcut>
#include <QSignalBlocker>
#include <QString>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>

void table_slot::open_settings_editor() {
    if (!gameplay_owner
        || (gameplay_owner->phase() != gameplay::session_phase::setup
            && gameplay_owner->phase() != gameplay::session_phase::paused))
        return;
    if (controls_surface && controls_surface->window())
        controls_surface->window()->close();
    auto* panel = new shared_ui::qt::editor_panel(this);
    settings_editor_panel = panel;
    panel->setObjectName(QStringLiteral("slot_settings_editor"));
    panel->setAccessibleName(str_label("Card settings"));
    panel->setWindowTitle(str_label("Card settings"));
    panel->installEventFilter(this);
    const auto notice = str_label("Card settings — changes apply with OK");
    const auto help = str_label(
        "Cancel or Escape discards this draft. Gameplay settings are editable "
        "before Start; Training Show Count is also editable during manual "
        "Pause. Phase changes, external edits or roster replacement discard it."
    );
    panel->set_heading(notice, help);
    const bool kit_card = shared_ui::qt::presentation_for(*this).value
            == shared_ui::qt::material::felt
        && settings_style == slot_settings_style::card;
    if (kit_card && card_widget_internal->displayed_slot_number() > 0) {
        panel->setAccessibleDescription(notice);
        panel->set_heading(
            str_label("SLOT %1").arg(
                card_widget_internal->displayed_slot_number(), 2, 10,
                QLatin1Char('0')
            ),
            help
        );
        panel->set_header_style(shared_ui::qt::editor_header::closable);
        // Stable host identifiers are retained for tests and accessibility
        // tools.
        panel->heading_label()->setObjectName(
            QStringLiteral("kit_settings_heading")
        );
        panel->header_widget()->setObjectName(
            QStringLiteral("kit_settings_header")
        );
        panel->close_button()->setObjectName(
            QStringLiteral("kit_settings_close")
        );
        panel->close_button()->setAccessibleName(str_label("Cancel"));
        panel->close_button()->setToolTip(str_label("Discard these changes"));
    }
    connect(
        panel, &shared_ui::qt::editor_panel::accept_requested, this,
        [this] { finish_settings_editor(true); }
    );
    connect(
        panel, &shared_ui::qt::editor_panel::cancel_requested, this,
        [this] { finish_settings_editor(false); }
    );
    connect(
        panel, &shared_ui::qt::editor_panel::detached, this,
        [panel](QDialog* window) {
            window->setObjectName(QStringLiteral("slot_settings_host"));
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
            // Measure the actual form before deciding to detach. A scroll
            // area's tiny minimum must not disguise an unusable inline form.
            panel->set_scrollable(true);
#else
            Q_UNUSED(panel);
#endif
        }
    );
    if (gameplay_owner) {
        gameplay_editor_fields = new gameplay::deck_setup_widget(
            *gameplay_owner, gameplay_id, strategy_repository(), panel,
            gameplay::deck_setup_widget::edit_mode::staged
        );
        panel->set_content(gameplay_editor_fields);
        connect(
            gameplay_editor_fields,
            &gameplay::deck_setup_widget::configuration_changed, this,
            &table_slot::gameplay_configuration_changed
        );
        connect(
            gameplay_editor_fields,
            &gameplay::deck_setup_widget::show_count_changed, this,
            &table_slot::gameplay_show_count_changed
        );
    }
    auto* buttons = panel->buttons();
    update_overlay_palette();
    update_settings_button_state();
    update_compact_controls();
    place_settings_editor();
    if (gameplay_editor_fields) {
        for (const auto* name :
             { "gameplay_deck_strategy", "gameplay_deck_count",
               "gameplay_deck_infinite", "gameplay_deck_training",
               "gameplay_deck_show_count" }) {
            auto* field = gameplay_editor_fields->findChild<QWidget*>(
                QString::fromLatin1(name)
            );
            if (field && field->isEnabled() && field->isVisible()) {
                field->setFocus(Qt::OtherFocusReason);
                return;
            }
        }
        buttons->button(QDialogButtonBox::Cancel)
            ->setFocus(Qt::OtherFocusReason);
        return;
    }
}

void table_slot::place_settings_editor() {
    if (!settings_editor_panel)
        return;
    // Card/legend clearance and the chosen overlay edge remain host policy.
    const int clear
        = std::max(8, card_widget_internal->bottom_legend_clearance());
    const QRect available = rect().adjusted(8, clear, -8, -clear);
    const Qt::Alignment horizontal
        = settings_style == slot_settings_style::drawer ? Qt::AlignRight
                                                        : Qt::AlignHCenter;
    const Qt::Alignment vertical = settings_style == slot_settings_style::sill
        ? Qt::AlignBottom
        : Qt::AlignVCenter;
    settings_editor_panel->place_in(
        available, horizontal | vertical | Qt::AlignAbsolute
    );
}

void table_slot::finish_settings_editor(bool apply) {
    if (!settings_editor_panel)
        return;
    auto* panel = settings_editor_panel.data();
    auto* target_fields = gameplay_editor_fields;
    settings_editor_panel = nullptr;
    gameplay_editor_fields = nullptr;
    QPointer<table_slot> lifetime(this);
    panel->dismiss();
    if (!lifetime)
        return;
    if (apply && target_fields)
        (void)target_fields->apply_pending_changes();
    if (!lifetime)
        return; // accepted-change observers may replace the entire roster
    panel->deleteLater();
    update_compact_controls();
    if (compact_controls_button && compact_controls_button->isVisible()) {
        compact_controls_button->setFocus(Qt::OtherFocusReason);
    } else {
        (void)focus_details_action();
    }
}

void table_slot::on_settings_button_clicked() {
    if (settings_editor_panel)
        finish_settings_editor(false);
    else
        open_settings_editor();
}
