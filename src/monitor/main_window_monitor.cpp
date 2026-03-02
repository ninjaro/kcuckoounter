#include "main_window.hpp"

#include "arch/str_label.hpp"
#include "monitor/raster_cache_debug_strings.hpp"
#include "monitor/resource_monitor.hpp"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QStandardPaths>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QString format_px_size(const QSize& size) {
    if (size.width() <= 0 || size.height() <= 0) {
        return QStringLiteral("n/a");
    }
    return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

int scale_to_cells(int value, int full_value, int max_cells) {
    if (value <= 0 || full_value <= 0 || max_cells <= 0) {
        return 0;
    }
    return std::clamp(
        static_cast<int>(
            std::llround((double(value) / double(full_value)) * max_cells)
        ),
        1, max_cells
    );
}

QString build_lower_left_geometry_schematic(
    const geometry_debug_snapshot& geometry
) {
    constexpr int width_cells = 48;
    constexpr int height_cells = 16;

    QVector<QString> rows(
        height_cells, QString(width_cells, QLatin1Char(' '))
    );

    for (int x = 0; x < width_cells; ++x) {
        rows[height_cells - 1][x] = QLatin1Char('_');
    }
    for (int y = 0; y < height_cells; ++y) {
        rows[y][0] = QLatin1Char('|');
    }
    rows[height_cells - 1][0] = QLatin1Char('+');

    const int max_width = std::max(
        1,
        std::max({
            geometry.window_size.width(), geometry.layout_size.width(),
            geometry.display_card_size.width(), geometry.cache_raster_size.width(),
            geometry.preloaded_raster_size.width()
        })
    );
    const int max_height = std::max(
        1,
        std::max({
            geometry.window_size.height(), geometry.layout_size.height(),
            geometry.display_card_size.height(), geometry.cache_raster_size.height(),
            geometry.preloaded_raster_size.height()
        })
    );

    auto draw_rect = [&rows](const QSize& size, int full_width, int full_height,
                             QChar edge) {
        if (size.width() <= 0 || size.height() <= 0) {
            return;
        }

        const int drawable_width = width_cells - 2;
        const int drawable_height = height_cells - 2;
        const int rect_width
            = scale_to_cells(size.width(), full_width, drawable_width);
        const int rect_height
            = scale_to_cells(size.height(), full_height, drawable_height);
        if (rect_width <= 0 || rect_height <= 0) {
            return;
        }

        const int x0 = 1;
        const int y_bottom = height_cells - 2;
        const int x1 = std::min(width_cells - 1, x0 + rect_width - 1);
        const int y_top = std::max(0, y_bottom - rect_height + 1);

        for (int x = x0; x <= x1; ++x) {
            rows[y_top][x] = edge;
            rows[y_bottom][x] = edge;
        }
        for (int y = y_top; y <= y_bottom; ++y) {
            rows[y][x0] = edge;
            rows[y][x1] = edge;
        }
    };

    draw_rect(geometry.window_size, max_width, max_height, QLatin1Char('W'));
    draw_rect(geometry.layout_size, max_width, max_height, QLatin1Char('L'));
    draw_rect(
        geometry.display_card_size, max_width, max_height, QLatin1Char('D')
    );
    draw_rect(geometry.cache_raster_size, max_width, max_height, QLatin1Char('C'));
    draw_rect(
        geometry.preloaded_raster_size, max_width, max_height, QLatin1Char('P')
    );

    QStringList lines;
    lines.append(
        QStringLiteral("Lower-left anchored schematic (origin = bottom-left)")
    );
    for (const QString& row : rows) {
        lines.append(row);
    }
    lines.append(
        QStringLiteral(
            "Legend: W=window L=layout D=display-card C=cache-raster P=preloaded-raster"
        )
    );
    return lines.join(QLatin1Char('\n'));
}

} // namespace

#ifndef NDEBUG
void main_window::on_export_debug_snapshot_triggered() {
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
}

