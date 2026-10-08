#ifndef KCUCKOOUNTER_TESTS_GAMEPLAY_RESTORE_HPP
#define KCUCKOOUNTER_TESTS_GAMEPLAY_RESTORE_HPP

#include <QObject>
#include <QSettings>
#include <functional>

class gameplay_restore_tests : public QObject {
    Q_OBJECT

private slots:
    void active_restores_identity_data();
    void active_restores_identity();
    void absent_keeps_scene();
    void protected_records_keep_scene_data();
    void protected_records_keep_scene();
    void active_preflight_keeps_scene_data();
    void active_preflight_keeps_scene();
    void active_binding_rejects_reuse();
    void active_commit_blocks_reentry();
    void active_publication_retirement_data();
    void active_publication_retirement();
    void pending_retries_without_scene_data();
    void pending_retries_without_scene();
    void pending_failures_reopen_exactly_data();
    void pending_failures_reopen_exactly();
    void pending_dispatch_blocks_reentry();

private:
    static bool read_settings(QIODevice&, QSettings::SettingsMap&);
    static bool write_settings(QIODevice&, const QSettings::SettingsMap&);
    static int write_failure;
    static std::function<void()> on_write;
};

#endif
