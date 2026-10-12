#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_RECOVERY_FIXTURE_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_RECOVERY_FIXTURE_HPP

#include "settings/gameplay_recovery.hpp"
#include "table/gameplay_session.hpp"

#include <QJsonObject>

namespace recovery_test {

inline const QString storage_key = QStringLiteral("gameplay_recovery/document");
inline const QString progress_key
    = QStringLiteral("gameplay_progress/document");
inline const QString checkpoint_key
    = QStringLiteral("gameplay_checkpoint/document");
gameplay_checkpoint
snapshot(const gameplay::session& model, std::uint64_t elapsed = 73);
gameplay::session restored(const gameplay_checkpoint& checkpoint);
void prime(gameplay::session& model);
void drive(gameplay::session& model, int steps);
gameplay_result_record
finish(gameplay::session& model, std::uint64_t sequence = 1);
QByteArray document(const QJsonObject& root);

} // namespace recovery_test

#endif // KCUCKOOUNTER_TESTS_GAMEPLAY_RECOVERY_FIXTURE_HPP
