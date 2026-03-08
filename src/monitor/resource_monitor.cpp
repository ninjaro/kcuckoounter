#include "monitor/resource_monitor.hpp"

#include "arch/num_helpers.hpp"
#include "monitor/debug_broadcaster.hpp"
#include "monitor/debug_probe_core.hpp"
#include "table/table.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QThreadPool>
#include <QUuid>
#include <QtConcurrent>
#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace {

QString effective_protocol_app_name() {
    const QString app_name = QCoreApplication::applicationName().trimmed();
    return app_name.isEmpty() ? QStringLiteral("kcuckoounter") : app_name;
}

QString effective_protocol_build_id() {
    const QString app_version
        = QCoreApplication::applicationVersion().trimmed();
    return app_version.isEmpty() ? QStringLiteral("dev") : app_version;
}

QStringList default_protocol_debug_flags() {
    QStringList flags;
#if defined(NDEBUG)
    flags.push_back(QStringLiteral("release_build"));
#else
    flags.push_back(QStringLiteral("debug_build"));
#endif
    return flags;
}

QString process_memory_report_trigger_to_string(
    resource_monitor::process_memory_report_trigger trigger
) {
    switch (trigger) {
    case resource_monitor::process_memory_report_trigger::threshold_rss_growth:
        return QStringLiteral("threshold_rss_growth");
    case resource_monitor::process_memory_report_trigger::manual_on_demand:
    default:
        return QStringLiteral("manual_on_demand_heavy_dump");
    }
}

QString process_memory_source_for_rss(qint64 rss_bytes);
QString process_memory_unavailable_reason();

QString write_debug_snapshot_json(
    const QString& output_path,
    const resource_monitor::export_request_metadata& metadata,
    const QVector<resource_monitor::cache_timeline_entry>& cache_entries,
    const QVector<resource_monitor::event_timeline_entry>& event_entries,
    const QVector<geometry_debug_snapshot>& geometry_entries,
    const QVector<resource_monitor::resize_history_entry>& resize_entries,
    resource_monitor::debug_cadence_mode export_mode
) {
    const QJsonObject root = debug_probe_core::build_snapshot_export_json(
        metadata, cache_entries, event_entries, geometry_entries,
        resize_entries, export_mode
    );

    const QJsonDocument document(root);
    QFile file(output_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QStringLiteral("unable to open output file");
    }

    const qint64 written = file.write(document.toJson(QJsonDocument::Indented));
    if (written < 0) {
        return QStringLiteral("unable to write output file");
    }

    return QString();
}

struct process_status_sample {
    qint64 vm_rss_bytes = -1;
    qint64 vm_hwm_bytes = -1;
    qint64 vm_size_bytes = -1;
    qint64 vm_swap_bytes = -1;
};

struct process_memory_sample_result {
    qint64 rss_bytes = -1;
    QString source;
    QString unavailable_reason;
};

QString process_memory_source_for_rss(qint64 rss_bytes) {
    return rss_bytes >= 0 ? QStringLiteral("proc_status_vm_rss")
                          : QStringLiteral("unavailable");
}

QString process_memory_unavailable_reason() {
#if defined(Q_OS_LINUX)
    return QStringLiteral("proc_status_vm_rss_unreadable");
#else
    return QStringLiteral("proc_status_vm_rss_unsupported_platform");
#endif
}

qint64 parse_status_kb_line(const QString& line, const QString& key) {
    if (!line.startsWith(key)) {
        return -1;
    }

    const QString value_text = line.mid(key.size()).trimmed();
    const QStringList parts
        = value_text.split(QRegularExpression(QStringLiteral("\\s+")));
    if (parts.isEmpty()) {
        return -1;
    }

    bool ok = false;
    const qint64 value_kb = parts.first().toLongLong(&ok);
    if (!ok || value_kb < 0) {
        return -1;
    }

    return value_kb * 1024;
}

process_status_sample read_process_status_sample() {
    process_status_sample sample;
    QFile status_file(QStringLiteral("/proc/self/status"));
    if (!status_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return sample;
    }

    QTextStream stream(&status_file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (sample.vm_rss_bytes < 0) {
            sample.vm_rss_bytes
                = parse_status_kb_line(line, QStringLiteral("VmRSS:"));
        }
        if (sample.vm_hwm_bytes < 0) {
            sample.vm_hwm_bytes
                = parse_status_kb_line(line, QStringLiteral("VmHWM:"));
        }
        if (sample.vm_size_bytes < 0) {
            sample.vm_size_bytes
                = parse_status_kb_line(line, QStringLiteral("VmSize:"));
        }
        if (sample.vm_swap_bytes < 0) {
            sample.vm_swap_bytes
                = parse_status_kb_line(line, QStringLiteral("VmSwap:"));
        }
    }

    return sample;
}

QJsonObject read_smaps_rollup_bytes() {
    QJsonObject rollup;
    QFile smaps_rollup_file(QStringLiteral("/proc/self/smaps_rollup"));
    if (!smaps_rollup_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return rollup;
    }

    QTextStream stream(&smaps_rollup_file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        const int separator = static_cast<int>(line.indexOf(QLatin1Char(':')));
        if (separator <= 0) {
            continue;
        }

        const QString key = line.left(separator).trimmed();
        const qint64 value_bytes
            = parse_status_kb_line(line, QStringLiteral("%1:").arg(key));
        if (value_bytes >= 0) {
            rollup.insert(key, value_bytes);
        }
    }

    return rollup;
}

QString write_process_memory_report_json(
    const QString& output_path,
    resource_monitor::debug_cadence_mode cadence_mode,
    qint64 process_sample_interval_ms,
    qint64 auto_process_report_rss_growth_threshold_bytes,
    qint64 auto_process_report_cooldown_ms,
    qint64 auto_process_report_baseline_rss_bytes,
    qint64 auto_process_report_rss_growth_since_baseline_bytes,
    qint64 auto_process_report_last_trigger_utc_ms,
    qint64 auto_process_report_cooldown_remaining_ms,
    qint64 auto_process_report_consecutive_growth_hits_required,
    qint64 auto_process_report_consecutive_growth_hits_current,
    qint64 latest_process_rss_bytes, const QString& latest_process_rss_source,
    const QString& latest_process_rss_unavailable_reason,
    const QString& protocol_app_name, qint64 protocol_process_id,
    const QString& protocol_session_id, const QString& protocol_build_id,
    const QString& protocol_version, const QStringList& protocol_debug_flags,
    const QString& protocol_instrumentation_mode,
    resource_monitor::process_memory_report_trigger trigger
) {
    const process_status_sample status_sample = read_process_status_sample();
    const QJsonObject smaps_rollup = read_smaps_rollup_bytes();
    const bool has_status = status_sample.vm_rss_bytes >= 0
        || status_sample.vm_hwm_bytes >= 0 || status_sample.vm_size_bytes >= 0
        || status_sample.vm_swap_bytes >= 0;
    const bool has_smaps_rollup = !smaps_rollup.isEmpty();

    const debug_probe_core::process_memory_report_inputs inputs {
        .cadence_mode = cadence_mode,
        .captured_at_utc_ms
        = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch(),
        .process_memory_sample_interval_ms = process_sample_interval_ms,
        .auto_process_report_rss_growth_threshold_bytes
        = auto_process_report_rss_growth_threshold_bytes,
        .auto_process_report_cooldown_ms = auto_process_report_cooldown_ms,
        .auto_process_report_baseline_rss_bytes
        = auto_process_report_baseline_rss_bytes,
        .auto_process_report_rss_growth_since_baseline_bytes
        = auto_process_report_rss_growth_since_baseline_bytes,
        .auto_process_report_last_trigger_utc_ms
        = auto_process_report_last_trigger_utc_ms,
        .auto_process_report_cooldown_remaining_ms
        = auto_process_report_cooldown_remaining_ms,
        .auto_process_report_consecutive_growth_hits_required
        = auto_process_report_consecutive_growth_hits_required,
        .auto_process_report_consecutive_growth_hits_current
        = auto_process_report_consecutive_growth_hits_current,
        .latest_process_rss_bytes = latest_process_rss_bytes,
        .latest_process_rss_source = latest_process_rss_source,
        .latest_process_rss_unavailable_reason
        = latest_process_rss_unavailable_reason,
        .report_trigger_label
        = process_memory_report_trigger_to_string(trigger),
        .status_vm_rss_bytes = status_sample.vm_rss_bytes,
        .status_vm_hwm_bytes = status_sample.vm_hwm_bytes,
        .status_vm_size_bytes = status_sample.vm_size_bytes,
        .status_vm_swap_bytes = status_sample.vm_swap_bytes,
        .status_bytes_available = has_status,
        .status_bytes_unavailable_reason = process_memory_unavailable_reason(),
        .smaps_rollup_bytes = smaps_rollup,
        .smaps_rollup_bytes_available = has_smaps_rollup,
        .smaps_rollup_bytes_unavailable_reason
        = QStringLiteral("proc_smaps_rollup_unreadable_or_unsupported"),
        .protocol_app_name = protocol_app_name,
        .protocol_process_id = protocol_process_id,
        .protocol_session_id = protocol_session_id,
        .protocol_build_id = protocol_build_id,
        .protocol_version = protocol_version,
        .protocol_debug_flags = protocol_debug_flags,
        .protocol_instrumentation_mode = protocol_instrumentation_mode,
    };

    const QJsonObject root
        = debug_probe_core::build_process_memory_report_json(inputs);

    const QJsonDocument document(root);
    QFile file(output_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QStringLiteral("unable to open output file");
    }

    const qint64 written = file.write(document.toJson(QJsonDocument::Indented));
    if (written < 0) {
        return QStringLiteral("unable to write output file");
    }

    return QString();
}

} // namespace

