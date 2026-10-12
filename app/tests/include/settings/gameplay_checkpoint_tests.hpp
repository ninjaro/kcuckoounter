#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_CHECKPOINT_TESTS_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_CHECKPOINT_TESTS_HPP

#include <QObject>

class gameplay_checkpoint_tests : public QObject {
    Q_OBJECT

private slots:
    void finite_round_trip_data();
    void finite_round_trip();
    void mixed_round_trip_data();
    void mixed_round_trip();
    void full_width_values_round_trip();
    void required_fields_and_shapes();
    void canonical_integer_validation();
    void budgets_caps_and_text();
    void storage_absence_and_legacy_isolation();
    void storage_reopen_and_clear();
    void storage_protects_unreadable_records();
    void storage_reports_failed_writes();
};

#endif // KCUCKOOUNTER_TESTS_GAMEPLAY_CHECKPOINT_TESTS_HPP
