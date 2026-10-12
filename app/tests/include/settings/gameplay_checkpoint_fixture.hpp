#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_CHECKPOINT_FIXTURE_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_CHECKPOINT_FIXTURE_HPP

#include "settings/gameplay_checkpoint.hpp"

#include <QJsonObject>

namespace checkpoint_test {

inline const QString storage_key
    = QStringLiteral("gameplay_checkpoint/document");
gameplay::session finite_session(gameplay::session_configuration settings = {});
gameplay::session mixed_session(gameplay::session_configuration settings);
gameplay_checkpoint sample_checkpoint();
QByteArray document(const QJsonObject& root);
bool continue_pair(
    gameplay::session& first, gameplay::session& second, std::size_t steps
);

} // namespace checkpoint_test

#endif // KCUCKOOUNTER_TESTS_GAMEPLAY_CHECKPOINT_FIXTURE_HPP
