#include "settings/gameplay_drill_fixture.hpp"

namespace preferences_test {

gameplay_drill sample_drill() {
    gameplay_drill result;
    result.name = QStringLiteral("Mixed shoes ♥");
    result.configuration.failure = gameplay::failure_policy::block;
    result.configuration.initial_lives = 12;
    result.configuration.global_quiz_pause = false;
    result.configuration.allow_skip = false;
    result.configuration.dealing = gameplay::dealing_mode::simultaneous;
    result.configuration.sequential_count = 2;
    result.pickup_interval_ms = 735;
    result.decks = { { { "first_strategy", 1, false, false }, false },
                     { { "renamed_strategy", 17, true, true }, true } };
    return result;
}

strategy_catalog drill_catalog() {
    auto result = test_catalog();
    for (auto& strategy : result.strategies) {
        strategy.min_decks = 8;
        strategy.weights = QVector<int>(13, 1);
        strategy.initial_count = strategy_initial_count { -2, true };
    }
    return result;
}

QVariantMap other_drill_settings(QSettings& settings) {
    QVariantMap result;
    for (const auto& key : settings.allKeys())
        if (key != gameplay_drills_key)
            result.insert(key, settings.value(key));
    return result;
}

} // namespace preferences_test
