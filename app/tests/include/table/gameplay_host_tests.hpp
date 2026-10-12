#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_HOST_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_HOST_HPP

#include <QObject>
#include <cstdint>

class main_window;
class table;

class gameplay_host_tests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void menu_start_pause_finish();
    void startup_restores_paused();
    void startup_retries_pending();
    void startup_protects_records_data();
    void startup_protects_records();
    void start_rejection_keeps_choices();
    void failed_result_blocks_retirement_data();
    void failed_result_blocks_retirement();
    void close_saves_continuation();
    void close_io_failure_keeps_paused();
    void start_retirement_keeps_reservation_data();
    void start_retirement_keeps_reservation();
    void terminal_retirement_keeps_retry_data();
    void terminal_retirement_keeps_retry();
    void nested_finish_is_admitted();
    void recovery_finish_is_admitted();

private:
    static void launch(main_window& window);
    static bool tick(table& view, std::uint64_t elapsed);
};

#endif
