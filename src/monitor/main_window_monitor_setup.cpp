#include "main_window.hpp"

#include "arch/str_label.hpp"
#include "monitor/monitor_visual_widgets.hpp"
#include "monitor/resource_monitor.hpp"
#include "table/table.hpp"

#include <QActionGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>

#ifndef NDEBUG

void main_window::setup_debug_resource_monitor_ui() {
    debug_telemetry_collector = new resource_monitor(this);
    if (table_widget != nullptr) {
        debug_telemetry_collector->attach_cache_service(
            table_widget->shared_raster_cache_service()
        );
        debug_telemetry_collector->attach_table_service(table_widget);
    }
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        dump_debug_telemetry_on_exit();
    });

    resource_monitor_window = new QDialog(nullptr, Qt::Window);
    resource_monitor_window->setWindowTitle(str_label("Resource monitor"));
    resource_monitor_window->setModal(false);
    resource_monitor_window->resize(900, 640);

    auto monitor_layout = new BaseVBoxLayout(resource_monitor_window);
    monitor_layout->setContentsMargins(6, 6, 6, 6);
    monitor_layout->setSpacing(6);

    auto monitor_actions_bar = new QToolBar(resource_monitor_window);
    monitor_actions_bar->setMovable(false);
    monitor_actions_bar->setFloatable(false);
    monitor_actions_bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    export_debug_snapshot_action
        = monitor_actions_bar->addAction(str_label("Export snapshot"));
    export_debug_snapshot_action->setToolTip(
        str_label("Export cache/resource telemetry snapshot to JSON")
    );
    QObject::connect(
        export_debug_snapshot_action, &BaseAction::triggered, this,
        &main_window::on_export_debug_snapshot_triggered
    );

    export_process_memory_report_action
        = monitor_actions_bar->addAction(str_label("Export process detail"));
    export_process_memory_report_action->setToolTip(str_label(
        "Export on-demand OS process memory details (/proc status + smaps "
        "rollup)"
    ));
    QObject::connect(
        export_process_memory_report_action, &BaseAction::triggered, this,
        &main_window::on_export_process_memory_report_triggered
    );

    export_monitor_charts_action
        = monitor_actions_bar->addAction(str_label("Save chart image"));
    export_monitor_charts_action->setToolTip(
        str_label("Save current chart panels as a PNG image")
    );
    QObject::connect(
        export_monitor_charts_action, &BaseAction::triggered, this,
        &main_window::on_export_monitor_charts_triggered
    );

    add_monitor_marker_action
        = monitor_actions_bar->addAction(str_label("Add marker"));
    add_monitor_marker_action->setToolTip(
        str_label("Add a manual timeline marker to correlate events")
    );
    QObject::connect(
        add_monitor_marker_action, &BaseAction::triggered, this,
        &main_window::on_add_monitor_marker_triggered
    );

    toggle_debug_broadcaster_action
        = monitor_actions_bar->addAction(str_label("Broadcast telemetry"));
    toggle_debug_broadcaster_action->setToolTip(str_label(
        "Enable debug-only local IPC telemetry broadcaster for external "
        "listeners"
    ));
    toggle_debug_broadcaster_action->setCheckable(true);
    toggle_debug_broadcaster_action->setChecked(
        debug_telemetry_collector->is_debug_broadcaster_enabled()
    );
    QObject::connect(
        toggle_debug_broadcaster_action, &BaseAction::triggered, this,
        &main_window::on_toggle_debug_broadcaster_triggered
    );

    monitor_actions_bar->addSeparator();

    auto cadence_action_group = new QActionGroup(this);
    cadence_action_group->setExclusive(true);

    realistic_cadence_mode_action
        = monitor_actions_bar->addAction(str_label("Mode: realistic"));
    realistic_cadence_mode_action->setCheckable(true);
    cadence_action_group->addAction(realistic_cadence_mode_action);
    QObject::connect(
        realistic_cadence_mode_action, &BaseAction::triggered, this,
        &main_window::on_set_realistic_cadence_mode_triggered
    );

    instrumented_cadence_mode_action
        = monitor_actions_bar->addAction(str_label("Mode: instrumented"));
    instrumented_cadence_mode_action->setCheckable(true);
    cadence_action_group->addAction(instrumented_cadence_mode_action);
    QObject::connect(
        instrumented_cadence_mode_action, &BaseAction::triggered, this,
        &main_window::on_set_instrumented_cadence_mode_triggered
    );

    sync_debug_cadence_mode_actions();
    monitor_layout->addWidget(monitor_actions_bar);

    resource_monitor_tabs = new QTabWidget(resource_monitor_window);
    monitor_layout->addWidget(resource_monitor_tabs, 1);

    auto dashboard_tab = new BaseWidget(resource_monitor_tabs);
    auto dashboard_layout = new BaseVBoxLayout(dashboard_tab);
    dashboard_layout->setContentsMargins(4, 4, 4, 4);
    dashboard_layout->setSpacing(6);

    auto summary_layout = new QGridLayout;
    auto setup_card = [](QLabel* label) {
        label->setFrameShape(QFrame::StyledPanel);
        label->setFrameShadow(QFrame::Raised);
        label->setMargin(6);
        label->setWordWrap(true);
        label->setMinimumHeight(64);
    };

    resource_monitor_summary_memory_card = new QLabel(dashboard_tab);
    resource_monitor_summary_process_card = new QLabel(dashboard_tab);
    resource_monitor_summary_stock_card = new QLabel(dashboard_tab);
    resource_monitor_summary_activity_card = new QLabel(dashboard_tab);
    resource_monitor_summary_memory_card->setToolTip(str_label(
        "Memory classes are explicit: measured vs accounted vs estimated vs "
        "derived."
    ));
    resource_monitor_summary_process_card->setToolTip(
        str_label("Measured process values come from OS sampling intervals.")
    );
    resource_monitor_summary_stock_card->setToolTip(str_label(
        "Unique stock counts are cardinality metrics, not byte totals."
    ));
    resource_monitor_summary_activity_card->setToolTip(str_label(
        "Includes workload counters and fidelity diagnostics from broadcaster "
        "backpressure."
    ));
    setup_card(resource_monitor_summary_memory_card);
    setup_card(resource_monitor_summary_process_card);
    setup_card(resource_monitor_summary_stock_card);
    setup_card(resource_monitor_summary_activity_card);
    summary_layout->addWidget(resource_monitor_summary_memory_card, 0, 0);
    summary_layout->addWidget(resource_monitor_summary_process_card, 0, 1);
    summary_layout->addWidget(resource_monitor_summary_stock_card, 1, 0);
    summary_layout->addWidget(resource_monitor_summary_activity_card, 1, 1);
    dashboard_layout->addLayout(summary_layout);

    auto semantics_glossary = new QLabel(
        str_label(
            "Glossary: measured = OS source, accounted = owned object bytes, "
            "estimated = heuristic display footprint, derived = comparison "
            "diagnostics."
        ),
        dashboard_tab
    );
    semantics_glossary->setWordWrap(true);
    semantics_glossary->setToolTip(str_label(
        "Derived values are diagnostic helpers and are not ownership classes."
    ));
    dashboard_layout->addWidget(semantics_glossary);
    auto* series_legend = new QLabel(
        str_label(
            "Color legend: "
            "<span style='color:#0072B2'>■</span> cache-accounted, "
            "<span style='color:#E69F00'>■</span> widget-local estimated, "
            "<span style='color:#009E73'>■</span> process RSS measured, "
            "<span style='color:#D55E00'>■</span> measured-accounted gap "
            "derived, "
            "<span style='color:#CC79A7'>■</span> ratio/high-water derived, "
            "<span style='color:#56B4E9'>■</span> displayed-recent counts."
        ),
        dashboard_tab
    );
    series_legend->setWordWrap(true);
    series_legend->setToolTip(
        str_label("Legend remains visible even when series are toggled on/off.")
    );
    dashboard_layout->addWidget(series_legend);

    auto primary_series_layout = new QHBoxLayout;
    resource_monitor_show_cache_bytes_series
        = new QCheckBox(str_label("Cache-accounted bytes"), dashboard_tab);
    resource_monitor_show_cache_bytes_series->setToolTip(
        str_label("Accounted bytes retained in shared cache payloads.")
    );
    resource_monitor_show_widget_local_series = new QCheckBox(
        str_label("Widget-local estimated bytes"), dashboard_tab
    );
    resource_monitor_show_widget_local_series->setToolTip(str_label(
        "Estimated display-side footprint that may overlap with cache bytes."
    ));
    resource_monitor_show_process_rss_series
        = new QCheckBox(str_label("Process RSS (OS)"), dashboard_tab);
    resource_monitor_show_process_rss_series->setToolTip(
        str_label("Measured process memory from OS sampling.")
    );
    resource_monitor_show_gap_bytes_series = new QCheckBox(
        str_label("Measured-accounted gap (derived)"), dashboard_tab
    );
    resource_monitor_show_gap_bytes_series->setToolTip(str_label(
        "Derived difference between measured RSS and accounted cache bytes."
    ));
    resource_monitor_show_ratio_series = new QCheckBox(
        str_label("Accounted/measured ratio (derived)"), dashboard_tab
    );
    resource_monitor_show_ratio_series->setToolTip(
        str_label("Derived ratio to spot long-running divergence trends.")
    );
    resource_monitor_show_cache_bytes_series->setChecked(true);
    resource_monitor_show_widget_local_series->setChecked(true);
    resource_monitor_show_process_rss_series->setChecked(true);
    resource_monitor_show_gap_bytes_series->setChecked(true);
    resource_monitor_show_ratio_series->setChecked(true);
    primary_series_layout->addWidget(resource_monitor_show_cache_bytes_series);
    primary_series_layout->addWidget(resource_monitor_show_widget_local_series);
    primary_series_layout->addWidget(resource_monitor_show_process_rss_series);
    primary_series_layout->addWidget(resource_monitor_show_gap_bytes_series);
    primary_series_layout->addWidget(resource_monitor_show_ratio_series);
    primary_series_layout->addStretch(1);
    dashboard_layout->addLayout(primary_series_layout);

    resource_monitor_primary_chart_view
        = new monitor_line_chart_widget(dashboard_tab);
    resource_monitor_primary_chart_view->set_title(
        str_label("Primary memory timelines")
    );
    resource_monitor_primary_chart_view->set_unit_label(str_label("MiB"));
    resource_monitor_primary_chart_view->set_x_axis_label(
        str_label("sample index (cache snapshots, oldest -> newest)")
    );
    dashboard_layout->addWidget(resource_monitor_primary_chart_view, 2);

    resource_monitor_ratio_chart_view
        = new monitor_line_chart_widget(dashboard_tab);
    resource_monitor_ratio_chart_view->set_title(
        str_label("Derived ratio timeline")
    );
    resource_monitor_ratio_chart_view->set_unit_label(str_label("%"));
    resource_monitor_ratio_chart_view->set_x_axis_label(
        str_label("sample index (cache snapshots, oldest -> newest)")
    );
    dashboard_layout->addWidget(resource_monitor_ratio_chart_view, 1);

    auto secondary_series_layout = new QHBoxLayout;
    resource_monitor_show_displayed_series
        = new QCheckBox(str_label("Displayed-recent entries"), dashboard_tab);
    resource_monitor_show_displayed_series->setToolTip(str_label(
        "Recent-window heuristic; this is not exact on-screen visibility."
    ));
    resource_monitor_show_pending_series
        = new QCheckBox(str_label("Pending raster requests"), dashboard_tab);
    resource_monitor_show_pending_series->setToolTip(
        str_label("Work families queued for rasterization.")
    );
    resource_monitor_show_flight_series
        = new QCheckBox(str_label("In-flight raster families"), dashboard_tab);
    resource_monitor_show_flight_series->setToolTip(
        str_label("Families currently being rasterized.")
    );
    resource_monitor_show_event_series
        = new QCheckBox(str_label("Event markers"), dashboard_tab);
    resource_monitor_show_event_series->setToolTip(
        str_label("Manual and system markers for comparing bounded workloads.")
    );
    resource_monitor_show_displayed_series->setChecked(true);
    resource_monitor_show_pending_series->setChecked(true);
    resource_monitor_show_flight_series->setChecked(true);
    resource_monitor_show_event_series->setChecked(true);
    secondary_series_layout->addWidget(resource_monitor_show_displayed_series);
    secondary_series_layout->addWidget(resource_monitor_show_pending_series);
    secondary_series_layout->addWidget(resource_monitor_show_flight_series);
    secondary_series_layout->addWidget(resource_monitor_show_event_series);
    secondary_series_layout->addStretch(1);
    dashboard_layout->addLayout(secondary_series_layout);

    resource_monitor_secondary_chart_view
        = new monitor_line_chart_widget(dashboard_tab);
    resource_monitor_secondary_chart_view->set_title(
        str_label("Activity timelines")
    );
    resource_monitor_secondary_chart_view->set_unit_label(str_label("count"));
    resource_monitor_secondary_chart_view->set_x_axis_label(
        str_label("sample index (activity snapshots, oldest -> newest)")
    );
    dashboard_layout->addWidget(resource_monitor_secondary_chart_view, 1);

    resource_monitor_composition_chart_view
        = new monitor_pie_chart_widget(dashboard_tab);
    resource_monitor_composition_chart_view->set_title(
        str_label("Composition view (explicit subsets)")
    );
    resource_monitor_composition_chart_view->setToolTip(str_label(
        "Entry-count subsets only; this view does not represent byte totals."
    ));
    dashboard_layout->addWidget(resource_monitor_composition_chart_view, 1);

    resource_monitor_tabs->addTab(dashboard_tab, str_label("Dashboard"));

    auto timeline_tab = new BaseWidget(resource_monitor_tabs);
    auto timeline_layout = new BaseVBoxLayout(timeline_tab);
    timeline_layout->setContentsMargins(4, 4, 4, 4);
    timeline_layout->setSpacing(6);
    resource_monitor_timeline_text = new QPlainTextEdit(timeline_tab);
    resource_monitor_timeline_text->setReadOnly(true);
    resource_monitor_timeline_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    timeline_layout->addWidget(resource_monitor_timeline_text);
    resource_monitor_tabs->addTab(timeline_tab, str_label("Timeline"));

    auto geometry_tab = new BaseWidget(resource_monitor_tabs);
    auto geometry_layout = new BaseVBoxLayout(geometry_tab);
    geometry_layout->setContentsMargins(4, 4, 4, 4);
    geometry_layout->setSpacing(6);
    auto geometry_splitter = new QSplitter(Qt::Vertical, geometry_tab);
    resource_monitor_geometry_view
        = new monitor_geometry_schematic_widget(geometry_splitter);
    resource_monitor_geometry_view->setToolTip(str_label(
        "Coverage and generation legend: active/warming generation ids, "
        "prewarm state, and preload spread region."
    ));
    resource_monitor_geometry_text = new QPlainTextEdit(geometry_splitter);
    resource_monitor_geometry_text->setReadOnly(true);
    resource_monitor_geometry_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    resource_monitor_geometry_text->setMaximumBlockCount(512);
    geometry_splitter->addWidget(resource_monitor_geometry_view);
    geometry_splitter->addWidget(resource_monitor_geometry_text);
    geometry_splitter->setStretchFactor(0, 4);
    geometry_splitter->setStretchFactor(1, 2);
    geometry_layout->addWidget(geometry_splitter);
    resource_monitor_tabs->addTab(geometry_tab, str_label("Geometry"));

    auto resize_history_tab = new BaseWidget(resource_monitor_tabs);
    auto resize_history_layout = new BaseVBoxLayout(resize_history_tab);
    resize_history_layout->setContentsMargins(4, 4, 4, 4);
    resize_history_layout->setSpacing(6);
    auto resize_splitter = new QSplitter(Qt::Vertical, resize_history_tab);
    resource_monitor_resize_history_view
        = new monitor_resize_history_widget(resize_splitter);
    resource_monitor_resize_history_view->setToolTip(str_label(
        "Timeline x-axis is resize order; bars encode prewarm completion "
        "duration in milliseconds."
    ));
    resource_monitor_resize_history_text = new QPlainTextEdit(resize_splitter);
    resource_monitor_resize_history_text->setReadOnly(true);
    resource_monitor_resize_history_text->setLineWrapMode(
        QPlainTextEdit::NoWrap
    );
    resource_monitor_resize_history_text->setMaximumBlockCount(768);
    resize_splitter->addWidget(resource_monitor_resize_history_view);
    resize_splitter->addWidget(resource_monitor_resize_history_text);
    resize_splitter->setStretchFactor(0, 4);
    resize_splitter->setStretchFactor(1, 2);
    resize_history_layout->addWidget(resize_splitter);
    resource_monitor_tabs->addTab(
        resize_history_tab, str_label("Resize history")
    );

    auto diagnostics_tab = new BaseWidget(resource_monitor_tabs);
    auto diagnostics_layout = new BaseVBoxLayout(diagnostics_tab);
    diagnostics_layout->setContentsMargins(4, 4, 4, 4);
    diagnostics_layout->setSpacing(6);
    resource_monitor_diagnostics_text = new QPlainTextEdit(diagnostics_tab);
    resource_monitor_diagnostics_text->setReadOnly(true);
    resource_monitor_diagnostics_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    diagnostics_layout->addWidget(resource_monitor_diagnostics_text);
    resource_monitor_tabs->addTab(diagnostics_tab, str_label("Diagnostics"));

    resource_monitor_timeline_group = nullptr;
    resource_monitor_diagnostics_group = nullptr;

    resource_monitor_window->setLayout(monitor_layout);
    resource_monitor_window->hide();

    QObject::connect(
        resource_monitor_window, &QDialog::finished, this,
        [this](int) { on_resource_monitor_visibility_changed(false); }
    );
    QObject::connect(
        debug_telemetry_collector, &resource_monitor::cache_snapshot_collected,
        this, &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        debug_telemetry_collector, &resource_monitor::event_recorded, this,
        &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        debug_telemetry_collector,
        &resource_monitor::geometry_snapshot_collected, this,
        &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        debug_telemetry_collector, &resource_monitor::resize_history_recorded,
        this, &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        debug_telemetry_collector,
        &resource_monitor::debug_broadcaster_state_changed, this,
        &main_window::on_resource_monitor_data_changed
    );

    QObject::connect(
        resource_monitor_show_cache_bytes_series, &QCheckBox::toggled, this,
        &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        resource_monitor_show_widget_local_series, &QCheckBox::toggled, this,
        &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        resource_monitor_show_process_rss_series, &QCheckBox::toggled, this,
        &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        resource_monitor_show_gap_bytes_series, &QCheckBox::toggled, this,
        &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        resource_monitor_show_ratio_series, &QCheckBox::toggled, this,
        &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        resource_monitor_show_displayed_series, &QCheckBox::toggled, this,
        &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        resource_monitor_show_pending_series, &QCheckBox::toggled, this,
        &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        resource_monitor_show_flight_series, &QCheckBox::toggled, this,
        &main_window::on_resource_monitor_data_changed
    );
    QObject::connect(
        resource_monitor_show_event_series, &QCheckBox::toggled, this,
        &main_window::on_resource_monitor_data_changed
    );

    refresh_resource_monitor_view();
}

#endif