void main_window::on_export_process_memory_report_triggered() {
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
}

void main_window::on_export_monitor_charts_triggered() {
    if (resource_monitor_primary_chart_text == nullptr
        || resource_monitor_secondary_chart_text == nullptr) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd_hhmmss")
    );
    const QString suggested_name
        = QStringLiteral("resource_monitor_charts_%1.png").arg(timestamp);
    QString output_path = QFileDialog::getSaveFileName(
        this, str_label("Export monitor chart image"), suggested_name,
        str_label("PNG files (*.png)")
    );
    if (output_path.isEmpty()) {
        return;
    }

    if (QFileInfo(output_path).suffix().isEmpty()) {
        output_path += QStringLiteral(".png");
    }

    const QPixmap primary
        = resource_monitor_primary_chart_text->viewport()->grab();
    const QPixmap secondary
        = resource_monitor_secondary_chart_text->viewport()->grab();
    if (primary.isNull() || secondary.isNull()) {
        qWarning() << "Unable to export monitor charts: chart pixmap is null";
        return;
    }

    const int spacing_px = 12;
    const int width_px = std::max(primary.width(), secondary.width());
    const int height_px = primary.height() + spacing_px + secondary.height();
    QPixmap composed(width_px, height_px);
    composed.fill(resource_monitor_primary_chart_text->palette().color(QPalette::Base));

    QPainter painter(&composed);
    painter.drawPixmap(0, 0, primary);
    painter.drawPixmap(0, primary.height() + spacing_px, secondary);
    painter.end();

    if (!composed.save(output_path, "PNG")) {
        qWarning() << "Unable to export monitor charts to" << output_path;
        return;
    }

    add_debug_marker(QStringLiteral("manual_export_chart_image"));
}

void main_window::on_add_monitor_marker_triggered() {
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("hh:mm:ss")
    );
    add_debug_marker(QStringLiteral("manual_marker_%1").arg(timestamp));
}

void main_window::on_set_realistic_cadence_mode_triggered() {
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    debug_telemetry_collector->set_debug_cadence_mode(
        resource_monitor::debug_cadence_mode::realistic
    );
    add_debug_marker(QStringLiteral("cadence_mode_realistic"));
    sync_debug_cadence_mode_actions();
    update_status_text();
}

void main_window::on_set_instrumented_cadence_mode_triggered() {
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    debug_telemetry_collector->set_debug_cadence_mode(
        resource_monitor::debug_cadence_mode::instrumented
    );
    add_debug_marker(QStringLiteral("cadence_mode_instrumented"));
    sync_debug_cadence_mode_actions();
    update_status_text();
}

void main_window::add_debug_marker(const QString& label) const {
    if (debug_telemetry_collector == nullptr) {
        return;
    }

    debug_telemetry_collector->add_manual_marker(label);
}

void main_window::sync_debug_cadence_mode_actions() const {
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
}

void main_window::on_toggle_resource_monitor_triggered(bool checked) {
    if (resource_monitor_window == nullptr) {
        return;
    }

    if (checked) {
        resource_monitor_window->show();
        resource_monitor_window->raise();
        resource_monitor_window->activateWindow();
        on_resource_monitor_visibility_changed(true);
        return;
    }

    resource_monitor_window->hide();
    on_resource_monitor_visibility_changed(false);
}

void main_window::on_resource_monitor_visibility_changed(bool visible) {
    if (toggle_resource_monitor_action != nullptr) {
        toggle_resource_monitor_action->setChecked(visible);
    }

    if (visible) {
        refresh_resource_monitor_view();
    }
}

