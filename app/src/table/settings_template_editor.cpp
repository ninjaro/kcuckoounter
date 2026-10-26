#include "table/settings_template.hpp"

#include "arch/str_label.hpp"
#include "settings/theme_palette.hpp"
#include "table/card_widget.hpp"
#include "table/settings_template_support.hpp"
#include <shared_ui/qt/action_group.hpp>
#include <shared_ui/qt/answer_panel.hpp>
#include <shared_ui/qt/appearance_editor.hpp>
#include <shared_ui/qt/editor_panel.hpp>
#include <shared_ui/qt/presentation.hpp>
#include <shared_ui/qt/settings_section.hpp>
#include <shared_ui/qt/status_strip.hpp>
#include <shared_ui/qt/toolbar_presentation.hpp>

#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
using namespace shared_ui::qt;

QHash<QString, QString> preset_choices(desktop_ui_preset id) {
    desktop_ui_preferences value;
    value.preset = id;
    // Resolve defaults through the existing host contract and ID adapter.
    value.frame_override = value.frame();
    value.speed_readout_override = value.show_speed_readout();
    value.answer_override = value.answer();
    value.feedback_override = value.feedback();
    value.actions_override = value.actions();
    value.settings_override = value.settings_surface();
    value.toolbar_override = value.toolbar();
    value.hud_override = value.hud();
    value.chrome_override = value.chrome();
    return value.appearance().overrides;
}

#endif
} // namespace

shared_ui::qt::appearance_editor*
settings_template_widget::desktop_editor() const {
    return appearance_editor_;
}

shared_ui::qt::appearance_model*
settings_template_widget::desktop_model() const {
    return appearance_model_;
}

