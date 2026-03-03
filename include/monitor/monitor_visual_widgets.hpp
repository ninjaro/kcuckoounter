#ifndef KCUCKOOUNTER_MONITOR_MONITOR_VISUAL_WIDGETS_HPP
#define KCUCKOOUNTER_MONITOR_MONITOR_VISUAL_WIDGETS_HPP

#include "monitor/geometry_debug_telemetry.hpp"

#include <QColor>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class monitor_line_chart_widget : public QWidget {
    Q_OBJECT

public:
    struct series {
        QString label;
        QColor color;
        QVector<double> values;
    };

    explicit monitor_line_chart_widget(QWidget* parent = nullptr);
    void set_title(const QString& title);
    void set_unit_label(const QString& unit_label);
    void set_series(const QVector<series>& series_list);
    void set_footer_lines(const QStringList& footer_lines);
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString title_text;
    QString unit_text;
    QVector<series> chart_series;
    QStringList footer_text_lines;
};

class monitor_pie_chart_widget : public QWidget {
    Q_OBJECT

public:
    struct slice {
        QString label;
        QColor color;
        double value = 0.0;
    };

    explicit monitor_pie_chart_widget(QWidget* parent = nullptr);
    void set_title(const QString& title);
    void set_slices(const QVector<slice>& slice_list);
    void set_footer_text(const QString& footer_text);
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString title_text;
    QVector<slice> chart_slices;
    QString footer;
};

class monitor_geometry_schematic_widget : public QWidget {
    Q_OBJECT

public:
    explicit monitor_geometry_schematic_widget(QWidget* parent = nullptr);
    void set_snapshot(const geometry_debug_snapshot& snapshot);
    void clear_snapshot();
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool has_snapshot;
    geometry_debug_snapshot current_snapshot;
};

class monitor_resize_history_widget : public QWidget {
    Q_OBJECT

public:
    struct resize_entry {
        qint64 timestamp_ms = 0;
        qint64 prewarm_completion_ms = -1;
        int old_active_bucket_px = 0;
        int new_active_bucket_px = 0;
        QSize old_window_size;
        QSize new_window_size;
    };

    explicit monitor_resize_history_widget(QWidget* parent = nullptr);
    void set_entries(const QVector<resize_entry>& entries);
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<resize_entry> recent_entries;
};

#endif // KCUCKOOUNTER_MONITOR_MONITOR_VISUAL_WIDGETS_HPP
