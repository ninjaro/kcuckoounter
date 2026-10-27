#include "settings/preferences_fixture.hpp"

#include "settings/preferences.hpp"
#include "settings/strategy_data.hpp"
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
