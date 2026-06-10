#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace maxchat::core {

class ChatLogStore final {
  public:
    explicit ChatLogStore(QString logRoot);

    [[nodiscard]] QString logRoot() const;
    [[nodiscard]] QString logMask() const;
    void setLogMask(const QString& mask);
    [[nodiscard]] QString logFilePath(const QString& network, const QString& target,
                                      QDate date = QDate::currentDate()) const;
    [[nodiscard]] bool appendLine(const QString& network, const QString& target,
                                  const QString& line,
                                  QDateTime timestamp = QDateTime::currentDateTime()) const;
    [[nodiscard]] QStringList recentLines(const QString& network, const QString& target,
                                          int maxLines = 200,
                                          QDate date = QDate::currentDate()) const;

    [[nodiscard]] static QString safePathPart(const QString& value,
                                              const QString& fallback = QStringLiteral("server"));

  private:
    QString logRoot_;
    QString logMask_;
};

} // namespace maxchat::core
