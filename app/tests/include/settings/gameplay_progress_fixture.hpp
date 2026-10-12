#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_PROGRESS_FIXTURE_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_PROGRESS_FIXTURE_HPP

#include "settings/gameplay_progress.hpp"
#include "table/gameplay_session.hpp"

#include <QJsonObject>

namespace progress_test {

inline const QString storage_key = QStringLiteral("gameplay_progress/document");
gameplay::session ready_session(
    gameplay::session_configuration settings = {}, bool infinite = false
);
gameplay::session_result sample_result();
gameplay_progress progress_for(const gameplay::session_result& result);
QByteArray document(const QJsonObject& root);

} // namespace progress_test

#endif // KCUCKOOUNTER_TESTS_GAMEPLAY_PROGRESS_FIXTURE_HPP
