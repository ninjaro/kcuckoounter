#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_MOBILE_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_MOBILE_HPP

#include <QObject>
#include <cstdint>

class main_window;
class table;

class gameplay_mobile_tests : public QObject {
    Q_OBJECT

private slots:
    void background_stops_dealing_data();
    void background_stops_dealing();
    void background_freezes_quiz_data();
    void background_freezes_quiz();
    void background_save_can_retry();
    void background_retries_exact_result();
    void restore_keeps_future_data();
    void restore_keeps_future();
    void background_during_start();
    void background_during_restore();
    void background_retirement_data();
    void background_retirement();
    void modal_background_stays_paused_data();
    void modal_background_stays_paused();
    void mobile_callback_is_connected();
    void mobile_restores_legacy();
    void mobile_early_action_restores();
    void mobile_preserves_both_records();

private:
    static void prepare_window(main_window& window);
    static void start_owned(main_window& window, bool frozen = true);
    static bool tick(table& view, std::uint64_t elapsed);
};

#endif
