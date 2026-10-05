#include "table/table_fixture.hpp"

#include "card_helpers/card_sheet.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "settings/training_progress.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/slot_settings.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"

#include <QLineEdit>
#include <QMessageBox>
#include <QProxyStyle>
#include <QTimer>
#include <QtTest/QtTest>

#include <algorithm>
#include <memory>

#ifdef KC_KDE
#include <KActionCollection>
#endif

namespace table_test {

std::unique_ptr<gameplay::session>
owned_table_fixture(std::size_t count, bool global_pause) {
    gameplay::session_configuration settings;
    settings.failure = gameplay::failure_policy::block;
    settings.global_quiz_pause = global_pause;
    settings.allow_extra_slots = true;
    settings.dealing = gameplay::dealing_mode::simultaneous;
    std::vector<gameplay::deck_configuration> configurations(
        count, { "hi_lo", 1, false, false }
    );
    configurations.front().training = true;
    auto owner = gameplay::session::create(
        settings, configurations, { 1, std::max(count, std::size_t { 64 }) }
    );
    if (!owner)
        qFatal("Invalid table configuration fixture");
    std::vector<gameplay::prepared_deck> prepared(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto& deck = prepared[index];
        deck.cards = index == 1
            ? std::vector<std::uint8_t> { 12, 11 }
            : std::vector<std::uint8_t> { 0, 1, 2, 3, 4, 5, 6, 7 };
        deck.rank_weights.fill(1);
        deck.initial_running_count
            = index == 0 ? -(std::int64_t { 1 } << 40) : 100;
        if (index == 2 || index == 3)
            deck.virtual_quiz_targets = { 1 };
    }
    if (!owner->prepare_decks(prepared) || !owner->set_show_count({ 0 }, true))
        qFatal("Invalid table preparation fixture");
    return std::make_unique<gameplay::session>(std::move(*owner));
}

card_widget* owned_card(table_slot* slot) {
    return slot->findChild<card_widget*>();
}

QRect widget_rectangle(const packing::rectangle& rectangle) {
    return { static_cast<int>(rectangle.x), static_cast<int>(rectangle.y),
             static_cast<int>(rectangle.width),
             static_cast<int>(rectangle.height) };
}

std::unique_ptr<gameplay::session> owned_clock_fixture(
    std::size_t count, bool global_pause, gameplay::dealing_mode mode,
    gameplay::quiz_scope scope, const std::vector<std::uint64_t>& targets,
    bool allow_skip
) {
    auto owner = owned_table_fixture(count, global_pause);
    auto settings = owner->configuration();
    settings.dealing = mode;
    settings.scope = scope;
    settings.sequential_count = std::min(count, std::size_t { 2 });
    settings.allow_skip = allow_skip;
    if (!owner->configure(settings))
        qFatal("Invalid clock settings fixture");
    std::vector<gameplay::prepared_deck> prepared(count);
    for (std::size_t index = 0; index < count; ++index) {
        prepared[index].cards = { 0, 1, 2, 3, 4, 5, 6, 7 };
        prepared[index].rank_weights.fill(1);
        prepared[index].initial_running_count
            = index == 0 ? -(std::int64_t { 1 } << 40) : 100;
        if (scope == gameplay::quiz_scope::single)
            prepared[index].virtual_quiz_targets = targets;
    }
    if (!owner->prepare_decks(
            prepared,
            scope == gameplay::quiz_scope::multi
                ? std::span<const std::uint64_t>(targets)
                : std::span<const std::uint64_t> {}
        ))
        qFatal("Invalid clock preparation fixture");
    return owner;
}

QLineEdit* owned_input(table_slot* slot) {
    return slot->findChild<QLineEdit*>(QStringLiteral("gameplay_quiz_input"));
}

void enter_owned_count(QLineEdit* input, const QString& text) {
    input->selectAll();
    QTest::keyClicks(input, text);
}

// Exercise the native shell with deterministic presentation, without persisting
// test choices into either legacy preferences or other profile cases.
launcher_preferences_guard::launcher_preferences_guard()
    : ui(load_desktop_ui_preferences())
    , trainer(load_trainer_preferences()) {
    desktop_ui_preferences defaults;
    defaults.settings_override = slot_settings_style::card;
    (void)save_desktop_ui_preferences(defaults);
    auto preferences = trainer;
    preferences.card_orientation = card_orientation_mode::automatic;
    (void)save_trainer_preferences(preferences);
}

launcher_preferences_guard::~launcher_preferences_guard() {
    (void)save_desktop_ui_preferences(ui);
    (void)save_trainer_preferences(trainer);
}

QAction* launcher_start_action(main_window& window) {
    const auto actions = window.findChildren<QAction*>();
    const auto found
        = std::find_if(actions.begin(), actions.end(), [](auto* a) {
              return a->shortcut() == QKeySequence(Qt::Key_P);
          });
    return found == actions.end() ? nullptr : *found;
}

// A real confirmation, answered only once its nested event loop is entered.
void answer_launcher_finish(
    main_window& window, QMessageBox::StandardButton answer
) {
    QTimer::singleShot(0, &window, [&window, answer] {
        if (auto* question = window.findChild<QMessageBox*>())
            question->button(answer)->click();
    });
}

class gameplay_motion_style : public QProxyStyle {
public:
    explicit gameplay_motion_style(int duration)
        : QProxyStyle(QStringLiteral("Fusion"))
        , duration_ms(duration) { }

    int styleHint(
        StyleHint hint, const QStyleOption* option, const QWidget* widget,
        QStyleHintReturn* result
    ) const override {
        if (hint == QStyle::SH_Widget_Animation_Duration)
            return duration_ms;
        return QProxyStyle::styleHint(hint, option, widget, result);
    }

private:
    int duration_ms;
};

void set_gameplay_motion_style(table& view, int duration) {
    auto* style = new gameplay_motion_style(duration);
    style->setParent(&view);
    view.setStyle(style);
}

} // namespace table_test
