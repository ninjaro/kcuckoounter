#ifndef KCUCKOOUNTER_TESTS_INCLUDE_RESOURCE_MONITOR_TESTS_HPP
#define KCUCKOOUNTER_TESTS_INCLUDE_RESOURCE_MONITOR_TESTS_HPP

#include <QObject>

class resource_monitor_tests : public QObject {
    Q_OBJECT

private slots:
    void collects_initial_snapshot_when_attached();
    void keeps_bounded_timeline_and_emits_updates();
    void records_manual_markers_in_bounded_event_timeline();
    void exports_snapshot_asynchronously();
    void export_contains_size_bucket_and_largest_entry_diagnostics();
    void exports_active_debug_cadence_mode();
};

#endif // KCUCKOOUNTER_TESTS_INCLUDE_RESOURCE_MONITOR_TESTS_HPP
