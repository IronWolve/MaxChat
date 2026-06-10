#pragma once

#include <QColor>
#include <QList>
#include <QPair>
#include <QPalette>
#include <QString>

namespace maxchat::ui {

struct AppThemeDefinition {
  QString id;
  QString label;
  bool dark = true;
  QColor bg;
  QColor panel;
  QColor panel2;
  QColor text;
  QColor on;
  QString onText;
  QColor accent;
  QColor groove;
  QColor scroll;
  QColor scrollHi;
  QColor chatBg;
  QColor chatFg;
  QString wallpaper;
  QList<QPair<double, QColor>> bgGradient;
};

struct ChatThemeDefinition {
  QString id;
  QString label;
  QColor bg;
  QColor fg;
  bool fixedFont = true;
  // Optional terminal-look extras (irssi/BitchX): invalid color = follow the
  // formatter's defaults.
  QColor timestamp;
  QColor bracket;
  QColor system;
  bool monoNicks = false;
  QList<QColor> nickPalette;
};

struct WallpaperDefinition {
  QString value;
  QString label;
};

[[nodiscard]] QString defaultThemeId();
[[nodiscard]] QString systemThemeId();
[[nodiscard]] QString normalizeThemeId(const QString &theme);
[[nodiscard]] QString normalizeChatThemeId(const QString &chatTheme);
[[nodiscard]] QString normalizeWallpaperValue(const QString &wallpaper);
[[nodiscard]] QList<AppThemeDefinition> appThemes();
[[nodiscard]] QList<ChatThemeDefinition> chatThemes();
[[nodiscard]] QList<WallpaperDefinition> wallpaperChoices();
[[nodiscard]] AppThemeDefinition appThemeById(const QString &theme);
[[nodiscard]] ChatThemeDefinition chatThemeById(const QString &chatTheme);
[[nodiscard]] QString effectiveWallpaperPath(const QString &theme,
                                             const QString &wallpaper);
// QPalette matching a theme, so the OS palette can't bleed into unstyled
// widgets. For "Themes Off" returns a default QPalette() - the caller should
// restore the platform standard palette instead.
[[nodiscard]] QPalette paletteForAppearance(const QString &theme);
[[nodiscard]] QString styleSheetForAppearance(const QString &theme,
                                              const QString &chatTheme,
                                              const QString &wallpaper);

} // namespace maxchat::ui
