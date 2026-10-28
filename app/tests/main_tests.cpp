#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cstdio>

#include "include/asset_locator_tests.hpp"
#include "include/card_sheet_tests.hpp"
#include "include/image_source_tests.hpp"
#include "include/preview_carousel_tests.hpp"
#include "include/settings/gameplay_checkpoint_tests.hpp"
#include "include/settings/gameplay_progress_tests.hpp"
#include "include/settings/gameplay_recovery_tests.hpp"
#include "include/settings/preferences_tests.hpp"
#include "include/strategy_data_tests.hpp"
#include "include/table/card_widget_tests.hpp"
#include "include/table/gameplay_completion_tests.hpp"
#include "include/table/gameplay_host_tests.hpp"
#include "include/table/gameplay_mobile_tests.hpp"
#include "include/table/gameplay_restore_tests.hpp"
#include "include/table/gameplay_session_tests.hpp"
#include "include/table/gameplay_start_tests.hpp"
#include "include/table/table_tests.hpp"

int main(int argc, char** argv) {
    // Application-facing settings tests must never write the user's trainer
    // preferences or KDE config/command history. Isolate Frameworks paths
    // before QApplication can initialize a native style or shared config.
    QTemporaryDir settings_directory;
    if (!settings_directory.isValid()) {
        qFatal("Could not create isolated test settings directory");
    }
    qputenv("XDG_CONFIG_HOME", settings_directory.filePath("config").toUtf8());
    qputenv("XDG_STATE_HOME", settings_directory.filePath("state").toUtf8());
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    // Native platform helpers can block before queued Qt test replies run.
    // Exercise the host's modal/reentrancy semantics with the Qt dialog;
    // real native dialog interaction is covered by the separate app session.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
    QApplication app(argc, argv);
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    // Many independent window lifetimes share this test activity. Finishing
    // one fixture must not finish Android's activity before the suite ends.
    QApplication::setQuitOnLastWindowClosed(false);
#endif
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat, QSettings::UserScope, settings_directory.path()
    );
    // Organization/application constructors use NativeFormat independently
    // of the default format. Isolate those production entry points too.
    QSettings::setPath(
        QSettings::NativeFormat, QSettings::UserScope, settings_directory.path()
    );

    int status = 0;
    const QString selected_suite
        = qEnvironmentVariable("KCUCKOOUNTER_TEST_SUITE");
    bool matched = false;
    const auto run = [&](auto& suite) {
        if (!selected_suite.isEmpty()
            && selected_suite != QLatin1String(suite.metaObject()->className()))
            return 0;
        matched = true;
        return QTest::qExec(&suite, argc, argv);
    };

    {
        gameplay_session_tests t;
        status |= run(t);
    }
    {
        gameplay_checkpoint_tests t;
        status |= run(t);
    }
    {
        gameplay_progress_tests t;
        status |= run(t);
    }
    {
        gameplay_recovery_tests t;
        status |= run(t);
    }

    {
        asset_locator_tests t;
        status |= run(t);
    }
    {
        card_sheet_tests t;
        status |= run(t);
    }
    {
        card_widget_tests t;
        status |= run(t);
    }
    {
        preview_carousel_tests t;
        status |= run(t);
    }
    {
        preferences_tests t;
        status |= run(t);
    }
    {
        image_source_tests t;
        status |= run(t);
    }
    {
        strategy_data_tests t;
        status |= run(t);
    }
    {
        table_tests t;
        status |= run(t);
    }
    {
        gameplay_start_tests t;
        status |= run(t);
    }
    {
        gameplay_restore_tests t;
        status |= run(t);
    }
    {
        gameplay_completion_tests t;
        status |= run(t);
    }
    {
        gameplay_mobile_tests t;
        status |= run(t);
    }
    // This desktop fixture skips in initTestCase on Android. Keep it last:
    // the combined QtTest runner otherwise skips the following mobile class.
    {
        gameplay_host_tests t;
        status |= run(t);
    }

    if (!matched) {
        std::fprintf(
            stderr, "Unknown test suite: %s\n", qPrintable(selected_suite)
        );
        return 1;
    }
    return status;
}
