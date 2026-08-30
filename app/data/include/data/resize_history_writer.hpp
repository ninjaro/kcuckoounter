#ifndef KCUCKOOUNTER_MONITOR_RESIZE_HISTORY_WRITER_HPP
#define KCUCKOOUNTER_MONITOR_RESIZE_HISTORY_WRITER_HPP

#include <QByteArray>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QString>

class resize_history_writer : public QObject {
    Q_OBJECT

public:
    explicit resize_history_writer(
        QObject* parent = nullptr, qint64 maximum_pending_bytes = 256 * 1024,
        qint64 maximum_batch_bytes = 64 * 1024,
        qint64 maximum_file_bytes = 8 * 1024 * 1024,
        int maximum_retained_file_count = 8
    );
    ~resize_history_writer() override;

    void set_output_path(const QString& output_path);
    [[nodiscard]] QString output_path() const;
    void append_line(const QByteArray& jsonl_line);
    void close(qint64 deadline_ms = 50);
    [[nodiscard]] QJsonObject runtime_state() const;

signals:
    void
    warning_raised(const QString& warning_code, const QString& warning_message);

private slots:
    void on_write_finished();

private:
    struct batch_write_result {
        QString error_message;
        qint64 active_file_bytes = 0;
        qint64 retained_bytes = 0;
        qint64 rotations = 0;
        qint64 retention_deletions = 0;
        int retained_file_count = 0;
    };

    QString target_path;
    qint64 pending_byte_limit;
    qint64 batch_byte_limit;
    qint64 file_byte_limit;
    int retained_file_limit;
    QFutureWatcher<batch_write_result>* active_watcher;
    QQueue<QByteArray> pending_lines;
    qint64 pending_bytes;
    qint64 dropped_lines;
    qint64 write_errors;
    qint64 detached_writes;
    qint64 completed_rotations;
    qint64 retention_deletions;
    qint64 active_file_bytes;
    qint64 retained_bytes;
    int retained_file_count;
    bool closed;

    void apply_write_result(const batch_write_result& result);
    void drain();
    void record_write_error(const QString& error_message);
    static batch_write_result append_batch(
        const QString& output_path, const QByteArray& bytes,
        qint64 maximum_file_bytes, int maximum_retained_file_count
    );
};

#endif // KCUCKOOUNTER_MONITOR_RESIZE_HISTORY_WRITER_HPP
