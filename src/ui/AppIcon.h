#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace maxchat::ui {

/**
 * Tray / window icon choices matching the Python app.
 * "bubble" = generated speech bubble tinted by the current accent colour.
 * Any other value is treated as an emoji glyph.
 */
class AppIcon {
public:
    /** Default generated speech-bubble icon (theme tinted). */
    static QIcon makeIcon(const QString& choice, const QColor& accent);

    /** The 64 px pixmap used for both window and tray icons. */
    static QPixmap bubblePixmap(const QColor& accent, int size = 64);
    static QPixmap emojiPixmap(const QString& glyph, int size = 64);
};

} // namespace maxchat::ui