void settings_template_widget::setup_appearance_editor() {
#if !defined(KC_ANDROID) && !defined(Q_OS_ANDROID)
    using namespace shared_ui::qt;
    auto* model = new appearance_model;
    auto* editor = new appearance_editor(model, this);
    model->setParent(editor);
    appearance_model_ = model;
    appearance_editor_ = editor;
    editor->setObjectName(QStringLiteral("desktop_appearance_editor"));
    editor->hide(); // The settings dialog places this beside Table and cards.
    model->set_families(
        { { "frame",
            str_label("Slot frame"),
            { { "classic", str_label("Classic"), {} },
              { "thin", str_label("Thin outline"), {} },
              { "piping", str_label("Gold piping"), {} } } },
          { "speed_readout",
            str_label("Pickup interval text"),
            { { "shown", str_label("Shown"), {} },
              { "hidden", str_label("Hidden"), {} } } },
          { "answer_entry",
            str_label("Answer entry"),
            { { "numeric", str_label("Numeric"), {} },
              { "chips", str_label("Chip stepper"), {} } } },
          { "feedback",
            str_label("Answer feedback"),
            { { "classic", str_label("Classic text"), {} },
              { "stamp", str_label("Count stamp"), {} } } },
          { "slot_actions",
            str_label("Slot actions"),
            { { "classic", str_label("Classic"), {} },
              { "rail", str_label("Icon rail"), {} },
              { "pills", str_label("Labelled pills"), {} } } },
          { "settings_surface",
            str_label("Slot settings"),
            { { "classic", str_label("Classic"), {} },
              { "card", str_label("Card"), {} },
              { "drawer", str_label("Drawer"), {} },
              { "sill", str_label("Sill"), {} } } },
          { "toolbar",
            str_label("Main toolbar"),
            { { "classic", str_label("Classic"), {} },
              { "compact", str_label("Compact"), {} } } },
          { "hud",
            str_label("Session HUD"),
            { { "classic", str_label("Classic"), {} },
              { "instruments", str_label("Instrument strip"), {} } } },
          // Keep the existing host ID "native"; this selects window chrome,
          // not the library's generic material catalogue (whose ID is
          // "system").
          { "chrome",
            str_label("Window chrome"),
            { { "native", str_label("Native"), {} },
              { "felt", str_label("Felt"), {} } } } }
    );
    model->set_presets(
        { { "classic",
            str_label("Classic"),
            {},
            preset_choices(desktop_ui_preset::classic) },
          { "quiet",
            str_label("Quiet"),
            {},
            preset_choices(desktop_ui_preset::quiet) },
          { "felt",
            str_label("Felt"),
            {},
            preset_choices(desktop_ui_preset::felt) } }
    );
    auto families = model->families();
    for (auto& family : families)
        for (auto& option : family.options)
            option.support.profiles = { platform_profile::desktop_qt,
                                        platform_profile::desktop_kde };
    model->set_families(families);
    model->load(load_desktop_ui_preferences().appearance());
    editor->set_identity(
        { str_label("Kcuckoounter"), str_label("Appearance"),
          str_label(
              "Presentation only. Table color, artwork, suit and orientation "
              "are in Table and cards. Game rules are unchanged."
          ) }
    );
    editor->set_preview_caption(str_label("Presentation sample; no live game"));
    const QPointer<settings_template_widget> owner(this);
    editor->set_preview_factory(
        [owner](const QString& family, const QString& option) -> QWidget* {
            if (!owner || !owner->desktop_model())
                return nullptr;
            auto value = presentation_for(*owner);
            value.profile = owner->desktop_model()->profile();
            const auto choices = desktop_ui_preferences::from_appearance(
                owner->desktop_model()->state()
            );
            value.value = choices.chrome() == desktop_chrome_style::felt
                ? material::felt
                : material::system;
            value.context.felt_surface
                = settings_template_support::theme_color_from_label(
                    owner->theme_combo_box->currentText()
                );
            auto* root = new QWidget;
            apply_presentation(*root, value);
            auto* layout = new QVBoxLayout(root);
            if (family == "frame") {
                auto* card = new card_widget(root);
                card->set_frame_style(
                    option == "piping"     ? slot_frame_style::piping
                        : option == "thin" ? slot_frame_style::thin
                                           : slot_frame_style::classic
                );
                card->setMinimumSize(160, 120);
                layout->addWidget(card);
            } else if (family == "answer_entry" || family == "feedback") {
                auto* answer = new answer_panel(root);
                auto* input = new QLineEdit(QStringLiteral("-42"));
                input->setAccessibleName(str_label("Sample count"));
                answer->set_widgets(
                    { new QLabel(str_label("Count")),
                      input,
                      { new QPushButton(str_label("Check")),
                        new QPushButton(str_label("Skip")) },
                      new QLabel(str_label("Answer")),
                      new QLabel(str_label("Sample feedback")),
                      new QPushButton(str_label("Continue")) }
                );
                QList<answer_step> steps;
                for (int direction : { -1, 1 }) {
                    auto* step = new QToolButton;
                    step->setText(
                        direction < 0 ? QStringLiteral("−1")
                                      : QStringLiteral("+1")
                    );
                    step->setAccessibleName(
                        direction < 0 ? str_label("Decrease sample")
                                      : str_label("Increase sample")
                    );
                    steps.append({ step, direction, false });
                }
                answer->set_steps(steps);
                answer->set_variants(
                    choices.answer() == quiz_answer_style::chips
                        ? answer_variant::chips
                        : answer_variant::numeric,
                    choices.feedback() == quiz_feedback_style::stamp
                        ? answer_feedback_variant::stamp
                        : answer_feedback_variant::classic
                );
                answer->set_feedback_fields(
                    { { str_label("Your count"), QStringLiteral("−42") },
                      { str_label("Expected"), QStringLiteral("−42") } }
                );
                answer->set_available_area(QSize(480, 320));
                answer->show_feedback(family == "feedback");
                layout->addWidget(answer);
            } else if (family == "settings_surface") {
                auto* panel = new editor_panel(root);
                panel->set_heading(str_label("Sample settings"));
                panel->set_header_style(
                    option == "card" ? editor_header::closable
                                     : editor_header::plain
                );
                auto* fields = new settings_section;
                fields->add_row(
                    str_label("Name"), new QLineEdit(str_label("Preview"))
                );
                fields->add_full_width_row(
                    new QCheckBox(str_label("Sample option"))
                );
                panel->set_content(fields);
                root->setMinimumSize(420, 280);
                layout->addWidget(
                    panel, 0,
                    option == "drawer"     ? Qt::AlignRight
                        : option == "sill" ? Qt::AlignBottom
                                           : Qt::AlignCenter
                );
            } else if (family == "toolbar") {
                auto* bar = new QToolBar(root);
                auto* start = bar->addAction(
                    root->style()->standardIcon(QStyle::SP_MediaPlay),
                    str_label("Start")
                );
                bar->addAction(
                    root->style()->standardIcon(
                        QStyle::SP_FileDialogDetailedView
                    ),
                    str_label("Settings")
                );
                set_toolbar_icon(*start, toolbar_icon::play);
                set_toolbar_presentation(
                    *bar,
                    option == "compact" ? toolbar_variant::compact
                                        : toolbar_variant::labelled,
                    start
                );
                layout->addWidget(bar);
            } else if (family == "slot_actions") {
                auto* actions = new action_group(root);
                actions->set_presentation(
                    value.value, value.profile, value.context
                );
                actions->set_variant(
                    option == "pills" ? action_variant::pills
                        : option == "rail" || option == "compact"
                        ? action_variant::compact
                        : action_variant::labelled
                );
                actions->set_orientation(
                    option == "rail" ? Qt::Vertical : Qt::Horizontal
                );
                actions->set_icons_visible(option != "pills");
                actions->set_actions(
                    { new QAction(
                          root->style()->standardIcon(
                              QStyle::SP_FileDialogDetailedView
                          ),
                          str_label("Details"), root
                      ),
                      new QAction(
                          root->style()->standardIcon(QStyle::SP_ArrowUp),
                          str_label("Swap"), root
                      ),
                      new QAction(
                          root->style()->standardIcon(QStyle::SP_FileIcon),
                          str_label("Copy"), root
                      ) }
                );
                layout->addWidget(actions);
            } else {
                auto* strip = new status_strip(root);
                if (choices.hud() == desktop_hud_style::instruments
                    || (family == "hud" && option == "instruments")) {
                    auto instruments = value;
                    instruments.value = material::felt;
                    apply_presentation(
                        *strip, instruments, presentation_surface::instruments
                    );
                    strip->set_variant(status_strip_variant::instruments);
                }
                strip->set_status(
                    str_label("Ready"),
                    { { str_label("Status"), str_label("Ready"), {} } }
                );
                strip->set_readout(
                    str_label("Pickup 300 ms"),
                    { { str_label("Pickup"), QStringLiteral("300 ms"), {} } }
                );
                strip->readout_label()->setVisible(
                    family == "speed_readout" ? option != "hidden"
                                              : choices.show_speed_readout()
                );
                strip->control_slider()->setRange(
                    trainer_preferences::minimum_pickup_interval_ms,
                    trainer_preferences::maximum_pickup_interval_ms
                );
                strip->control_slider()->setValue(300);
                layout->addWidget(strip);
            }
            return root;
        }
    );
    const auto refresh = [owner, editor, model] {
        if (!owner)
            return;
        auto value = presentation_for(*owner);
#ifdef KC_KDE
        value.profile = platform_profile::desktop_kde;
#endif
        const auto choices
            = desktop_ui_preferences::from_appearance(model->state());
        value.value = choices.chrome() == desktop_chrome_style::felt
            ? material::felt
            : material::system;
        value.context.felt_surface
            = settings_template_support::theme_color_from_label(
                owner->theme_combo_box->currentText()
            );
        editor->set_presentation(value.value, value.profile, value.context);
    };
    connect(model, &appearance_model::changed, editor, refresh);
    connect(theme_combo_box, &QComboBox::currentIndexChanged, editor, refresh);
    connect(
        editor, &appearance_editor::save_requested, this,
        [this, editor](quint64 id, const appearance_state& state) {
            if (commit_theme_settings(&state))
                editor->report_saved(id);
            else
                editor->report_save_failed(
                    id, str_label("Could not save the appearance settings."), {}
                );
        }
    );
    connect(
        editor->command(editor_command::cancel), &QAbstractButton::clicked,
        this, &settings_template_widget::reset_theme_selection
    );
    refresh();
#endif
}

bool settings_template_widget::apply_theme_settings() {
    bool applied = false;
    if (appearance_editor_) {
        if (appearance_editor_->save_pending())
            return false;
        // The existing store is synchronous. Route dialog commands through
        // the same editor request/ack, even for a table/artwork-only edit.
        const auto completed = connect(
            this, &settings_template_widget::desktop_presentation_applied, this,
            [&applied] { applied = true; }
        );
        auto* save
            = appearance_editor_->command(shared_ui::qt::editor_command::save);
        save->setEnabled(true);
        save->click();
        disconnect(completed);
    } else {
        applied = commit_theme_settings(nullptr);
    }
    if (!applied) {
        QMessageBox::warning(
            this, str_label("Appearance"),
            str_label("Could not save the appearance settings.")
        );
        return false;
    }
    return true;
}
