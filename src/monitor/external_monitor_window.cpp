#include "monitor/external_monitor_window.hpp"

#include "monitor/debug_probe_core.hpp"
#include "monitor/monitor_shared_helpers.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocalSocket>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int chart_history_limit = 1024;
constexpr int marker_diff_history_limit = 16;

QString connection_status_label_for_state(const QLocalSocket* socket) {
    if (socket == nullptr) {
        return QStringLiteral("Disconnected");
    }

    switch (socket->state()) {
    case QLocalSocket::ConnectedState:
        return QStringLiteral("Connected");
    case QLocalSocket::ConnectingState:
        return QStringLiteral("Connecting");
    case QLocalSocket::ClosingState:
        return QStringLiteral("Closing");
    case QLocalSocket::UnconnectedState:
    default:
        return QStringLiteral("Disconnected");
    }
}

QString preferred_log_base_dir() {
    const QString app_data_dir
        = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!app_data_dir.isEmpty()) {
        return app_data_dir;
    }

    const QString temp_location
        = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (!temp_location.isEmpty()) {
        return temp_location;
    }

    return QDir::tempPath();
}

void trim_series_to_limit(QVector<double>* series) {
    monitor_shared::trim_series(series, chart_history_limit);
}

} // namespace

external_monitor_window::external_monitor_window(QWidget* parent)
    : QMainWindow(parent)
    , endpoint_input(nullptr)
    , connection_status_label(nullptr)
    , session_status_label(nullptr)
    , leak_status_label(nullptr)
    , log_path_label(nullptr)
    , connect_button(nullptr)
    , disconnect_button(nullptr)
    , export_charts_button(nullptr)
    , primary_memory_chart(nullptr)
    , leak_signal_chart(nullptr)
    , events_text(nullptr)
    , snapshot_text(nullptr)
    , warnings_text(nullptr)
    , connect_timeout_timer(nullptr)
    , socket(nullptr)
    , pending_read_buffer()
    , current_endpoint_path()
    , history_log_path()
    , line_counter(0)
    , warning_counter(0)
    , marker_counter(0)
    , snapshot_counter(0)
    , monotonic_growth_suspicion(false)
    , latest_metric_point()
    , high_water_point()
    , settle_baseline_point()
    , settle_baseline_valid(false)
    , metric_hints_by_id()
    , reported_metric_hint_warnings()
    , marker_history()
    , marker_cache_diffs()
    , series_cache_mib()
    , series_widget_mib()
    , series_rss_mib()
    , series_gap_mib()
    , series_high_water_cache_mib()
    , series_baseline_delta_mib() {
    setWindowTitle(QStringLiteral("External monitor"));

    auto* root = new QWidget(this);
    auto* root_layout = new QVBoxLayout(root);
    root_layout->setContentsMargins(8, 8, 8, 8);
    root_layout->setSpacing(8);

    auto* controls_row = new QHBoxLayout;
    controls_row->setSpacing(6);

    auto* endpoint_label = new QLabel(QStringLiteral("Endpoint:"), root);
    endpoint_input = new QLineEdit(root);
    endpoint_input->setPlaceholderText(QStringLiteral("/tmp/cppr_...sock"));
    connect_button = new QPushButton(QStringLiteral("Connect"), root);
    disconnect_button = new QPushButton(QStringLiteral("Disconnect"), root);
    export_charts_button
        = new QPushButton(QStringLiteral("Save chart image"), root);
    disconnect_button->setEnabled(false);

    controls_row->addWidget(endpoint_label);
    controls_row->addWidget(endpoint_input, 1);
    controls_row->addWidget(connect_button);
    controls_row->addWidget(disconnect_button);
    controls_row->addWidget(export_charts_button);
    root_layout->addLayout(controls_row);

    auto* status_row = new QHBoxLayout;
    status_row->setSpacing(10);
    connection_status_label = new QLabel(root);
    session_status_label = new QLabel(root);
    leak_status_label = new QLabel(root);
    log_path_label = new QLabel(root);
    status_row->addWidget(connection_status_label, 1);
    status_row->addWidget(session_status_label, 2);
    status_row->addWidget(leak_status_label, 2);
    root_layout->addLayout(status_row);
    root_layout->addWidget(log_path_label);

    auto* semantics_glossary = new QLabel(
        QStringLiteral(
            "Glossary: measured = OS source, accounted = owned object bytes, "
            "estimated = display heuristic, derived = diagnostics."
        ),
        root
    );
    semantics_glossary->setWordWrap(true);
    semantics_glossary->setToolTip(QStringLiteral(
        "Derived lines are leak-analysis aids, not direct ownership classes."
    ));
    root_layout->addWidget(semantics_glossary);
    auto* series_legend = new QLabel(
        QStringLiteral(
            "Color legend: "
            "<span style='color:#0072B2'>■</span> cache-accounted, "
            "<span style='color:#E69F00'>■</span> widget-local estimated, "
            "<span style='color:#009E73'>■</span> process RSS measured, "
            "<span style='color:#D55E00'>■</span> measured-accounted gap "
            "derived, "
            "<span style='color:#CC79A7'>■</span> high-water/baseline derived."
        ),
        root
    );
    series_legend->setWordWrap(true);
    series_legend->setToolTip(QStringLiteral(
        "Legend remains visible even when historical series are decimated."
    ));
    root_layout->addWidget(series_legend);

    auto* tabs = new QTabWidget(root);
    auto* dashboard_tab = new QWidget(tabs);
    auto* dashboard_layout = new QVBoxLayout(dashboard_tab);
    dashboard_layout->setContentsMargins(4, 4, 4, 4);
    dashboard_layout->setSpacing(8);

    primary_memory_chart = new monitor_line_chart_widget(dashboard_tab);
    primary_memory_chart->set_title(QStringLiteral(
        "Primary memory lines (measured/accounted/estimated/derived)"
    ));
    primary_memory_chart->set_unit_label(QStringLiteral("MiB"));
    primary_memory_chart->set_x_axis_label(
        QStringLiteral("sample index (oldest -> newest)")
    );
    primary_memory_chart->setToolTip(
        QStringLiteral("Hover a line point for value and sample index details.")
    );
    dashboard_layout->addWidget(primary_memory_chart, 3);

    leak_signal_chart = new monitor_line_chart_widget(dashboard_tab);
    leak_signal_chart->set_title(QStringLiteral(
        "Leak-oriented derived lines (high-water / baseline delta)"
    ));
    leak_signal_chart->set_unit_label(QStringLiteral("MiB"));
    leak_signal_chart->set_x_axis_label(
        QStringLiteral("sample index (oldest -> newest)")
    );
    leak_signal_chart->setToolTip(QStringLiteral(
        "Derived leak diagnostics across markers and settle baselines."
    ));
    dashboard_layout->addWidget(leak_signal_chart, 2);
    tabs->addTab(dashboard_tab, QStringLiteral("Dashboard"));

    auto* events_tab = new QWidget(tabs);
    auto* events_layout = new QVBoxLayout(events_tab);
    events_layout->setContentsMargins(4, 4, 4, 4);
    events_layout->setSpacing(6);
    events_text = new QPlainTextEdit(events_tab);
    events_text->setReadOnly(true);
    events_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    events_text->setMaximumBlockCount(2048);
    events_layout->addWidget(events_text);
    tabs->addTab(events_tab, QStringLiteral("Events"));

    auto* snapshot_tab = new QWidget(tabs);
    auto* snapshot_layout = new QVBoxLayout(snapshot_tab);
    snapshot_layout->setContentsMargins(4, 4, 4, 4);
    snapshot_layout->setSpacing(6);
    snapshot_text = new QPlainTextEdit(snapshot_tab);
    snapshot_text->setReadOnly(true);
    snapshot_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    snapshot_layout->addWidget(snapshot_text);
    tabs->addTab(snapshot_tab, QStringLiteral("Snapshot"));

    auto* warnings_tab = new QWidget(tabs);
    auto* warnings_layout = new QVBoxLayout(warnings_tab);
    warnings_layout->setContentsMargins(4, 4, 4, 4);
    warnings_layout->setSpacing(6);
    warnings_text = new QPlainTextEdit(warnings_tab);
    warnings_text->setReadOnly(true);
    warnings_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    warnings_text->setMaximumBlockCount(2048);
    warnings_layout->addWidget(warnings_text);
    tabs->addTab(warnings_tab, QStringLiteral("Warnings"));

    root_layout->addWidget(tabs, 1);
    setCentralWidget(root);

    QObject::connect(
        connect_button, &QPushButton::clicked, this,
        &external_monitor_window::on_connect_clicked
    );
    QObject::connect(
        disconnect_button, &QPushButton::clicked, this,
        &external_monitor_window::on_disconnect_clicked
    );
    QObject::connect(
        export_charts_button, &QPushButton::clicked, this,
        &external_monitor_window::on_export_charts_clicked
    );

    update_status_labels();
    update_primary_memory_chart();
    update_leak_signal_chart();
}

