#include "shell/main_window.hpp"

#include "arch/str_label.hpp"
#include "monitor/resource_monitor.hpp"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>

void main_window::on_export_debug_snapshot_triggered() {
#if defined(NDEBUG)
    return;
#else
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd_hhmmss")
    );
    const QString suggested_name
        = QStringLiteral("debug_snapshot_%1.json").arg(timestamp);
    const QString output_path = QFileDialog::getSaveFileName(
        this, str_label("Export debug snapshot"), suggested_name,
        str_label("JSON files (*.json)")
    );
    if (output_path.isEmpty()) {
        return;
    }

    add_debug_marker(QStringLiteral("manual_export_snapshot"));
    debug_telemetry_collector->export_debug_snapshot_async(output_path);
#endif
}

void main_window::on_export_process_memory_report_triggered() {
#if defined(NDEBUG)
    return;
#else
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd_hhmmss")
    );
    const QString suggested_name
        = QStringLiteral("process_memory_detail_%1.json").arg(timestamp);
    const QString output_path = QFileDialog::getSaveFileName(
        this, str_label("Export process memory detail"), suggested_name,
        str_label("JSON files (*.json)")
    );
    if (output_path.isEmpty()) {
        return;
    }

    add_debug_marker(QStringLiteral("manual_export_process_memory_detail"));
    debug_telemetry_collector->export_process_memory_report_async(output_path);
#endif
}

void main_window::on_add_monitor_marker_triggered() {
#if defined(NDEBUG)
    return;
#else
    const QString timestamp
        = QDateTime::currentDateTimeUtc().toString(QStringLiteral("hh:mm:ss"));
    add_debug_marker(QStringLiteral("manual_marker_%1").arg(timestamp));
#endif
}

void main_window::on_set_realistic_cadence_mode_triggered() {
#if defined(NDEBUG)
    return;
#else
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    debug_telemetry_collector->set_debug_cadence_mode(
        resource_monitor::debug_cadence_mode::realistic
    );
    add_debug_marker(QStringLiteral("cadence_mode_realistic"));
    sync_debug_cadence_mode_actions();
    update_status_text();
#endif
}

void main_window::on_set_instrumented_cadence_mode_triggered() {
#if defined(NDEBUG)
    return;
#else
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    debug_telemetry_collector->set_debug_cadence_mode(
        resource_monitor::debug_cadence_mode::instrumented
    );
    add_debug_marker(QStringLiteral("cadence_mode_instrumented"));
    sync_debug_cadence_mode_actions();
    update_status_text();
#endif
}

void main_window::on_toggle_debug_broadcaster_triggered(bool checked) {
#if defined(NDEBUG)
    Q_UNUSED(checked);
    return;
#else
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    debug_telemetry_collector->set_debug_broadcaster_enabled(checked);
    const bool enabled
        = debug_telemetry_collector->is_debug_broadcaster_enabled();

    if (toggle_debug_broadcaster_action != nullptr) {
        toggle_debug_broadcaster_action->setChecked(enabled);
    }

    add_debug_marker(
        enabled ? QStringLiteral("debug_broadcaster_enabled")
                : QStringLiteral("debug_broadcaster_disabled")
    );
    update_status_text();
#endif
}

void main_window::add_debug_marker(const QString& label) const {
#if defined(NDEBUG)
    Q_UNUSED(label);
    return;
#else
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    debug_telemetry_collector->add_manual_marker(label);
#endif
}

void main_window::sync_debug_cadence_mode_actions() const {
#if defined(NDEBUG)
    return;
#else
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    const bool is_instrumented
        = debug_telemetry_collector->get_debug_cadence_mode()
        == resource_monitor::debug_cadence_mode::instrumented;
    if (realistic_cadence_mode_action != nullptr) {
        realistic_cadence_mode_action->setChecked(!is_instrumented);
    }
    if (instrumented_cadence_mode_action != nullptr) {
        instrumented_cadence_mode_action->setChecked(is_instrumented);
    }
#endif
}

QString main_window::debug_status_suffix() const {
#if defined(NDEBUG)
    return QString();
#else
    if (debug_telemetry_collector == nullptr) {
        return QString();
    }

    const bool instrumented
        = debug_telemetry_collector->get_debug_cadence_mode()
        == resource_monitor::debug_cadence_mode::instrumented;
    const bool broadcaster_enabled
        = debug_telemetry_collector->is_debug_broadcaster_enabled();
    return str_label("  Debug cadence: %1  Broadcaster: %2")
        .arg(
            instrumented ? str_label("instrumented")
                         : str_label("realistic")
        )
        .arg(broadcaster_enabled ? str_label("on") : str_label("off"));
#endif
}

void main_window::dump_debug_telemetry_on_exit() const {
#if defined(NDEBUG)
    return;
#else
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    const QString base_dir
        = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base_dir.isEmpty()) {
        return;
    }

    QDir dir(base_dir);
    if (!dir.mkpath(QStringLiteral("."))) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd_hhmmss")
    );
    const QString output_path = dir.filePath(
        QStringLiteral("debug_snapshot_exit_%1.json").arg(timestamp)
    );

    QString error_message;
    const bool success = debug_telemetry_collector->export_debug_snapshot_sync(
        output_path, &error_message
    );
    if (!success) {
        qWarning() << "Unable to export debug snapshot on exit:"
                   << error_message;
    }
#endif
}
