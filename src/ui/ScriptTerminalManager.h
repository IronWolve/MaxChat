#pragma once

#include "ui/TerminalProfile.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSize>

namespace maxchat::ui {

class ScriptTerminalDialog;

class ScriptTerminalManager final : public QObject {
    Q_OBJECT

  public:
    explicit ScriptTerminalManager(QWidget* parent = nullptr);

    [[nodiscard]] bool hasTerminal(const QString& id) const;
    [[nodiscard]] QSize terminalSize(const QString& id) const;

    ScriptTerminalDialog* openTerminal(const QString& id, const QString& title,
                                       const TerminalProfile& profile);
    void closeTerminal(const QString& id);
    void closeAll();
    void writeText(const QString& id, const QString& text);
    void clear(const QString& id);
    bool applyFrame(const QString& id, const QString& ops, QString* error = nullptr);
    void setStatusText(const QString& id, const QString& text);
    void setPromptText(const QString& id, const QString& text);
    void setProfile(const QString& id, const TerminalProfile& profile);
    void setFitMode(const QString& id, const QString& mode);
    // Apply the user's terminal font preference to all current and future
    // terminals. pointSize <= 0 keeps each profile's own size.
    void setTerminalFont(const QString& family, int pointSize, bool bold);

  signals:
    void inputSubmitted(const QString& id, const QString& text);
    void linkActivated(const QString& id, const QString& actionId);
    void terminalClosed(const QString& id);

  private:
    [[nodiscard]] ScriptTerminalDialog* terminal(const QString& id) const;

    QWidget* parentWidget_ = nullptr;
    QHash<QString, QPointer<ScriptTerminalDialog>> terminals_;
    QString fontFamily_ = QStringLiteral("JetBrains Mono");
    int fontPointSize_ = 0;
    bool fontBold_ = false;
};

} // namespace maxchat::ui