resource_monitor::resource_monitor(QObject* parent, int max_timeline_entries)
    : QObject(parent)
    , observed_cache_service(nullptr)
    , observed_table_service(nullptr)
    , timeline_limit(std::max(1, max_timeline_entries))
    , collector_sequence(0)
    , cache_timeline_entries()
    , event_timeline_entries()
    , geometry_timeline_entries()
    , resize_history_entries()
    , pending_resize_transition_state(std::nullopt)
    , resize_history_log_stream_path()
    , last_export_metadata()
    , active_export_watcher(nullptr)
    , active_process_report_export_watcher(nullptr)
    , cadence_mode(debug_cadence_mode::realistic)
    , last_process_sample_ms(0)
    , current_process_rss_bytes(-1)
    , current_process_rss_source(process_memory_source_for_rss(-1))
    , current_process_rss_unavailable_reason(
          process_memory_unavailable_reason()
      )
    , auto_process_dump_rss_growth_threshold_bytes(
          debug_probe_core::auto_process_report_policy_for_mode(
              debug_cadence_mode::realistic
          )
              .rss_growth_threshold_bytes
      )
    , auto_process_dump_cooldown_ms(
          debug_probe_core::auto_process_report_policy_for_mode(
              debug_cadence_mode::realistic
          )
              .cooldown_ms
      )
    , auto_process_dump_policy_override_for_tests(false)
    , auto_process_dump_consecutive_growth_hits_required_override(1)
    , auto_process_dump_last_trigger_ms(0)
    , auto_process_dump_baseline_rss_bytes(-1)
    , auto_process_dump_consecutive_growth_hits(0)
    , auto_process_dump_window_start_ms(0)
    , auto_process_dump_window_exports_used(0)
    , protocol_app_name(effective_protocol_app_name())
    , protocol_session_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , protocol_build_id(effective_protocol_build_id())
    , protocol_debug_flags(default_protocol_debug_flags())
    , telemetry_broadcaster(new debug_broadcaster(this))
    , broadcaster_monotonic_clock()
    , process_sampling_timer() {
    cache_timeline_entries.reserve(timeline_limit);
    event_timeline_entries.reserve(timeline_limit);
    geometry_timeline_entries.reserve(timeline_limit);
    resize_history_entries.reserve(timeline_limit);
    broadcaster_monotonic_clock.start();

    if (telemetry_broadcaster != nullptr) {
        QObject::connect(
            telemetry_broadcaster,
            &debug_broadcaster::listener_connection_changed, this,
            [this](bool connected) {
                if (connected) {
                    publish_broadcaster_session_start();
                    publish_broadcaster_snapshot_now();
                }
                emit debug_broadcaster_state_changed();
            }
        );
        QObject::connect(
            telemetry_broadcaster, &debug_broadcaster::warning_raised, this,
            [this](
                const QString& warning_code, const QString& warning_message
            ) {
                Q_UNUSED(warning_code);
                Q_UNUSED(warning_message);
                emit debug_broadcaster_state_changed();
            }
        );
    }

    process_sampling_timer.setSingleShot(false);
    process_sampling_timer.setInterval(250);
    QObject::connect(
        &process_sampling_timer, &QTimer::timeout, this,
        &resource_monitor::on_periodic_collection_tick
    );
    process_sampling_timer.start();
}

void resource_monitor::attach_cache_service(raster_cache* cache_service) {
    if (observed_cache_service == cache_service) {
        return;
    }

    if (observed_cache_service != nullptr) {
        QObject::disconnect(
            observed_cache_service, &raster_cache::debug_snapshot_updated, this,
            &resource_monitor::on_cache_snapshot_updated
        );
    }

    observed_cache_service = cache_service;
    cache_timeline_entries.clear();
    event_timeline_entries.clear();
    pending_resize_transition_state.reset();
    auto_process_dump_baseline_rss_bytes = -1;
    auto_process_dump_last_trigger_ms = 0;
    auto_process_dump_consecutive_growth_hits = 0;
    auto_process_dump_window_start_ms = 0;
    auto_process_dump_window_exports_used = 0;

    if (observed_cache_service == nullptr) {
        return;
    }

    QObject::connect(
        observed_cache_service, &raster_cache::debug_snapshot_updated, this,
        &resource_monitor::on_cache_snapshot_updated
    );

    on_cache_snapshot_updated(observed_cache_service->get_debug_snapshot());
}

void resource_monitor::attach_table_service(QObject* table_service) {
    if (observed_table_service == table_service) {
        return;
    }

    table* previous_table = qobject_cast<table*>(observed_table_service);
    if (previous_table != nullptr) {
        QObject::disconnect(
            previous_table, &table::debug_geometry_snapshot_updated, this,
            &resource_monitor::on_geometry_debug_snapshot_updated
        );
        QObject::disconnect(
            previous_table, &table::debug_resize_transition_recorded, this,
            &resource_monitor::on_resize_transition_recorded
        );
    }

    observed_table_service = table_service;
    geometry_timeline_entries.clear();
    resize_history_entries.clear();
    pending_resize_transition_state.reset();
    resize_history_log_stream_path.clear();

    table* observed_table = qobject_cast<table*>(observed_table_service);
    if (observed_table == nullptr) {
        return;
    }

    QObject::connect(
        observed_table, &table::debug_geometry_snapshot_updated, this,
        &resource_monitor::on_geometry_debug_snapshot_updated
    );
    QObject::connect(
        observed_table, &table::debug_resize_transition_recorded, this,
        &resource_monitor::on_resize_transition_recorded
    );

    on_geometry_debug_snapshot_updated(
        observed_table->current_geometry_debug_snapshot()
    );
}

int resource_monitor::max_timeline_entries() const { return timeline_limit; }

int resource_monitor::timeline_size() const {
    return num_helpers::to_int(cache_timeline_entries.size());
}

int resource_monitor::event_timeline_size() const {
    return num_helpers::to_int(event_timeline_entries.size());
}

int resource_monitor::geometry_timeline_size() const {
    return num_helpers::to_int(geometry_timeline_entries.size());
}

int resource_monitor::resize_history_size() const {
    return num_helpers::to_int(resize_history_entries.size());
}

bool resource_monitor::has_cache_snapshot() const {
    return !cache_timeline_entries.isEmpty();
}

bool resource_monitor::has_geometry_snapshot() const {
    return !geometry_timeline_entries.isEmpty();
}

resource_monitor::cache_timeline_entry
resource_monitor::latest_cache_snapshot() const {
    if (cache_timeline_entries.isEmpty()) {
        return cache_timeline_entry();
    }

    return cache_timeline_entries.constLast();
}

geometry_debug_snapshot resource_monitor::latest_geometry_snapshot() const {
    if (geometry_timeline_entries.isEmpty()) {
        return geometry_debug_snapshot();
    }

    return geometry_timeline_entries.constLast();
}

QVector<resource_monitor::cache_timeline_entry>
resource_monitor::cache_timeline() const {
    return cache_timeline_entries;
}

QVector<resource_monitor::event_timeline_entry>
resource_monitor::event_timeline() const {
    return event_timeline_entries;
}

