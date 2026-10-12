#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_MOBILE_FIXTURE_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_MOBILE_FIXTURE_HPP

#include "table/table_fixture.hpp"

namespace mobile_test {

inline const QString legacy_key = QStringLiteral("trainer_session/payload");

struct preferences_guard {
    table_test::launcher_preferences_guard launcher;
    QVariant checkpoint;
    preferences_guard();
    ~preferences_guard();
};

} // namespace mobile_test

#endif
