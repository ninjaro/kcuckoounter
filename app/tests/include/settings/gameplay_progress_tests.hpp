#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_PROGRESS_TESTS_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_PROGRESS_TESTS_HPP

#include <QObject>

class gameplay_progress_tests : public QObject {
    Q_OBJECT

private slots:
    void result_round_trip_data();
    void result_round_trip();
    void automatic_end_results();
    void exact_integer_round_trip();
    void result_invariants();
    void required_fields_and_shapes();
    void canonical_integer_validation();
    void bounds_and_revisions();
    void history_accounting();
    void lifetime_sequence_bounds();
    void history_reopen_and_retries();
    void legacy_history_remains_readable();
    void storage_protects_unreadable_records();
    void storage_overflow_is_atomic();
    void storage_reports_failed_writes();
};

#endif // KCUCKOOUNTER_TESTS_GAMEPLAY_PROGRESS_TESTS_HPP
