#ifndef KCUCKOOUNTER_SETTINGS_TRAINING_PROGRESS_HPP
#define KCUCKOOUNTER_SETTINGS_TRAINING_PROGRESS_HPP

#include <QVector>

class QSettings;

struct completed_training_result {
    int correct = 0;
    int answered = 0;
    qint64 elapsed_ms = 0;
    qint64 completed_at_utc_ms = 0;

    bool operator==(const completed_training_result&) const = default;
};

struct training_progress {
    static constexpr int schema_version = 1;
    static constexpr qsizetype maximum_recent_results = 20;

    qint64 completed_sessions = 0;
    qint64 correct_answers = 0;
    qint64 answered_questions = 0;
    qint64 elapsed_ms = 0;
    int best_correct = 0;
    int best_answered = 0;
    qint64 best_elapsed_ms = 0;
    QVector<completed_training_result> recent_results;

    bool operator==(const training_progress&) const = default;
};

class training_progress_service {
public:
    explicit training_progress_service(QSettings& settings);

    [[nodiscard]] training_progress load() const;
    bool save(const training_progress& progress);
    bool record(const completed_training_result& result);

private:
    QSettings& settings;
};

[[nodiscard]] training_progress load_training_progress();
bool record_training_result(const completed_training_result& result);

#endif // KCUCKOOUNTER_SETTINGS_TRAINING_PROGRESS_HPP
