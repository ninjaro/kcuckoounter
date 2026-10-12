#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_DRILL_FIXTURE_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_DRILL_FIXTURE_HPP

#include "settings/gameplay_drill.hpp"
#include "settings/preferences_fixture.hpp"

#include <QVariantMap>

namespace preferences_test {

inline const QString gameplay_drills_key
    = QStringLiteral("gameplay_drills/document");

gameplay_drill sample_drill();
strategy_catalog drill_catalog();
QVariantMap other_drill_settings(QSettings& settings);

} // namespace preferences_test

#endif // KCUCKOOUNTER_TESTS_GAMEPLAY_DRILL_FIXTURE_HPP
