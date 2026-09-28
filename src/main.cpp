#include "app/BundledPaths.h"
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
        maxchat::core::SettingsStore(maxchat::core::standardSettingsPaths()).loadPublicWithDefaults();
    const QString configured =
        settings.value(QStringLiteral("interface_language"), QStringLiteral("system"))
            .toString()
            .trimmed();
    const QLocale locale = configured.isEmpty() || configured == QStringLiteral("system")
                               ? QLocale::system()
                               : QLocale(configured);
    // Mirror the whole UI for RTL locales (Arabic, Hebrew, …). Qt does not flip
    // layout direction automatically just because an RTL translation loaded.
    app.setLayoutDirection(locale.textDirection());
    if (locale.language() == QLocale::C || locale.language() == QLocale::English) {
        return;
    }

    static QTranslator qtTranslator;
    if (qtTranslator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(&qtTranslator);
    }

    static QTranslator appTranslator;
    const QString appDir = maxchat::app::bundledDataDirectory();
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
            if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--selftest") ||
                QString::fromLocal8Bit(argv[i]).startsWith(QStringLiteral("--selftest-screenshot"))) {
                return true;
            }
        }
        return false;
    }();

    if (selfTestRequested && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    // Remote media is downloaded through the guarded transport first. Forbid
    // nested network protocols in the decoder even for a disguised playlist.
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    qputenv("QT_FFMPEG_PROTOCOL_WHITELIST", "file");
    QApplication app(argc, argv);
    QApplication::setApplicationName(maxchat::app::applicationName());
    QApplication::setApplicationDisplayName(maxchat::app::displayName());
    QApplication::setApplicationVersion(maxchat::app::version());
    QApplication::setOrganizationName(maxchat::app::organizationName());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MaxChat graphical IRC client"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption selfTestOption(QStringLiteral("selftest"),
                                      QStringLiteral("Run startup self-test and exit."));
    parser.addOption(selfTestOption);
    QCommandLineOption profileOption(QStringLiteral("profile"),
        QStringLiteral("Use an isolated profile (relative paths are beside the application)."), QStringLiteral("directory"));
    QCommandLineOption screenshotOption(QStringLiteral("selftest-screenshot"),
        QStringLiteral("Save a startup self-test screenshot and exit."), QStringLiteral("file"));
    parser.addOption(profileOption);
    parser.addOption(screenshotOption);
    parser.process(app);
    if (parser.isSet(profileOption)) {
        if (parser.value(profileOption).trimmed().isEmpty()) { QTextStream(stderr) << "Profile directory must not be empty\n"; return 1; }
        qputenv("MAXCHAT_PROFILE_DIR", parser.value(profileOption).toUtf8());
    }
    app.setProperty("maxchat.selftest", parser.isSet(selfTestOption) || parser.isSet(screenshotOption));

    installTranslators(app);

    maxchat::ui::MainWindow window;

    if (parser.isSet(selfTestOption) || parser.isSet(screenshotOption)) {
        QTextStream out(stdout);
        if (!window.selfTest()) {
            QTextStream err(stderr);
            err << "MaxChat C++ selftest FAILED\n";
            return 1;
        }
        if (parser.isSet(screenshotOption)) {
            window.show();
            QApplication::processEvents();
            if (!window.grab().save(parser.value(screenshotOption))) {
                QTextStream(stderr) << "Could not save self-test screenshot\n";
                return 1;
            }
        }
        out << maxchat::app::displayName() << " " << maxchat::app::version() << " selftest OK\n";
        return 0;
    }

    window.show();
    return QApplication::exec();
}
