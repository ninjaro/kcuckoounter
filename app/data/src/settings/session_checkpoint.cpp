#include "settings/session_checkpoint.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const QString session_group = QStringLiteral("trainer_session");
const QString payload_key = QStringLiteral("payload");
constexpr qsizetype maximum_payload_bytes = 256 * 1024;
constexpr int maximum_slots = 16;
constexpr int maximum_cards_per_deck = 52;
constexpr int maximum_decks = 16;
constexpr int maximum_cards_per_slot = maximum_cards_per_deck * maximum_decks;
constexpr qint64 maximum_elapsed_ms = 31LL * 24LL * 60LL * 60LL * 1000LL;
constexpr int maximum_score = 1000000;
constexpr qsizetype maximum_text_length = 2048;

QJsonObject card_to_json(const card_session_state& state) {
    QJsonArray deck;
    for (const int card : state.deck) {
        deck.append(card);
    }
    return {
        { QStringLiteral("cards_per_deck"), state.cards_per_deck },
        { QStringLiteral("decks_count"), state.decks_count },
        { QStringLiteral("infinity"), state.infinity_enabled },
        { QStringLiteral("deck"), deck },
        { QStringLiteral("position"), state.deck_position },
    };
}

QJsonObject slot_to_json(const table_slot_session_state& state) {
    return {
        { QStringLiteral("card"), card_to_json(state.card) },
        { QStringLiteral("paused"), state.paused },
        { QStringLiteral("infinity"), state.infinity_enabled },
        { QStringLiteral("deck_count"), state.deck_count },
        { QStringLiteral("strategy_slug"), state.strategy_slug },
        { QStringLiteral("strategy_id"), state.strategy_id },
        { QStringLiteral("show_card_indexing"), state.show_card_indexing },
        { QStringLiteral("show_strategy_name"), state.show_strategy_name },
        { QStringLiteral("training_mode"), state.training_mode },
        { QStringLiteral("quiz_prompt_active"), state.quiz_prompt_active },
        { QStringLiteral("quiz_feedback_active"), state.quiz_feedback_active },
        { QStringLiteral("quiz_continue_visible"),
          state.quiz_continue_visible },
        { QStringLiteral("quiz_input_value"), state.quiz_input_value },
        { QStringLiteral("quiz_feedback_text"), state.quiz_feedback_text },
    };
}

