#ifndef KCUCKOOUNTER_TESTS_PREFERENCES_FIXTURE_HPP
#define KCUCKOOUNTER_TESTS_PREFERENCES_FIXTURE_HPP

#include "settings/session_checkpoint.hpp"
#include "settings/strategy_data.hpp"
#include "table/gameplay_setup.hpp"

#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

namespace preferences_test {

strategy_catalog test_catalog();
QSettings temporary_settings(const QTemporaryDir& directory);
trainer_session_checkpoint test_checkpoint();
QJsonObject stored_checkpoint_root(QSettings& settings);
void store_checkpoint_root(QSettings& settings, const QJsonObject& root);
std::optional<gameplay::session> setup_session(
    std::size_t count = 4,
    const gameplay::session_configuration& configuration = {}
);
bool prepare_setup_session(gameplay::session& owner);
std::optional<gameplay::session> deck_setup_session(
    const gameplay::deck_configuration& first = { "uston_ss", 1, false, true }
);

template <typename Enum> void choose(QComboBox* combo, Enum value) {
    combo->setCurrentIndex(combo->findData(static_cast<int>(value)));
}

} // namespace preferences_test

#endif // KCUCKOOUNTER_TESTS_PREFERENCES_FIXTURE_HPP
