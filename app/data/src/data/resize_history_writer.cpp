#include "data/resize_history_writer.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QThread>
#include <QtConcurrent>

#include <algorithm>
#include <ranges>

namespace {

QString rotated_segment_path(const QString& output_path, int segment_index) {
    const QFileInfo output_info(output_path);
    const QString suffix = output_info.completeSuffix();
    const QString file_name = suffix.isEmpty()
        ? QStringLiteral("%1.%2").arg(output_info.fileName()).arg(segment_index)
        : QStringLiteral("%1.%2.%3")
              .arg(output_info.completeBaseName())
              .arg(segment_index)
              .arg(suffix);
    return output_info.dir().filePath(file_name);
}

QMap<int, QString> rotated_segments(const QString& output_path) {
    const QFileInfo output_info(output_path);
    const QString suffix = output_info.completeSuffix();
    const QString suffix_expression = suffix.isEmpty()
        ? QString()
        : QStringLiteral("\\.%1").arg(QRegularExpression::escape(suffix));
    const QRegularExpression segment_expression(
        QStringLiteral("^%1\\.([1-9][0-9]*)%2$")
            .arg(
                QRegularExpression::escape(
                    suffix.isEmpty() ? output_info.fileName()
                                     : output_info.completeBaseName()
                ),
                suffix_expression
            )
    );

    QMap<int, QString> result;
    const QFileInfoList entries
        = output_info.dir().entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        const QRegularExpressionMatch match
            = segment_expression.match(entry.fileName());
        if (!match.hasMatch()) {
            continue;
        }
        bool parsed = false;
        const int index = match.captured(1).toInt(&parsed);
        if (parsed) {
            result.insert(index, entry.absoluteFilePath());
        }
    }
    return result;
}

QString remove_file(const QString& path, qint64* deletion_count) {
    if (!QFileInfo::exists(path)) {
        return {};
    }
    QFile file(path);
    if (!file.remove()) {
        return QStringLiteral("unable to remove retained resize history %1: %2")
            .arg(path, file.errorString());
    }
    ++(*deletion_count);
    return {};
}

QString rotate_segments(
    const QString& output_path, int maximum_retained_file_count,
    qint64* deletion_count
) {
    QMap<int, QString> segments = rotated_segments(output_path);
    const QList<int> indices = segments.keys();
    for (int old_index : std::views::reverse(indices)) {
        const QString old_path = segments.value(old_index);
        const int new_index = old_index + 1;
        if (new_index >= maximum_retained_file_count) {
            const QString error = remove_file(old_path, deletion_count);
            if (!error.isEmpty()) {
                return error;
            }
            continue;
        }

        const QString new_path = rotated_segment_path(output_path, new_index);
        QFile file(old_path);
        if (!file.rename(new_path)) {
            return QStringLiteral(
                       "unable to rotate resize history %1 to %2: %3"
            )
                .arg(old_path, new_path, file.errorString());
        }
    }

    if (maximum_retained_file_count == 1) {
        return remove_file(output_path, deletion_count);
    }

    QFile active_file(output_path);
    const QString rotated_path = rotated_segment_path(output_path, 1);
    if (!active_file.rename(rotated_path)) {
        return QStringLiteral("unable to rotate resize history %1 to %2: %3")
            .arg(output_path, rotated_path, active_file.errorString());
    }
    return {};
}

QString enforce_retention(
    const QString& output_path, int maximum_retained_file_count,
    qint64* deletion_count
) {
    const QMap<int, QString> segments = rotated_segments(output_path);
    for (auto iterator = segments.cbegin(); iterator != segments.cend();
         ++iterator) {
        if (iterator.key() < maximum_retained_file_count) {
            continue;
        }
        const QString error = remove_file(iterator.value(), deletion_count);
        if (!error.isEmpty()) {
            return error;
        }
    }
    return {};
}

struct retained_state {
    qint64 active_file_bytes = 0;
    qint64 retained_bytes = 0;
    int retained_file_count = 0;
};

retained_state inspect_retained_state(const QString& output_path) {
    retained_state state;
    const QFileInfo active_info(output_path);
    if (active_info.isFile()) {
        state.active_file_bytes = active_info.size();
        state.retained_bytes += active_info.size();
        ++state.retained_file_count;
    }
    const QMap<int, QString> segments = rotated_segments(output_path);
    for (const QString& path : segments) {
        const QFileInfo segment_info(path);
        if (!segment_info.isFile()) {
            continue;
        }
        state.retained_bytes += segment_info.size();
        ++state.retained_file_count;
    }
    return state;
}

} // namespace