external_monitor_window::~external_monitor_window() {
    disconnect_from_endpoint();
}

void external_monitor_window::set_initial_endpoint(
    const QString& endpoint_path
) {
    if (endpoint_input != nullptr) {
        endpoint_input->setText(endpoint_path);
    }
    if (!endpoint_path.trimmed().isEmpty()) {
        connect_to_endpoint(endpoint_path.trimmed());
    }
}

void external_monitor_window::on_connect_clicked() {
    if (endpoint_input == nullptr) {
        return;
    }
    connect_to_endpoint(endpoint_input->text().trimmed());
}

void external_monitor_window::on_disconnect_clicked() {
    disconnect_from_endpoint();
}

void external_monitor_window::on_export_charts_clicked() {
    if (primary_memory_chart == nullptr || leak_signal_chart == nullptr) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd_hhmmss")
    );
    QString output_path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save chart image"),
        QStringLiteral("external_monitor_charts_%1.png").arg(timestamp),
        QStringLiteral("PNG files (*.png)")
    );
    if (output_path.isEmpty()) {
        return;
    }
    if (QFileInfo(output_path).suffix().isEmpty()) {
        output_path += QStringLiteral(".png");
    }

    const QVector<QPixmap> charts = {
        primary_memory_chart->grab(),
        leak_signal_chart->grab(),
    };
    for (const QPixmap& chart : charts) {
        if (chart.isNull()) {
            return;
        }
    }

    constexpr int spacing_px = 10;
    int width_px = 0;
    int height_px = 0;
    for (qsizetype index = 0; index < charts.size(); ++index) {
        width_px = std::max(width_px, charts.at(index).width());
        height_px += charts.at(index).height();
        if (index + 1 < charts.size()) {
            height_px += spacing_px;
        }
    }

    if (width_px <= 0 || height_px <= 0) {
        return;
    }

    QPixmap composed(width_px, height_px);
    composed.fill(primary_memory_chart->palette().color(QPalette::Base));

    QPainter painter(&composed);
    int y_offset = 0;
    for (qsizetype index = 0; index < charts.size(); ++index) {
        const QPixmap& chart = charts.at(index);
        painter.drawPixmap(0, y_offset, chart);
        y_offset += chart.height();
        if (index + 1 < charts.size()) {
            y_offset += spacing_px;
        }
    }
    painter.end();
    composed.save(output_path, "PNG");
}

