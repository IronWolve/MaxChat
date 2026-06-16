#include "app/AppInfo.h"
#include "core/SettingsStore.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QString>
#include <QTextStream>
#include <QTranslator>

namespace {

// Loads UI translations per the "interface_language" setting ("system" = OS
// locale). App .qm files live in translations/ next to the binary (or one
// level up in the source tree); Qt's own dialogs load from the Qt install.
// Translators must outlive the app, hence static.
void installTranslators(QApplication& app) {
    const QVariantMap settings =
        maxchat::core::SettingsStore(maxchat::core::standardSettingsPaths()).loadWithDefaults();
    const QString configured =
        settings.value(QStringLiteral("interface_language"), QStringLiteral("system"))
            .toString()
            .trimmed();
    const QLocale locale = configured.isEmpty() || configured == QStringLiteral("system")
                               ? QLocale::system()
                               : QLocale(configured);
    if (locale.language() == QLocale::C || locale.language() == QLocale::English) {
        return;
    }

    static QTranslator qtTranslator;
    if (qtTranslator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(&qtTranslator);
    }

    static QTranslator appTranslator;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QStringLiteral(":/i18n"), // .qm embedded in the binary by qt_add_translations
        QDir(appDir).filePath(QStringLiteral("translations")),
        QDir(appDir).filePath(QStringLiteral("../translations")),
    };
    for (const QString& dir : candidates) {
        if (appTranslator.load(locale, QStringLiteral("maxchat"), QStringLiteral("_"), dir)) {
            QCoreApplication::installTranslator(&appTranslator);
            break;
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    const bool selfTestRequested = [&argc, &argv]() {
        for (int i = 1; i < argc; ++i) {
            if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--selftest")) {
                return true;
            }
        }
        return false;
    }();

    if (selfTestRequested && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(maxchat::app::applicationName());
    QApplication::setApplicationDisplayName(maxchat::app::displayName());
    QApplication::setApplicationVersion(maxchat::app::version());
    QApplication::setOrganizationName(maxchat::app::organizationName());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Native C++/Qt port experiment for MaxChat"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption selfTestOption(QStringLiteral("selftest"),
                                      QStringLiteral("Run startup self-test and exit."));
    parser.addOption(selfTestOption);
    parser.process(app);

    installTranslators(app);

    maxchat::ui::MainWindow window;

    if (parser.isSet(selfTestOption)) {
        QTextStream out(stdout);
        if (!window.selfTest()) {
            QTextStream err(stderr);
            err << "MaxChat C++ selftest FAILED\n";
            return 1;
        }
        out << maxchat::app::displayName() << " " << maxchat::app::version() << " selftest OK\n";
        return 0;
    }

    window.show();
    return QApplication::exec();
}