QVector<geometry_debug_snapshot> resource_monitor::geometry_timeline() const {
    return geometry_timeline_entries;
}

QVector<resource_monitor::resize_history_entry>
resource_monitor::resize_history() const {
    return resize_history_entries;
}

QString resource_monitor::resize_history_log_path() const {
    return resize_history_log_stream_path;
}

void resource_monitor::add_manual_marker(const QString& label) {
    push_event_entry(event_timeline_entry::event_kind::manual_marker, label);
}

void resource_monitor::set_debug_cadence_mode(debug_cadence_mode mode) {
    cadence_mode = mode;
    last_process_sample_ms = 0;
    auto_process_dump_consecutive_growth_hits = 0;
    auto_process_dump_window_start_ms = 0;
    auto_process_dump_window_exports_used = 0;
    refresh_process_memory_sample_if_needed();
    maybe_trigger_auto_process_report(0);
}

resource_monitor::debug_cadence_mode
resource_monitor::get_debug_cadence_mode() const {
    return cadence_mode;
}

resource_monitor::export_request_metadata
resource_monitor::latest_export_metadata() const {
    return last_export_metadata;
}

resource_monitor::auto_process_report_runtime_state
resource_monitor::current_auto_process_report_runtime_state() const {
    return auto_process_report_runtime_state {
        .rss_growth_threshold_bytes
        = auto_process_dump_rss_growth_threshold_bytes_effective(),
        .cooldown_ms = auto_process_dump_cooldown_ms_effective(),
        .baseline_rss_bytes = auto_process_dump_baseline_rss_bytes,
        .rss_growth_since_baseline_bytes
        = auto_process_dump_rss_growth_since_baseline_bytes(),
        .last_trigger_utc_ms = auto_process_dump_last_trigger_ms,
        .cooldown_remaining_ms = auto_process_dump_cooldown_remaining_ms(
            QDateTime::currentMSecsSinceEpoch()
        ),
        .consecutive_growth_hits_required
        = auto_process_dump_consecutive_growth_hits_required(),
        .consecutive_growth_hits_current
        = auto_process_dump_consecutive_growth_hits,
        .window_ms = auto_process_dump_window_ms_effective(),
        .window_max_exports = auto_process_dump_window_max_exports_effective(),
        .window_exports_used = auto_process_dump_window_exports_used,
    };
}

qint64 resource_monitor::latest_process_rss_bytes() const {
    return current_process_rss_bytes;
}

qint64 resource_monitor::process_memory_sample_interval_ms() const {
    return process_sample_interval_ms_for_mode();
}

void resource_monitor::set_debug_broadcaster_enabled(bool enabled) {
    if (telemetry_broadcaster == nullptr) {
        return;
    }

    const bool was_enabled = telemetry_broadcaster->is_enabled();
    if (was_enabled && !enabled) {
        publish_broadcaster_session_end(QStringLiteral("disabled_by_user"));
    }

    telemetry_broadcaster->set_enabled(enabled);
    if (telemetry_broadcaster->is_enabled()) {
        publish_broadcaster_session_start();
        publish_broadcaster_snapshot_now();
    }

    emit debug_broadcaster_state_changed();
}

bool resource_monitor::is_debug_broadcaster_enabled() const {
    return telemetry_broadcaster != nullptr
        && telemetry_broadcaster->is_enabled();
}

QString resource_monitor::debug_broadcaster_endpoint_name() const {
    const QJsonObject state = debug_broadcaster_runtime_state();
    return state.value(QStringLiteral("endpoint_name")).toString();
}

QJsonObject resource_monitor::debug_broadcaster_runtime_state() const {
    QJsonObject object;
    if (telemetry_broadcaster == nullptr) {
        return object;
    }

    const debug_broadcaster::runtime_state state
        = telemetry_broadcaster->state();
    object.insert(
        QStringLiteral("compile_time_enabled"), state.compile_time_enabled
    );
    object.insert(QStringLiteral("runtime_enabled"), state.runtime_enabled);
    object.insert(
        QStringLiteral("listener_connected"), state.listener_connected
    );
    object.insert(QStringLiteral("endpoint_name"), state.endpoint_name);
    object.insert(QStringLiteral("queued_messages"), state.queued_messages);
    object.insert(QStringLiteral("queued_bytes"), state.queued_bytes);
    object.insert(QStringLiteral("sent_messages"), state.sent_messages);
    object.insert(
        QStringLiteral("dropped_low_priority_messages"),
        state.dropped_low_priority_messages
    );
    object.insert(
        QStringLiteral("dropped_medium_priority_messages"),
        state.dropped_medium_priority_messages
    );
    object.insert(
        QStringLiteral("dropped_high_priority_messages"),
        state.dropped_high_priority_messages
    );
    object.insert(QStringLiteral("write_error_count"), state.write_error_count);
    return object;
}

void resource_monitor::set_auto_process_report_policy_for_tests(
    qint64 rss_growth_threshold_bytes, qint64 cooldown_ms,
    qint64 consecutive_growth_hits_required
) {
    auto_process_dump_policy_override_for_tests = true;
    auto_process_dump_rss_growth_threshold_bytes
        = std::max<qint64>(0, rss_growth_threshold_bytes);
    auto_process_dump_cooldown_ms = std::max<qint64>(0, cooldown_ms);
    auto_process_dump_consecutive_growth_hits_required_override
        = std::max<qint64>(1, consecutive_growth_hits_required);
    last_process_sample_ms = 0;
    refresh_process_memory_sample_if_needed();
    auto_process_dump_baseline_rss_bytes = current_process_rss_bytes;
    auto_process_dump_consecutive_growth_hits = 0;
    auto_process_dump_window_start_ms = 0;
    auto_process_dump_window_exports_used = 0;
}

void resource_monitor::export_debug_snapshot_async(const QString& output_path) {
    const export_request_metadata metadata {
        .collector_sequence = collector_sequence,
        .cache_timeline_size
        = num_helpers::to_int(cache_timeline_entries.size()),
        .event_timeline_size
        = num_helpers::to_int(event_timeline_entries.size()),
        .geometry_timeline_size
        = num_helpers::to_int(geometry_timeline_entries.size()),
        .resize_history_size
        = num_helpers::to_int(resize_history_entries.size()),
        .resize_history_log_path = resize_history_log_stream_path,
        .latest_process_rss_bytes = current_process_rss_bytes,
        .process_memory_source = current_process_rss_source,
        .process_memory_unavailable_reason
        = current_process_rss_unavailable_reason,
        .process_memory_sample_interval_ms
        = process_sample_interval_ms_for_mode(),
        .auto_process_report_rss_growth_threshold_bytes
        = auto_process_dump_rss_growth_threshold_bytes_effective(),
        .auto_process_report_cooldown_ms
        = auto_process_dump_cooldown_ms_effective(),
        .auto_process_report_baseline_rss_bytes
        = auto_process_dump_baseline_rss_bytes,
        .auto_process_report_rss_growth_since_baseline_bytes
        = auto_process_dump_rss_growth_since_baseline_bytes(),
        .auto_process_report_last_trigger_utc_ms
        = auto_process_dump_last_trigger_ms,
        .auto_process_report_cooldown_remaining_ms
        = auto_process_dump_cooldown_remaining_ms(
            QDateTime::currentMSecsSinceEpoch()
        ),
        .auto_process_report_consecutive_growth_hits_required
        = auto_process_dump_consecutive_growth_hits_required(),
        .auto_process_report_consecutive_growth_hits_current
        = auto_process_dump_consecutive_growth_hits,
        .auto_process_report_window_ms
        = auto_process_dump_window_ms_effective(),
        .auto_process_report_window_max_exports
        = auto_process_dump_window_max_exports_effective(),
        .auto_process_report_window_exports_used
        = auto_process_dump_window_exports_used,
        .protocol_app_name = protocol_app_name,
        .protocol_process_id = QCoreApplication::applicationPid(),
        .protocol_session_id = protocol_session_id,
        .protocol_build_id = protocol_build_id,
        .protocol_version = debug_probe_core::protocol_version_string(),
        .protocol_debug_flags = protocol_debug_flags,
        .protocol_instrumentation_mode = cadence_mode_to_string(cadence_mode),
    };
    last_export_metadata = metadata;
    publish_broadcaster_snapshot_now();

    const QVector<cache_timeline_entry> cache_entries = cache_timeline_entries;
    const QVector<event_timeline_entry> event_entries = event_timeline_entries;
    const QVector<geometry_debug_snapshot> geometry_entries
        = geometry_timeline_entries;
    const QVector<resize_history_entry> resize_entries = resize_history_entries;

    if (active_export_watcher != nullptr) {
        active_export_watcher->deleteLater();
        active_export_watcher = nullptr;
    }

    active_export_watcher = new QFutureWatcher<QString>(this);
    QObject::connect(
        active_export_watcher, &QFutureWatcher<QString>::finished, this,
        [this, output_path]() {
            if (active_export_watcher == nullptr) {
                emit snapshot_export_finished(
                    output_path, false, QStringLiteral("export watcher lost")
                );
                return;
            }

            const QString error_message
                = active_export_watcher->future().result();
            const bool success = error_message.isEmpty();

            active_export_watcher->deleteLater();
            active_export_watcher = nullptr;
            emit snapshot_export_finished(output_path, success, error_message);
        }
    );

    const debug_cadence_mode export_mode = cadence_mode;
    active_export_watcher->setFuture(
        QtConcurrent::run([output_path, metadata, cache_entries, event_entries,
                           geometry_entries, resize_entries, export_mode]() {
            return write_debug_snapshot_json(
                output_path, metadata, cache_entries, event_entries,
                geometry_entries, resize_entries, export_mode
            );
        })
    );
}

