#include "ui/NotificationController.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QSystemTrayIcon>

#include <QMenu>

#include "app/AppInfo.h"
#include "ui/AppIcon.h"
#include "ui/AppearanceController.h"
#include "ui/MainWindow.h"
#include "ui/Notifier.h"
#include "ui/ThemeCatalog.h"

namespace maxchat::ui {

void NotificationController::notify(const QString& title, const QString& text,
                        const QString& network, const QString& target) {
    // Don't notify if window is active or DND is on (matches Python)
    if (m_window.isActiveWindow() || !m_window.m_notifier) return;
    if (m_window.m_settings.loadWithDefaults().value(QStringLiteral("dnd"), false).toBool()) return;

    // Taskbar flash
    if (m_window.m_notifyFlash) {
        QApplication::alert(&m_window, 0);
    }

    // Sound — the user's chosen .wav (or a bundled default) via QSoundEffect,
    // falling back to the system beep if no .wav is available.
    if (m_window.m_notifySound) {
        const QString soundsDir =
            QDir(m_window.m_settings.paths().configDir).filePath(QStringLiteral("sounds"));
        const QString bundled = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("assets/sounds"));
        const QString selectedSound = m_window.m_settings.loadWithDefaults().value(QStringLiteral("notify_sound_file"), QStringLiteral("notify.wav")).toString();
        if (!m_window.m_soundPlayer.play(notifySoundPath(soundsDir, bundled, selectedSound))) {
            QApplication::beep();
        }
    }

    // Popup style
    if (m_window.m_notifyStyle == QLatin1String("off")) return;

    // OS native notification - post through the visible tray icon (showMessage
    // on a hidden tray is a no-op), available only when a daemon/tray accepts it.
    if (m_window.m_notifyStyle == QLatin1String("system") && m_window.m_osNotifyAvailable && m_window.m_tray != nullptr) {
        // Remember where this notification points so messageClicked can open
        // the right buffer (clicks used to do nothing on the system path).
        m_window.m_lastNotifyNetwork = network;
        m_window.m_lastNotifyTarget = target;
        m_window.m_tray->showMessage(title, text, m_window.m_tray->icon(),
                            std::max(1, m_window.m_notifyDuration) * 1000);
        return;
    }

    // Custom toast (also fallback when system tray unavailable)
    const AppThemeDefinition& themeDef = m_window.m_appearance->appTheme();
    QColor followBg = themeDef.panel.isValid() ? themeDef.panel : QColor(QStringLiteral("#2b2b2b"));
    QColor followFg = themeDef.text.isValid() ? themeDef.text : QColor(QStringLiteral("#e8e8e8"));
    QColor followAccent = themeDef.accent.isValid() ? themeDef.accent : QColor(QStringLiteral("#4a9eff"));

    QColor bg = Notifier::paletteBg(m_window.m_notifyTheme, followBg);
    QColor fg = Notifier::paletteFg(m_window.m_notifyTheme, followFg);
    QColor accent = Notifier::paletteAccent(m_window.m_notifyTheme, followAccent);

    const int durationMs = m_window.m_notifyDuration * 1000;

    QIcon icon;
    icon = ui::AppIcon::makeIcon(
        m_window.m_settings.loadWithDefaults().value(QStringLiteral("tray_icon"), QStringLiteral("bubble")).toString(),
        accent);

    std::function<void()> onClick;
    if (!network.isEmpty() && !target.isEmpty()) {
        QString net = network;
        QString tgt = target;
        onClick = [this, net, tgt]() {
            if (m_window.isMinimized()) {
                m_window.showNormal();
            } else {
                m_window.show();
            }
            m_window.raise();
            m_window.activateWindow();
            m_window.setActiveNetwork(net);
            m_window.activateBufferTarget(tgt);
        };
    } else {
        onClick = [this]() {
            if (m_window.isMinimized()) {
                m_window.showNormal();
            } else {
                m_window.show();
            }
            m_window.raise();
            m_window.activateWindow();
        };
    }

    m_window.m_notifier->show(title, text, bg, fg, accent, m_window.m_notifyCorner, durationMs, icon, std::move(onClick));
}

void NotificationController::setupTrayIcon() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        m_window.m_tray = nullptr;
        return;
    }

    m_window.m_tray = new ::QSystemTrayIcon(&m_window);
    m_window.m_trayMenu = new QMenu(&m_window);

    m_window.m_trayMenu->addAction(tr("Show / Hide"), &m_window, &MainWindow::toggleWindowVisibility);
    m_window.m_trayMenu->addSeparator();

    // Reuse the menu-bar DND action so both stay in sync (matches Python)
    if (m_window.m_doNotDisturbAction) {
        m_window.m_trayMenu->addAction(m_window.m_doNotDisturbAction);
    }
    m_window.m_trayMenu->addSeparator();
    m_window.m_trayMenu->addAction(tr("Quit"), &m_window, &MainWindow::quitApplication);

    m_window.m_tray->setContextMenu(m_window.m_trayMenu);
    connect(m_window.m_tray, &::QSystemTrayIcon::activated, this, [this](::QSystemTrayIcon::ActivationReason r) {
        if (r == ::QSystemTrayIcon::Trigger) {
            m_window.toggleWindowVisibility();
        }
    });

    connect(m_window.m_tray, &QSystemTrayIcon::messageClicked, this, [this]() {
        if (m_window.isMinimized()) {
            m_window.showNormal();
        } else {
            m_window.show();
        }
        m_window.raise();
        m_window.activateWindow();
        if (!m_window.m_lastNotifyTarget.isEmpty()) {
            m_window.setActiveNetwork(m_window.m_lastNotifyNetwork);
            m_window.activateBufferTarget(m_window.m_lastNotifyTarget);
        }
    });

    updateTrayIcon();
    m_window.m_tray->show();
}

void NotificationController::updateTrayIcon() {
    if (!m_window.m_tray) return;

    // Pull accent from current theme, fallback to default blue
    QColor accent = m_window.m_appearance->appTheme().accent;
    if (!accent.isValid()) {
        accent = QColor(QStringLiteral("#4a9eff"));
    }

    QString choice = m_window.m_settings.loadWithDefaults()
        .value(QStringLiteral("tray_icon"), QStringLiteral("bubble")).toString();

    QIcon icon = ui::AppIcon::makeIcon(choice, accent);
    m_window.m_tray->setIcon(icon);
    m_window.m_tray->setToolTip(app::displayName());
}

void NotificationController::updateMinimizeToTrayFromSettings() {
    auto s = m_window.m_settings.loadWithDefaults();
    m_window.m_minimizeToTray = s.value(QStringLiteral("minimize_to_tray"), false).toBool();
}

} // namespace maxchat::ui
