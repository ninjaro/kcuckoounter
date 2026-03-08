#include "include/cli_scripts_tests.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QtTest/QtTest>

namespace {

QString locate_repo_root() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 12; ++depth) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("scripts/cli.sh")))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QString();
}

struct process_result {
    int exit_code = -1;
    QProcess::ExitStatus exit_status = QProcess::CrashExit;
    QString output;
};

process_result run_process(
    const QString& program, const QStringList& arguments,
    const QString& working_directory
) {
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setWorkingDirectory(working_directory);
    process.start();
    const bool finished = process.waitForFinished(20000);
    process_result result;
    if (!finished) {
        process.kill();
        process.waitForFinished(5000);
        result.output = QStringLiteral("process timeout");
        return result;
    }

    result.exit_code = process.exitCode();
    result.exit_status = process.exitStatus();
    result.output = QString::fromUtf8(process.readAllStandardOutput())
        + QString::fromUtf8(process.readAllStandardError());
    return result;
}

} // namespace

void cli_scripts_tests::cli_help_command_succeeds() {
    const QString repo_root = locate_repo_root();
    QVERIFY2(!repo_root.isEmpty(), "Unable to locate repository root");

    const process_result result = run_process(
        QStringLiteral("bash"),
        QStringList {
            QStringLiteral("scripts/cli.sh"),
            QStringLiteral("help"),
        },
        repo_root
    );
    QCOMPARE(result.exit_status, QProcess::NormalExit);
    QCOMPARE(result.exit_code, 0);
    QVERIFY(result.output.contains(QStringLiteral("Usage: ./scripts/cli.sh")));
}

void cli_scripts_tests::cli_unknown_command_fails() {
    const QString repo_root = locate_repo_root();
    QVERIFY2(!repo_root.isEmpty(), "Unable to locate repository root");

    const process_result result = run_process(
        QStringLiteral("bash"),
        QStringList {
            QStringLiteral("scripts/cli.sh"),
            QStringLiteral("definitely-unknown-command"),
        },
        repo_root
    );
    QCOMPARE(result.exit_status, QProcess::NormalExit);
    QCOMPARE(result.exit_code, 1);
    QVERIFY(result.output.contains(QStringLiteral("Usage: ./scripts/cli.sh")));
}

void cli_scripts_tests::build_command_rejects_unknown_mode() {
    const QString repo_root = locate_repo_root();
    QVERIFY2(!repo_root.isEmpty(), "Unable to locate repository root");

    const process_result result = run_process(
        QStringLiteral("bash"),
        QStringList {
            QStringLiteral("scripts/cli.sh"),
            QStringLiteral("build"),
            QStringLiteral("invalid-mode"),
        },
        repo_root
    );
    QCOMPARE(result.exit_status, QProcess::NormalExit);
    QCOMPARE(result.exit_code, 1);
    QVERIFY(result.output.contains(QStringLiteral("usage:")));
}

void cli_scripts_tests::test_command_rejects_unknown_mode() {
    const QString repo_root = locate_repo_root();
    QVERIFY2(!repo_root.isEmpty(), "Unable to locate repository root");

    const process_result result = run_process(
        QStringLiteral("bash"),
        QStringList {
            QStringLiteral("scripts/cli.sh"),
            QStringLiteral("test"),
            QStringLiteral("invalid-mode"),
        },
        repo_root
    );
    QCOMPARE(result.exit_status, QProcess::NormalExit);
    QCOMPARE(result.exit_code, 1);
    QVERIFY(result.output.contains(QStringLiteral("usage:")));
}

void cli_scripts_tests::run_command_rejects_unknown_mode() {
    const QString repo_root = locate_repo_root();
    QVERIFY2(!repo_root.isEmpty(), "Unable to locate repository root");

    const process_result result = run_process(
        QStringLiteral("bash"),
        QStringList {
            QStringLiteral("scripts/cli.sh"),
            QStringLiteral("run"),
            QStringLiteral("invalid-mode"),
        },
        repo_root
    );
    QCOMPARE(result.exit_status, QProcess::NormalExit);
    QCOMPARE(result.exit_code, 1);
    QVERIFY(result.output.contains(QStringLiteral("usage:")));
}

void cli_scripts_tests::android_command_rejects_unknown_subcommand() {
    const QString repo_root = locate_repo_root();
    QVERIFY2(!repo_root.isEmpty(), "Unable to locate repository root");

    const process_result result = run_process(
        QStringLiteral("bash"),
        QStringList {
            QStringLiteral("scripts/cli.sh"),
            QStringLiteral("android"),
            QStringLiteral("invalid-subcommand"),
        },
        repo_root
    );
    QCOMPARE(result.exit_status, QProcess::NormalExit);
    QCOMPARE(result.exit_code, 1);
    QVERIFY(result.output.contains(
        QStringLiteral("usage: ./scripts/cli.sh android")
    ));
}

void cli_scripts_tests::deps_command_rejects_unknown_mode() {
    const QString repo_root = locate_repo_root();
    QVERIFY2(!repo_root.isEmpty(), "Unable to locate repository root");

    const process_result result = run_process(
        QStringLiteral("bash"),
        QStringList {
            QStringLiteral("scripts/cli.sh"),
            QStringLiteral("deps"),
            QStringLiteral("invalid-mode"),
        },
        repo_root
    );
    QCOMPARE(result.exit_status, QProcess::NormalExit);
    QCOMPARE(result.exit_code, 1);
    QVERIFY(result.output.contains(QStringLiteral("usage:")));
}

void cli_scripts_tests::check_leaks_rejects_unknown_option() {
    const QString repo_root = locate_repo_root();
    QVERIFY2(!repo_root.isEmpty(), "Unable to locate repository root");

    const process_result result = run_process(
        QStringLiteral("bash"),
        QStringList {
            QStringLiteral("scripts/check_leaks.sh"),
            QStringLiteral("--unknown-flag"),
        },
        repo_root
    );
    QCOMPARE(result.exit_status, QProcess::NormalExit);
    QCOMPARE(result.exit_code, 1);
    QVERIFY(result.output.contains(QStringLiteral("unknown option")));
}

void cli_scripts_tests::make_run_target_delegates_to_cli() {
    const QString repo_root = locate_repo_root();
    QVERIFY2(!repo_root.isEmpty(), "Unable to locate repository root");

    const process_result result = run_process(
        QStringLiteral("bash"),
        QStringList {
            QStringLiteral("-lc"),
            QStringLiteral("make -n run"),
        },
        repo_root
    );
    QCOMPARE(result.exit_status, QProcess::NormalExit);
    QCOMPARE(result.exit_code, 0);
    QVERIFY(result.output.contains(QStringLiteral("scripts/cli.sh run kde")));
}

void cli_scripts_tests::make_check_target_delegates_to_cli() {
    const QString repo_root = locate_repo_root();
    QVERIFY2(!repo_root.isEmpty(), "Unable to locate repository root");

    const process_result result = run_process(
        QStringLiteral("bash"),
        QStringList {
            QStringLiteral("-lc"),
            QStringLiteral("make -n check"),
        },
        repo_root
    );
    QCOMPARE(result.exit_status, QProcess::NormalExit);
    QCOMPARE(result.exit_code, 0);
    QVERIFY(result.output.contains(QStringLiteral("scripts/cli.sh check")));
}
