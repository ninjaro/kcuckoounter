#include "shell/drill_picker.hpp"

#include "arch/str_label.hpp"
#include "settings/strategy_data.hpp"

#include <QStringList>

namespace {

QString yes_no(bool value) {
    return value ? str_label("Yes") : str_label("No");
}

QString strategy_name(const QString& slug) {
    for (const auto& strategy : strategy_repository().strategies)
        if (strategy.slug == slug)
            return strategy.name + QStringLiteral(" (") + slug
                + QLatin1Char(')');
    return slug;
}

QString dealing_label(int mode) {
    return mode == 0 ? str_label("Sequential")
        : mode == 1  ? str_label("Random")
                     : str_label("Simultaneous");
}

} // namespace

QString drill_picker::summarize(const training_drill& drill) const {
    QStringList lines {
        str_label("Legacy drill (v1)"),
        str_label("Pickup interval (ms)") + QStringLiteral(": ")
            + QString::number(drill.pickup_interval_ms),
        str_label("Quiz mode") + QStringLiteral(": ")
            + (drill.quiz_type == 0 ? str_label("Single question")
                                    : str_label("Multi question")),
        str_label("Pause for answers") + QStringLiteral(": ")
            + yes_no(drill.wait_for_answers),
        str_label("Allow skipping") + QStringLiteral(": ")
            + yes_no(drill.allow_skipping),
        str_label("Dealing mode") + QStringLiteral(": ")
            + dealing_label(drill.dealing_mode),
    };
    int number = 0;
    for (const auto& slot : drill.slot_settings)
        lines << QString()
              << str_label("Slot") + QStringLiteral(" ")
                + QString::number(++number)
              << strategy_name(slot.strategy_slug)
              << str_label("Deck count") + QStringLiteral(": ")
                + QString::number(slot.deck_count)
              << (slot.infinity_enabled ? str_label("Infinite shoe")
                                        : str_label("Finite shoe"))
              << (slot.training_mode ? str_label("Training mode")
                                     : str_label("Scored mode"));
    return lines.join(QLatin1Char('\n'));
}

QString drill_picker::summarize(const gameplay_drill& drill) const {
    const auto& config = drill.configuration;
    QStringList lines {
        str_label("Gameplay drill (v2) — fresh Setup, not a checkpoint"),
        str_label("Slots") + QStringLiteral(": ")
            + QString::number(drill.decks.size()),
        str_label("Pickup interval (ms)") + QStringLiteral(": ")
            + QString::number(drill.pickup_interval_ms),
        str_label("Failure policy") + QStringLiteral(": ")
            + (config.failure == gameplay::failure_policy::lives
                   ? str_label("Lives")
                   : str_label("Block deck")),
        str_label("Starting lives") + QStringLiteral(": ")
            + QString::number(config.initial_lives),
        str_label("Quiz source") + QStringLiteral(": ")
            + (config.source == gameplay::quiz_source::physical_joker
                   ? str_label("Physical Jokers")
                   : str_label("Virtual questions")),
        str_label("Quiz scope") + QStringLiteral(": ")
            + (config.scope == gameplay::quiz_scope::single
                   ? str_label("Single question")
                   : str_label("Multi question")),
        str_label("Pause for all answers") + QStringLiteral(": ")
            + yes_no(config.global_quiz_pause),
        str_label("Allow skipping") + QStringLiteral(": ")
            + yes_no(config.allow_skip),
        str_label("Dealing mode") + QStringLiteral(": ")
            + dealing_label(static_cast<int>(config.dealing)),
        str_label("Sequential N/M") + QStringLiteral(": ")
            + QString::number(config.sequential_count) + QLatin1Char('/')
            + QString::number(drill.decks.size()),
        str_label("Allow more slots than recommended") + QStringLiteral(": ")
            + yes_no(config.allow_extra_slots),
    };
    std::size_t number = 0;
    for (const auto& deck : drill.decks)
        lines << QString()
              << str_label("Deck") + QStringLiteral(" ")
                + QString::number(++number)
              << strategy_name(
                     QString::fromStdString(deck.configuration.strategy_slug)
                 )
              << str_label("Deck count") + QStringLiteral(": ")
                + QString::number(deck.configuration.deck_count)
              << (deck.configuration.infinite ? str_label("Infinite shoe")
                                              : str_label("Finite shoe"))
              << (deck.configuration.training ? str_label("Training mode")
                                              : str_label("Scored mode"))
              << str_label("Training Show Count") + QStringLiteral(": ")
                + yes_no(deck.show_count);
    return lines.join(QLatin1Char('\n'));
}
