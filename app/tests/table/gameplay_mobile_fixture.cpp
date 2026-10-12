#include "table/gameplay_mobile_fixture.hpp"
#include "table/gameplay_mobile_tests.hpp"

#include "shell/main_window.hpp"

#include <QDialog>
#include <QSettings>
#include <QtTest/QtTest>

namespace mobile_test {

preferences_guard::preferences_guard() {
    QSettings storage("ninjaro", "kcuckoounter");
    checkpoint = storage.value(legacy_key);
    storage.remove(legacy_key);
    storage.sync();
}

preferences_guard::~preferences_guard() {
    QSettings storage("ninjaro", "kcuckoounter");
    if (checkpoint.isValid())
        storage.setValue(legacy_key, checkpoint);
    else
        storage.remove(legacy_key);
    storage.sync();
}

} // namespace mobile_test

void gameplay_mobile_tests::prepare_window(main_window& window) {
    window.resize(1200, 900);
    window.show();
    QCoreApplication::processEvents();
    QVERIFY(window.gameplay_recovery_ready);
    if (window.setup_dialog)
        window.setup_dialog->hide();
}

void gameplay_mobile_tests::start_owned(main_window& window, bool frozen) {
    prepare_window(window);
    auto* view = window.table_widget;
    table_test::set_gameplay_motion_style(*view, 0);
    QVERIFY(view->install_gameplay_session(
        table_test::owned_clock_fixture(
            2, frozen, gameplay::dealing_mode::simultaneous,
            gameplay::quiz_scope::single, { 1, 4 }
        )
    ));
    QVERIFY(window.reserve_gameplay_from_ui(73));
    view->stop_gameplay_clock();
}

bool gameplay_mobile_tests::tick(table& view, std::uint64_t elapsed) {
    std::vector<gameplay::quiz_answer> answers;
    const bool accepted = view.advance_gameplay_runtime(elapsed, answers);
    view.publish_gameplay_runtime(answers);
    return accepted;
}
