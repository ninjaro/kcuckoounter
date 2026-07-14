#include "shell/main_window.hpp"

#include "arch/str_label.hpp"
#include "monitor/resource_monitor.hpp"
#include "table/table.hpp"

#include <QActionGroup>
#include <QCoreApplication>

void main_window::setup_debug_monitoring() {
#if defined(NDEBUG)
    debug_telemetry_collector = nullptr;
    return;
#else
    debug_telemetry_collector = new resource_monitor(this);
    if (table_widget != nullptr) {
        debug_telemetry_collector->attach_cache_service(
            table_widget->shared_raster_cache_service()
        );
        debug_telemetry_collector->attach_table_service(table_widget);
    }
    QObject::connect(
        qApp, &QCoreApplication::aboutToQuit, this,
        &main_window::on_application_about_to_quit
    );
    QObject::connect(
        debug_telemetry_collector,
        &resource_monitor::debug_broadcaster_state_changed, this,
        &main_window::update_status_text
    );
#endif

    insert_shell_separator();

    export_debug_snapshot_action
        = new BaseAction(str_label("Export snapshot"), this);
    register_shell_action(
        export_debug_snapshot_action, QStringLiteral("debug_export_snapshot")
    );
    export_debug_snapshot_action->setToolTip(
        str_label("Export cache/resource telemetry snapshot to JSON")
    );
    QObject::connect(
        export_debug_snapshot_action, &BaseAction::triggered, this,
        &main_window::on_export_debug_snapshot_triggered
    );

    export_process_memory_report_action
        = new BaseAction(str_label("Export process detail"), this);
    register_shell_action(
        export_process_memory_report_action,
        QStringLiteral("debug_export_process_memory_report")
    );
    export_process_memory_report_action->setToolTip(str_label(
        "Export on-demand OS process memory details (/proc status + smaps "
        "rollup)"
    ));
    QObject::connect(
        export_process_memory_report_action, &BaseAction::triggered, this,
        &main_window::on_export_process_report
    );

    add_monitor_marker_action = new BaseAction(str_label("Add marker"), this);
    register_shell_action(
        add_monitor_marker_action, QStringLiteral("debug_add_marker")
    );
    add_monitor_marker_action->setToolTip(
        str_label("Add a manual telemetry marker for later monitor analysis")
    );
    QObject::connect(
        add_monitor_marker_action, &BaseAction::triggered, this,
        &main_window::on_add_monitor_marker_triggered
    );

    toggle_debug_broadcaster_action
        = new BaseAction(str_label("Broadcast telemetry"), this);
    register_shell_action(
        toggle_debug_broadcaster_action,
        QStringLiteral("debug_toggle_broadcaster")
    );
    toggle_debug_broadcaster_action->setToolTip(str_label(
        "Enable debug-only local IPC telemetry broadcasting for the "
        "standalone monitor"
    ));
    toggle_debug_broadcaster_action->setCheckable(true);
    toggle_debug_broadcaster_action->setChecked(
        debug_telemetry_collector->is_debug_broadcaster_enabled()
    );
    QObject::connect(
        toggle_debug_broadcaster_action, &BaseAction::triggered, this,
        &main_window::on_toggle_debug_broadcaster_triggered
    );

    auto* cadence_action_group = new QActionGroup(this);
    cadence_action_group->setExclusive(true);

    realistic_cadence_mode_action
        = new BaseAction(str_label("Mode: realistic"), this);
    register_shell_action(
        realistic_cadence_mode_action, QStringLiteral("debug_realistic_cadence")
    );
    realistic_cadence_mode_action->setCheckable(true);
    cadence_action_group->addAction(realistic_cadence_mode_action);
    QObject::connect(
        realistic_cadence_mode_action, &BaseAction::triggered, this,
        &main_window::on_realistic_cadence_selected
    );

    instrumented_cadence_mode_action
        = new BaseAction(str_label("Mode: instrumented"), this);
    register_shell_action(
        instrumented_cadence_mode_action,
        QStringLiteral("debug_instrumented_cadence")
    );
    instrumented_cadence_mode_action->setCheckable(true);
    cadence_action_group->addAction(instrumented_cadence_mode_action);
    QObject::connect(
        instrumented_cadence_mode_action, &BaseAction::triggered, this,
        &main_window::on_instrumented_cadence_selected
    );

    sync_debug_cadence_mode_actions();
}

void main_window::on_application_about_to_quit() {
    dump_debug_telemetry_on_exit();
}
