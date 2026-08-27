#include "settings/training_progress.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const QString progress_group = QStringLiteral("training_progress");
const QString schema_version_key = QStringLiteral("schema_version");
constexpr qint64 maximum_counter = 1000000000LL;
constexpr qint64 maximum_elapsed_ms
    = 100LL * 365LL * 24LL * 60LL * 60LL * 1000LL;

bool valid_result(const completed_training_result& result) {
    return result.correct >= 0 && result.answered > 0
        && result.correct <= result.answered && result.answered <= 1000000
        && result.elapsed_ms >= 0 && result.elapsed_ms <= maximum_elapsed_ms
        && result.completed_at_utc_ms >= 0;
}

bool json_integer(
    const QJsonValue& value, qint64 minimum, qint64 maximum, qint64* output
) {
    if (!value.isDouble() || output == nullptr) {
        return false;
    }
    const double number = value.toDouble();
    const double maximum_as_double
        = maximum == std::numeric_limits<qint64>::max()
        ? std::nextafter(
              static_cast<double>(maximum),
              -std::numeric_limits<double>::infinity()
          )
        : static_cast<double>(maximum);
    if (!std::isfinite(number) || number < static_cast<double>(minimum)
        || number > maximum_as_double) {
        return false;
    }
    const qint64 integer = static_cast<qint64>(number);
    if (static_cast<double>(integer) != number) {
        return false;
    }
    *output = integer;
    return true;
}

bool valid_progress(const training_progress& progress) {
    if (progress.completed_sessions < 0
        || progress.completed_sessions > maximum_counter
        || progress.correct_answers < 0
        || progress.correct_answers > maximum_counter
        || progress.answered_questions < progress.correct_answers
        || progress.answered_questions > maximum_counter
        || progress.elapsed_ms < 0 || progress.elapsed_ms > maximum_elapsed_ms
        || progress.best_correct < 0 || progress.best_answered < 0
        || progress.best_correct > progress.best_answered
        || progress.best_answered > 1000000 || progress.best_elapsed_ms < 0
        || progress.best_elapsed_ms > maximum_elapsed_ms
        || progress.recent_results.size()
            > training_progress::maximum_recent_results) {
        return false;
    }
    return std::ranges::all_of(progress.recent_results, valid_result);
}

QByteArray
serialize_recent_results(const QVector<completed_training_result>& results) {
    QJsonArray array;
    for (const completed_training_result& result : results) {
        array.append(
            QJsonObject {
                { QStringLiteral("correct"), result.correct },
                { QStringLiteral("answered"), result.answered },
                { QStringLiteral("elapsed_ms"), result.elapsed_ms },
                { QStringLiteral("completed_at_utc_ms"),
                  result.completed_at_utc_ms },
            }
        );
    }
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QVector<completed_training_result>
parse_recent_results(const QByteArray& payload) {
    if (payload.isEmpty() || payload.size() > 64 * 1024) {
        return {};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()
        || document.array().size()
            > training_progress::maximum_recent_results) {
        return {};
    }

    QVector<completed_training_result> results;
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject()) {
            return {};
        }
        const QJsonObject object = value.toObject();
        qint64 correct = 0;
        qint64 answered = 0;
        qint64 elapsed_ms = 0;
        qint64 completed_at_utc_ms = 0;
        if (!json_integer(
                object.value(QStringLiteral("correct")), 0, 1000000, &correct
            )
            || !json_integer(
                object.value(QStringLiteral("answered")), 1, 1000000, &answered
            )
            || !json_integer(
                object.value(QStringLiteral("elapsed_ms")), 0,
                maximum_elapsed_ms, &elapsed_ms
            )
            || !json_integer(
                object.value(QStringLiteral("completed_at_utc_ms")), 0,
                std::numeric_limits<qint64>::max(), &completed_at_utc_ms
            )) {
            return {};
        }
        completed_training_result result {
            .correct = static_cast<int>(correct),
            .answered = static_cast<int>(answered),
            .elapsed_ms = elapsed_ms,
            .completed_at_utc_ms = completed_at_utc_ms,
        };
        if (!valid_result(result)) {
            return {};
        }
        results.append(result);
    }
    return results;
}

qint64 bounded_counter(const QVariant& value, qint64 maximum) {
    bool ok = false;
    const qint64 converted = value.toLongLong(&ok);
    return ok && converted >= 0 && converted <= maximum ? converted : 0;
}

int bounded_integer(const QVariant& value, int maximum) {
    return static_cast<int>(bounded_counter(value, maximum));
}

