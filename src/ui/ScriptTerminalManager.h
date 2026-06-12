#pragma once

#include "ui/TerminalProfile.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>

namespace maxchat::ui {

class ScriptTerminalDialog;

// Tree/launcher metadata for one live terminal session.
struct TerminalInfo {
    QString id;         // scoped id (script\x1fterminal)
    QString network;    // IRC network the owning script ran on
    QString scriptName; // script that opened it
    QString label;      // display label, e.g. "Term 1"
};

class ScriptTerminalManager final : public QObject {
    Q_OBJECT

  public:
    explicit ScriptTerminalManager(QWidget* parent = nullptr);

    [[nodiscard]] bool hasTerminal(const QString& id) const;
    [[nodiscard]] QSize terminalSize(const QString& id) const;

    ScriptTerminalDialog* openTerminal(const QString& id, const QString& title,
                                       const TerminalProfile& profile,
                                       const QString& network = {},
                                       const QString& scriptName = {});
    // Re-show / raise a hidden terminal (clicking its tree node).
    void showTerminal(const QString& id);
    // Hide without destroying (close button / File > Close).
    void closeTerminal(const QString& id);
    // Master close: destroy the session and forget it (File > Kill / tree menu).
    void killTerminal(const QString& id);
    void closeAll();
    // Live terminals in creation order, for building the tree nodes.
    [[nodiscard]] QList<TerminalInfo> terminals() const;
    // Network the terminal's owning script ran on (empty if unknown).
    [[nodiscard]] QString terminalNetwork(const QString& id) const;
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
    // network = the terminal's owning network, so script teardown hooks run in
    // the right connection context (not whatever network happens to be active).
    void terminalClosed(const QString& id, const QString& network);
    // A terminal was created or killed; the tree should rebuild its nodes.
    void terminalsChanged();
    // The user changed terminal font/grid from a terminal's Settings menu.
    void fontPreferenceChanged(const QString& family, int pointSize, bool bold);
    void gridSizeChanged(const QString& id, int cols, int rows);

  private:
    [[nodiscard]] ScriptTerminalDialog* terminal(const QString& id) const;

    QWidget* parentWidget_ = nullptr;
    QHash<QString, QPointer<ScriptTerminalDialog>> terminals_;
    QHash<QString, TerminalInfo> meta_;
    QList<QString> order_; // creation order of ids
    int nextOrdinal_ = 1;
    QString fontFamily_ = QStringLiteral("JetBrains Mono");
    int fontPointSize_ = 12;
    bool fontBold_ = false;
};

} // namespace maxchat::ui