void resource_monitor::export_process_memory_report_async(
    const QString& output_path
) {
    export_process_memory_report_async(
        output_path, process_memory_report_trigger::manual_on_demand
    );
}

void resource_monitor::export_process_memory_report_async(
    const QString& output_path, process_memory_report_trigger trigger
) {
    if (active_process_report_export_watcher != nullptr) {
        active_process_report_export_watcher->deleteLater();
        active_process_report_export_watcher = nullptr;
    }

    active_process_report_export_watcher = new QFutureWatcher<QString>(this);
    QObject::connect(
        active_process_report_export_watcher,
        &QFutureWatcher<QString>::finished, this, [this, output_path]() {
            if (active_process_report_export_watcher == nullptr) {
                emit process_memory_report_export_finished(
                    output_path, false,
                    QStringLiteral("process-report export watcher lost")
                );
                return;
            }

            const QString error_message
                = active_process_report_export_watcher->future().result();
            const bool success = error_message.isEmpty();

            active_process_report_export_watcher->deleteLater();
            active_process_report_export_watcher = nullptr;
            emit process_memory_report_export_finished(
                output_path, success, error_message
            );
        }
    );

    const debug_cadence_mode export_mode = cadence_mode;
    const qint64 sample_interval_ms = process_sample_interval_ms_for_mode();
    const qint64 auto_rss_growth_threshold_bytes
        = auto_process_dump_rss_growth_threshold_bytes_effective();
    const qint64 auto_cooldown_ms = auto_process_dump_cooldown_ms_effective();
    const qint64 auto_baseline_rss_bytes = auto_process_dump_baseline_rss_bytes;
    const qint64 auto_rss_growth_since_baseline_bytes
        = auto_process_dump_rss_growth_since_baseline_bytes();
    const qint64 auto_last_trigger_utc_ms = auto_process_dump_last_trigger_ms;
    const qint64 auto_cooldown_remaining_ms
        = auto_process_dump_cooldown_remaining_ms(
            QDateTime::currentMSecsSinceEpoch()
        );
    const qint64 auto_consecutive_growth_hits_required
        = auto_process_dump_consecutive_growth_hits_required();
    const qint64 auto_consecutive_growth_hits_current
        = auto_process_dump_consecutive_growth_hits;
    const qint64 latest_rss = current_process_rss_bytes;
    const QString latest_rss_source = current_process_rss_source;
    const QString latest_rss_unavailable_reason
        = current_process_rss_unavailable_reason;
    const QString protocol_app = protocol_app_name;
    const qint64 protocol_pid = QCoreApplication::applicationPid();
    const QString protocol_session = protocol_session_id;
    const QString protocol_build = protocol_build_id;
    const QString protocol_version
        = debug_probe_core::protocol_version_string();
    const QStringList protocol_flags = protocol_debug_flags;
    const QString protocol_instrumentation_mode
        = cadence_mode_to_string(export_mode);
    active_process_report_export_watcher->setFuture(
        QtConcurrent::run([output_path, export_mode, sample_interval_ms,
                           auto_rss_growth_threshold_bytes, auto_cooldown_ms,
                           auto_baseline_rss_bytes,
                           auto_rss_growth_since_baseline_bytes,
                           auto_last_trigger_utc_ms, auto_cooldown_remaining_ms,
                           auto_consecutive_growth_hits_required,
                           auto_consecutive_growth_hits_current, latest_rss,
                           latest_rss_source, latest_rss_unavailable_reason,
                           protocol_app, protocol_pid, protocol_session,
                           protocol_build, protocol_version, protocol_flags,
                           protocol_instrumentation_mode, trigger]() {
            return write_process_memory_report_json(
                output_path, export_mode, sample_interval_ms,
                auto_rss_growth_threshold_bytes, auto_cooldown_ms,
                auto_baseline_rss_bytes, auto_rss_growth_since_baseline_bytes,
                auto_last_trigger_utc_ms, auto_cooldown_remaining_ms,
                auto_consecutive_growth_hits_required,
                auto_consecutive_growth_hits_current, latest_rss,
                latest_rss_source, latest_rss_unavailable_reason, protocol_app,
                protocol_pid, protocol_session, protocol_build,
                protocol_version, protocol_flags, protocol_instrumentation_mode,
                trigger
            );
        })
    );
}

bool resource_monitor::export_debug_snapshot_sync(
    const QString& output_path, QString* error_message
) const {
    const export_request_metadata metadata {
        .collector_sequence = collector_sequence,
        .cache_timeline_size
        = num_helpers::to_int(cache_timeline_entries.size()),
        .event_timeline_size
        = num_helpers::to_int(event_timeline_entries.size()),
        .geometry_timeline_size
        = num_helpers::to_int(geometry_timeline_entries.size()),
        .resize_history_size
        = num_helpers::to_int(resize_history_entries.size()),
        .resize_history_log_path = resize_history_log_stream_path,
        .latest_process_rss_bytes = current_process_rss_bytes,
        .process_memory_source = current_process_rss_source,
        .process_memory_unavailable_reason
        = current_process_rss_unavailable_reason,
        .process_memory_sample_interval_ms
        = process_sample_interval_ms_for_mode(),
        .auto_process_report_rss_growth_threshold_bytes
        = auto_process_dump_rss_growth_threshold_bytes_effective(),
        .auto_process_report_cooldown_ms
        = auto_process_dump_cooldown_ms_effective(),
        .auto_process_report_baseline_rss_bytes
        = auto_process_dump_baseline_rss_bytes,
        .auto_process_report_rss_growth_since_baseline_bytes
        = auto_process_dump_rss_growth_since_baseline_bytes(),
        .auto_process_report_last_trigger_utc_ms
        = auto_process_dump_last_trigger_ms,
        .auto_process_report_cooldown_remaining_ms
        = auto_process_dump_cooldown_remaining_ms(
            QDateTime::currentMSecsSinceEpoch()
        ),
        .auto_process_report_consecutive_growth_hits_required
        = auto_process_dump_consecutive_growth_hits_required(),
        .auto_process_report_consecutive_growth_hits_current
        = auto_process_dump_consecutive_growth_hits,
        .auto_process_report_window_ms
        = auto_process_dump_window_ms_effective(),
        .auto_process_report_window_max_exports
        = auto_process_dump_window_max_exports_effective(),
        .auto_process_report_window_exports_used
        = auto_process_dump_window_exports_used,
        .protocol_app_name = protocol_app_name,
        .protocol_process_id = QCoreApplication::applicationPid(),
        .protocol_session_id = protocol_session_id,
        .protocol_build_id = protocol_build_id,
        .protocol_version = debug_probe_core::protocol_version_string(),
        .protocol_debug_flags = protocol_debug_flags,
        .protocol_instrumentation_mode = cadence_mode_to_string(cadence_mode),
    };

    const QString error = write_debug_snapshot_json(
        output_path, metadata, cache_timeline_entries, event_timeline_entries,
        geometry_timeline_entries, resize_history_entries, cadence_mode
    );
    if (error_message != nullptr) {
        *error_message = error;
    }
    return error.isEmpty();
}