void external_monitor_window::on_socket_connected() {
    if (connect_timeout_timer != nullptr) {
        connect_timeout_timer->stop();
    }
    append_event_line(
        QStringLiteral("[%1] connected to %2")
            .arg(monitor_shared::utc_now_text(), current_endpoint_path)
    );
    update_status_labels();
}

void external_monitor_window::on_socket_disconnected() {
    if (connect_timeout_timer != nullptr) {
        connect_timeout_timer->stop();
    }
    append_event_line(
        QStringLiteral("[%1] disconnected").arg(monitor_shared::utc_now_text())
    );
    update_status_labels();
}

void external_monitor_window::on_socket_ready_read() {
    if (socket == nullptr) {
        return;
    }

    pending_read_buffer.append(socket->readAll());
    while (true) {
        const qsizetype newline_index = pending_read_buffer.indexOf('\n');
        if (newline_index < 0) {
            break;
        }

        const QByteArray line
            = pending_read_buffer.left(newline_index).trimmed();
        pending_read_buffer.remove(0, newline_index + 1);
        if (line.isEmpty()) {
            continue;
        }
        process_incoming_line(line);
    }
}

void external_monitor_window::on_socket_error() {
    if (connect_timeout_timer != nullptr) {
        connect_timeout_timer->stop();
    }
    if (socket != nullptr) {
        append_warning_line(
            QStringLiteral("[%1] socket error: %2")
                .arg(monitor_shared::utc_now_text(), socket->errorString())
        );
    }
    disconnect_from_endpoint();
}

void external_monitor_window::on_connect_timeout() {
    if (socket == nullptr || socket->state() == QLocalSocket::ConnectedState) {
        return;
    }

    append_warning_line(
        QStringLiteral("[%1] failed to connect to %2: timed out after 2000 ms")
            .arg(monitor_shared::utc_now_text(), current_endpoint_path)
    );
    disconnect_from_endpoint();
}

void external_monitor_window::connect_to_endpoint(
    const QString& endpoint_path
) {
    if (endpoint_path.isEmpty()) {
        append_warning_line(QStringLiteral("[%1] endpoint is empty")
                                .arg(monitor_shared::utc_now_text()));
        return;
    }

    disconnect_from_endpoint();
    metric_hints_by_id.clear();
    reported_metric_hint_warnings.clear();

    if (connect_timeout_timer == nullptr) {
        connect_timeout_timer = new QTimer(this);
        connect_timeout_timer->setSingleShot(true);
        QObject::connect(
            connect_timeout_timer, &QTimer::timeout, this,
            &external_monitor_window::on_connect_timeout
        );
    }

    socket = new QLocalSocket(this);
    current_endpoint_path = endpoint_path;
    QObject::connect(
        socket, &QLocalSocket::connected, this,
        &external_monitor_window::on_socket_connected
    );
    QObject::connect(
        socket, &QLocalSocket::disconnected, this,
        &external_monitor_window::on_socket_disconnected
    );
    QObject::connect(
        socket, &QLocalSocket::readyRead, this,
        &external_monitor_window::on_socket_ready_read
    );
    QObject::connect(
        socket, &QLocalSocket::errorOccurred, this,
        &external_monitor_window::on_socket_error
    );

    socket->connectToServer(endpoint_path);
    connect_timeout_timer->start(2000);

    if (endpoint_input != nullptr) {
        endpoint_input->setText(endpoint_path);
    }
    update_status_labels();
}

