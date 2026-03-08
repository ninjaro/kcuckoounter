#ifndef KCUCKOOUNTER_TESTS_CLI_SCRIPTS_TESTS_HPP
#define KCUCKOOUNTER_TESTS_CLI_SCRIPTS_TESTS_HPP

#include <QObject>

class cli_scripts_tests : public QObject {
    Q_OBJECT

private slots:
    void cli_help_command_succeeds();
    void cli_unknown_command_fails();
    void build_command_rejects_unknown_mode();
    void test_command_rejects_unknown_mode();
    void run_command_rejects_unknown_mode();
    void android_command_rejects_unknown_subcommand();
    void deps_command_rejects_unknown_mode();
    void check_leaks_rejects_unknown_option();
    void make_run_target_delegates_to_cli();
    void make_check_target_delegates_to_cli();
};

#endif // KCUCKOOUNTER_TESTS_CLI_SCRIPTS_TESTS_HPP