bool resource_monitor::export_process_memory_report_sync(
    const QString& output_path, QString* error_message
) const {
    const QString error = write_process_memory_report_json(
        output_path, cadence_mode, process_sample_interval_ms_for_mode(),
        auto_process_dump_rss_growth_threshold_bytes_effective(),
        auto_process_dump_cooldown_ms_effective(),
        auto_process_dump_baseline_rss_bytes,
        auto_process_dump_rss_growth_since_baseline_bytes(),
        auto_process_dump_last_trigger_ms,
        auto_process_dump_cooldown_remaining_ms(
            QDateTime::currentMSecsSinceEpoch()
        ),
        auto_process_dump_consecutive_growth_hits_required(),
        auto_process_dump_consecutive_growth_hits, current_process_rss_bytes,
        current_process_rss_source, current_process_rss_unavailable_reason,
        protocol_app_name, QCoreApplication::applicationPid(),
        protocol_session_id, protocol_build_id,
        debug_probe_core::protocol_version_string(), protocol_debug_flags,
        cadence_mode_to_string(cadence_mode),
        process_memory_report_trigger::manual_on_demand
    );
    if (error_message != nullptr) {
        *error_message = error;
    }
    return error.isEmpty();
}

void resource_monitor::on_cache_snapshot_updated(
    const raster_cache::debug_snapshot& snapshot
) {
    if (auto_process_dump_policy_override_for_tests) {
        last_process_sample_ms = 0;
    }
    refresh_process_memory_sample_if_needed();

    const cache_timeline_entry previous_entry = cache_timeline_entries.isEmpty()
        ? cache_timeline_entry()
        : cache_timeline_entries.constLast();

    const qint64 cache_ready_bytes_delta = cache_timeline_entries.isEmpty()
        ? 0
        : snapshot.ready_bytes - previous_entry.cache_snapshot.ready_bytes;
    maybe_trigger_auto_process_report(cache_ready_bytes_delta);
    const qint64 widget_local_display_delta = cache_timeline_entries.isEmpty()
        ? 0
        : snapshot.widget_local_display_bytes_estimated
            - previous_entry.cache_snapshot
                  .widget_local_display_bytes_estimated;
    const qint64 widget_local_display_bytes_materialized_interval
        = std::max<qint64>(0, widget_local_display_delta);
    const qint64 widget_local_display_bytes_released_interval
        = std::max<qint64>(0, -widget_local_display_delta);

    qint64 process_rss_bytes_delta = 0;
    if (!cache_timeline_entries.isEmpty()
        && previous_entry.process_rss_bytes >= 0
        && current_process_rss_bytes >= 0) {
        process_rss_bytes_delta
            = current_process_rss_bytes - previous_entry.process_rss_bytes;
    }

    const cache_timeline_entry entry {
        .collector_sequence = ++collector_sequence,
        .cache_snapshot = snapshot,
        .cache_entries_added_interval = snapshot.interval_deltas.entries_added,
        .cache_entries_removed_interval
        = snapshot.interval_deltas.entries_removed,
        .cache_bytes_added_interval = snapshot.interval_deltas.bytes_added,
        .cache_bytes_removed_interval = snapshot.interval_deltas.bytes_removed,
        .cache_images_added_interval = snapshot.interval_deltas.images_added,
        .cache_images_removed_interval
        = snapshot.interval_deltas.images_removed,
        .cache_accounted_ready_bytes_delta = cache_ready_bytes_delta,
        .widget_local_display_bytes_estimated_delta
        = widget_local_display_delta,
        .widget_local_display_bytes_materialized_interval
        = widget_local_display_bytes_materialized_interval,
        .widget_local_display_bytes_released_interval
        = widget_local_display_bytes_released_interval,
        .process_rss_bytes = current_process_rss_bytes,
        .process_rss_bytes_delta = process_rss_bytes_delta,
        .process_rss_bytes_growth_interval
        = std::max<qint64>(0, process_rss_bytes_delta),
        .process_rss_bytes_drop_interval
        = std::max<qint64>(0, -process_rss_bytes_delta),
    };

    cache_timeline_entries.push_back(entry);
    while (cache_timeline_entries.size() > timeline_limit) {
        cache_timeline_entries.removeFirst();
    }

    push_event_entry(
        event_timeline_entry::event_kind::cache_snapshot, QString()
    );
    publish_broadcaster_sample_batch(entry);

    if (!geometry_timeline_entries.isEmpty()) {
        maybe_finalize_pending_resize_transition(
            geometry_timeline_entries.constLast()
        );
    }

    emit cache_snapshot_collected(entry);
}

void resource_monitor::on_geometry_debug_snapshot_updated(
    const geometry_debug_snapshot& snapshot
) {
    geometry_timeline_entries.push_back(snapshot);
    while (geometry_timeline_entries.size() > timeline_limit) {
        geometry_timeline_entries.removeFirst();
    }

    maybe_finalize_pending_resize_transition(snapshot);
    emit geometry_snapshot_collected(snapshot);
}

void resource_monitor::on_resize_transition_recorded(
    const resize_transition_debug_event& event
) {
    if (pending_resize_transition_state.has_value()) {
        finalize_pending_resize_transition(event.timestamp_ms, -1);
    }

    const cache_timeline_entry latest_cache = has_cache_snapshot()
        ? latest_cache_snapshot()
        : cache_timeline_entry();
    const qint64 before_cache_accounted_ready_bytes
        = latest_cache.cache_snapshot.ready_bytes;
    const qint64 before_widget_local_display_bytes_estimated
        = latest_cache.cache_snapshot.widget_local_display_bytes_estimated;
    const qint64 before_process_rss_bytes = current_process_rss_bytes;
    const qint64 before_measured_accounted_gap_bytes
        = before_process_rss_bytes >= 0
        ? before_process_rss_bytes - before_cache_accounted_ready_bytes
        : 0;

    pending_resize_transition_state = pending_resize_transition {
        .transition = event,
        .before_process_rss_bytes = before_process_rss_bytes,
        .before_cache_accounted_ready_bytes
        = before_cache_accounted_ready_bytes,
        .before_widget_local_display_bytes_estimated
        = before_widget_local_display_bytes_estimated,
        .before_measured_accounted_gap_bytes
        = before_measured_accounted_gap_bytes,
    };

    maybe_finalize_pending_resize_transition(event.geometry_after_resize);
}

void resource_monitor::on_periodic_collection_tick() {
    if (observed_cache_service == nullptr) {
        return;
    }

    const qint64 previous_sample_timestamp_ms = last_process_sample_ms;
    refresh_process_memory_sample_if_needed();
    if (last_process_sample_ms <= previous_sample_timestamp_ms) {
        return;
    }

    on_cache_snapshot_updated(observed_cache_service->get_debug_snapshot());
}

void resource_monitor::maybe_finalize_pending_resize_transition(
    const geometry_debug_snapshot& snapshot
) {
    if (!pending_resize_transition_state.has_value()) {
        return;
    }

    const pending_resize_transition& pending = *pending_resize_transition_state;
    const bool window_size_applied
        = !pending.transition.new_window_size.isValid()
        || snapshot.window_size == pending.transition.new_window_size;
    const bool active_bucket_applied
        = pending.transition.new_active_bucket_px <= 0
        || snapshot.active_bucket_px == pending.transition.new_active_bucket_px;
    const bool no_prewarm_expected
        = pending.transition.new_warming_bucket_px <= 0
        && pending.transition.new_active_bucket_px
            == pending.transition.old_active_bucket_px;
    const bool prewarm_drained = snapshot.warming_generation_id <= 0
        && snapshot.warming_bucket_px <= 0;

    if (!window_size_applied || !active_bucket_applied
        || (!no_prewarm_expected && !prewarm_drained)) {
        return;
    }

    const qint64 transition_end_timestamp_ms = snapshot.timestamp_ms > 0
        ? snapshot.timestamp_ms
        : QDateTime::currentMSecsSinceEpoch();
    const qint64 prewarm_completion_ms = no_prewarm_expected
        ? 0
        : std::max<qint64>(
              0, transition_end_timestamp_ms - pending.transition.timestamp_ms
          );
    finalize_pending_resize_transition(
        transition_end_timestamp_ms, prewarm_completion_ms
    );
}

