#include "monitor/external_monitor_window.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QString>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(
        QStringLiteral("cppr_external_monitor")
    );
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("External debug monitor for CPPR telemetry")
    );
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption endpoint_option(
        QStringList() << QStringLiteral("e") << QStringLiteral("endpoint"),
        QStringLiteral("Local IPC endpoint path to connect to."),
        QStringLiteral("path")
    );
    parser.addOption(endpoint_option);
    parser.process(app);

    QString endpoint = parser.value(endpoint_option).trimmed();
    if (endpoint.isEmpty()) {
        endpoint = qEnvironmentVariable("CPPR_DEBUG_ENDPOINT");
    }

    external_monitor_window window;
    window.resize(1100, 760);
    window.set_initial_endpoint(endpoint);
    window.show();

    return app.exec();
}
