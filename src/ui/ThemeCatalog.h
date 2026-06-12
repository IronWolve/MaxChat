#pragma once

#include <QColor>
#include <QList>
#include <QPair>
#include <QPalette>
#include <QString>
#include <QVariantMap>

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
  // Optional bundled font preferences (only whitelisted *_font_family/size/bold
  // keys survive parsing) — applied when the theme is selected.
  QVariantMap fonts;
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

// Rebuild the in-memory theme lists from bundled assets + the user config dir
// (call after saving a user theme so menus/combos pick it up without a restart).
void reloadThemes();
// Save an edited theme as a user JSON file and reload; returns the new id
// ("u-<slug>"), or empty on failure.
[[nodiscard]] QString saveUserAppTheme(const QString &name, const AppThemeDefinition &theme);
[[nodiscard]] QString saveUserChatTheme(const QString &name, const ChatThemeDefinition &theme);

// Theme packs: a single shareable JSON file bundling an app theme, a chat
// theme, font preferences, and a wallpaper choice.
struct ThemePack {
  QString name;
  AppThemeDefinition app;   // participates when !app.id.isEmpty()
  ChatThemeDefinition chat; // participates when !chat.id.isEmpty()
  QVariantMap fonts;        // whitelisted *_font_* keys only
  QString wallpaper;
  QString error; // import: non-empty on failure
};
// Write the pack to a JSON file. Returns false on I/O failure.
[[nodiscard]] bool exportThemePack(const QString &path, const ThemePack &pack);
// Read a pack (or a bare single-theme JSON) and install its themes as user
// themes; the returned pack carries the new theme ids. Check .error.
[[nodiscard]] ThemePack importThemePack(const QString &path);
// The font settings keys a theme may bundle (used for whitelisting).
[[nodiscard]] QStringList themeFontKeys();

// Where user app themes live on disk (<config>/maxchat/themes).
[[nodiscard]] QString userThemeDirectoryPath();

// User-created themes ("u-" ids) can be deleted; built-ins cannot.
[[nodiscard]] bool isUserThemeId(const QString &id);
bool deleteUserAppTheme(const QString &id);
bool deleteUserChatTheme(const QString &id);
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
