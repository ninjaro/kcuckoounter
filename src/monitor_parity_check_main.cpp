#include "monitor/monitor_parity_checker.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(
        QStringLiteral("cppr_monitor_parity_check")
    );
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Compare embedded snapshot exports with external monitor traces."
    ));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption embedded_option(
        QStringList() << QStringLiteral("embedded"),
        QStringLiteral("Path to embedded snapshot JSON export."),
        QStringLiteral("path")
    );
    const QCommandLineOption external_option(
        QStringList() << QStringLiteral("external"),
        QStringLiteral("Path to external monitor history JSONL."),
        QStringLiteral("path")
    );
    const QCommandLineOption tolerance_option(
        QStringList() << QStringLiteral("tolerance-bytes"),
        QStringLiteral("Byte tolerance for overlapping metric drift checks."),
        QStringLiteral("bytes"), QStringLiteral("4194304")
    );
    parser.addOption(embedded_option);
    parser.addOption(external_option);
    parser.addOption(tolerance_option);
    parser.process(app);

    const QString embedded_path = parser.value(embedded_option).trimmed();
    const QString external_path = parser.value(external_option).trimmed();
    if (embedded_path.isEmpty() || external_path.isEmpty()) {
        QTextStream(stderr)
            << "Both --embedded and --external paths are required.\n";
        return 2;
    }

    QFile embedded_file(embedded_path);
    if (!embedded_file.open(QIODevice::ReadOnly)) {
        QTextStream(stderr)
            << "Unable to open embedded snapshot: " << embedded_path << "\n";
        return 2;
    }
    const QJsonDocument embedded_document
        = QJsonDocument::fromJson(embedded_file.readAll());
    if (!embedded_document.isObject()) {
        QTextStream(stderr) << "Embedded snapshot is not valid JSON object.\n";
        return 2;
    }

    QFile external_file(external_path);
    if (!external_file.open(QIODevice::ReadOnly)) {
        QTextStream(stderr)
            << "Unable to open external history: " << external_path << "\n";
        return 2;
    }
    const QByteArray external_jsonl = external_file.readAll();
    const QVector<QJsonObject> external_messages
        = monitor_parity_checker::parse_external_history_jsonl(external_jsonl);
    if (external_messages.isEmpty()) {
        QTextStream(stderr)
            << "External history has no valid protocol JSON messages.\n";
        return 2;
    }

    bool tolerance_ok = false;
    const qint64 tolerance_bytes
        = parser.value(tolerance_option).toLongLong(&tolerance_ok);
    if (!tolerance_ok || tolerance_bytes < 0) {
        QTextStream(stderr)
            << "--tolerance-bytes must be a non-negative integer.\n";
        return 2;
    }

    const monitor_parity_checker::parity_result parity
        = monitor_parity_checker::
            compare_embedded_snapshot_and_external_messages(
                embedded_document.object(), external_messages, tolerance_bytes
            );

    QTextStream(stdout) << "Compared messages: "
                        << parity.compared_message_count
                        << "\nCompared metrics: "
                        << parity.compared_metric_count
                        << "\nWarnings: " << parity.warnings.size() << "\n";
    for (const QString& warning : parity.warnings) {
        QTextStream(stdout) << " - " << warning << "\n";
    }

    return parity.ok() ? 0 : 1;
}