void external_monitor_window::disconnect_from_endpoint() {
    if (connect_timeout_timer != nullptr) {
        connect_timeout_timer->stop();
    }
    if (socket != nullptr) {
        socket->disconnect(this);
        socket->close();
        socket->deleteLater();
        socket = nullptr;
    }
    current_endpoint_path.clear();
    pending_read_buffer.clear();
    metric_hints_by_id.clear();
    reported_metric_hint_warnings.clear();
    update_status_labels();
}

void external_monitor_window::append_raw_line_to_history(
    const QByteArray& compact_json_line
) {
    ensure_history_log_path(QString());
    if (history_log_path.isEmpty()) {
        return;
    }

    QFile file(history_log_path);
    if (!file.open(
            QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text
        )) {
        return;
    }
    file.write(compact_json_line);
    file.write("\n");
}

void external_monitor_window::ensure_history_log_path(
    const QString& session_hint
) {
    if (!history_log_path.isEmpty()) {
        return;
    }

    const QString base_dir = preferred_log_base_dir();
    if (base_dir.isEmpty()) {
        return;
    }

    QDir dir(base_dir);
    if (!dir.mkpath(QStringLiteral("external_monitor_history"))) {
        return;
    }

    QString session_token = session_hint.trimmed();
    if (session_token.isEmpty()) {
        session_token = QStringLiteral("unknown_session");
    }
    session_token.replace(QChar('/'), QChar('_'));
    session_token.replace(QChar(' '), QChar('_'));
    session_token = session_token.left(40);

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd_hhmmss")
    );
    history_log_path
        = dir.filePath(QStringLiteral("external_monitor_history/%1_%2.jsonl")
                           .arg(session_token, timestamp));
    update_status_labels();
}

void external_monitor_window::process_incoming_line(
    const QByteArray& compact_json_line
) {
    const QJsonDocument document = QJsonDocument::fromJson(compact_json_line);
    if (!document.isObject()) {
        append_warning_line(QStringLiteral("[%1] invalid JSON message")
                                .arg(monitor_shared::utc_now_text()));
        return;
    }

    append_raw_line_to_history(compact_json_line);
    handle_protocol_message(document.object());
}

void external_monitor_window::handle_protocol_message(
    const QJsonObject& message
) {
    ++line_counter;
    const QString family = message_family(message);
    if (family.isEmpty()) {
        append_warning_line(
            QStringLiteral("[%1] message missing protocol family")
                .arg(monitor_shared::utc_now_text())
        );
        return;
    }

    if (family == QStringLiteral("hello")) {
        handle_hello(message);
    } else if (family == QStringLiteral("capabilities")) {
        handle_capabilities(message);
    } else if (family == QStringLiteral("sample_batch")) {
        handle_sample_batch(message);
    } else if (family == QStringLiteral("event_batch")) {
        handle_event_batch(message);
    } else if (family == QStringLiteral("snapshot")) {
        handle_snapshot(message);
    } else if (family == QStringLiteral("marker")) {
        handle_marker(message);
    } else if (family == QStringLiteral("warning")) {
        handle_warning(message);
    } else if (family == QStringLiteral("goodbye")) {
        append_event_line(QStringLiteral("[%1] goodbye message")
                              .arg(monitor_shared::utc_now_text()));
    }

    update_status_labels();
}

void external_monitor_window::handle_hello(const QJsonObject& message) {
    const QJsonObject identity = protocol_identity(message);
    ensure_history_log_path(
        identity.value(QStringLiteral("session")).toString()
    );
    append_event_line(
        QStringLiteral("[%1] hello app=%2 pid=%3 session=%4 build=%5 mode=%6")
            .arg(
                monitor_shared::utc_now_text(),
                identity.value(QStringLiteral("app")).toString(),
                QString::number(
                    identity.value(QStringLiteral("pid")).toInteger()
                ),
                identity.value(QStringLiteral("session")).toString(),
                identity.value(QStringLiteral("build")).toString(),
                identity.value(QStringLiteral("instrumentation_mode"))
                    .toString()
            )
    );
}

