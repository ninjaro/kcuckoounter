#ifndef KCUCKOOUNTER_MONITOR_MONITOR_PARITY_CHECKER_HPP
#define KCUCKOOUNTER_MONITOR_MONITOR_PARITY_CHECKER_HPP

#include <QJsonObject>
#include <QString>
#include <QVector>

class monitor_parity_checker {
public:
    struct parity_result {
        QVector<QString> warnings;
        qint64 compared_metric_count = 0;
        qint64 compared_message_count = 0;

        bool ok() const { return warnings.isEmpty(); }
    };

    static parity_result compare_embedded_snapshot_and_external_messages(
        const QJsonObject& embedded_snapshot_export,
        const QVector<QJsonObject>& external_messages,
        qint64 byte_tolerance = 4 * 1024 * 1024
    );

    static QVector<QJsonObject>
    parse_external_history_jsonl(const QByteArray& jsonl_data);
};

#endif // KCUCKOOUNTER_MONITOR_MONITOR_PARITY_CHECKER_HPP
