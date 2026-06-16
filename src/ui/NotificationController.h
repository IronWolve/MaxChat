#pragma once

#include <QObject>
#include <QString>

namespace maxchat::ui {

class MainWindow;

// The notification + tray subsystem (decomp Phase 4). Owns the toast/OS-
// notification dispatch (notify), the system-tray icon setup + state
// (setupTrayIcon / updateTrayIcon), and the minimize-to-tray policy. Lifted out
// of MainWindow as a behaviour-preserving relocation: NotificationController is a
// friend holding a MainWindow& and drives the window's m_tray / m_notif* state
// directly via m_window. prefixes (compiler-verified). MainWindow keeps thin
// notify()/updateTrayIcon() forwarders for its external callers + host interface.
class NotificationController : public QObject {
    Q_OBJECT

  public:
    NotificationController(MainWindow& window, QObject* parent = nullptr)
        : QObject(parent), m_window(window) {}

    void notify(const QString& title, const QString& text, const QString& network,
                const QString& target);
    void setupTrayIcon();
    void updateTrayIcon();
    void updateMinimizeToTrayFromSettings();

  private:
    MainWindow& m_window;
};

} // namespace maxchat::ui
