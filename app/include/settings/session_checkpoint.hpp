#ifndef KCUCKOOUNTER_SETTINGS_SESSION_CHECKPOINT_HPP
#define KCUCKOOUNTER_SETTINGS_SESSION_CHECKPOINT_HPP

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

class QSettings;

struct card_session_state {
    int cards_per_deck = 0;
    int decks_count = 0;
    bool infinity_enabled = false;
    QVector<int> deck;
    int deck_position = 0;

    bool operator==(const card_session_state&) const = default;
};

struct table_slot_session_state {
    card_session_state card;
    bool paused = true;
    bool infinity_enabled = false;
    int deck_count = 1;
    QString strategy_slug;
    int strategy_id = 0;
    bool show_card_indexing = false;
    bool show_strategy_name = false;
    bool training_mode = false;
    bool quiz_prompt_active = false;
    bool quiz_feedback_active = false;
    bool quiz_continue_visible = false;
    int quiz_input_value = 0;
    QString quiz_feedback_text;

    bool operator==(const table_slot_session_state&) const = default;
};

struct table_session_state {
    QVector<table_slot_session_state> slot_states;
    qint64 pick_elapsed_ms = 0;
    bool quiz_running = false;
    bool quiz_paused = true;
    bool allow_skipping = true;
    int dealing_mode = 0;
    int next_slot_index = 0;

    bool operator==(const table_session_state&) const = default;
};

struct trainer_session_checkpoint {
    static constexpr int schema_version = 1;

    table_session_state table;
    int score_correct = 0;
    int score_total = 0;
    qint64 elapsed_ms = 0;
    bool was_running = false;
    qint64 saved_at_utc_ms = 0;

    bool operator==(const trainer_session_checkpoint&) const = default;
};

class trainer_session_checkpoint_service {
public:
    explicit trainer_session_checkpoint_service(QSettings& settings);

    [[nodiscard]] std::optional<trainer_session_checkpoint> load() const;
    bool save(const trainer_session_checkpoint& checkpoint);
    void clear();

private:
    QSettings& settings;
};

[[nodiscard]] std::optional<trainer_session_checkpoint>
load_trainer_session_checkpoint();
bool save_trainer_session_checkpoint(
    const trainer_session_checkpoint& checkpoint
);
void clear_trainer_session_checkpoint();

#endif // KCUCKOOUNTER_SETTINGS_SESSION_CHECKPOINT_HPP