void external_monitor_window::handle_capabilities(const QJsonObject& message) {
    const QJsonObject capabilities
        = message.value(QStringLiteral("capabilities")).toObject();
    const QJsonArray catalog
        = capabilities.value(QStringLiteral("metric_catalog")).toArray();
    if (catalog.isEmpty()) {
        append_parity_warning(
            QStringLiteral("capabilities_missing_metric_catalog"),
            QStringLiteral("metric catalog is empty")
        );
        return;
    }

    metric_hints_by_id.clear();
    for (const QJsonValue& value : catalog) {
        const QJsonObject metric = value.toObject();
        const QString id = metric.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            metric_hints_by_id.insert(id, metric);
        }
    }

    const QJsonArray expected_catalog
        = debug_probe_core::protocol_metric_catalog_v1();
    for (const QJsonValue& value : expected_catalog) {
        const QJsonObject expected = value.toObject();
        const QString id = expected.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            continue;
        }
        if (!metric_hints_by_id.contains(id)) {
            append_parity_warning(
                QStringLiteral("metric_missing_%1").arg(id),
                QStringLiteral("capabilities missing expected metric '%1'")
                    .arg(id)
            );
            continue;
        }

        const QJsonObject incoming = metric_hints_by_id.value(id);
        const QJsonArray required_fields
            = debug_probe_core::protocol_required_metric_hint_fields_v1();
        for (const QJsonValue& field_value : required_fields) {
            const QString field = field_value.toString();
            if (field.isEmpty()) {
                continue;
            }
            if (incoming.value(field).toString()
                != expected.value(field).toString()) {
                append_parity_warning(
                    QStringLiteral("metric_hint_drift_%1_%2").arg(id, field),
                    QStringLiteral(
                        "metric '%1' hint '%2' drift incoming='%3' "
                        "expected='%4'"
                    )
                        .arg(
                            id, field, incoming.value(field).toString(),
                            expected.value(field).toString()
                        )
                );
            }
        }
    }
}

void external_monitor_window::handle_sample_batch(const QJsonObject& message) {
    const QJsonArray samples
        = message.value(QStringLiteral("samples")).toArray();
    if (samples.isEmpty()) {
        return;
    }

    for (const QJsonValue& sample_value : samples) {
        const QJsonObject sample = sample_value.toObject();
        const QString metric_id
            = sample.value(QStringLiteral("metric_id")).toString();
        if (metric_id.isEmpty()) {
            continue;
        }

        QJsonObject metric_hint
            = sample.value(QStringLiteral("metric_hint")).toObject();
        if (metric_hint.isEmpty()) {
            metric_hint = metric_hints_by_id.value(metric_id);
        }
        if (metric_hint.isEmpty()) {
            const QString warning_key
                = QStringLiteral("missing_hint_%1").arg(metric_id);
            if (!reported_metric_hint_warnings.contains(warning_key)) {
                reported_metric_hint_warnings.insert(warning_key);
                append_parity_warning(
                    QStringLiteral("sample_metric_hint_missing_%1")
                        .arg(metric_id),
                    QStringLiteral(
                        "sample metric '%1' is missing required hint metadata"
                    )
                        .arg(metric_id)
                );
            }
            continue;
        }

        const QJsonArray required_fields
            = debug_probe_core::protocol_required_metric_hint_fields_v1();
        for (const QJsonValue& field_value : required_fields) {
            const QString field = field_value.toString();
            if (field.isEmpty()) {
                continue;
            }
            if (!metric_hint.contains(field)
                || metric_hint.value(field).toString().isEmpty()) {
                const QString warning_key
                    = QStringLiteral("hint_field_%1_%2").arg(metric_id, field);
                if (reported_metric_hint_warnings.contains(warning_key)) {
                    continue;
                }
                reported_metric_hint_warnings.insert(warning_key);
                append_parity_warning(
                    QStringLiteral("sample_metric_hint_field_missing")
                        + QStringLiteral("_%1_%2").arg(metric_id, field),
                    QStringLiteral(
                        "sample metric '%1' hint field '%2' is missing"
                    )
                        .arg(metric_id, field)
                );
            }
        }
    }

    const metric_point sample_point
        = debug_probe_core::metric_point_from_sample_batch_v1(samples);
    debug_probe_core::merge_metric_point_v1(&latest_metric_point, sample_point);

    if (sample_point.cache_accounted_ready_bytes >= 0) {
        high_water_point.cache_accounted_ready_bytes = std::max(
            high_water_point.cache_accounted_ready_bytes,
            sample_point.cache_accounted_ready_bytes
        );
    }
    if (sample_point.widget_local_display_bytes_estimated >= 0) {
        high_water_point.widget_local_display_bytes_estimated = std::max(
            high_water_point.widget_local_display_bytes_estimated,
            sample_point.widget_local_display_bytes_estimated
        );
    }
    if (sample_point.process_memory_rss_bytes >= 0) {
        high_water_point.process_memory_rss_bytes = std::max(
            high_water_point.process_memory_rss_bytes,
            sample_point.process_memory_rss_bytes
        );
    }
    if (sample_point.measured_accounted_gap_bytes_derived >= 0) {
        high_water_point.measured_accounted_gap_bytes_derived = std::max(
            high_water_point.measured_accounted_gap_bytes_derived,
            sample_point.measured_accounted_gap_bytes_derived
        );
    }

    series_cache_mib.push_back(
        monitor_shared::to_mib_nullable(
            latest_metric_point.cache_accounted_ready_bytes
        )
    );
    series_widget_mib.push_back(
        monitor_shared::to_mib_nullable(
            latest_metric_point.widget_local_display_bytes_estimated
        )
    );
    series_rss_mib.push_back(
        monitor_shared::to_mib_nullable(
            latest_metric_point.process_memory_rss_bytes
        )
    );
    series_gap_mib.push_back(
        monitor_shared::to_mib_nullable(
            latest_metric_point.measured_accounted_gap_bytes_derived
        )
    );
    series_high_water_cache_mib.push_back(
        monitor_shared::to_mib_nullable(
            high_water_point.cache_accounted_ready_bytes
        )
    );
    series_baseline_delta_mib.push_back(
        settle_baseline_valid
            ? monitor_shared::to_mib(
                  latest_metric_point.cache_accounted_ready_bytes
                  - settle_baseline_point.cache_accounted_ready_bytes
              )
            : std::numeric_limits<double>::quiet_NaN()
    );

    trim_series_to_limit(&series_cache_mib);
    trim_series_to_limit(&series_widget_mib);
    trim_series_to_limit(&series_rss_mib);
    trim_series_to_limit(&series_gap_mib);
    trim_series_to_limit(&series_high_water_cache_mib);
    trim_series_to_limit(&series_baseline_delta_mib);

    update_primary_memory_chart();
    update_leak_signal_chart();
}

