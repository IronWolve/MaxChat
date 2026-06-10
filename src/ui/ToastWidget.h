#pragma once

#include <QColor>
#include <QIcon>
#include <QString>
#include <QWidget>

#include <functional>

namespace maxchat::ui {

/**
 * A single frameless toast notification popup.
 * Port of Python maxchat.ui.notifier._Toast.
 */
class ToastWidget final : public QWidget {
    Q_OBJECT
public:
    ToastWidget(const QString& title, const QString& body,
                const QColor& bg, const QColor& fg, const QColor& accent,
                int durationMs, const QIcon& icon,
                std::function<void()> onClick,
                QWidget* parent = nullptr);

signals:
    void done(ToastWidget* self);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    std::function<void()> m_onClick;
};

} // namespace maxchat::ui