void resource_monitor::finalize_pending_resize_transition(
    qint64 transition_end_timestamp_ms, qint64 prewarm_completion_ms
) {
    if (!pending_resize_transition_state.has_value()) {
        return;
    }

    const pending_resize_transition pending = *pending_resize_transition_state;
    pending_resize_transition_state.reset();

    const cache_timeline_entry latest_cache = has_cache_snapshot()
        ? latest_cache_snapshot()
        : cache_timeline_entry();
    const qint64 after_cache_accounted_ready_bytes
        = latest_cache.cache_snapshot.ready_bytes;
    const qint64 after_widget_local_display_bytes_estimated
        = latest_cache.cache_snapshot.widget_local_display_bytes_estimated;
    const qint64 after_process_rss_bytes = current_process_rss_bytes;
    const qint64 after_measured_accounted_gap_bytes
        = after_process_rss_bytes >= 0
        ? after_process_rss_bytes - after_cache_accounted_ready_bytes
        : 0;

    const qint64 normalized_transition_end = transition_end_timestamp_ms > 0
        ? transition_end_timestamp_ms
        : QDateTime::currentMSecsSinceEpoch();

    const resize_history_entry entry {
        .collector_sequence = collector_sequence,
        .transition_start_timestamp_ms = pending.transition.timestamp_ms,
        .transition_end_timestamp_ms = normalized_transition_end,
        .prewarm_completion_ms = prewarm_completion_ms,
        .old_window_size = pending.transition.old_window_size,
        .new_window_size = pending.transition.new_window_size,
        .old_active_bucket_px = pending.transition.old_active_bucket_px,
        .new_active_bucket_px = pending.transition.new_active_bucket_px,
        .old_warming_bucket_px = pending.transition.old_warming_bucket_px,
        .new_warming_bucket_px = pending.transition.new_warming_bucket_px,
        .geometry_after_resize = pending.transition.geometry_after_resize,
        .before_process_rss_bytes = pending.before_process_rss_bytes,
        .after_process_rss_bytes = after_process_rss_bytes,
        .before_cache_accounted_ready_bytes
        = pending.before_cache_accounted_ready_bytes,
        .after_cache_accounted_ready_bytes = after_cache_accounted_ready_bytes,
        .before_widget_local_display_bytes_estimated
        = pending.before_widget_local_display_bytes_estimated,
        .after_widget_local_display_bytes_estimated
        = after_widget_local_display_bytes_estimated,
        .before_measured_accounted_gap_bytes
        = pending.before_measured_accounted_gap_bytes,
        .after_measured_accounted_gap_bytes
        = after_measured_accounted_gap_bytes,
    };

    resize_history_entries.push_back(entry);
    while (resize_history_entries.size() > timeline_limit) {
        resize_history_entries.removeFirst();
    }

    append_resize_history_entry_async(entry);
    push_event_entry(
        event_timeline_entry::event_kind::manual_marker,
        QStringLiteral("resize_transition_recorded")
    );
    emit resize_history_recorded(entry);
}

void resource_monitor::append_resize_history_entry_async(
    const resize_history_entry& entry
) {
    initialize_resize_history_log_stream_path_if_needed();
    if (resize_history_log_stream_path.isEmpty()) {
        return;
    }

    const QString output_path = resize_history_log_stream_path;
    const QByteArray line
        = (debug_probe_core::resize_history_entry_to_jsonl_line(entry)
           + QLatin1Char('\n'))
              .toUtf8();
    QThreadPool::globalInstance()->start([output_path, line]() {
        QFile file(output_path);
        if (!file.open(
                QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text
            )) {
            return;
        }
        file.write(line);
        file.flush();
    });
}

void resource_monitor::initialize_resize_history_log_stream_path_if_needed() {
    if (!resize_history_log_stream_path.isEmpty()) {
        return;
    }

    const QString app_data_dir
        = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString temp_location_dir
        = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString temp_path_dir = QDir::tempPath();

    QStringList candidate_base_dirs;
    if (!app_data_dir.isEmpty()) {
        candidate_base_dirs.push_back(app_data_dir);
    }
    if (!temp_location_dir.isEmpty()
        && !candidate_base_dirs.contains(temp_location_dir)) {
        candidate_base_dirs.push_back(temp_location_dir);
    }
    if (!temp_path_dir.isEmpty()
        && !candidate_base_dirs.contains(temp_path_dir)) {
        candidate_base_dirs.push_back(temp_path_dir);
    }

    for (const QString& candidate_dir : candidate_base_dirs) {
        QDir dir(candidate_dir);
        if (!dir.mkpath(QStringLiteral("monitor_resize_history"))) {
            continue;
        }

        const QString timestamp = QDateTime::currentDateTimeUtc().toString(
            QStringLiteral("yyyyMMdd_hhmmss")
        );
        resize_history_log_stream_path = dir.filePath(
            QStringLiteral("monitor_resize_history/resize_history_%1.jsonl")
                .arg(timestamp)
        );
        return;
    }
}

void resource_monitor::push_event_entry(
    event_timeline_entry::event_kind kind, const QString& label
) {
    const event_timeline_entry entry {
        .collector_sequence = collector_sequence,
        .kind = kind,
        .timestamp_ms = QDateTime::currentMSecsSinceEpoch(),
        .label = label,
    };

    event_timeline_entries.push_back(entry);
    while (event_timeline_entries.size() > timeline_limit) {
        event_timeline_entries.removeFirst();
    }

    emit event_recorded(entry);
    publish_broadcaster_event(entry);
}

QString resource_monitor::cadence_mode_to_string(debug_cadence_mode mode) {
    return debug_probe_core::cadence_mode_to_string(mode);
}

qint64 resource_monitor::broadcaster_monotonic_timestamp_ms() const {
    if (!broadcaster_monotonic_clock.isValid()) {
        return 0;
    }
    return broadcaster_monotonic_clock.elapsed();
}

debug_probe_core::protocol_identity
resource_monitor::broadcaster_protocol_identity() const {
    return debug_probe_core::protocol_identity {
        .app_name = protocol_app_name,
        .process_id = QCoreApplication::applicationPid(),
        .session_id = protocol_session_id,
        .build_id = protocol_build_id,
        .protocol_version = debug_probe_core::protocol_version_string(),
        .debug_flags = protocol_debug_flags,
        .instrumentation_mode = cadence_mode_to_string(cadence_mode),
    };
}

void resource_monitor::publish_broadcaster_session_start() {
    if (!is_debug_broadcaster_enabled() || telemetry_broadcaster == nullptr) {
        return;
    }

    const debug_probe_core::protocol_identity identity
        = broadcaster_protocol_identity();

    QJsonObject hello_payload;
    hello_payload.insert(
        QStringLiteral("session_start_utc_ms"),
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()
    );
    hello_payload.insert(
        QStringLiteral("debug_cadence_mode"),
        cadence_mode_to_string(cadence_mode)
    );
    hello_payload.insert(
        QStringLiteral("process_memory_sample_interval_ms"),
        process_sample_interval_ms_for_mode()
    );

    telemetry_broadcaster->publish_json(
        debug_probe_core::build_protocol_message_v1(
            QStringLiteral("hello"), identity,
            broadcaster_monotonic_timestamp_ms(), hello_payload
        ),
        debug_broadcaster::message_priority::high, false
    );

    QJsonObject capabilities_payload;
    capabilities_payload.insert(
        QStringLiteral("capabilities"),
        debug_probe_core::protocol_capabilities_v1()
    );
    telemetry_broadcaster->publish_json(
        debug_probe_core::build_protocol_message_v1(
            QStringLiteral("capabilities"), identity,
            broadcaster_monotonic_timestamp_ms(), capabilities_payload
        ),
        debug_broadcaster::message_priority::high, false
    );
}

void resource_monitor::publish_broadcaster_session_end(const QString& reason) {
    if (!is_debug_broadcaster_enabled() || telemetry_broadcaster == nullptr) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("reason"), reason);
    payload.insert(
        QStringLiteral("session_end_utc_ms"),
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()
    );
    payload.insert(QStringLiteral("collector_sequence"), collector_sequence);

    telemetry_broadcaster->publish_json(
        debug_probe_core::build_protocol_message_v1(
            QStringLiteral("goodbye"), broadcaster_protocol_identity(),
            broadcaster_monotonic_timestamp_ms(), payload
        ),
        debug_broadcaster::message_priority::high, false
    );
}