void external_monitor_window::handle_event_batch(const QJsonObject& message) {
    const QJsonArray events = message.value(QStringLiteral("events")).toArray();
    for (const QJsonValue& event_value : events) {
        const QJsonObject event = event_value.toObject();
        append_event_line(
            QStringLiteral("[%1] event kind=%2 t=%3 seq=%4")
                .arg(
                    monitor_shared::utc_now_text(),
                    event.value(QStringLiteral("kind")).toString(),
                    QString::number(
                        event.value(QStringLiteral("timestamp_ms")).toInteger()
                    ),
                    QString::number(
                        event.value(QStringLiteral("collector_sequence"))
                            .toInteger()
                    )
                )
        );
    }
}

void external_monitor_window::handle_snapshot(const QJsonObject& message) {
    ++snapshot_counter;
    const QJsonObject snapshot
        = message.value(QStringLiteral("snapshot")).toObject();
    const qint64 previous_live_cache
        = latest_metric_point.cache_accounted_ready_bytes;
    if (!snapshot.isEmpty()) {
        const metric_point snapshot_point
            = debug_probe_core::metric_point_from_snapshot_payload_v1(snapshot);
        debug_probe_core::merge_metric_point_v1(
            &latest_metric_point, snapshot_point
        );
        if (snapshot_point.cache_accounted_ready_bytes >= 0) {
            high_water_point.cache_accounted_ready_bytes = std::max(
                high_water_point.cache_accounted_ready_bytes,
                snapshot_point.cache_accounted_ready_bytes
            );
        }
        if (snapshot_point.widget_local_display_bytes_estimated >= 0) {
            high_water_point.widget_local_display_bytes_estimated = std::max(
                high_water_point.widget_local_display_bytes_estimated,
                snapshot_point.widget_local_display_bytes_estimated
            );
        }
        if (snapshot_point.process_memory_rss_bytes >= 0) {
            high_water_point.process_memory_rss_bytes = std::max(
                high_water_point.process_memory_rss_bytes,
                snapshot_point.process_memory_rss_bytes
            );
        }
        if (snapshot_point.measured_accounted_gap_bytes_derived >= 0) {
            high_water_point.measured_accounted_gap_bytes_derived = std::max(
                high_water_point.measured_accounted_gap_bytes_derived,
                snapshot_point.measured_accounted_gap_bytes_derived
            );
        }
    }

    if (snapshot_text != nullptr) {
        const QJsonDocument document(snapshot);
        snapshot_text->setPlainText(
            QString::fromUtf8(document.toJson(QJsonDocument::Indented))
        );
    }

    if (snapshot.contains(QStringLiteral("cache_accounted_ready_bytes"))
        && previous_live_cache >= 0) {
        const qint64 snapshot_cache = integer_like_value(
            snapshot.value(QStringLiteral("cache_accounted_ready_bytes"))
        );
        const qint64 live_cache = previous_live_cache;
        if (std::llabs(snapshot_cache - live_cache) > (8 * 1024 * 1024)) {
            append_parity_warning(
                QStringLiteral("snapshot_sample_cache_drift"),
                QStringLiteral(
                    "snapshot cache bytes (%1) and sample cache bytes (%2) "
                    "drift"
                )
                    .arg(snapshot_cache)
                    .arg(live_cache)
            );
        }
    }

    update_status_labels();
}