QByteArray checkpoint_to_json(const trainer_session_checkpoint& checkpoint) {
    QJsonArray slot_array;
    for (const table_slot_session_state& slot : checkpoint.table.slot_states) {
        slot_array.append(slot_to_json(slot));
    }

    const QJsonObject table {
        { QStringLiteral("slots"), slot_array },
        { QStringLiteral("pick_elapsed_ms"), checkpoint.table.pick_elapsed_ms },
        { QStringLiteral("quiz_running"), checkpoint.table.quiz_running },
        { QStringLiteral("quiz_paused"), checkpoint.table.quiz_paused },
        { QStringLiteral("allow_skipping"), checkpoint.table.allow_skipping },
        { QStringLiteral("dealing_mode"), checkpoint.table.dealing_mode },
        { QStringLiteral("next_slot_index"), checkpoint.table.next_slot_index },
    };
    const QJsonObject root {
        { QStringLiteral("table"), table },
        { QStringLiteral("score_correct"), checkpoint.score_correct },
        { QStringLiteral("score_total"), checkpoint.score_total },
        { QStringLiteral("elapsed_ms"), checkpoint.elapsed_ms },
        { QStringLiteral("was_running"), checkpoint.was_running },
        { QStringLiteral("saved_at_utc_ms"), checkpoint.saved_at_utc_ms },
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool integer_in_range(
    const QJsonValue& value, qint64 minimum, qint64 maximum, qint64* output
) {
    if (!value.isDouble()) {
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
    if (static_cast<double>(integer) != number || integer < minimum
        || integer > maximum) {
        return false;
    }
    if (output != nullptr) {
        *output = integer;
    }
    return true;
}

bool bool_value(const QJsonObject& object, const QString& key, bool* output) {
    const QJsonValue value = object.value(key);
    if (!value.isBool() || output == nullptr) {
        return false;
    }
    *output = value.toBool();
    return true;
}

bool parse_card(const QJsonObject& object, card_session_state* output) {
    if (output == nullptr) {
        return false;
    }
    qint64 cards_per_deck = 0;
    qint64 decks_count = 0;
    qint64 deck_position = 0;
    if (!integer_in_range(
            object.value(QStringLiteral("cards_per_deck")), 1,
            maximum_cards_per_deck, &cards_per_deck
        )
        || !integer_in_range(
            object.value(QStringLiteral("decks_count")), 1, maximum_decks,
            &decks_count
        )
        || !integer_in_range(
            object.value(QStringLiteral("position")), 0, maximum_cards_per_slot,
            &deck_position
        )
        || !bool_value(
            object, QStringLiteral("infinity"), &output->infinity_enabled
        )) {
        return false;
    }

    const QJsonValue deck_value = object.value(QStringLiteral("deck"));
    if (!deck_value.isArray()) {
        return false;
    }
    const QJsonArray deck = deck_value.toArray();
    const qint64 expected_size = cards_per_deck * decks_count;
    if (deck.isEmpty() || deck.size() != expected_size
        || deck.size() > maximum_cards_per_slot
        || deck_position > deck.size()) {
        return false;
    }

    output->deck.clear();
    output->deck.reserve(deck.size());
    QVector<int> occurrences(static_cast<qsizetype>(cards_per_deck), 0);
    for (const QJsonValue& card : deck) {
        qint64 card_index = 0;
        if (!integer_in_range(card, 0, cards_per_deck - 1, &card_index)) {
            return false;
        }
        output->deck.append(static_cast<int>(card_index));
        ++occurrences[static_cast<qsizetype>(card_index)];
    }
    if (std::ranges::any_of(occurrences, [decks_count](int count) {
            return count != decks_count;
        })) {
        return false;
    }
    if (output->infinity_enabled && deck_position == deck.size()) {
        return false;
    }
    output->cards_per_deck = static_cast<int>(cards_per_deck);
    output->decks_count = static_cast<int>(decks_count);
    output->deck_position = static_cast<int>(deck_position);
    return true;
}

bool parse_slot(const QJsonObject& object, table_slot_session_state* output) {
    if (output == nullptr || !object.value(QStringLiteral("card")).isObject()
        || !parse_card(
            object.value(QStringLiteral("card")).toObject(), &output->card
        )) {
        return false;
    }

    qint64 deck_count = 0;
    qint64 strategy_id = 0;
    qint64 quiz_input = 0;
    if (!integer_in_range(
            object.value(QStringLiteral("deck_count")), 1, maximum_decks,
            &deck_count
        )
        || !integer_in_range(
            object.value(QStringLiteral("quiz_input_value")), -9999, 9999,
            &quiz_input
        )
        || !integer_in_range(
            object.value(QStringLiteral("strategy_id")), 0, 1000000,
            &strategy_id
        )
        || !bool_value(object, QStringLiteral("paused"), &output->paused)
        || !bool_value(
            object, QStringLiteral("infinity"), &output->infinity_enabled
        )
        || !bool_value(
            object, QStringLiteral("show_card_indexing"),
            &output->show_card_indexing
        )
        || !bool_value(
            object, QStringLiteral("show_strategy_name"),
            &output->show_strategy_name
        )
        || !bool_value(
            object, QStringLiteral("training_mode"), &output->training_mode
        )
        || !bool_value(
            object, QStringLiteral("quiz_prompt_active"),
            &output->quiz_prompt_active
        )
        || !bool_value(
            object, QStringLiteral("quiz_feedback_active"),
            &output->quiz_feedback_active
        )
        || !bool_value(
            object, QStringLiteral("quiz_continue_visible"),
            &output->quiz_continue_visible
        )) {
        return false;
    }

    const QString strategy_slug
        = object.value(QStringLiteral("strategy_slug")).toString();
    const QString feedback
        = object.value(QStringLiteral("quiz_feedback_text")).toString();
    if (strategy_slug.size() > 128 || feedback.size() > maximum_text_length) {
        return false;
    }
    if (output->quiz_feedback_active && !output->quiz_prompt_active) {
        return false;
    }

    output->deck_count = static_cast<int>(deck_count);
    output->quiz_input_value = static_cast<int>(quiz_input);
    output->strategy_id = static_cast<int>(strategy_id);
    output->strategy_slug = strategy_slug;
    output->quiz_feedback_text = feedback;
    if (output->infinity_enabled != output->card.infinity_enabled
        || output->deck_count != output->card.decks_count) {
        return false;
    }
    return true;
}

std::optional<trainer_session_checkpoint>
checkpoint_from_json(const QByteArray& payload) {
    if (payload.isEmpty() || payload.size() > maximum_payload_bytes) {
        return std::nullopt;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    const QJsonValue table_value = root.value(QStringLiteral("table"));
    if (!table_value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject table_object = table_value.toObject();
    const QJsonValue slots_value = table_object.value(QStringLiteral("slots"));
    if (!slots_value.isArray()) {
        return std::nullopt;
    }
    const QJsonArray slot_array = slots_value.toArray();
    if (slot_array.isEmpty() || slot_array.size() > maximum_slots) {
        return std::nullopt;
    }

    trainer_session_checkpoint result;
    result.table.slot_states.reserve(slot_array.size());
    for (const QJsonValue& slot_value : slot_array) {
        if (!slot_value.isObject()) {
            return std::nullopt;
        }
        table_slot_session_state slot;
        if (!parse_slot(slot_value.toObject(), &slot)) {
            return std::nullopt;
        }
        result.table.slot_states.append(slot);
    }

    qint64 pick_elapsed_ms = 0;
    qint64 dealing_mode = 0;
    qint64 next_slot_index = 0;
    qint64 score_correct = 0;
    qint64 score_total = 0;
    qint64 elapsed_ms = 0;
    qint64 saved_at_utc_ms = 0;
    if (!integer_in_range(
            table_object.value(QStringLiteral("pick_elapsed_ms")), 0, 1000,
            &pick_elapsed_ms
        )
        || !integer_in_range(
            table_object.value(QStringLiteral("dealing_mode")), 0, 2,
            &dealing_mode
        )
        || !integer_in_range(
            table_object.value(QStringLiteral("next_slot_index")), 0,
            slot_array.size() - 1, &next_slot_index
        )
        || !bool_value(
            table_object, QStringLiteral("quiz_running"),
            &result.table.quiz_running
        )
        || !bool_value(
            table_object, QStringLiteral("quiz_paused"),
            &result.table.quiz_paused
        )
        || !bool_value(
            table_object, QStringLiteral("allow_skipping"),
            &result.table.allow_skipping
        )
        || !integer_in_range(
            root.value(QStringLiteral("score_correct")), 0, maximum_score,
            &score_correct
        )
        || !integer_in_range(
            root.value(QStringLiteral("score_total")), 0, maximum_score,
            &score_total
        )
        || score_correct > score_total
        || !integer_in_range(
            root.value(QStringLiteral("elapsed_ms")), 0, maximum_elapsed_ms,
            &elapsed_ms
        )
        || !integer_in_range(
            root.value(QStringLiteral("saved_at_utc_ms")), 0,
            std::numeric_limits<qint64>::max(), &saved_at_utc_ms
        )
        || !bool_value(
            root, QStringLiteral("was_running"), &result.was_running
        )) {
        return std::nullopt;
    }

    result.table.pick_elapsed_ms = pick_elapsed_ms;
    result.table.dealing_mode = static_cast<int>(dealing_mode);
    result.table.next_slot_index = static_cast<int>(next_slot_index);
    result.score_correct = static_cast<int>(score_correct);
    result.score_total = static_cast<int>(score_total);
    result.elapsed_ms = elapsed_ms;
    result.saved_at_utc_ms = saved_at_utc_ms;
    if (!result.table.quiz_running) {
        return std::nullopt;
    }
    return result;
}

} // namespace

trainer_session_checkpoint_service::trainer_session_checkpoint_service(
    QSettings& settings_value
)
    : settings(settings_value) { }

std::optional<trainer_session_checkpoint>
trainer_session_checkpoint_service::load() const {
    settings.beginGroup(session_group);
    const QByteArray payload = settings.value(payload_key).toByteArray();
    settings.endGroup();
    return checkpoint_from_json(payload);
}

bool trainer_session_checkpoint_service::save(
    const trainer_session_checkpoint& checkpoint
) {
    const QByteArray payload = checkpoint_to_json(checkpoint);
    if (!checkpoint_from_json(payload).has_value()) {
        return false;
    }

    settings.beginGroup(session_group);
    settings.remove(QString());
    settings.setValue(payload_key, payload);
    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

void trainer_session_checkpoint_service::clear() {
    settings.remove(session_group);
    settings.sync();
}

std::optional<trainer_session_checkpoint> load_trainer_session_checkpoint() {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    return trainer_session_checkpoint_service(settings).load();
}

bool save_trainer_session_checkpoint(
    const trainer_session_checkpoint& checkpoint
) {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    return trainer_session_checkpoint_service(settings).save(checkpoint);
}

void clear_trainer_session_checkpoint() {
    QSettings settings(
        QStringLiteral("ninjaro"), QStringLiteral("kcuckoounter")
    );
    trainer_session_checkpoint_service(settings).clear();
}
