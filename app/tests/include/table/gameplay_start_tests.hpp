#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_START_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_START_HPP

#include <QObject>

class gameplay_start_tests : public QObject {
    Q_OBJECT

private slots:
    void initial_capture_matches_start();
    void initial_capture_checks_resources();
    void gate_precedes_activation();
    void gate_rejection_keeps_setup();
    void gate_blocks_reentrant_start_and_replacement();
    void gate_checks_callback_mutation_data();
    void gate_checks_callback_mutation();
    void gate_survives_destruction_and_exceptions();
    void reservation_precedes_publication();
    void reservation_rejects_protected_storage_data();
    void reservation_rejects_protected_storage();
    void reservation_write_failure_keeps_setup();
    void reservation_survives_scene_retirement_data();
    void reservation_survives_scene_retirement();
    void reservation_after_failed_activation_data();
    void reservation_after_failed_activation();
};

#endif
