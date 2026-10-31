#include <QCoreApplication>
#include <QtTest/QtTest>

#include "include/asset_locator_tests.hpp"
#include "include/strategy_data_tests.hpp"
#include "include/table/gameplay_session_tests.hpp"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int status = 0;
    const auto selected = qEnvironmentVariable("KCUCKOOUNTER_TEST_SUITE");
    bool matched = false;
    const auto run = [&](auto& suite) {
        if (!selected.isEmpty()
            && selected != QLatin1String(suite.metaObject()->className()))
            return 0;
        matched = true;
        return QTest::qExec(&suite, argc, argv);
    };
    gameplay_session_tests gameplay;
    status |= run(gameplay);
    asset_locator_tests assets;
    status |= run(assets);
    strategy_data_tests strategies;
    status |= run(strategies);
    return matched ? status : 1;
}
