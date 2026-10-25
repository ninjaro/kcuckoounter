#include "table/table_slot.hpp"
#include <shared_ui/qt/editor_panel.hpp>

#include "arch/android_ui.hpp"
#include "arch/asset_locator.hpp"
#include "arch/icon_loader.hpp"
#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/card_widget.hpp"
#include "table/gameplay_setup.hpp"
#include "table/infinity_spinbox.hpp"
#include "table/settings_template.hpp"
#include "table/slot_settings.hpp"
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

static QVector<int> weights_for_strategy_slug(const QString& strategy_slug) {
    const strategy_catalog& repository = strategy_repository();
    if (!repository.is_valid()) {
        return {};
    }
    for (const strategy_data& strategy : repository.strategies) {
        if (strategy.slug == strategy_slug) {
            return strategy.weights;
        }
    }
    return {};
}

void table_slot::on_infinity_toggled(bool checked) {
    if (gameplay_owner)
        return;
    Q_UNUSED(checked);

    update_infinity_state(infinity_check_box, deck_count_spin_box);

    const bool is_infinity = is_infinity_enabled();
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_infinity(is_infinity);
    }
    update_lockable_settings();
}

void table_slot::populate_settings_editor(slot_settings* editor) {
    auto* infinite = editor->infinity_check_box();
    auto* decks = editor->deck_count_spin_box();
    auto* strategy = editor->strategy_combo_box();
    infinite->setChecked(infinity_check_box->isChecked());
    decks->setRange(
        deck_count_spin_box->minimum(), deck_count_spin_box->maximum()
    );
    decks->setSingleStep(deck_count_spin_box->singleStep());
    decks->setValue(deck_count_spin_box->value());
    strategy->clear();
    for (int i = 0; i < strategy_combo_box->count(); ++i) {
        strategy->addItem(
            strategy_combo_box->itemText(i), strategy_combo_box->itemData(i)
        );
        strategy->setItemData(
            i, strategy_combo_box->itemData(i, Qt::UserRole + 1),
            Qt::UserRole + 1
        );
    }
    strategy->setCurrentIndex(strategy_combo_box->currentIndex());
    strategy->setEnabled(strategy_combo_box->isEnabled());
    editor->show_card_indexing()->setChecked(show_card_indexing->isChecked());
    editor->show_strategy_name()->setChecked(show_strategy_name->isChecked());
    editor->training_check_box()->setChecked(training_check_box->isChecked());
    const bool locked = card_widget_internal->has_cards()
        && current_phase == slot_phase::paused;
    infinite->setEnabled(!(locked && infinite->isChecked()));
    editor->training_check_box()->setEnabled(
        !(locked && editor->training_check_box()->isChecked())
    );
    update_infinity_state(infinite, decks);
    connect(infinite, &BaseCheckBox::toggled, editor, [infinite, decks](bool) {
        update_infinity_state(infinite, decks);
    });
}

void table_slot::apply_settings_editor(const slot_settings* editor) {
    editor->deck_count_spin_box()->interpretText();
    infinity_check_box->setChecked(editor->infinity_check_box()->isChecked());
    deck_count_spin_box->setValue(editor->deck_count_spin_box()->value());
    strategy_combo_box->setCurrentIndex(
        editor->strategy_combo_box()->currentIndex()
    );
    show_card_indexing->setChecked(editor->show_card_indexing()->isChecked());
    show_strategy_name->setChecked(editor->show_strategy_name()->isChecked());
    training_check_box->setChecked(editor->training_check_box()->isChecked());
    sync_card_display_settings();
}