void main_window::refresh_resource_monitor_view() {
    if (resource_monitor_summary_memory_card == nullptr
        || resource_monitor_summary_process_card == nullptr
        || resource_monitor_summary_stock_card == nullptr
        || resource_monitor_summary_activity_card == nullptr
        || resource_monitor_primary_chart_text == nullptr
        || resource_monitor_secondary_chart_text == nullptr
        || resource_monitor_timeline_text == nullptr
        || resource_monitor_diagnostics_text == nullptr
        || resource_monitor_geometry_text == nullptr
        || resource_monitor_resize_history_text == nullptr
        || debug_telemetry_collector == nullptr) {
        return;
    }

    if (!debug_telemetry_collector->has_cache_snapshot()) {
        const QString no_data
            = str_label("No cache snapshot has been collected yet.");
        resource_monitor_summary_memory_card->setText(no_data);
        resource_monitor_summary_process_card->setText(no_data);
        resource_monitor_summary_stock_card->setText(no_data);
        resource_monitor_summary_activity_card->setText(no_data);
        resource_monitor_primary_chart_text->setPlainText(no_data);
        resource_monitor_secondary_chart_text->setPlainText(no_data);
        resource_monitor_timeline_text->setPlainText(
            str_label("No timeline rows yet.")
        );
        resource_monitor_diagnostics_text->setPlainText(
            str_label("No diagnostics data yet.")
        );
        resource_monitor_geometry_text->setPlainText(
            str_label("No geometry telemetry yet.")
        );
        resource_monitor_resize_history_text->setPlainText(
            str_label("No resize-history telemetry yet.")
        );
        return;
    }

    const resource_monitor::cache_timeline_entry latest
        = debug_telemetry_collector->latest_cache_snapshot();
    const raster_cache::debug_snapshot& snapshot = latest.cache_snapshot;
    const qint64 measured_vs_accounted_gap_bytes = latest.process_rss_bytes >= 0
        ? latest.process_rss_bytes - snapshot.ready_bytes
        : 0;

    const auto to_mib = [](qint64 bytes) {
        return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 2);
    };

    resource_monitor_summary_memory_card->setText(
        str_label(
            "Memory classes\nCache-accounted: %1 MiB\nWidget-local est: %2 "
            "MiB\nMeasured-accounted gap: %3"
        )
            .arg(to_mib(snapshot.ready_bytes))
            .arg(to_mib(snapshot.widget_local_display_bytes_estimated))
            .arg(
                latest.process_rss_bytes >= 0
                    ? str_label("%1 MiB").arg(
                          to_mib(measured_vs_accounted_gap_bytes)
                      )
                    : str_label("unavailable")
            )
    );
    resource_monitor_summary_process_card->setText(
        str_label("Process (OS-measured)\nRSS: %1 MiB\nSample interval: %2 ms")
            .arg(
                latest.process_rss_bytes >= 0 ? to_mib(latest.process_rss_bytes)
                                              : str_label("unavailable")
            )
            .arg(debug_telemetry_collector->process_memory_sample_interval_ms())
    );
    resource_monitor_summary_stock_card->setText(
        str_label(
            "Unique stock\nReady entries/images: %1 / %2\nCached-only entries: "
            "%3"
        )
            .arg(snapshot.ready_entries)
            .arg(snapshot.ready_images)
            .arg(snapshot.cached_only_ready_entries)
    );
    resource_monitor_summary_activity_card->setText(
        str_label(
            "Activity / flow\nPending families: %1 | In-flight families: %2\n"
            "Interval bytes +%3/-%4"
        )
            .arg(snapshot.pending_families)
            .arg(snapshot.in_flight_families)
            .arg(snapshot.interval_deltas.bytes_added)
            .arg(snapshot.interval_deltas.bytes_removed)
    );

    const QVector<resource_monitor::cache_timeline_entry> cache_rows
        = debug_telemetry_collector->cache_timeline();
    const QVector<resource_monitor::event_timeline_entry> event_rows
        = debug_telemetry_collector->event_timeline();

    auto build_series_line =
        [](const QString& label,
           const QVector<resource_monitor::cache_timeline_entry>& rows,
           qint64 (*value_fn)(const resource_monitor::cache_timeline_entry&),
           bool (*is_available_fn)(
               const resource_monitor::cache_timeline_entry&
           )
           = nullptr) {
            if (rows.isEmpty()) {
                return QStringLiteral("- ") + label + QStringLiteral(": n/a");
            }

            QVector<qint64> values;
            values.reserve(rows.size());
            qint64 min_value = std::numeric_limits<qint64>::max();
            qint64 max_value = std::numeric_limits<qint64>::min();
            for (const auto& row : rows) {
                if (is_available_fn != nullptr && !is_available_fn(row)) {
                    continue;
                }
                const qint64 value = value_fn(row);
                values.push_back(value);
                min_value = std::min(min_value, value);
                max_value = std::max(max_value, value);
            }

            if (values.isEmpty()) {
                return QStringLiteral("- ") + label
                    + QStringLiteral(": unavailable");
            }

            const QString ramps = QString::fromUtf8("▁▂▃▄▅▆▇█");
            QString spark;
            const int start = std::max(0, int(values.size() - 20));
            const qint64 span = std::max<qint64>(1, max_value - min_value);
            for (int i = start; i < values.size(); ++i) {
                const qint64 value = values.at(i);
                const int idx = std::clamp(
                    int((double(value - min_value) / double(span)) * 7.0), 0, 7
                );
                spark.append(ramps.at(idx));
            }

            const qint64 latest_value = values.constLast();
            const qint64 baseline_value = values.at(start);
            const qint64 delta = latest_value - baseline_value;
            const QString delta_sign
                = delta >= 0 ? QStringLiteral("+") : QString();

            return QStringLiteral("- ") + label + QStringLiteral(": latest=")
                + QString::number(latest_value) + QStringLiteral(" Δ=")
                + delta_sign + QString::number(delta) + QStringLiteral(" min=")
                + QString::number(min_value) + QStringLiteral(" max=")
                + QString::number(max_value) + QStringLiteral(" | ") + spark;
        };

    QStringList primary_chart;
    primary_chart.append(str_label("Primary memory chart (last 20 snapshots)"));
    primary_chart.append(
        str_label("Measured/accounted/estimated series stay separate.")
    );
    if (resource_monitor_show_cache_bytes_series != nullptr
        && resource_monitor_show_cache_bytes_series->isChecked()) {
        primary_chart.append(build_series_line(
            str_label("Cache-accounted bytes"), cache_rows,
            [](const resource_monitor::cache_timeline_entry& row) {
                return row.cache_snapshot.ready_bytes;
            }
        ));
    }
    if (resource_monitor_show_widget_local_series != nullptr
        && resource_monitor_show_widget_local_series->isChecked()) {
        primary_chart.append(build_series_line(
            str_label("Widget-local estimated bytes"), cache_rows,
            [](const resource_monitor::cache_timeline_entry& row) {
                return row.cache_snapshot.widget_local_display_bytes_estimated;
            }
        ));
    }
    if (resource_monitor_show_process_rss_series != nullptr
        && resource_monitor_show_process_rss_series->isChecked()) {
        primary_chart.append(build_series_line(
            str_label("Process RSS bytes (OS)"), cache_rows,
            [](const resource_monitor::cache_timeline_entry& row) {
                return row.process_rss_bytes;
            },
            [](const resource_monitor::cache_timeline_entry& row) {
                return row.process_rss_bytes >= 0;
            }
        ));
    }
    if (resource_monitor_show_gap_bytes_series != nullptr
        && resource_monitor_show_gap_bytes_series->isChecked()) {
        primary_chart.append(build_series_line(
            str_label("Measured-accounted gap bytes (derived)"), cache_rows,
            [](const resource_monitor::cache_timeline_entry& row) {
                return row.process_rss_bytes >= 0
                    ? row.process_rss_bytes - row.cache_snapshot.ready_bytes
                    : 0;
            },
            [](const resource_monitor::cache_timeline_entry& row) {
                return row.process_rss_bytes >= 0;
            }
        ));
    }
    if (resource_monitor_show_accounted_to_measured_ratio_series != nullptr
        && resource_monitor_show_accounted_to_measured_ratio_series
               ->isChecked()) {
        primary_chart.append(build_series_line(
            str_label("Accounted/measured ratio permille (derived)"),
            cache_rows,
            [](const resource_monitor::cache_timeline_entry& row) {
                if (row.process_rss_bytes <= 0) {
                    return qint64(0);
                }
                return (row.cache_snapshot.ready_bytes * 1000)
                    / row.process_rss_bytes;
            },
            [](const resource_monitor::cache_timeline_entry& row) {
                return row.process_rss_bytes > 0;
            }
        ));
    }
    resource_monitor_primary_chart_text->setPlainText(
        primary_chart.join(QLatin1Char('\n'))
    );

    QStringList secondary_chart;
    secondary_chart.append(
        str_label("Secondary counts/activity chart (last 20 snapshots)")
    );
    if (resource_monitor_show_activity_displayed_series != nullptr
        && resource_monitor_show_activity_displayed_series->isChecked()) {
        secondary_chart.append(build_series_line(
            str_label("Displayed-recent entries"), cache_rows,
            [](const resource_monitor::cache_timeline_entry& row) {
                return qint64(row.cache_snapshot.displayed_ready_entries);
            }
        ));
    }
    if (resource_monitor_show_activity_pending_series != nullptr
        && resource_monitor_show_activity_pending_series->isChecked()) {
        secondary_chart.append(build_series_line(
            str_label("Pending families"), cache_rows,
            [](const resource_monitor::cache_timeline_entry& row) {
                return qint64(row.cache_snapshot.pending_families);
            }
        ));
    }
    if (resource_monitor_show_activity_in_flight_series != nullptr
        && resource_monitor_show_activity_in_flight_series->isChecked()) {
        secondary_chart.append(build_series_line(
            str_label("In-flight families"), cache_rows,
            [](const resource_monitor::cache_timeline_entry& row) {
                return qint64(row.cache_snapshot.in_flight_families);
            }
        ));
    }
    if (resource_monitor_show_activity_events_series != nullptr
        && resource_monitor_show_activity_events_series->isChecked()) {
        secondary_chart.append(
            str_label("- Event markers total: %1").arg(event_rows.size())
        );
    }
    resource_monitor_secondary_chart_text->setPlainText(
        secondary_chart.join(QLatin1Char('\n'))
    );

    QStringList timeline_lines;
    timeline_lines.append(str_label("Latest timeline rows (up to 12):"));

    const int cache_start = std::max(0, int(cache_rows.size() - 12));
    for (int index = cache_start; index < cache_rows.size(); ++index) {
        const auto& row = cache_rows.at(index);
        timeline_lines.append(
            str_label(
                "[cache] seq=%1 snap=%2 ready=%3 cache_bytes=%4 (net=%5 "
                "+%6/-%7) "
                "entries(+%8/-%9) images(+%10/-%11) "
                "displayed_recent=%12 cached_only=%13 widget_local=%14 "
                "(net=%15 +%16/-%17) "
                "rss=%18 (net=%19 +%20/-%21)"
            )
                .arg(row.collector_sequence)
                .arg(row.cache_snapshot.snapshot_sequence)
                .arg(row.cache_snapshot.ready_entries)
                .arg(row.cache_snapshot.ready_bytes)
                .arg(row.cache_accounted_ready_bytes_delta)
                .arg(row.cache_bytes_added_interval)
                .arg(row.cache_bytes_removed_interval)
                .arg(row.cache_entries_added_interval)
                .arg(row.cache_entries_removed_interval)
                .arg(row.cache_images_added_interval)
                .arg(row.cache_images_removed_interval)
                .arg(row.cache_snapshot.displayed_ready_entries)
                .arg(row.cache_snapshot.cached_only_ready_entries)
                .arg(row.cache_snapshot.widget_local_display_bytes_estimated)
                .arg(row.widget_local_display_bytes_estimated_delta)
                .arg(row.widget_local_display_bytes_materialized_interval)
                .arg(row.widget_local_display_bytes_released_interval)
                .arg(row.process_rss_bytes)
                .arg(row.process_rss_bytes_delta)
                .arg(row.process_rss_bytes_growth_interval)
                .arg(row.process_rss_bytes_drop_interval)
        );
    }

    const int event_start = std::max(0, int(event_rows.size() - 12));
    for (int index = event_start; index < event_rows.size(); ++index) {
        const auto& row = event_rows.at(index);
        const QString kind = row.kind
                == resource_monitor::event_timeline_entry::event_kind::
                    manual_marker
            ? QStringLiteral("marker")
            : QStringLiteral("cache");
        timeline_lines.append(str_label("[event] seq=%1 kind=%2 t=%3 label=%4")
                                  .arg(row.collector_sequence)
                                  .arg(kind)
                                  .arg(row.timestamp_ms)
                                  .arg(row.label));
    }
    resource_monitor_timeline_text->setPlainText(
        timeline_lines.join(QLatin1Char('\n'))
    );

    QStringList diagnostics_lines;
    diagnostics_lines.append(str_label("Recency-window diagnostics"));
    diagnostics_lines.append(
        str_label("Window size: %1 ms").arg(snapshot.displayed_entry_window_ms)
    );
    diagnostics_lines.append(str_label("Displayed-recently entries: %1")
                                 .arg(snapshot.displayed_ready_entries));
    diagnostics_lines.append(str_label("Cached-only entries: %1")
                                 .arg(snapshot.cached_only_ready_entries));
    diagnostics_lines.append(str_label("Top expensive tasks: %1")
                                 .arg(snapshot.top_expensive_tasks.size()));
    diagnostics_lines.append(
        str_label("Subsystem summaries: %1 | Consumer summaries: %2")
            .arg(snapshot.subsystem_summaries.size())
            .arg(snapshot.consumer_summaries.size())
    );
    resource_monitor_diagnostics_text->setPlainText(
        diagnostics_lines.join(QLatin1Char('\n'))
    );

    QStringList geometry_lines;
    geometry_lines.append(str_label("Geometry and coverage view"));
    if (debug_telemetry_collector->has_geometry_snapshot()) {
        const geometry_debug_snapshot geometry
            = debug_telemetry_collector->latest_geometry_snapshot();
        geometry_lines.append(
            str_label(
                "Slots: %1 total / %2 visible | window=%3 | layout=%4"
            )
                .arg(geometry.slot_count)
                .arg(geometry.visible_slot_count)
                .arg(format_px_size(geometry.window_size))
                .arg(format_px_size(geometry.layout_size))
        );
        geometry_lines.append(
            str_label(
                "Display-card=%1 (need short px=%2) | active bucket=%3 | "
                "warming bucket=%4"
            )
                .arg(format_px_size(geometry.display_card_size))
                .arg(geometry.display_card_need_short_px)
                .arg(geometry.active_bucket_px)
                .arg(geometry.warming_bucket_px)
        );
        geometry_lines.append(
            str_label(
                "Cache raster=%1 | preloaded raster=%2 | unique buckets=%3"
            )
                .arg(format_px_size(geometry.cache_raster_size))
                .arg(format_px_size(geometry.preloaded_raster_size))
                .arg(geometry.unique_size_buckets)
        );
        geometry_lines.append(
            str_label(
                "Coverage: %1%% over %2 ms | prewarm in flight: %3 | "
                "generation active/warming: %4/%5"
            )
                .arg(geometry.coverage_percent)
                .arg(geometry.coverage_window_ms)
                .arg(geometry.prewarm_in_flight ? str_label("yes")
                                                : str_label("no"))
                .arg(geometry.active_generation_id)
                .arg(geometry.warming_generation_id)
        );
        geometry_lines.append(QString());
        geometry_lines.append(build_lower_left_geometry_schematic(geometry));
    } else {
        geometry_lines.append(
            str_label("No geometry snapshots have been collected yet.")
        );
    }
    resource_monitor_geometry_text->setPlainText(
        geometry_lines.join(QLatin1Char('\n'))
    );

    const QVector<resource_monitor::resize_history_entry> resize_history_rows
        = debug_telemetry_collector->resize_history();
    QStringList resize_lines;
    resize_lines.append(str_label("Recent resize transitions"));
    const QString resize_log_path
        = debug_telemetry_collector->resize_history_log_path();
    resize_lines.append(
        str_label("Append-only log stream: %1")
            .arg(
                resize_log_path.isEmpty() ? str_label("not initialized yet")
                                          : resize_log_path
            )
    );

    if (resize_history_rows.isEmpty()) {
        resize_lines.append(
            str_label("No resize transitions have been recorded yet.")
        );
    } else {
        const int start = std::max(0, int(resize_history_rows.size() - 14));
        for (int index = start; index < resize_history_rows.size(); ++index) {
            const auto& row = resize_history_rows.at(index);
            const QString started_at
                = QDateTime::fromMSecsSinceEpoch(
                      row.transition_start_timestamp_ms, QTimeZone::UTC
                  )
                      .toString(QStringLiteral("hh:mm:ss.zzz"));
            const QString ended_at = QDateTime::fromMSecsSinceEpoch(
                                         row.transition_end_timestamp_ms,
                                         QTimeZone::UTC
            )
                                         .toString(
                                             QStringLiteral("hh:mm:ss.zzz")
                                         );
            resize_lines.append(
                str_label(
                    "[resize] t=%1 -> %2 window=%3 -> %4 bucket(active "
                    "%5->%6, warming %7->%8) prewarm_ms=%9"
                )
                    .arg(started_at)
                    .arg(ended_at)
                    .arg(format_px_size(row.old_window_size))
                    .arg(format_px_size(row.new_window_size))
                    .arg(row.old_active_bucket_px)
                    .arg(row.new_active_bucket_px)
                    .arg(row.old_warming_bucket_px)
                    .arg(row.new_warming_bucket_px)
                    .arg(row.prewarm_completion_ms)
            );
            resize_lines.append(
                str_label(
                    "  before rss(measured)=%1 MiB cache(accounted)=%2 MiB "
                    "widget(est)=%3 MiB gap(derived)=%4 MiB"
                )
                    .arg(
                        row.before_process_rss_bytes >= 0
                            ? to_mib(row.before_process_rss_bytes)
                            : str_label("unavailable")
                    )
                    .arg(to_mib(row.before_cache_accounted_ready_bytes))
                    .arg(
                        to_mib(row.before_widget_local_display_bytes_estimated)
                    )
                    .arg(to_mib(row.before_measured_accounted_gap_bytes))
            );
            resize_lines.append(
                str_label(
                    "  after  rss(measured)=%1 MiB cache(accounted)=%2 MiB "
                    "widget(est)=%3 MiB gap(derived)=%4 MiB"
                )
                    .arg(
                        row.after_process_rss_bytes >= 0
                            ? to_mib(row.after_process_rss_bytes)
                            : str_label("unavailable")
                    )
                    .arg(to_mib(row.after_cache_accounted_ready_bytes))
                    .arg(to_mib(row.after_widget_local_display_bytes_estimated)
                    )
                    .arg(to_mib(row.after_measured_accounted_gap_bytes))
            );
        }
    }
    resource_monitor_resize_history_text->setPlainText(
        resize_lines.join(QLatin1Char('\n'))
    );
}

void main_window::dump_debug_telemetry_on_exit() const {
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
}

#endif
