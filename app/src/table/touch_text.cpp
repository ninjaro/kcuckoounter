#include "table/touch_text.hpp"

#include "arch/str_label.hpp"
#include "settings/strategy_data.hpp"

#include <QRegularExpression>

#include <algorithm>
#include <cstdlib>

namespace touch_text {

QString short_strategy(const QString& name) {
    QString value = name;
    value.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)")));
    value.remove(QRegularExpression(
        QStringLiteral("\\s+Count$"), QRegularExpression::CaseInsensitiveOption
    ));
    return value.trimmed().isEmpty() ? name : value.trimmed();
}

const strategy_data* strategy(const std::string& slug) {
    const auto& catalog = strategy_repository();
    const auto key = QString::fromStdString(slug);
    const auto found = std::ranges::find_if(
        catalog.strategies,
        [&key](const strategy_data& candidate) { return candidate.slug == key; }
    );
    return found == catalog.strategies.end() ? nullptr : &*found;
}

QString strategy_name(const std::string& slug) {
    const auto* found = strategy(slug);
    return found ? found->name : QString::fromStdString(slug);
}

int strategy_level(const strategy_data& strategy) {
    int level = 0;
    for (const int weight : strategy.weights)
        level = std::max(level, std::abs(weight));
    return level <= 4 ? level : -1;
}

QString deck_legend(
    const gameplay::deck_id id, const gameplay::deck_configuration& deck
) {
    return str_label("Deck %1 · %2")
        .arg(static_cast<qulonglong>(id.value + 1))
        .arg(short_strategy(strategy_name(deck.strategy_slug)))
        .toUpper();
}

QString shoe_summary(const gameplay::deck_configuration& deck) {
    if (deck.infinite)
        return str_label("Endless stream");
    return str_label("%1-deck shoe · ends when dealt")
        .arg(static_cast<qulonglong>(deck.deck_count));
}

QString dealing_summary(const gameplay::session_configuration& configuration) {
    switch (configuration.dealing) {
    case gameplay::dealing_mode::sequential:
        return configuration.sequential_count <= 1
            ? str_label("Sequential 1/M")
            : str_label("Sequential %1/M")
                  .arg(static_cast<qulonglong>(configuration.sequential_count));
    case gameplay::dealing_mode::random:
        return str_label("Random");
    case gameplay::dealing_mode::simultaneous:
        return str_label("Together");
    }
    return {};
}

QStringList setup_chips(
    const gameplay::session_configuration& configuration,
    const std::size_t decks
) {
    QStringList chips {
        decks == 1 ? str_label("1 deck")
                   : str_label("%1 decks").arg(static_cast<qulonglong>(decks)),
        dealing_summary(configuration),
        configuration.scope == gameplay::quiz_scope::multi
            ? str_label("Multi")
            : str_label("Single"),
        configuration.source == gameplay::quiz_source::physical_joker
            ? str_label("Jokers")
            : str_label("Virtual"),
    };
    chips.append(
        configuration.failure == gameplay::failure_policy::lives
            ? str_label("Lives %1").arg(configuration.initial_lives)
            : str_label("Block")
    );
    chips.append(
        configuration.global_quiz_pause ? str_label("Frozen checks")
                                        : str_label("Dealing continues")
    );
    return chips;
}

QString clock(const std::uint64_t milliseconds) {
    const auto seconds = milliseconds / 1000;
    const auto hours = seconds / 3600;
    const auto minutes = (seconds / 60) % 60;
    const auto rest = seconds % 60;
    if (hours > 0)
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(rest, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(rest, 2, 10, QLatin1Char('0'));
}

QString countdown(const std::uint64_t milliseconds) {
    return clock((milliseconds + 999) / 1000 * 1000);
}

QString interval(const int milliseconds) {
    return str_label("%1 s").arg(QString::number(milliseconds / 1000.0, 'f', 1)
    );
}

} // namespace touch_text
