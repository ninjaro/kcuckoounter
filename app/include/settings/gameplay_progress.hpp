#ifndef KCUCKOOUNTER_SETTINGS_GAMEPLAY_PROGRESS_HPP
#define KCUCKOOUNTER_SETTINGS_GAMEPLAY_PROGRESS_HPP

#include "table/gameplay_types.hpp"

#include <QByteArray>
#include <QString>

class QSettings;

struct gameplay_result_record {
    std::uint64_t sequence = 0;
    std::int64_t completed_at_utc_ms = 0;
    gameplay::session_result result;
    bool operator==(const gameplay_result_record&) const = default;
};

/// Raw lifetime counters and bounded newest-first history, not a ranked score.
struct gameplay_progress {
    static constexpr std::size_t maximum_recent_results = 20;
    std::uint64_t completed_sessions = 0;
    gameplay::session_statistics challenge;
    gameplay::session_timing timing;
    std::vector<gameplay_result_record> recent_results;
    bool operator==(const gameplay_progress&) const = default;
};

class gameplay_progress_service {
public:
    static constexpr int schema_version = 1;
    static constexpr qsizetype maximum_document_bytes = 8 * 1024 * 1024;
    static constexpr std::size_t maximum_result_targets = 65'536;
    gameplay_progress_service(
        QSettings& settings, gameplay::slot_limits limits
    );
    /// Absent returns null without error; unreadable/future data returns an
    /// error and blocks writes. Does not read, migrate or rank legacy history.
    [[nodiscard]] std::optional<gameplay_progress>
    load(QString* error = nullptr) const;
    /// Owner-serialized admission of exactly the next sequence. Identical
    /// retained retries succeed; conflicting, skipped or evicted retries fail.
    /// Caller must preserve sequence/time across retries and host recovery.
    [[nodiscard]] bool
    record(const gameplay_result_record& record, QString* error = nullptr);

private:
    QSettings& settings;
    gameplay::slot_limits limits;
};

[[nodiscard]] std::optional<QByteArray> encode_gameplay_progress(
    const gameplay_progress& progress, gameplay::slot_limits limits
);
[[nodiscard]] std::optional<gameplay_progress>
decode_gameplay_progress(const QByteArray& bytes, gameplay::slot_limits limits);

#endif // KCUCKOOUNTER_SETTINGS_GAMEPLAY_PROGRESS_HPP
