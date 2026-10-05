#include "settings/preferences_fixture.hpp"

#include "settings/preferences.hpp"
#include "settings/session_checkpoint.hpp"
#include "settings/strategy_data.hpp"
#include "settings/training_progress.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <numeric>

namespace preferences_test {

strategy_catalog test_catalog() {
    strategy_catalog catalog;

    strategy_data first;
    first.id = 11;
    first.slug = QStringLiteral("first_strategy");
    first.name = QStringLiteral("Translated first name");
    catalog.strategies.push_back(first);

    strategy_data second;
    second.id = 22;
    second.slug = QStringLiteral("renamed_strategy");
    second.name = QStringLiteral("Translated second name");
    catalog.strategies.push_back(second);
    return catalog;
}

QSettings temporary_settings(const QTemporaryDir& directory) {
    return { directory.filePath(QStringLiteral("preferences.ini")),
             QSettings::IniFormat };
}

trainer_session_checkpoint test_checkpoint() {
    table_slot_session_state slot;
    slot.card.cards_per_deck = 4;
    slot.card.decks_count = 2;
    slot.card.deck = { 0, 3, 2, 1, 2, 0, 1, 3 };
    slot.card.deck_position = 5;
    slot.deck_count = 2;
    slot.strategy_slug = QStringLiteral("first_strategy");
    slot.strategy_id = 11;
    slot.show_card_indexing = true;
    slot.quiz_prompt_active = true;
    slot.quiz_feedback_active = true;
    slot.quiz_continue_visible = true;
    slot.quiz_input_value = -3;
    slot.quiz_feedback_text = QStringLiteral("Try again");

    trainer_session_checkpoint checkpoint;
    checkpoint.table.slot_states.append(slot);
    checkpoint.table.pick_elapsed_ms = 175;
    checkpoint.table.quiz_running = true;
    checkpoint.table.quiz_paused = false;
    checkpoint.table.allow_skipping = false;
    checkpoint.table.dealing_mode = 1;
    checkpoint.score_correct = 3;
    checkpoint.score_total = 5;
    checkpoint.elapsed_ms = 12345;
    checkpoint.was_running = true;
    checkpoint.saved_at_utc_ms = 1700000000000LL;
    return checkpoint;
}

QJsonObject stored_checkpoint_root(QSettings& settings) {
    return QJsonDocument::fromJson(
               settings.value(QStringLiteral("trainer_session/payload"))
                   .toByteArray()
    )
        .object();
}

void store_checkpoint_root(QSettings& settings, const QJsonObject& root) {
    settings.setValue(
        QStringLiteral("trainer_session/payload"),
        QJsonDocument(root).toJson(QJsonDocument::Compact)
    );
    settings.sync();
}

std::optional<gameplay::session> setup_session(
    std::size_t count, const gameplay::session_configuration& configuration
) {
    return gameplay::session::create(
        configuration,
        std::vector<gameplay::deck_configuration>(
            count, { "first_strategy", 1, false, false }
        ),
        { count, count }
    );
}

bool prepare_setup_session(gameplay::session& owner) {
    std::vector<std::size_t> traversal(owner.size());
    std::iota(traversal.begin(), traversal.end(), std::size_t { 0 });
    gameplay::prepared_deck prepared;
    prepared.cards = { 0, 1, 2, 3 };
    prepared.initial_running_count = -4;
    for (std::size_t index = 0; index < owner.size(); ++index) {
        if (!owner.prepare_deck({ index }, prepared))
            return false;
    }
    return owner.set_traversal(traversal);
}

std::optional<gameplay::session>
deck_setup_session(const gameplay::deck_configuration& first) {
    return gameplay::session::create(
        {}, { first, { "hi_lo", 2, false, false } }, { 2, 64 }
    );
}

} // namespace preferences_test
