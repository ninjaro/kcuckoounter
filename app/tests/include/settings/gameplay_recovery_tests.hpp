#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_RECOVERY_TESTS_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_RECOVERY_TESTS_HPP

#include <QObject>

class gameplay_recovery_tests : public QObject {
    Q_OBJECT

private slots:
    void active_round_trip_data();
    void active_round_trip();
    void manual_finish_after_restore();
    void natural_finish();
    void lives_batch_finish();
    void mutable_state_and_stopped_decks();
    void reservation_rejects_replacement();
    void save_rejects_rewind();
    void codec_shapes();
    void codec_identity_bounds();
    void legacy_checkpoint_policy();
    void storage_conflicts();
    void completion_conflicts();
    void duplicate_preserves_new_session();
    void full_width_sequence();
    void write_failures_data();
    void write_failures();
    void pending_admission_overflow();
};

#endif // KCUCKOOUNTER_TESTS_GAMEPLAY_RECOVERY_TESTS_HPP
