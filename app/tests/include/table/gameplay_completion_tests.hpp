#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_COMPLETION_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_COMPLETION_HPP

#include <QObject>
#include <QSettings>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <memory>

class table;

namespace gameplay {
class session;
}

class gameplay_completion_tests : public QObject {
    Q_OBJECT

private slots:
    void manual_retains_result_data();
    void manual_retains_result();
    void natural_retains_result_data();
    void natural_retains_result();
    void lives_waits_for_all_answers_data();
    void lives_waits_for_all_answers();
    void terminal_precedes_retirement_data();
    void terminal_precedes_retirement();
    void restored_paused_owner_completes();
    void finish_during_start_retains_result();
    void stale_scene_cannot_complete();
    void binding_teardown_retires_hook();
    void completion_requires_terminal();
    void write_failures_keep_record_data();
    void write_failures_keep_record();
    void completion_blocks_reentry();
    void aborted_start_retires_hook();
    void conflicting_pending_preserves_result();

private:
    static std::unique_ptr<gameplay::session> lives_fixture();
    static bool tick(table& view, std::uint64_t elapsed_ms);
    static bool read_settings(QIODevice&, QSettings::SettingsMap&);
    static bool write_settings(QIODevice&, const QSettings::SettingsMap&);
    static int write_failure;
    static QStringList attempts;
    static std::function<void()> on_write;
};

#endif