void table_slot::open_settings_editor() {
    if (gameplay_owner) {
        if (gameplay_owner->phase() != gameplay::session_phase::setup
            && gameplay_owner->phase() != gameplay::session_phase::paused)
            return;
    } else if (
        quiz_prompt_active
        || (current_phase == slot_phase::running
            && card_widget_internal->has_cards())
    ) {
        return;
    }
    if (controls_dialog)
        controls_dialog->close();
    // Only classic editors notify the classic shell's pause handler. Target
    // edits require setup/manual Pause already; neither path auto-resumes.
    if (!gameplay_owner)
        emit dialog_opened();
    auto* panel = new shared_ui::qt::editor_panel(this);
    settings_editor_panel = panel;
    panel->setObjectName(QStringLiteral("slot_settings_editor"));
    panel->setAccessibleName(str_label("Card settings"));
    panel->setWindowTitle(str_label("Card settings"));
    panel->installEventFilter(this);
    const auto notice = str_label("Card settings — changes apply with OK");
    const auto help = gameplay_owner
        ? str_label(
              "Cancel or Escape discards this draft. Gameplay settings are "
              "editable only before Start; Training Show Count is also "
              "editable during "
              "manual Pause. Phase changes, external edits or roster "
              "replacement "
              "discard it."
          )
        : str_label(
              "Cancel or Escape discards this draft. Resuming play, "
              "restarting, "
              "restoring a session or copying settings here also discards "
              "unapplied edits."
          );
    panel->set_heading(notice, help);
    const bool kit_card = theme_settings::felt_chrome(this)
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
        [](QDialog* window) {
            window->setObjectName(QStringLiteral("slot_settings_host"));
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
    } else {
        settings_editor_fields = new slot_settings(
            panel, true,
            kit_card ? slot_settings::layout::kit
                     : slot_settings::layout::classic
        );
        populate_settings_editor(settings_editor_fields);
        panel->set_content(settings_editor_fields);
        connect(
            settings_editor_fields->info_button(), &BasePushButton::clicked,
            this, [this] {
                if (settings_editor_fields)
                    show_template_dialog(
                        str_label("Strategy details"),
                        settings_editor_fields->strategy_combo_box()
                            ->currentText()
                    );
            }
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
    for (QWidget* field :
         { static_cast<QWidget*>(settings_editor_fields->infinity_check_box()),
           static_cast<QWidget*>(settings_editor_fields->deck_count_spin_box()),
           static_cast<QWidget*>(settings_editor_fields->strategy_combo_box()),
           static_cast<QWidget*>(
               settings_editor_fields->show_card_indexing()
           ) }) {
        if (field->isEnabled()) {
            field->setFocus(Qt::OtherFocusReason);
            break;
        }
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
    auto* fields = settings_editor_fields;
    auto* target_fields = gameplay_editor_fields;
    settings_editor_panel = nullptr;
    settings_editor_fields = nullptr;
    gameplay_editor_fields = nullptr;
    QPointer<table_slot> lifetime(this);
    panel->dismiss();
    if (!lifetime)
        return;
    if (apply && target_fields)
        (void)target_fields->apply_pending_changes();
    else if (apply)
        apply_settings_editor(fields);
    if (!lifetime)
        return; // accepted-change observers may replace the entire roster
    panel->deleteLater();
    if (gameplay_owner)
        update_compact_controls();
    else
        set_paused(current_phase == slot_phase::paused);
    if (compact_controls_button && compact_controls_button->isVisible()) {
        compact_controls_button->setFocus(Qt::OtherFocusReason);
    } else {
        (void)focus_details_action();
    }
}

void table_slot::on_settings_button_clicked() {
    if (settings_editor_panel) {
        finish_settings_editor(false);
        return;
    }
    if (gameplay_owner || settings_style != slot_settings_style::classic) {
        open_settings_editor();
        return;
    }
    if (use_dialog_for_settings) {
        if (infinity_check_box == nullptr || deck_count_spin_box == nullptr
            || strategy_combo_box == nullptr || show_card_indexing == nullptr
            || show_strategy_name == nullptr || training_check_box == nullptr) {
            return;
        }

        emit dialog_opened();

        QDialog dialog(this);
        dialog.setWindowTitle(str_label("Card details"));
        update_settings_button_state(true);

        auto dialog_layout = new QVBoxLayout(&dialog);
        auto dialog_settings_widget = new slot_settings(&dialog, false);
        dialog_layout->addWidget(dialog_settings_widget);

        populate_settings_editor(dialog_settings_widget);

        auto button_box = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog
        );
        QObject::connect(
            button_box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept
        );
        QObject::connect(
            button_box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject
        );
        dialog_layout->addWidget(button_box);

#if defined(Q_OS_ANDROID)
        dialog.setWindowState(Qt::WindowMaximized);
#endif
        if (dialog.exec() == QDialog::Accepted) {
            apply_settings_editor(dialog_settings_widget);
        }

        update_settings_button_state(false);
        return;
    }

    if (settings_bar_widget == nullptr) {
        return;
    }

    settings_overlay_visible = !settings_overlay_visible;
    settings_bar_widget->setVisible(settings_overlay_visible);
    update_settings_button_state();
}

void table_slot::on_info_button_clicked() {
    if (gameplay_owner)
        return;
    const QString strategy_name = strategy_combo_box != nullptr
        ? strategy_combo_box->currentText()
        : QString();
    show_template_dialog(str_label("Strategy details"), strategy_name);
}

void table_slot::on_show_card_indexing_toggled(bool checked) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_show_card_indexing(checked);
    }
}

void table_slot::on_show_strategy_name_toggled(bool checked) {
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_show_strategy_name(checked);
    }
}

