#include "core/ChatLogStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

#include <utility>

namespace maxchat::core {

ChatLogStore::ChatLogStore(QString logRoot) : logRoot_(std::move(logRoot)) {}

QString ChatLogStore::logRoot() const {
    return logRoot_;
}

QString ChatLogStore::safePathPart(const QString& value, const QString& fallback) {
    QString result = value.trimmed();
    result.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f])")),
                   QStringLiteral("_"));
    result.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    result = result.left(80).trimmed();
    while (result.startsWith(QLatin1Char('.'))) {
        result.remove(0, 1);
    }
    return result.isEmpty() ? fallback : result;
}

QString ChatLogStore::logMask() const {
    return logMask_;
}

void ChatLogStore::setLogMask(const QString& mask) {
    logMask_ = mask.trimmed();
}

QString ChatLogStore::logFilePath(const QString& network, const QString& target, QDate date) const {
    const QString networkPart = safePathPart(network, QStringLiteral("network"));
    const QString targetPart = safePathPart(target);

    // %network / %channel tokens plus the strftime-style date codes the Python
    // app documents (%Y %m %d); "/" makes subfolders. The default mask matches
    // the historical Network/target/yyyy-MM-dd.log layout.
    QString mask = logMask_;
    if (mask.isEmpty()) {
        mask = QStringLiteral("%network-%channel");
    }
    mask.replace(QStringLiteral("%network"), networkPart);
    mask.replace(QStringLiteral("%channel"), targetPart);
    mask.replace(QStringLiteral("%Y"), date.toString(QStringLiteral("yyyy")));
    mask.replace(QStringLiteral("%m"), date.toString(QStringLiteral("MM")));
    mask.replace(QStringLiteral("%d"), date.toString(QStringLiteral("dd")));

    QString path = logRoot_;
    const QStringList parts =
        mask.replace(QLatin1Char('\\'), QLatin1Char('/')).split(QLatin1Char('/'),
                                                                Qt::SkipEmptyParts);
    bool added = false;
    for (const QString& part : parts) {
        const QString safe = safePathPart(part, QString());
        if (safe.isEmpty() || safe == QStringLiteral(".") || safe == QStringLiteral("..")) {
            continue;
        }
        path = QDir(path).filePath(safe);
        added = true;
    }
    if (!added) {
        path = QDir(path).filePath(QStringLiteral("chat"));
    }
    return path + QStringLiteral(".log");
}

bool ChatLogStore::appendLine(const QString& network, const QString& target, const QString& line,
                              QDateTime timestamp) const {
    const QString trimmed = line.trimmed();
    if (logRoot_.isEmpty() || trimmed.isEmpty() || !timestamp.isValid()) {
        return false;
    }

    const QString path = logFilePath(network, target, timestamp.date());
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) << QStringLiteral(" ")
           << trimmed << Qt::endl;
    return stream.status() == QTextStream::Ok;
}

QStringList ChatLogStore::recentLines(const QString& network, const QString& target, int maxLines,
                                      QDate date) const {
    if (maxLines <= 0) {
        return {};
    }

    QFile file(logFilePath(network, target, date));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QStringList lines;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        lines.append(stream.readLine());
        while (lines.size() > maxLines) {
            lines.removeFirst();
        }
    }
    return lines;
}

} // namespace maxchat::core
