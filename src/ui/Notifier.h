#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>

namespace maxchat::ui {

class ToastWidget;

/**
 * Manages a stack of toast notifications in the chosen screen corner.
 * Port of Python maxchat.ui.notifier.Notifier.
 */
class Notifier final : public QObject {
    Q_OBJECT
public:
    explicit Notifier(QObject* parent = nullptr);

    /// Show a toast. Colors are (bg, fg, accent). corner: "tl","tr","bl","br".
    void show(const QString& title, const QString& body,
              const QColor& bg, const QColor& fg, const QColor& accent,
              const QString& corner = QStringLiteral("br"),
              int durationMs = 6000,
              const QIcon& icon = {},
              std::function<void()> onClick = {});

    /// Resolve a notify_theme setting to (bg, fg, accent) colors.
    /// follow = (appPanel, appText, appAccent) — used when theme == "follow".
    static QColor paletteBg(const QString& theme, const QColor& follow);
    static QColor paletteFg(const QString& theme, const QColor& follow);
    static QColor paletteAccent(const QString& theme, const QColor& follow);

private:
    void remove(ToastWidget* toast);
    void relayout();

    QList<ToastWidget*> m_toasts;
    QString m_corner = QStringLiteral("br");

    static constexpr int Margin = 18;
    static constexpr int Gap = 9;
    static constexpr int MaxVisible = 4;
};

} // namespace maxchat::ui