void table_slot::on_training_check_box_toggled(bool checked) {
    if (gameplay_owner)
        return;
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_training_mode(checked);
    }
    update_lockable_settings();
}

void table_slot::on_strategy_name_changed(const QString& text) {
    if (gameplay_owner)
        return;
    if (card_widget_internal != nullptr) {
        card_widget_internal->set_strategy_name(text);
        update_strategy_weights();
    }

    if (strategy_combo_box == nullptr
        || strategy_combo_box->currentIndex() < 0) {
        return;
    }
    const int index = strategy_combo_box->currentIndex();
    trainer_preferences preferences = load_trainer_preferences();
    preferences.preferred_strategy_slug
        = strategy_combo_box->itemData(index).toString();
    save_trainer_preferences(preferences);
}

void table_slot::update_infinity_state(
    BaseCheckBox* check_box, BaseSpinBox* spin_box
) {
    if (check_box == nullptr || spin_box == nullptr) {
        return;
    }

    auto internal_spin_box = dynamic_cast<infinity_spinbox*>(spin_box);
    if (!internal_spin_box) {
        return;
    }

    bool checked = check_box->isChecked();
    internal_spin_box->set_infinity_mode(checked);
    spin_box->setEnabled(!checked);
}

bool table_slot::is_infinity_enabled() const {
    return infinity_check_box != nullptr && infinity_check_box->isChecked();
}

bool table_slot::is_training_enabled() const {
    return training_check_box != nullptr && training_check_box->isChecked();
}

void table_slot::apply_settings_from(const table_slot& source) {
    if (gameplay_owner || source.gameplay_owner)
        return;
    finish_settings_editor(false);
    if (infinity_check_box == nullptr || deck_count_spin_box == nullptr
        || strategy_combo_box == nullptr || show_card_indexing == nullptr
        || show_strategy_name == nullptr || training_check_box == nullptr) {
        return;
    }

    if (source.infinity_check_box != nullptr) {
        infinity_check_box->setChecked(source.infinity_check_box->isChecked());
    }

    if (source.deck_count_spin_box != nullptr) {
        deck_count_spin_box->setValue(source.deck_count_spin_box->value());
    }

    if (source.strategy_combo_box != nullptr) {
        const int source_index = source.strategy_combo_box->currentIndex();
        if (source_index >= 0 && source_index < strategy_combo_box->count()) {
            strategy_combo_box->setCurrentIndex(source_index);
        }
    }

    if (source.show_card_indexing != nullptr) {
        show_card_indexing->setChecked(source.show_card_indexing->isChecked());
    }

    if (source.show_strategy_name != nullptr) {
        show_strategy_name->setChecked(source.show_strategy_name->isChecked());
    }

    if (source.training_check_box != nullptr) {
        training_check_box->setChecked(source.training_check_box->isChecked());
    }

    update_infinity_state(infinity_check_box, deck_count_spin_box);
    sync_card_display_settings();
    update_lockable_settings();
}

void table_slot::sync_card_display_settings() {
    if (card_widget_internal == nullptr) {
        return;
    }

    if (show_card_indexing != nullptr) {
        card_widget_internal->set_show_card_indexing(
            show_card_indexing->isChecked()
        );
    }
    if (show_strategy_name != nullptr) {
        card_widget_internal->set_show_strategy_name(
            show_strategy_name->isChecked()
        );
    }
    if (gameplay_owner) {
        refresh_gameplay_deck();
        return;
    }
    if (training_check_box != nullptr) {
        card_widget_internal->set_training_mode(
            training_check_box->isChecked()
        );
    }
    if (strategy_combo_box != nullptr) {
        const QString strategy_name = strategy_combo_box->currentText();
        card_widget_internal->set_strategy_name(strategy_name);
        update_strategy_weights();
    }
}

void table_slot::show_template_dialog(
    const QString& title, const QString& strategy_name
) {
    emit dialog_opened();

    QDialog dialog(this);
    dialog.setWindowTitle(title);

    auto dialog_layout = new QVBoxLayout(&dialog);
    dialog_layout->addWidget(new settings_template_widget(
        settings_tab_kind::strategies, &dialog, strategy_name
    ));

    auto button_box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(
        button_box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject
    );
    dialog_layout->addWidget(button_box);

#if defined(Q_OS_ANDROID)
    dialog.setWindowState(Qt::WindowMaximized);
#endif
    dialog.exec();
}

void table_slot::update_strategy_weights() {
    if (card_widget_internal == nullptr || strategy_combo_box == nullptr) {
        return;
    }
    card_widget_internal->set_strategy_weights(
        weights_for_strategy_slug(strategy_combo_box->currentData().toString())
    );
}
