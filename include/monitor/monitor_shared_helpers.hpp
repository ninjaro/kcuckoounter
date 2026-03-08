#ifndef KCUCKOOUNTER_MONITOR_MONITOR_SHARED_HELPERS_HPP
#define KCUCKOOUNTER_MONITOR_MONITOR_SHARED_HELPERS_HPP

#include <QColor>
#include <QDateTime>
#include <QSize>
#include <QString>
#include <QVector>

#include <cmath>
#include <limits>

namespace monitor_palette {

inline const QColor& blue() {
    static const QColor color(0, 114, 178);
    return color;
}

inline const QColor& orange() {
    static const QColor color(230, 159, 0);
    return color;
}

inline const QColor& green() {
    static const QColor color(0, 158, 115);
    return color;
}

inline const QColor& red() {
    static const QColor color(213, 94, 0);
    return color;
}

inline const QColor& purple() {
    static const QColor color(204, 121, 167);
    return color;
}

inline const QColor& sky_blue() {
    static const QColor color(86, 180, 233);
    return color;
}

inline const QColor& gray() {
    static const QColor color(127, 127, 127);
    return color;
}

} // namespace monitor_palette

namespace monitor_shared {

inline double to_mib(qint64 bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

inline double to_mib_nullable(qint64 bytes) {
    if (bytes < 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return to_mib(bytes);
}

inline QString mib_text(qint64 bytes, int precision = 2) {
    return QString::number(to_mib(bytes), 'f', precision);
}

inline QString size_px_text(const QSize& size) {
    if (size.width() <= 0 || size.height() <= 0) {
        return QStringLiteral("n/a");
    }
    return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

inline QString utc_now_text() {
    return QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz 'UTC'")
    );
}

template <typename T>
inline void trim_series(QVector<T>* values, int max_size) {
    if (values == nullptr || max_size <= 0) {
        return;
    }
    while (values->size() > max_size) {
        values->removeFirst();
    }
}

} // namespace monitor_shared

#endif // KCUCKOOUNTER_MONITOR_MONITOR_SHARED_HELPERS_HPP