void resource_monitor::publish_broadcaster_sample_batch(
    const cache_timeline_entry& entry
) {
    if (!is_debug_broadcaster_enabled() || telemetry_broadcaster == nullptr) {
        return;
    }

    QJsonArray samples;
    auto append_int_sample
        = [&samples, &entry](const QString& metric_id, qint64 value) {
              QJsonObject sample;
              sample.insert(QStringLiteral("metric_id"), metric_id);
              sample.insert(QStringLiteral("value"), value);
              const QJsonObject metric_hint
                  = debug_probe_core::protocol_metric_hint_for_id_v1(metric_id);
              if (!metric_hint.isEmpty()) {
                  sample.insert(QStringLiteral("metric_hint"), metric_hint);
              }
              sample.insert(
                  QStringLiteral("collector_sequence"), entry.collector_sequence
              );
              sample.insert(
                  QStringLiteral("snapshot_sequence"),
                  entry.cache_snapshot.snapshot_sequence
              );
              samples.push_back(sample);
          };

    append_int_sample(
        QStringLiteral("cache_accounted_ready_bytes"),
        entry.cache_snapshot.ready_bytes
    );
    append_int_sample(
        QStringLiteral("widget_local_display_bytes_estimated"),
        entry.cache_snapshot.widget_local_display_bytes_estimated
    );
    append_int_sample(
        QStringLiteral("displayed_recent_entries"),
        entry.cache_snapshot.displayed_ready_entries
    );
    append_int_sample(
        QStringLiteral("cached_only_ready_entries"),
        entry.cache_snapshot.cached_only_ready_entries
    );
    if (has_geometry_snapshot()) {
        const geometry_debug_snapshot geometry = latest_geometry_snapshot();
        append_int_sample(
            QStringLiteral("active_generation_id"),
            geometry.active_generation_id
        );
        append_int_sample(
            QStringLiteral("warming_generation_id"),
            geometry.warming_generation_id
        );
    }

    if (entry.process_rss_bytes >= 0) {
        append_int_sample(
            QStringLiteral("process_memory_rss_bytes"), entry.process_rss_bytes
        );
        append_int_sample(
            QStringLiteral("measured_accounted_gap_bytes_derived"),
            entry.process_rss_bytes - entry.cache_snapshot.ready_bytes
        );

        if (entry.process_rss_bytes > 0) {
            QJsonObject ratio_sample;
            const QString ratio_metric_id
                = QStringLiteral("accounted_to_measured_ratio_percent_derived");
            ratio_sample.insert(QStringLiteral("metric_id"), ratio_metric_id);
            ratio_sample.insert(
                QStringLiteral("value"),
                (double(entry.cache_snapshot.ready_bytes) * 100.0)
                    / double(entry.process_rss_bytes)
            );
            const QJsonObject ratio_metric_hint
                = debug_probe_core::protocol_metric_hint_for_id_v1(
                    ratio_metric_id
                );
            if (!ratio_metric_hint.isEmpty()) {
                ratio_sample.insert(
                    QStringLiteral("metric_hint"), ratio_metric_hint
                );
            }
            ratio_sample.insert(
                QStringLiteral("collector_sequence"), entry.collector_sequence
            );
            ratio_sample.insert(
                QStringLiteral("snapshot_sequence"),
                entry.cache_snapshot.snapshot_sequence
            );
            samples.push_back(ratio_sample);
        }
    }

    QJsonObject payload;
    payload.insert(
        QStringLiteral("sample_count"), static_cast<qint64>(samples.size())
    );
    payload.insert(QStringLiteral("samples"), samples);
    payload.insert(
        QStringLiteral("telemetry_semantics"),
        debug_probe_core::snapshot_telemetry_semantics()
    );

    telemetry_broadcaster->publish_json(
        debug_probe_core::build_protocol_message_v1(
            QStringLiteral("sample_batch"), broadcaster_protocol_identity(),
            broadcaster_monotonic_timestamp_ms(), payload
        ),
        debug_broadcaster::message_priority::low, true
    );
}

void resource_monitor::publish_broadcaster_event(
    const event_timeline_entry& entry
) {
    if (!is_debug_broadcaster_enabled() || telemetry_broadcaster == nullptr) {
        return;
    }

    if (entry.kind == event_timeline_entry::event_kind::manual_marker) {
        QJsonObject payload;
        payload.insert(QStringLiteral("label"), entry.label);
        payload.insert(QStringLiteral("timestamp_ms"), entry.timestamp_ms);
        payload.insert(
            QStringLiteral("collector_sequence"), entry.collector_sequence
        );

        telemetry_broadcaster->publish_json(
            debug_probe_core::build_protocol_message_v1(
                QStringLiteral("marker"), broadcaster_protocol_identity(),
                broadcaster_monotonic_timestamp_ms(), payload
            ),
            debug_broadcaster::message_priority::high, false
        );
        return;
    }

    QJsonObject event_object;
    event_object.insert(
        QStringLiteral("kind"), QStringLiteral("cache_snapshot")
    );
    event_object.insert(QStringLiteral("timestamp_ms"), entry.timestamp_ms);
    event_object.insert(
        QStringLiteral("collector_sequence"), entry.collector_sequence
    );

    QJsonArray events;
    events.push_back(event_object);

    QJsonObject payload;
    payload.insert(
        QStringLiteral("event_count"), static_cast<qint64>(events.size())
    );
    payload.insert(QStringLiteral("events"), events);

    telemetry_broadcaster->publish_json(
        debug_probe_core::build_protocol_message_v1(
            QStringLiteral("event_batch"), broadcaster_protocol_identity(),
            broadcaster_monotonic_timestamp_ms(), payload
        ),
        debug_broadcaster::message_priority::low, true
    );
}

void resource_monitor::publish_broadcaster_snapshot_now() {
    if (!is_debug_broadcaster_enabled() || telemetry_broadcaster == nullptr) {
        return;
    }

    QJsonObject snapshot;
    snapshot.insert(QStringLiteral("collector_sequence"), collector_sequence);
    snapshot.insert(
        QStringLiteral("cache_timeline_size"),
        static_cast<qint64>(cache_timeline_entries.size())
    );
    snapshot.insert(
        QStringLiteral("event_timeline_size"),
        static_cast<qint64>(event_timeline_entries.size())
    );
    snapshot.insert(
        QStringLiteral("geometry_timeline_size"),
        static_cast<qint64>(geometry_timeline_entries.size())
    );
    snapshot.insert(
        QStringLiteral("resize_history_size"),
        static_cast<qint64>(resize_history_entries.size())
    );

    if (has_cache_snapshot()) {
        const cache_timeline_entry latest = latest_cache_snapshot();
        snapshot.insert(
            QStringLiteral("cache_accounted_ready_bytes"),
            latest.cache_snapshot.ready_bytes
        );
        snapshot.insert(
            QStringLiteral("widget_local_display_bytes_estimated"),
            latest.cache_snapshot.widget_local_display_bytes_estimated
        );
        snapshot.insert(
            QStringLiteral("displayed_recent_entries"),
            latest.cache_snapshot.displayed_ready_entries
        );
        snapshot.insert(
            QStringLiteral("cached_only_ready_entries"),
            latest.cache_snapshot.cached_only_ready_entries
        );
        snapshot.insert(
            QStringLiteral("process_memory_rss_bytes"), latest.process_rss_bytes
        );
    }

    if (has_geometry_snapshot()) {
        const geometry_debug_snapshot geometry = latest_geometry_snapshot();
        snapshot.insert(
            QStringLiteral("active_generation_id"),
            geometry.active_generation_id
        );
        snapshot.insert(
            QStringLiteral("warming_generation_id"),
            geometry.warming_generation_id
        );
        snapshot.insert(
            QStringLiteral("geometry"),
            debug_probe_core::geometry_snapshot_to_json(geometry)
        );
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("snapshot"), snapshot);
    payload.insert(
        QStringLiteral("telemetry_semantics"),
        debug_probe_core::snapshot_telemetry_semantics()
    );

    telemetry_broadcaster->publish_json(
        debug_probe_core::build_protocol_message_v1(
            QStringLiteral("snapshot"), broadcaster_protocol_identity(),
            broadcaster_monotonic_timestamp_ms(), payload
        ),
        debug_broadcaster::message_priority::high, false
    );
}

void resource_monitor::publish_broadcaster_warning(
    const QString& warning_code, const QString& warning_message
) {
    if (!is_debug_broadcaster_enabled() || telemetry_broadcaster == nullptr) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("warning_code"), warning_code);
    payload.insert(QStringLiteral("warning_message"), warning_message);
    payload.insert(
        QStringLiteral("timestamp_utc_ms"),
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()
    );

    telemetry_broadcaster->publish_json(
        debug_probe_core::build_protocol_message_v1(
            QStringLiteral("warning"), broadcaster_protocol_identity(),
            broadcaster_monotonic_timestamp_ms(), payload
        ),
        debug_broadcaster::message_priority::high, false
    );
}