resize_history_writer::resize_history_writer(
    QObject* parent, qint64 maximum_pending_bytes, qint64 maximum_batch_bytes,
    qint64 maximum_file_bytes, int maximum_retained_file_count
)
    : QObject(parent)
    , target_path()
    , pending_byte_limit(std::max<qint64>(1, maximum_pending_bytes))
    , batch_byte_limit(std::max<qint64>(1, maximum_batch_bytes))
    , file_byte_limit(std::max<qint64>(1, maximum_file_bytes))
    , retained_file_limit(std::max(1, maximum_retained_file_count))
    , active_watcher(nullptr)
    , pending_lines()
    , pending_bytes(0)
    , dropped_lines(0)
    , write_errors(0)
    , detached_writes(0)
    , completed_rotations(0)
    , retention_deletions(0)
    , active_file_bytes(0)
    , retained_bytes(0)
    , retained_file_count(0)
    , closed(false) { }

resize_history_writer::~resize_history_writer() {
    QObject::disconnect(this, nullptr, nullptr, nullptr);
    close();
}

void resize_history_writer::set_output_path(const QString& output_path) {
    if (target_path == output_path) {
        return;
    }
    close();
    target_path = output_path;
    completed_rotations = 0;
    retention_deletions = 0;
    active_file_bytes = 0;
    retained_bytes = 0;
    retained_file_count = 0;
    closed = target_path.isEmpty();
}

QString resize_history_writer::output_path() const { return target_path; }

void resize_history_writer::append_line(const QByteArray& jsonl_line) {
    if (jsonl_line.isEmpty()) {
        return;
    }
    if (closed) {
        ++dropped_lines;
        emit warning_raised(
            QStringLiteral("resize_history_writer_closed"),
            QStringLiteral("resize history record dropped after writer close")
        );
        return;
    }
    if (target_path.isEmpty()) {
        record_write_error(
            QStringLiteral("resize history output path is not configured")
        );
        return;
    }

    const auto line_bytes = static_cast<qint64>(jsonl_line.size());
    if (line_bytes > pending_byte_limit
        || pending_bytes > pending_byte_limit - line_bytes) {
        ++dropped_lines;
        emit warning_raised(
            QStringLiteral("resize_history_queue_full"),
            QStringLiteral(
                "Resize history persistence queue reached its %1-byte bound; "
                "the newest record was dropped."
            )
                .arg(pending_byte_limit)
        );
        return;
    }

    pending_lines.enqueue(jsonl_line);
    pending_bytes += line_bytes;
    drain();
}

void resize_history_writer::drain() {
    if (closed || active_watcher != nullptr || pending_lines.isEmpty()
        || target_path.isEmpty()) {
        return;
    }

    QByteArray batch;
    while (!pending_lines.isEmpty()) {
        const qsizetype next_size = pending_lines.head().size();
        if (!batch.isEmpty() && batch.size() + next_size > batch_byte_limit) {
            break;
        }
        batch.append(pending_lines.dequeue());
        pending_bytes -= static_cast<qint64>(next_size);
    }
    pending_bytes = std::max<qint64>(0, pending_bytes);

    auto* watcher = new QFutureWatcher<batch_write_result>(this);
    active_watcher = watcher;
    QObject::connect(
        watcher, &QFutureWatcher<batch_write_result>::finished, this,
        &resize_history_writer::on_write_finished
    );
    watcher->setFuture(
        QtConcurrent::run(
            append_batch, target_path, batch, file_byte_limit,
            retained_file_limit
        )
    );
}

void resize_history_writer::apply_write_result(
    const batch_write_result& result
) {
    completed_rotations += result.rotations;
    retention_deletions += result.retention_deletions;
    active_file_bytes = result.active_file_bytes;
    retained_bytes = result.retained_bytes;
    retained_file_count = result.retained_file_count;
    if (!result.error_message.isEmpty()) {
        record_write_error(result.error_message);
    }
}

void resize_history_writer::on_write_finished() {
    auto* watcher = dynamic_cast<QFutureWatcher<batch_write_result>*>(sender());
    if (watcher == nullptr) {
        return;
    }
    if (watcher != active_watcher) {
        watcher->deleteLater();
        return;
    }

    active_watcher = nullptr;
    apply_write_result(watcher->result());
    watcher->deleteLater();
    drain();
}

