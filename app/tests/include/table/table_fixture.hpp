#ifndef KCUCKOOUNTER_TESTS_TABLE_FIXTURE_HPP
#define KCUCKOOUNTER_TESTS_TABLE_FIXTURE_HPP

#include "settings/preferences.hpp"
#include "table/table.hpp"

#include <QMessageBox>
#include <QRect>

class QAction;
class QLineEdit;
class card_widget;
class main_window;
class table_slot;

namespace table_test {

std::unique_ptr<gameplay::session>
owned_table_fixture(std::size_t count = 4, bool global_pause = false);
card_widget* owned_card(table_slot* slot);
QRect widget_rectangle(const packing::rectangle& rectangle);
std::unique_ptr<gameplay::session> owned_clock_fixture(
    std::size_t count = 4, bool global_pause = false,
    gameplay::dealing_mode mode = gameplay::dealing_mode::simultaneous,
    gameplay::quiz_scope scope = gameplay::quiz_scope::single,
    const std::vector<std::uint64_t>& targets = {}, bool allow_skip = true
);
QLineEdit* owned_input(table_slot* slot);
void enter_owned_count(QLineEdit* input, const QString& text);

struct launcher_preferences_guard {
    desktop_ui_preferences ui;
    trainer_preferences trainer;

    launcher_preferences_guard();
    ~launcher_preferences_guard();
};

QAction* launcher_start_action(main_window& window);
void answer_launcher_finish(
    main_window& window, QMessageBox::StandardButton answer
);
void set_gameplay_motion_style(table& view, int duration);

} // namespace table_test

#endif // KCUCKOOUNTER_TESTS_TABLE_FIXTURE_HPP
