#pragma once

#include <QObject>
#include <QString>

namespace maxchat::irc {
class IrcConnection;
}

namespace maxchat::ui {

class MainWindow;

// The IRC event router (decomp Phase 7). Owns the wiring from an IrcConnection's
// ~30 signals to the per-network handlers that route server events into the
// buffer model / member list / notifications. Lifted out of MainWindow as a
// behaviour-preserving relocation: the handlers still drive MainWindow directly
// (IrcRouter is a friend holding a MainWindow&), so this is a pure move — the
// god-object's largest single function out of MainWindow.cpp — not a rewrite. A
// later pass can narrow the coupling to an interface.
class IrcRouter : public QObject {
    Q_OBJECT

  public:
    IrcRouter(MainWindow& window, QObject* parent = nullptr) : QObject(parent), m_window(window) {}

    // Wire every signal of `irc` (the connection for `network`; empty = the
    // primary connection) to its handler. Called once per connection.
    void wire(const QString& network, maxchat::irc::IrcConnection* irc);

  private:
    MainWindow& m_window;
};

} // namespace maxchat::ui
