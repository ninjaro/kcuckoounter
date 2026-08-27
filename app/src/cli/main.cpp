#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QIcon>
#include <memory>

#include "shell/main_window.hpp"

#include "arch/asset_locator.hpp"
#include "arch/str_label.hpp"

#ifndef ECOSYSTEM_PROJECT_VERSION
#define ECOSYSTEM_PROJECT_VERSION "1.0.0"
#endif

#ifdef KC_KDE
#include <KAboutData>
#include <KLocalizedString>
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setWindowIcon(
        QIcon(bundled_asset_path(QStringLiteral("favicon.ico")))
    );

#ifdef KC_KDE
    KLocalizedString::setApplicationDomain("kcuckoounter");

    KAboutData about_data(
        str_label("kcuckoounter"), str_label("kcuckoounter"),
        QStringLiteral(ECOSYSTEM_PROJECT_VERSION),
        str_label("A tool for card counting training."), KAboutLicense::MIT,
        str_label("(c) 2025, Yaroslav Riabtsev"), QString(),
        str_label("https://github.com/ninjaro/kcuckoounter"),
        str_label("yaroslav.riabtsev@rwth-aachen.de")
    );

    about_data.addAuthor(
        str_label("Yaroslav Riabtsev"), str_label("Original author"),
        str_label("yaroslav.riabtsev@rwth-aachen.de"),
        str_label("https://github.com/ninjaro"), str_label("ninjaro")
    );

    KAboutData::setApplicationData(about_data);

    QCommandLineParser parser;
    about_data.setupCommandLine(&parser);
    parser.process(app);
    about_data.processCommandLine(&parser);
#else
    QCoreApplication::setApplicationName(str_label("kcuckoounter"));
    QCoreApplication::setOrganizationName(str_label("kcuckoounter"));
    QCoreApplication::setApplicationVersion(
        QStringLiteral(ECOSYSTEM_PROJECT_VERSION)
    );

    QCommandLineParser parser;
    parser.setApplicationDescription(
        str_label("A tool for card counting training.")
    );
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);
#endif

    auto window = std::make_unique<main_window>();
#if defined(Q_OS_ANDROID)
    window->showMaximized();
#else
    window->show();
#endif

    int result = QApplication::exec();
    window.reset();
    return result;
}
