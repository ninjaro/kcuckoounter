#ifndef KCUCKOOUNTER_TESTS_PREFERENCES_TESTS_HPP
#define KCUCKOOUNTER_TESTS_PREFERENCES_TESTS_HPP

#include <QObject>

class preferences_tests : public QObject {
    Q_OBJECT

private slots:
    void round_trip_preserves_valid_values();
    void invalid_values_fall_back_independently();
    void strategy_id_repairs_a_renamed_or_removed_slug();
    void desktop_shell_state_round_trip_preserves_qt_state();
    void session_checkpoint_round_trip_preserves_exact_progress();
    void session_checkpoint_rejects_duplicate_or_missing_cards();
    void session_checkpoint_rejects_huge_numeric_values();
    void session_checkpoint_rejects_inconsistent_slot_state();
    void training_progress_is_bounded();
    void training_progress_rejects_huge_history_values();
};

#endif // KCUCKOOUNTER_TESTS_PREFERENCES_TESTS_HPP
