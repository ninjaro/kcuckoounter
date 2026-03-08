#ifndef KCUCKOOUNTER_MONITOR_MONITOR_CHART_HELPERS_HPP
#define KCUCKOOUNTER_MONITOR_MONITOR_CHART_HELPERS_HPP

#include "monitor/monitor_visual_widgets.hpp"
#include "monitor/resource_monitor.hpp"

#include <QCheckBox>
#include <QColor>
#include <QString>
#include <QVector>

#include <cmath>
#include <limits>

namespace monitor_chart_helpers {

template <typename ValueFn, typename AvailableFn>
inline void append_if_checked(
    const QVector<resource_monitor::cache_timeline_entry>& rows,
    const QCheckBox* toggle, const QString& label, const QColor& color,
    ValueFn value_fn, AvailableFn available_fn,
    QVector<monitor_line_chart_widget::series>* out
) {
    if (toggle == nullptr || out == nullptr || !toggle->isChecked()) {
        return;
    }

    monitor_line_chart_widget::series line;
    line.label = label;
    line.color = color;
    line.values.reserve(rows.size());

    for (const resource_monitor::cache_timeline_entry& row : rows) {
        if (available_fn(row)) {
            line.values.push_back(value_fn(row));
        } else {
            line.values.push_back(std::numeric_limits<double>::quiet_NaN());
        }
    }

    out->push_back(line);
}

inline bool always_available(const resource_monitor::cache_timeline_entry&) {
    return true;
}

} // namespace monitor_chart_helpers

#endif // KCUCKOOUNTER_MONITOR_MONITOR_CHART_HELPERS_HPP
