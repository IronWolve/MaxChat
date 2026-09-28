#pragma once
#include <QCoreApplication>
#include <QDir>

namespace maxchat::app {
// Assets stay inside the installed app; never resolve them from the caller's cwd.
inline QString bundledDataDirectory() {
    const QString executableDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    const QDir directory(executableDir);
    if (directory.dirName() == QLatin1String("MacOS") &&
        QDir(QDir::cleanPath(directory.filePath(QStringLiteral("..")))).dirName() == QLatin1String("Contents"))
        return QDir::cleanPath(directory.filePath(QStringLiteral("../Resources")));
#endif
    return executableDir;
}
// Portable user data belongs beside a .app, never inside its signed contents.
inline QString portableDataBaseDirectory() {
    const QString executableDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    if (bundledDataDirectory() != executableDir)
        return QDir::cleanPath(QDir(executableDir).filePath(QStringLiteral("../../..")));
#endif
    return executableDir;
}
} // namespace maxchat::app
