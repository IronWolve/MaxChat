#include "Notifier.h"
#include "ToastWidget.h"

#include <QGuiApplication>
#include <QScreen>

#include <functional>
#include <utility>

namespace maxchat::ui {

// Palette tables matching Python notifier.PALETTES
struct ToastPalette { const char* bg; const char* fg; const char* accent; };
static const ToastPalette kPalettes[] = {
    {"#2b2b2b", "#e8e8e8", "#4a9eff"},  // dark
    {"#f5f5f5", "#1a1a1a", "#2563eb"},  // light
    {"#073642", "#93a1a1", "#268bd2"},  // solar-dark
    {"#fdf6e3", "#586e75", "#cb4b16"},  // solar-light
};
static constexpr int kPaletteCount = 4;
static const char* kPaletteKeys[] = {"dark", "light", "solar-dark", "solar-light"};

static int paletteIndex(const QString& theme) {
    for (int i = 0; i < kPaletteCount; ++i) {
        if (theme == QLatin1String(kPaletteKeys[i])) return i;
    }
    return -1;
}

QColor Notifier::paletteBg(const QString& theme, const QColor& follow) {
    if (theme == QLatin1String("follow") || theme.isEmpty()) {
        // "follow" or unknown: use the follow palette (derived from live app theme)
        return follow;
    }
    int idx = paletteIndex(theme);
    return idx >= 0 ? QColor(QString::fromLatin1(kPalettes[idx].bg)) : follow;
}

QColor Notifier::paletteFg(const QString& theme, const QColor& follow) {
    if (theme == QLatin1String("follow") || theme.isEmpty()) {
        return follow;
    }
    int idx = paletteIndex(theme);
    return idx >= 0 ? QColor(QString::fromLatin1(kPalettes[idx].fg)) : follow;
}

QColor Notifier::paletteAccent(const QString& theme, const QColor& follow) {
    if (theme == QLatin1String("follow") || theme.isEmpty()) {
        return follow;
    }
    int idx = paletteIndex(theme);
    return idx >= 0 ? QColor(QString::fromLatin1(kPalettes[idx].accent)) : follow;
}

Notifier::Notifier(QObject* parent)
    : QObject(parent) {}

void Notifier::show(const QString& title, const QString& body,
                    const QColor& bg, const QColor& fg, const QColor& accent,
                    const QString& corner, int durationMs,
                    const QIcon& icon, std::function<void()> onClick) {
    m_corner = corner;

    // Drop oldest if at max
    while (m_toasts.size() >= MaxVisible) {
        m_toasts.first()->close();
    }

    auto* toast = new ToastWidget(title, body, bg, fg, accent, durationMs, icon,
                                  std::move(onClick));
    connect(toast, &ToastWidget::done, this, &Notifier::remove);
    m_toasts.append(toast);
    toast->show();
    relayout();
}

void Notifier::remove(ToastWidget* toast) {
    m_toasts.removeOne(toast);
    relayout();
}

void Notifier::relayout() {
    // Toasts belong on the screen the app lives on, not always the primary
    // monitor. The Notifier is parented to the main window.
    QScreen* screen = nullptr;
    if (auto* window = qobject_cast<QWidget*>(parent())) {
        screen = window->screen();
    }
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) return;

    QRect g = screen->availableGeometry();
    bool atRight = m_corner == QStringLiteral("tr") || m_corner == QStringLiteral("br");
    bool atBottom = m_corner == QStringLiteral("bl") || m_corner == QStringLiteral("br");

    // Bottom corners stack upward (newest nearest the corner)
    QList<ToastWidget*> order;
    if (atBottom) {
        order = m_toasts;
        std::reverse(order.begin(), order.end());
    } else {
        order = m_toasts;
    }

    int y = atBottom ? (g.bottom() - Margin) : (g.top() + Margin);
    for (ToastWidget* t : order) {
        int h = t->height();
        int x = atRight ? (g.right() - Margin - t->width()) : (g.left() + Margin);
        if (atBottom) {
            y -= h;
            t->move(x, y);
            y -= Gap;
        } else {
            t->move(x, y);
            y += h + Gap;
        }
    }
}

} // namespace maxchat::ui