void resize_history_writer::close(qint64 deadline_ms) {
    if (closed) {
        return;
    }
    const qint64 bounded_deadline_ms = std::max<qint64>(0, deadline_ms);
    QElapsedTimer timer;
    timer.start();

    while (active_watcher != nullptr && timer.elapsed() < bounded_deadline_ms) {
        if (active_watcher->isFinished()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            if (active_watcher != nullptr && active_watcher->isFinished()) {
                QFutureWatcher<batch_write_result>* watcher = active_watcher;
                active_watcher = nullptr;
                QObject::disconnect(watcher, nullptr, this, nullptr);
                apply_write_result(watcher->result());
                watcher->deleteLater();
                drain();
            }
            continue;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        QThread::msleep(1);
    }

    bool detached_active_write = false;
    if (active_watcher != nullptr) {
        QFutureWatcher<batch_write_result>* watcher = active_watcher;
        active_watcher = nullptr;
        QObject::disconnect(watcher, nullptr, this, nullptr);
        watcher->setParent(nullptr);
        delete watcher;
        ++detached_writes;
        detached_active_write = true;
    }

    const auto dropped_pending = static_cast<qint64>(pending_lines.size());
    dropped_lines += dropped_pending;
    pending_lines.clear();
    pending_bytes = 0;
    closed = true;

    if (detached_active_write) {
        emit warning_raised(
            QStringLiteral("resize_history_close_timeout"),
            QStringLiteral(
                "resize history close reached its %1 ms deadline; the "
                "value-only active write was detached"
            )
                .arg(bounded_deadline_ms)
        );
    }
    if (dropped_pending > 0) {
        emit warning_raised(
            QStringLiteral("resize_history_close_dropped_pending"),
            QStringLiteral(
                "resize history close dropped %1 queued records at its "
                "deadline"
            )
                .arg(dropped_pending)
        );
    }
}

QJsonObject resize_history_writer::runtime_state() const {
    QJsonObject state;
    state.insert(QStringLiteral("output_path"), target_path);
    state.insert(
        QStringLiteral("pending_line_count"),
        static_cast<qint64>(pending_lines.size())
    );
    state.insert(QStringLiteral("pending_bytes"), pending_bytes);
    state.insert(QStringLiteral("maximum_pending_bytes"), pending_byte_limit);
    state.insert(QStringLiteral("maximum_batch_bytes"), batch_byte_limit);
    state.insert(QStringLiteral("maximum_file_bytes"), file_byte_limit);
    state.insert(
        QStringLiteral("maximum_retained_file_count"), retained_file_limit
    );
    state.insert(QStringLiteral("write_in_flight"), active_watcher != nullptr);
    state.insert(QStringLiteral("dropped_line_count"), dropped_lines);
    state.insert(QStringLiteral("write_error_count"), write_errors);
    state.insert(QStringLiteral("detached_write_count"), detached_writes);
    state.insert(QStringLiteral("rotation_count"), completed_rotations);
    state.insert(
        QStringLiteral("retention_deletion_count"), retention_deletions
    );
    state.insert(QStringLiteral("active_file_bytes"), active_file_bytes);
    state.insert(QStringLiteral("retained_bytes"), retained_bytes);
    state.insert(QStringLiteral("retained_file_count"), retained_file_count);
    state.insert(QStringLiteral("closed"), closed);
    return state;
}

void resize_history_writer::record_write_error(const QString& error_message) {
    ++write_errors;
    emit warning_raised(
        QStringLiteral("resize_history_write_failed"), error_message
    );
}

resize_history_writer::batch_write_result resize_history_writer::append_batch(
    const QString& output_path, const QByteArray& bytes,
    qint64 maximum_file_bytes, int maximum_retained_file_count
) {
    batch_write_result result;
    const QFileInfo existing_file(output_path);
    const qint64 existing_bytes
        = existing_file.isFile() ? existing_file.size() : 0;
    const auto batch_bytes = static_cast<qint64>(bytes.size());
    const bool exceeds_file_limit = batch_bytes > maximum_file_bytes
        || existing_bytes > maximum_file_bytes - batch_bytes;
    if (existing_bytes > 0 && exceeds_file_limit) {
        result.error_message = rotate_segments(
            output_path, maximum_retained_file_count,
            &result.retention_deletions
        );
        if (!result.error_message.isEmpty()) {
            return result;
        }
        ++result.rotations;
    }

    QFile file(output_path);
    if (!file.open(
            QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text
        )) {
        result.error_message
            = QStringLiteral("unable to open resize history: %1")
                  .arg(file.errorString());
        return result;
    }
    const qint64 written = file.write(bytes);
    if (written != bytes.size()) {
        result.error_message
            = QStringLiteral("unable to append complete resize history: %1")
                  .arg(file.errorString());
        return result;
    }
    if (!file.flush()) {
        result.error_message
            = QStringLiteral("unable to flush resize history: %1")
                  .arg(file.errorString());
        return result;
    }
    file.close();

    result.error_message = enforce_retention(
        output_path, maximum_retained_file_count, &result.retention_deletions
    );
    const retained_state state = inspect_retained_state(output_path);
    result.active_file_bytes = state.active_file_bytes;
    result.retained_bytes = state.retained_bytes;
    result.retained_file_count = state.retained_file_count;
    return result;
}