void external_monitor_window::handle_marker(const QJsonObject& message) {
    ++marker_counter;
    const QString label = message.value(QStringLiteral("label")).toString();
    const qint64 monotonic_timestamp_ms = integer_like_value(
        message.value(QStringLiteral("monotonic_timestamp_ms"))
    );

    append_event_line(QStringLiteral("[%1] marker '%2' t=%3")
                          .arg(monitor_shared::utc_now_text(), label)
                          .arg(monotonic_timestamp_ms));

    const marker_checkpoint checkpoint {
        .label = label,
        .monotonic_timestamp_ms = monotonic_timestamp_ms,
        .point = latest_metric_point,
    };

    if (!marker_history.isEmpty()) {
        const marker_checkpoint previous = marker_history.constLast();
        if (previous.point.cache_accounted_ready_bytes >= 0
            && checkpoint.point.cache_accounted_ready_bytes >= 0) {
            const qint64 diff_cache
                = checkpoint.point.cache_accounted_ready_bytes
                - previous.point.cache_accounted_ready_bytes;
            marker_cache_diffs.push_back(diff_cache);
            while (marker_cache_diffs.size() > marker_diff_history_limit) {
                marker_cache_diffs.removeFirst();
            }

            append_event_line(
                QStringLiteral(
                    "  marker-to-marker diff: cache(accounted)=%1 MiB"
                )
                    .arg(
                        QString::number(
                            monitor_shared::to_mib(diff_cache), 'f', 2
                        )
                    )
            );
        }
    }

    const QString lowered_label = label.toLower();
    if (lowered_label.contains(QStringLiteral("settle"))
        || lowered_label.contains(QStringLiteral("quiescent"))
        || lowered_label.contains(QStringLiteral("idle"))) {
        settle_baseline_point = checkpoint.point;
        settle_baseline_valid = true;
        append_event_line(
            QStringLiteral("  baseline-after-settle captured at marker '%1'")
                .arg(label)
        );
    }

    if (settle_baseline_valid
        && checkpoint.point.cache_accounted_ready_bytes >= 0
        && settle_baseline_point.cache_accounted_ready_bytes >= 0) {
        const qint64 baseline_delta
            = checkpoint.point.cache_accounted_ready_bytes
            - settle_baseline_point.cache_accounted_ready_bytes;
        append_event_line(
            QStringLiteral(
                "  baseline-after-settle delta: cache(accounted)=%1 MiB"
            )
                .arg(
                    QString::number(
                        monitor_shared::to_mib(baseline_delta), 'f', 2
                    )
                )
        );
    }

    if (marker_cache_diffs.size() >= 3) {
        const int n = static_cast<int>(marker_cache_diffs.size());
        const bool strictly_positive = marker_cache_diffs.at(n - 1) > 0
            && marker_cache_diffs.at(n - 2) > 0
            && marker_cache_diffs.at(n - 3) > 0;
        monotonic_growth_suspicion = strictly_positive;
        if (strictly_positive) {
            append_warning_line(QStringLiteral(
                                    "[%1] monotonic-growth suspicion: last 3 "
                                    "marker cache diffs "
                                    "are positive"
            )
                                    .arg(monitor_shared::utc_now_text()));
        }
    }

    marker_history.push_back(checkpoint);
    while (marker_history.size() > marker_diff_history_limit) {
        marker_history.removeFirst();
    }

    update_leak_signal_chart();
    update_status_labels();
}

void external_monitor_window::handle_warning(const QJsonObject& message) {
    const QString code
        = message.value(QStringLiteral("warning_code")).toString();
    const QString warning_message
        = message.value(QStringLiteral("warning_message")).toString();
    append_warning_line(
        QStringLiteral("[%1] warning %2: %3")
            .arg(monitor_shared::utc_now_text(), code, warning_message)
    );
}

void external_monitor_window::update_primary_memory_chart() {
    if (primary_memory_chart == nullptr) {
        return;
    }

    QVector<monitor_line_chart_widget::series> lines;
    lines.push_back(
        monitor_line_chart_widget::series {
            .label = QStringLiteral("Cache-accounted bytes (accounted)"),
            .color = monitor_palette::blue(),
            .values = series_cache_mib,
        }
    );
    lines.push_back(
        monitor_line_chart_widget::series {
            .label = QStringLiteral("Widget-local bytes (estimated)"),
            .color = monitor_palette::orange(),
            .values = series_widget_mib,
        }
    );
    lines.push_back(
        monitor_line_chart_widget::series {
            .label = QStringLiteral("Process RSS (measured)"),
            .color = monitor_palette::green(),
            .values = series_rss_mib,
        }
    );
    lines.push_back(
        monitor_line_chart_widget::series {
            .label = QStringLiteral("Measured-accounted gap (derived)"),
            .color = monitor_palette::red(),
            .values = series_gap_mib,
        }
    );
    primary_memory_chart->set_series(lines);
    primary_memory_chart->set_footer_lines(
        QStringList() << QStringLiteral(
            "Measured/accounted/estimated/derived remain separate."
        )
                      << QStringLiteral("Samples retained in-memory: %1")
                             .arg(series_cache_mib.size())
    );
}