bool is_better_result(
    const completed_training_result& candidate, const training_progress& current
) {
    if (current.best_answered <= 0) {
        return true;
    }
    const qint64 candidate_ratio
        = static_cast<qint64>(candidate.correct) * current.best_answered;
    const qint64 current_ratio
        = static_cast<qint64>(current.best_correct) * candidate.answered;
    if (candidate_ratio != current_ratio) {
        return candidate_ratio > current_ratio;
    }
    if (candidate.correct != current.best_correct) {
        return candidate.correct > current.best_correct;
    }
    return candidate.elapsed_ms < current.best_elapsed_ms;
}

} // namespace

training_progress_service::training_progress_service(QSettings& settings_value)
    : settings(settings_value) { }

training_progress training_progress_service::load() const {
    settings.beginGroup(progress_group);
    bool version_ok = false;
    const int version = settings.value(schema_version_key).toInt(&version_ok);
    if (!version_ok || version != training_progress::schema_version) {
        settings.endGroup();
        return {};
    }

    training_progress result;
    result.completed_sessions = bounded_counter(
        settings.value(QStringLiteral("totals/completed_sessions")),
        maximum_counter
    );
    result.correct_answers = bounded_counter(
        settings.value(QStringLiteral("totals/correct_answers")),
        maximum_counter
    );
    result.answered_questions = bounded_counter(
        settings.value(QStringLiteral("totals/answered_questions")),
        maximum_counter
    );
    result.elapsed_ms = bounded_counter(
        settings.value(QStringLiteral("totals/elapsed_ms")), maximum_elapsed_ms
    );
    result.best_correct = bounded_integer(
        settings.value(QStringLiteral("best/correct")), 1000000
    );
    result.best_answered = bounded_integer(
        settings.value(QStringLiteral("best/answered")), 1000000
    );
    result.best_elapsed_ms = bounded_counter(
        settings.value(QStringLiteral("best/elapsed_ms")), maximum_elapsed_ms
    );
    result.recent_results = parse_recent_results(
        settings.value(QStringLiteral("recent_results")).toByteArray()
    );
    settings.endGroup();
    return valid_progress(result) ? result : training_progress {};
}

bool training_progress_service::save(const training_progress& progress) {
    if (!valid_progress(progress)) {
        return false;
    }
    settings.beginGroup(progress_group);
    if (settings.contains(schema_version_key)) {
        bool version_ok = false;
        const int version
            = settings.value(schema_version_key).toInt(&version_ok);
        if (version_ok && version > training_progress::schema_version) {
            settings.endGroup();
            return false;
        }
    }
    settings.remove(QString());
    settings.setValue(schema_version_key, training_progress::schema_version);
    settings.setValue(
        QStringLiteral("totals/completed_sessions"), progress.completed_sessions
    );
    settings.setValue(
        QStringLiteral("totals/correct_answers"), progress.correct_answers
    );
    settings.setValue(
        QStringLiteral("totals/answered_questions"), progress.answered_questions
    );
    settings.setValue(QStringLiteral("totals/elapsed_ms"), progress.elapsed_ms);
    settings.setValue(QStringLiteral("best/correct"), progress.best_correct);
    settings.setValue(QStringLiteral("best/answered"), progress.best_answered);
    settings.setValue(
        QStringLiteral("best/elapsed_ms"), progress.best_elapsed_ms
    );
    settings.setValue(
        QStringLiteral("recent_results"),
        serialize_recent_results(progress.recent_results)
    );
    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool training_progress_service::record(
    const completed_training_result& result
) {
    if (!valid_result(result)) {
        return false;
    }
    training_progress progress = load();
    if (progress.completed_sessions >= maximum_counter
        || progress.correct_answers > maximum_counter - result.correct
        || progress.answered_questions > maximum_counter - result.answered
        || progress.elapsed_ms > maximum_elapsed_ms - result.elapsed_ms) {
        return false;
    }
    ++progress.completed_sessions;
    progress.correct_answers += result.correct;
    progress.answered_questions += result.answered;
    progress.elapsed_ms += result.elapsed_ms;
    if (is_better_result(result, progress)) {
        progress.best_correct = result.correct;
        progress.best_answered = result.answered;
        progress.best_elapsed_ms = result.elapsed_ms;
    }
    progress.recent_results.prepend(result);
    progress.recent_results.resize(
        std::min(
            progress.recent_results.size(),
            training_progress::maximum_recent_results
        )
    );
    return save(progress);
}

training_progress load_training_progress() {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    return training_progress_service(settings).load();
}

bool record_training_result(const completed_training_result& result) {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    return training_progress_service(settings).record(result);
}