void resource_monitor::maybe_trigger_auto_process_report(
    qint64 cache_accounted_ready_bytes_delta_hint
) {
    const bool has_measured_rss = current_process_rss_bytes >= 0;
    qint64 effective_growth_bytes = -1;
    if (has_measured_rss) {
        if (auto_process_dump_baseline_rss_bytes < 0) {
            auto_process_dump_baseline_rss_bytes = current_process_rss_bytes;
        } else if (current_process_rss_bytes
                   < auto_process_dump_baseline_rss_bytes) {
            auto_process_dump_baseline_rss_bytes = current_process_rss_bytes;
        }

        effective_growth_bytes = std::max<qint64>(
            0, current_process_rss_bytes - auto_process_dump_baseline_rss_bytes
        );
    } else {
        auto_process_dump_baseline_rss_bytes = -1;
    }

    if (auto_process_dump_policy_override_for_tests
        && cache_accounted_ready_bytes_delta_hint > 0) {
        effective_growth_bytes = std::max(
            effective_growth_bytes, cache_accounted_ready_bytes_delta_hint
        );
    }

    if (effective_growth_bytes
        < auto_process_dump_rss_growth_threshold_bytes_effective()) {
        auto_process_dump_consecutive_growth_hits = 0;
        return;
    }

    ++auto_process_dump_consecutive_growth_hits;
    if (auto_process_dump_consecutive_growth_hits
        < auto_process_dump_consecutive_growth_hits_required()) {
        return;
    }

    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    if (auto_process_dump_last_trigger_ms > 0
        && (now_ms - auto_process_dump_last_trigger_ms)
            < auto_process_dump_cooldown_ms_effective()) {
        auto_process_dump_consecutive_growth_hits = 0;
        return;
    }

    const qint64 export_window_ms = auto_process_dump_window_ms_effective();
    if (auto_process_dump_window_start_ms <= 0
        || (now_ms - auto_process_dump_window_start_ms) >= export_window_ms) {
        auto_process_dump_window_start_ms = now_ms;
        auto_process_dump_window_exports_used = 0;
    }

    if (auto_process_dump_window_exports_used
        >= auto_process_dump_window_max_exports_effective()) {
        auto_process_dump_consecutive_growth_hits = 0;
        return;
    }

    if (active_process_report_export_watcher != nullptr) {
        return;
    }

    const QString app_data_dir
        = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString temp_location_dir
        = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString temp_path_dir = QDir::tempPath();

    QString output_base_dir;
    QStringList candidate_base_dirs;
    if (!app_data_dir.isEmpty()) {
        candidate_base_dirs.push_back(app_data_dir);
    }
    if (!temp_location_dir.isEmpty()
        && !candidate_base_dirs.contains(temp_location_dir)) {
        candidate_base_dirs.push_back(temp_location_dir);
    }
    if (!temp_path_dir.isEmpty()
        && !candidate_base_dirs.contains(temp_path_dir)) {
        candidate_base_dirs.push_back(temp_path_dir);
    }

    for (const QString& candidate_dir : candidate_base_dirs) {
        QDir dir(candidate_dir);
        if (!dir.mkpath(QStringLiteral("monitor_auto_dumps"))) {
            continue;
        }
        output_base_dir = candidate_dir;
        break;
    }

    if (output_base_dir.isEmpty()) {
        auto_process_dump_consecutive_growth_hits = 0;
        return;
    }

    QDir dir(output_base_dir);
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd_hhmmss_zzz")
    );
    const QString output_path = dir.filePath(
        QStringLiteral("monitor_auto_dumps/process_memory_auto_%1.json")
            .arg(timestamp)
    );

    auto_process_dump_last_trigger_ms = now_ms;
    if (has_measured_rss) {
        auto_process_dump_baseline_rss_bytes = current_process_rss_bytes;
    }
    auto_process_dump_consecutive_growth_hits = 0;
    ++auto_process_dump_window_exports_used;
    push_event_entry(
        event_timeline_entry::event_kind::manual_marker,
        QStringLiteral("auto_export_process_memory_detail")
    );
    export_process_memory_report_async(
        output_path, process_memory_report_trigger::threshold_rss_growth
    );
}

void resource_monitor::refresh_process_memory_sample_if_needed() {
    const qint64 process_sample_interval_ms
        = process_sample_interval_ms_for_mode();
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    if (last_process_sample_ms > 0
        && (now_ms - last_process_sample_ms) < process_sample_interval_ms) {
        return;
    }

    last_process_sample_ms = now_ms;

    const process_memory_sample_result sample = []() {
        process_memory_sample_result result;
        QFile status_file(QStringLiteral("/proc/self/status"));
        if (!status_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            result.unavailable_reason = process_memory_unavailable_reason();
            return result;
        }

        QTextStream stream(&status_file);
        while (!stream.atEnd()) {
            const QString line = stream.readLine();
            if (!line.startsWith(QStringLiteral("VmRSS:"))) {
                continue;
            }

            const qint64 rss_bytes
                = parse_status_kb_line(line, QStringLiteral("VmRSS:"));
            if (rss_bytes < 0) {
                result.unavailable_reason = process_memory_unavailable_reason();
                return result;
            }

            result.rss_bytes = rss_bytes;
            result.source = QStringLiteral("proc_status_vm_rss");
            return result;
        }

        result.unavailable_reason = process_memory_unavailable_reason();
        return result;
    }();

    if (sample.rss_bytes < 0) {
        current_process_rss_bytes = -1;
        current_process_rss_source = process_memory_source_for_rss(-1);
        current_process_rss_unavailable_reason = sample.unavailable_reason;
        return;
    }

    current_process_rss_bytes = sample.rss_bytes;
    current_process_rss_source = sample.source;
    current_process_rss_unavailable_reason.clear();
}

qint64 resource_monitor::process_sample_interval_ms_for_mode() const {
    return debug_probe_core::process_sample_interval_ms_for_mode(cadence_mode);
}

qint64
resource_monitor::auto_process_dump_rss_growth_since_baseline_bytes() const {
    if (auto_process_dump_baseline_rss_bytes < 0
        || current_process_rss_bytes < 0) {
        return 0;
    }

    return std::max<qint64>(
        0, current_process_rss_bytes - auto_process_dump_baseline_rss_bytes
    );
}

qint64
resource_monitor::auto_process_dump_cooldown_remaining_ms(qint64 now_ms) const {
    if (auto_process_dump_last_trigger_ms <= 0) {
        return 0;
    }

    const qint64 elapsed_ms
        = std::max<qint64>(0, now_ms - auto_process_dump_last_trigger_ms);
    const qint64 cooldown_ms = auto_process_dump_cooldown_ms_effective();
    return std::max<qint64>(0, cooldown_ms - elapsed_ms);
}

qint64
resource_monitor::auto_process_dump_consecutive_growth_hits_required() const {
    if (auto_process_dump_policy_override_for_tests) {
        return auto_process_dump_consecutive_growth_hits_required_override;
    }
    return debug_probe_core::auto_process_report_policy_for_mode(cadence_mode)
        .consecutive_growth_hits_required;
}

qint64 resource_monitor::auto_process_dump_window_ms_effective() const {
    return debug_probe_core::auto_process_report_policy_for_mode(cadence_mode)
        .window_ms;
}

qint64
resource_monitor::auto_process_dump_window_max_exports_effective() const {
    return debug_probe_core::auto_process_report_policy_for_mode(cadence_mode)
        .window_max_exports;
}

qint64 resource_monitor::
    auto_process_dump_rss_growth_threshold_bytes_effective() const {
    if (auto_process_dump_policy_override_for_tests) {
        return auto_process_dump_rss_growth_threshold_bytes;
    }
    return debug_probe_core::auto_process_report_policy_for_mode(cadence_mode)
        .rss_growth_threshold_bytes;
}

qint64 resource_monitor::auto_process_dump_cooldown_ms_effective() const {
    if (auto_process_dump_policy_override_for_tests) {
        return auto_process_dump_cooldown_ms;
    }
    return debug_probe_core::auto_process_report_policy_for_mode(cadence_mode)
        .cooldown_ms;
}