void external_monitor_window::update_leak_signal_chart() {
    if (leak_signal_chart == nullptr) {
        return;
    }

    QVector<monitor_line_chart_widget::series> lines;
    lines.push_back(
        monitor_line_chart_widget::series {
            .label = QStringLiteral("Cache high-water (derived diagnostic)"),
            .color = monitor_palette::purple(),
            .values = series_high_water_cache_mib,
        }
    );
    lines.push_back(
        monitor_line_chart_widget::series {
            .label = QStringLiteral("Cache delta vs settle baseline (derived)"),
            .color = monitor_palette::orange(),
            .values = series_baseline_delta_mib,
        }
    );
    leak_signal_chart->set_series(lines);
    leak_signal_chart->set_footer_lines(
        QStringList() << QStringLiteral(
            "Leak aids only: high-water, baseline delta, marker diffs, "
            "monotonic suspicion."
        )
    );
}

void external_monitor_window::update_status_labels() {
    if (connection_status_label != nullptr) {
        connection_status_label->setText(
            QStringLiteral("Connection: %1")
                .arg(connection_status_label_for_state(socket))
        );
    }

    if (session_status_label != nullptr) {
        session_status_label->setText(
            QStringLiteral("Messages=%1 snapshots=%2 markers=%3 warnings=%4")
                .arg(line_counter)
                .arg(snapshot_counter)
                .arg(marker_counter)
                .arg(warning_counter)
        );
    }

    if (leak_status_label != nullptr) {
        const QString high_water_cache
            = high_water_point.cache_accounted_ready_bytes >= 0
            ? QString::number(
                  monitor_shared::to_mib(
                      high_water_point.cache_accounted_ready_bytes
                  ),
                  'f', 2
              )
            : QStringLiteral("n/a");
        const QString baseline_delta = settle_baseline_valid
                && latest_metric_point.cache_accounted_ready_bytes >= 0
                && settle_baseline_point.cache_accounted_ready_bytes >= 0
            ? QString::number(
                  monitor_shared::to_mib(
                      latest_metric_point.cache_accounted_ready_bytes
                      - settle_baseline_point.cache_accounted_ready_bytes
                  ),
                  'f', 2
              )
            : QStringLiteral("n/a");

        leak_status_label->setText(
            QStringLiteral(
                "Leak signals: high-water=%1 MiB baseline-delta=%2 MiB "
                "monotonic-suspicion=%3"
            )
                .arg(high_water_cache)
                .arg(baseline_delta)
                .arg(
                    monotonic_growth_suspicion ? QStringLiteral("yes")
                                               : QStringLiteral("no")
                )
        );
    }

    if (log_path_label != nullptr) {
        log_path_label->setText(QStringLiteral("Long-history log: %1")
                                    .arg(
                                        history_log_path.isEmpty()
                                            ? QStringLiteral("not started")
                                            : history_log_path
                                    ));
    }

    if (connect_button != nullptr) {
        connect_button->setEnabled(socket == nullptr);
    }
    if (disconnect_button != nullptr) {
        disconnect_button->setEnabled(socket != nullptr);
    }
}

void external_monitor_window::append_event_line(const QString& line) {
    if (events_text == nullptr) {
        return;
    }
    events_text->appendPlainText(line);
}

void external_monitor_window::append_warning_line(const QString& line) {
    ++warning_counter;
    if (warnings_text != nullptr) {
        warnings_text->appendPlainText(line);
    }
    update_status_labels();
}

void external_monitor_window::append_parity_warning(
    const QString& code, const QString& details
) {
    append_warning_line(
        QStringLiteral("[%1] parity warning %2: %3")
            .arg(monitor_shared::utc_now_text(), code, details)
    );
}

QString external_monitor_window::message_family(const QJsonObject& message) {
    const QJsonObject protocol
        = message.value(QStringLiteral("protocol_v1")).toObject();
    return protocol.value(QStringLiteral("message_family")).toString();
}

QJsonObject
external_monitor_window::protocol_identity(const QJsonObject& message) {
    const QJsonObject protocol
        = message.value(QStringLiteral("protocol_v1")).toObject();
    return protocol.value(QStringLiteral("identity")).toObject();
}

qint64 external_monitor_window::integer_like_value(const QJsonValue& value) {
    if (value.isDouble()) {
        return static_cast<qint64>(std::llround(value.toDouble()));
    }
    return value.toInteger();
}
