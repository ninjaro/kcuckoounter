#include "main_window.hpp"

#include "arch/str_label.hpp"
#include "monitor/monitor_visual_widgets.hpp"
#include "monitor/raster_cache_debug_strings.hpp"
#include "monitor/resource_monitor.hpp"

#include <QCheckBox>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
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

double bytes_to_mib_value(qint64 bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
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
    if (resource_monitor_primary_chart_view == nullptr
        || resource_monitor_ratio_chart_view == nullptr
        || resource_monitor_secondary_chart_view == nullptr
        || resource_monitor_composition_chart_view == nullptr) {
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

    const QVector<QPixmap> dashboard_charts = {
        resource_monitor_primary_chart_view->grab(),
        resource_monitor_ratio_chart_view->grab(),
        resource_monitor_secondary_chart_view->grab(),
        resource_monitor_composition_chart_view->grab(),
    };

    for (const QPixmap& chart : dashboard_charts) {
        if (!chart.isNull()) {
            continue;
        }
        qWarning() << "Unable to export monitor charts: chart pixmap is null";
        return;
    }

    const int spacing_px = 12;
    int width_px = 0;
    int height_px = 0;
    for (qsizetype index = 0; index < dashboard_charts.size(); ++index) {
        const QPixmap& chart = dashboard_charts.at(index);
        width_px = std::max(width_px, chart.width());
        height_px += chart.height();
        if (index + 1 < dashboard_charts.size()) {
            height_px += spacing_px;
        }
    }

    if (width_px <= 0 || height_px <= 0) {
        qWarning() << "Unable to export monitor charts: invalid composed size";
        return;
    }

    QPixmap composed(width_px, height_px);
    composed.fill(
        resource_monitor_primary_chart_view->palette().color(QPalette::Base)
    );

    QPainter painter(&composed);
    int y_offset_px = 0;
    for (qsizetype index = 0; index < dashboard_charts.size(); ++index) {
        const QPixmap& chart = dashboard_charts.at(index);
        painter.drawPixmap(0, y_offset_px, chart);
        y_offset_px += chart.height();
        if (index + 1 < dashboard_charts.size()) {
            y_offset_px += spacing_px;
        }
    }
    painter.end();

    if (!composed.save(output_path, "PNG")) {
        qWarning() << "Unable to export monitor charts to" << output_path;
        return;
    }

    add_debug_marker(QStringLiteral("manual_export_chart_image"));
}

void main_window::on_add_monitor_marker_triggered() {
    const QString timestamp
        = QDateTime::currentDateTimeUtc().toString(QStringLiteral("hh:mm:ss"));
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
        || resource_monitor_primary_chart_view == nullptr
        || resource_monitor_ratio_chart_view == nullptr
        || resource_monitor_secondary_chart_view == nullptr
        || resource_monitor_composition_chart_view == nullptr
        || resource_monitor_timeline_text == nullptr
        || resource_monitor_diagnostics_text == nullptr
        || resource_monitor_geometry_view == nullptr
        || resource_monitor_resize_history_view == nullptr
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
        resource_monitor_primary_chart_view->set_series(
            QVector<monitor_line_chart_widget::series>()
        );
        resource_monitor_primary_chart_view->set_footer_lines(
            QStringList() << no_data
        );
        resource_monitor_ratio_chart_view->set_series(
            QVector<monitor_line_chart_widget::series>()
        );
        resource_monitor_ratio_chart_view->set_footer_lines(
            QStringList() << no_data
        );
        resource_monitor_secondary_chart_view->set_series(
            QVector<monitor_line_chart_widget::series>()
        );
        resource_monitor_secondary_chart_view->set_footer_lines(
            QStringList() << no_data
        );
        resource_monitor_composition_chart_view->set_slices(
            QVector<monitor_pie_chart_widget::slice>()
        );
        resource_monitor_composition_chart_view->set_footer_text(no_data);
        resource_monitor_timeline_text->setPlainText(
            str_label("No timeline rows yet.")
        );
        resource_monitor_diagnostics_text->setPlainText(
            str_label("No diagnostics data yet.")
        );
        resource_monitor_geometry_view->clear_snapshot();
        resource_monitor_resize_history_view->set_entries(
            QVector<monitor_resize_history_widget::resize_entry>()
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
        return QString::number(bytes_to_mib_value(bytes), 'f', 2);
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

    auto append_series_if_checked
        = [&cache_rows](
              QCheckBox* toggle, const QString& label, const QColor& color,
              auto value_fn, auto available_fn,
              QVector<monitor_line_chart_widget::series>* output_series
          ) {
              if (toggle == nullptr || output_series == nullptr
                  || !toggle->isChecked()) {
                  return;
              }

              monitor_line_chart_widget::series line;
              line.label = label;
              line.color = color;
              line.values.reserve(cache_rows.size());

              for (const auto& row : cache_rows) {
                  if (available_fn(row)) {
                      line.values.push_back(value_fn(row));
                  } else {
                      line.values.push_back(
                          std::numeric_limits<double>::quiet_NaN()
                      );
                  }
              }

              output_series->push_back(line);
          };

    const auto always_available
        = [](const resource_monitor::cache_timeline_entry&) { return true; };

    QVector<monitor_line_chart_widget::series> primary_series;
    append_series_if_checked(
        resource_monitor_show_cache_bytes_series,
        str_label("Cache-accounted bytes"), QColor(52, 111, 196),
        [](const resource_monitor::cache_timeline_entry& row) {
            return bytes_to_mib_value(row.cache_snapshot.ready_bytes);
        },
        always_available, &primary_series
    );
    append_series_if_checked(
        resource_monitor_show_widget_local_series,
        str_label("Widget-local estimated bytes"), QColor(216, 140, 52),
        [](const resource_monitor::cache_timeline_entry& row) {
            return bytes_to_mib_value(
                row.cache_snapshot.widget_local_display_bytes_estimated
            );
        },
        always_available, &primary_series
    );
    append_series_if_checked(
        resource_monitor_show_process_rss_series,
        str_label("Process RSS bytes (OS)"), QColor(52, 168, 110),
        [](const resource_monitor::cache_timeline_entry& row) {
            return bytes_to_mib_value(row.process_rss_bytes);
        },
        [](const resource_monitor::cache_timeline_entry& row) {
            return row.process_rss_bytes >= 0;
        },
        &primary_series
    );
    append_series_if_checked(
        resource_monitor_show_gap_bytes_series,
        str_label("Measured-accounted gap bytes (derived)"),
        QColor(196, 72, 88),
        [](const resource_monitor::cache_timeline_entry& row) {
            return bytes_to_mib_value(
                row.process_rss_bytes - row.cache_snapshot.ready_bytes
            );
        },
        [](const resource_monitor::cache_timeline_entry& row) {
            return row.process_rss_bytes >= 0;
        },
        &primary_series
    );

    resource_monitor_primary_chart_view->set_series(primary_series);
    resource_monitor_primary_chart_view->set_footer_lines(
        QStringList() << str_label(
            "Measured/accounted/estimated/derived remain distinct."
        ) << str_label("Samples: %1").arg(cache_rows.size())
    );

    QVector<monitor_line_chart_widget::series> ratio_series;
    append_series_if_checked(
        resource_monitor_show_accounted_to_measured_ratio_series,
        str_label("Accounted/measured ratio (derived %)"), QColor(122, 94, 220),
        [](const resource_monitor::cache_timeline_entry& row) {
            return (double(row.cache_snapshot.ready_bytes) * 100.0)
                / double(row.process_rss_bytes);
        },
        [](const resource_monitor::cache_timeline_entry& row) {
            return row.process_rss_bytes > 0;
        },
        &ratio_series
    );

    resource_monitor_ratio_chart_view->set_series(ratio_series);
    resource_monitor_ratio_chart_view->set_footer_lines(
        QStringList() << str_label(
            "Derived comparison series; not ownership attribution."
        )
    );

    QVector<monitor_line_chart_widget::series> secondary_series;
    append_series_if_checked(
        resource_monitor_show_activity_displayed_series,
        str_label("Displayed-recent entries"), QColor(44, 150, 198),
        [](const resource_monitor::cache_timeline_entry& row) {
            return double(row.cache_snapshot.displayed_ready_entries);
        },
        always_available, &secondary_series
    );
    append_series_if_checked(
        resource_monitor_show_activity_pending_series,
        str_label("Pending families"), QColor(224, 132, 36),
        [](const resource_monitor::cache_timeline_entry& row) {
            return double(row.cache_snapshot.pending_families);
        },
        always_available, &secondary_series
    );
    append_series_if_checked(
        resource_monitor_show_activity_in_flight_series,
        str_label("In-flight families"), QColor(92, 102, 222),
        [](const resource_monitor::cache_timeline_entry& row) {
            return double(row.cache_snapshot.in_flight_families);
        },
        always_available, &secondary_series
    );

    QStringList secondary_footer;
    secondary_footer.append(
        str_label("Activity counters only; byte ownership is in primary chart.")
    );
    if (resource_monitor_show_activity_events_series != nullptr
        && resource_monitor_show_activity_events_series->isChecked()) {
        secondary_footer.append(
            str_label("Event markers total: %1").arg(event_rows.size())
        );
    }
    resource_monitor_secondary_chart_view->set_series(secondary_series);
    resource_monitor_secondary_chart_view->set_footer_lines(secondary_footer);

    QVector<monitor_pie_chart_widget::slice> composition_slices;
    if (snapshot.displayed_ready_entries > 0) {
        composition_slices.push_back(
            {
                str_label("Displayed-recent entries"),
                QColor(44, 150, 198),
                double(snapshot.displayed_ready_entries),
            }
        );
    }
    if (snapshot.cached_only_ready_entries > 0) {
        composition_slices.push_back(
            {
                str_label("Cached-only entries"),
                QColor(120, 128, 140),
                double(snapshot.cached_only_ready_entries),
            }
        );
    }
    const int other_ready_entries = std::max(
        0,
        snapshot.ready_entries - snapshot.displayed_ready_entries
            - snapshot.cached_only_ready_entries
    );
    if (other_ready_entries > 0) {
        composition_slices.push_back(
            {
                str_label("Other ready entries"),
                QColor(170, 102, 190),
                double(other_ready_entries),
            }
        );
    }
    resource_monitor_composition_chart_view->set_slices(composition_slices);
    resource_monitor_composition_chart_view->set_footer_text(str_label(
        "Entry-count composition (unique stock subsets), not byte totals."
    ));

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
        resource_monitor_geometry_view->set_snapshot(geometry);
        geometry_lines.append(
            str_label("Slots: %1 total / %2 visible | window=%3 | layout=%4")
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
                .arg(
                    geometry.prewarm_in_flight ? str_label("yes")
                                               : str_label("no")
                )
                .arg(geometry.active_generation_id)
                .arg(geometry.warming_generation_id)
        );
        geometry_lines.append(
            str_label("Schematic view above is lower-left anchored.")
        );
    } else {
        resource_monitor_geometry_view->clear_snapshot();
        geometry_lines.append(
            str_label("No geometry snapshots have been collected yet.")
        );
    }
    resource_monitor_geometry_text->setPlainText(
        geometry_lines.join(QLatin1Char('\n'))
    );

    const QVector<resource_monitor::resize_history_entry> resize_history_rows
        = debug_telemetry_collector->resize_history();
    QVector<monitor_resize_history_widget::resize_entry> resize_visual_rows;
    resize_visual_rows.reserve(resize_history_rows.size());
    for (const auto& row : resize_history_rows) {
        monitor_resize_history_widget::resize_entry visual_row;
        visual_row.timestamp_ms = row.transition_end_timestamp_ms > 0
            ? row.transition_end_timestamp_ms
            : row.transition_start_timestamp_ms;
        visual_row.prewarm_completion_ms = row.prewarm_completion_ms;
        visual_row.old_active_bucket_px = row.old_active_bucket_px;
        visual_row.new_active_bucket_px = row.new_active_bucket_px;
        visual_row.old_window_size = row.old_window_size;
        visual_row.new_window_size = row.new_window_size;
        resize_visual_rows.push_back(visual_row);
    }
    resource_monitor_resize_history_view->set_entries(resize_visual_rows);

    QStringList resize_lines;
    resize_lines.append(str_label("Recent resize transitions"));
    const QString resize_log_path
        = debug_telemetry_collector->resize_history_log_path();
    resize_lines.append(str_label("Append-only log stream: %1")
                            .arg(
                                resize_log_path.isEmpty()
                                    ? str_label("not initialized yet")
                                    : resize_log_path
                            ));

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
            const QString ended_at
                = QDateTime::fromMSecsSinceEpoch(
                      row.transition_end_timestamp_ms, QTimeZone::UTC
                )
                      .toString(QStringLiteral("hh:mm:ss.zzz"));
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
                    .arg(to_mib(row.after_widget_local_display_bytes_estimated))
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
