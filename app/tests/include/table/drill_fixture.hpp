#ifndef KCUCKOOUNTER_TESTS_DRILL_FIXTURE_HPP
#define KCUCKOOUNTER_TESTS_DRILL_FIXTURE_HPP

#include "settings/gameplay_drill.hpp"
#include "settings/preferences.hpp"

#include <QMessageBox>
#include <QSettings>
#include <functional>

class main_window;
class QDialog;

namespace table_test {

inline constexpr gameplay::slot_limits drill_limits { 64, 64 };
gameplay_drill target_drill(std::size_t count = 4);
training_drill legacy_drill();
void visit_drill_picker(
    main_window& window, const std::function<void(QDialog&)>& inspect
);
void answer_drill_question(QWidget& parent, QMessageBox::StandardButton answer);

struct drill_storage_guard {
    QSettings storage { QStringLiteral("ninjaro"),
                        QStringLiteral("kcuckoounter") };
    QVariant legacy;
    QVariant target;
    drill_storage_guard();
    ~drill_storage_guard();
};

} // namespace table_test

#endif // KCUCKOOUNTER_TESTS_DRILL_FIXTURE_HPP
