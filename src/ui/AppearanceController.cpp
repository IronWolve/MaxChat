#include "ui/AppearanceController.h"

#include "core/SettingsStore.h"
#include "irc/IrcFormat.h"
#include "ui/AppIcon.h"
#include "ui/MainWindowHost.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QFontDatabase>
#include <QSignalBlocker>
#include <QStyle>
#include <QWidget>

#include <algorithm>

namespace maxchat::ui {

namespace {

// One area's font from its family/size/bold settings keys (JetBrains Mono 14
// bold is the shared fallback, matching the historical defaults).
QFont fontFromSettings(const QVariantMap& settings, const QString& familyKey,
                       const QString& sizeKey, const QString& boldKey) {
    QFont font(settings.value(familyKey, QStringLiteral("JetBrains Mono")).toString(),
               settings.value(sizeKey, 14).toInt());
    font.setBold(settings.value(boldKey, true).toBool());
    return font;
}

QStringList classicIrcNickPalette() {
    return {QStringLiteral("#009300"), QStringLiteral("#ff0000"), QStringLiteral("#7f0000"),
            QStringLiteral("#9c009c"), QStringLiteral("#fc7f00"), QStringLiteral("#ffff00"),
            QStringLiteral("#00fc00"), QStringLiteral("#009393"), QStringLiteral("#00ffff"),
            QStringLiteral("#0000fc"), QStringLiteral("#ff00ff"), QStringLiteral("#00007f")};
}

} // namespace

AppearanceController::AppearanceController(MainWindowHost& host, QObject* parent)
    : QObject(parent), host_(host) {}

void AppearanceController::registerBundledFonts() {
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMono-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMono-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/ComicRelief-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/ComicRelief-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/SymbolsNerdFontMono-Regular.ttf"));
}

ResolvedFonts AppearanceController::resolveFonts(const QVariantMap& settings) const {
    ResolvedFonts fonts;
    fonts.app = fontFromSettings(settings, QStringLiteral("app_font_family"),
                                 QStringLiteral("app_font_size"), QStringLiteral("app_font_bold"));
    fonts.chat = fontFromSettings(settings, QStringLiteral("chat_font_family"),
                                  QStringLiteral("chat_font_size"),
                                  QStringLiteral("chat_font_bold"));
    fonts.list = fontFromSettings(settings, QStringLiteral("list_font_family"),
                                  QStringLiteral("list_font_size"),
                                  QStringLiteral("list_font_bold"));
    return fonts;
}

// --- Theme selection state --------------------------------------------------

void AppearanceController::loadFromSettings(const QVariantMap& settings) {
    currentTheme_ = normalizeThemeId(
        settings.value(QStringLiteral("theme"), QStringLiteral("synthwave")).toString());
    currentChatTheme_ = normalizeChatThemeId(
        settings.value(QStringLiteral("chat_theme"), QStringLiteral("follow")).toString());
    currentWallpaper_ =
        normalizeWallpaperValue(settings.value(QStringLiteral("wallpaper")).toString());
    chatOpacity_ = std::clamp(settings.value(QStringLiteral("chat_opacity"), 100).toInt(), 20, 100);
    // nickColorMode_ + eventColor_ have window-side derivation (colored_nicks
    // fallback / the Fonts-page colour-override index) — pushed via their setters.
}

void AppearanceController::setThemeId(const QString& theme) {
    currentTheme_ = normalizeThemeId(theme);
}
void AppearanceController::setChatThemeId(const QString& chatTheme) {
    currentChatTheme_ = normalizeChatThemeId(chatTheme);
}
void AppearanceController::setWallpaperValue(const QString& wallpaper) {
    currentWallpaper_ = normalizeWallpaperValue(wallpaper);
}
void AppearanceController::setChatOpacity(int opacity) {
    chatOpacity_ = std::clamp(opacity, 20, 100);
}
void AppearanceController::setNickColorMode(const QString& mode) {
    nickColorMode_ = mode;
}
void AppearanceController::setEventColor(const QString& color) {
    eventColor_ = color;
}

// --- Theme-derived render inputs --------------------------------------------

QColor AppearanceController::resolvedChatBackground() const {
    const ChatThemeDefinition chatTheme = chatThemeById(currentChatTheme_);
    QColor bg = chatTheme.bg;
    if (chatTheme.id == QStringLiteral("follow") || !bg.isValid()) {
        const AppThemeDefinition appTheme = appThemeById(currentTheme_);
        bg = appTheme.chatBg.isValid() ? appTheme.chatBg : appTheme.panel;
    }
    return bg.isValid() ? bg : QColor(28, 30, 33);
}

QStringList AppearanceController::effectiveNickPalette(bool* monoOut) const {
    // THE one source of nick colours: chat view and member list both call this
    // so a given nick is the same colour everywhere.
    const ChatThemeDefinition chatTheme = chatThemeById(currentChatTheme_);
    bool mono = chatTheme.monoNicks;
    QStringList palette;
    if (nickColorMode_ == QLatin1String("irc")) {
        // Explicit classic-IRC choice overrides the chat theme's nick styling.
        mono = false;
        palette = classicIrcNickPalette();
    } else {
        for (const QColor& color : chatTheme.nickPalette) {
            palette.append(color.name());
        }
        if (palette.isEmpty()) {
            palette = maxchat::irc::defaultNickPalette();
        }
    }
    // Contrast guard: yellow/cyan vanish on light backgrounds, navy on dark.
    const QColor bg = resolvedChatBackground();
    const bool darkBg = (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()) < 150.0;
    for (QString& name : palette) {
        QColor c(name);
        if (!c.isValid()) {
            continue;
        }
        const double luma = 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
        if (darkBg && luma < 70.0) {
            name = c.lighter(170).name();
        } else if (!darkBg && luma > 170.0) {
            name = c.darker(170).name();
        }
    }
    if (monoOut != nullptr) {
        *monoOut = mono;
    }
    return palette;
}

ChatRenderTheme AppearanceController::buildChatRenderTheme() const {
    bool monoNicks = false;
    const QStringList nickPalette = effectiveNickPalette(&monoNicks);
    return resolveChatRenderTheme(chatThemeById(currentChatTheme_), appThemeById(currentTheme_),
                                  resolvedChatBackground(), nickPalette, monoNicks, eventColor_);
}

// --- Apply ------------------------------------------------------------------

void AppearanceController::applyTheme(const QString& theme) {
    const QString normalized = normalizeThemeId(theme);
    const QString styleSheet =
        styleSheetForAppearance(normalized, currentChatTheme_, currentWallpaper_, chatOpacity_);
    // Chrome font lives IN the stylesheet: with an app-wide QSS active,
    // qApp->setFont/menuBar()->setFont are unreliable — any re-polish (theme,
    // chat theme, wallpaper switch, even later widget churn) silently reverts
    // the menu bar / toolbar to the system font. A stylesheet rule cannot be
    // dropped that way.
    const QVariantMap fontSettings = host_.settings().loadWithDefaults();
    const QString family =
        fontSettings.value(QStringLiteral("app_font_family"), QStringLiteral("JetBrains Mono"))
            .toString();
    const int pointSize = fontSettings.value(QStringLiteral("app_font_size"), 14).toInt();
    const bool bold = fontSettings.value(QStringLiteral("app_font_bold"), true).toBool();
    const QString chromeFontCss =
        QStringLiteral("\nQMenuBar, QMenu, QToolBar, QToolButton { font-family:'%1'; "
                       "font-size:%2pt; font-weight:%3; }")
            .arg(family)
            .arg(pointSize)
            .arg(bold ? 700 : 400);
    // Apply palette + stylesheet app-wide so parentless dialogs are themed too,
    // and the OS palette can't bleed into widgets the QSS doesn't cover.
    if (normalized == systemThemeId()) {
        if (QStyle* style = QApplication::style()) {
            qApp->setPalette(style->standardPalette());
        }
        // Themes Off has no app QSS → setFont works reliably (no re-polish).
        qApp->setStyleSheet(QString());
    } else {
        qApp->setPalette(paletteForAppearance(normalized));
        qApp->setStyleSheet(styleSheet + chromeFontCss);
    }
    host_.updateTrayIcon();
    if (QWidget* window = host_.dialogParent()) {
        window->setWindowIcon(AppIcon::makeIcon(
            fontSettings.value(QStringLiteral("tray_icon"), QStringLiteral("bubble")).toString(),
            appThemeById(normalized).accent));
    }
    // Keep the app-font assert for everything else (dialogs, labels) — the
    // stylesheet rule above covers the chrome that re-polish was reverting.
    QFont appFont(family, pointSize);
    appFont.setBold(bold);
    qApp->setFont(appFont);
    host_.setMenuBarFont(appFont);
}

void AppearanceController::setTheme(const QString& theme, bool save) {
    const QString normalized = normalizeThemeId(theme);
    // Saved/imported themes can bundle font preferences — restore them with the
    // colours (the Preferences combo does the same via applyFontSelections).
    const QVariantMap themeFonts = appThemeById(normalized).fonts;
    if (save) {
        QVariantMap settings = host_.settings().loadWithDefaults();
        settings.insert(QStringLiteral("theme"), normalized);
        for (auto it = themeFonts.constBegin(); it != themeFonts.constEnd(); ++it) {
            settings.insert(it.key(), it.value());
        }
        if (!host_.settings().saveRaw(settings)) {
            host_.appendActiveSystemLine(tr("! Could not save theme."));
        }
        if (!themeFonts.isEmpty()) {
            host_.applyAllSettings(); // picks up the bundled fonts
        }
    }
    setThemeId(normalized);
    applyTheme(currentTheme_);
    syncThemeActions(currentTheme_);
    host_.renderActiveBuffer();
    host_.updateChatSeparatorGuide();
}

void AppearanceController::setChatTheme(const QString& chatTheme, bool save) {
    const QString normalized = normalizeChatThemeId(chatTheme);
    if (save) {
        QVariantMap settings = host_.settings().loadWithDefaults();
        settings.insert(QStringLiteral("chat_theme"), normalized);
        if (!host_.settings().saveRaw(settings)) {
            host_.appendActiveSystemLine(tr("! Could not save chat theme."));
        }
    }
    setChatThemeId(normalized);
    applyTheme(currentTheme_);
    syncChatThemeActions(currentChatTheme_);
    host_.renderActiveBuffer();
    host_.recolorMemberList();
    host_.updateChatSeparatorGuide();
}

void AppearanceController::setWallpaper(const QString& wallpaper, bool save) {
    const QString normalized = normalizeWallpaperValue(wallpaper);
    if (save) {
        QVariantMap settings = host_.settings().loadWithDefaults();
        settings.insert(QStringLiteral("wallpaper"), normalized);
        if (!host_.settings().saveRaw(settings)) {
            host_.appendActiveSystemLine(tr("! Could not save wallpaper."));
        }
    }
    setWallpaperValue(normalized);
    applyTheme(currentTheme_);
    syncWallpaperActions(currentWallpaper_);
}

void AppearanceController::syncThemeActions(const QString& theme) {
    const QString normalized = normalizeThemeId(theme);
    for (QAction* action : themeActions_) {
        if (action == nullptr) {
            continue;
        }
        const QSignalBlocker blocker(action);
        action->setChecked(action->data().toString() == normalized);
    }
}

void AppearanceController::syncChatThemeActions(const QString& chatTheme) {
    const QString normalized = normalizeChatThemeId(chatTheme);
    for (QAction* action : chatThemeActions_) {
        if (action == nullptr) {
            continue;
        }
        const QSignalBlocker blocker(action);
        action->setChecked(action->data().toString() == normalized);
    }
}

void AppearanceController::syncWallpaperActions(const QString& wallpaper) {
    const QString normalized = normalizeWallpaperValue(wallpaper);
    for (QAction* action : wallpaperActions_) {
        if (action == nullptr) {
            continue;
        }
        const QSignalBlocker blocker(action);
        action->setChecked(action->data().toString() == normalized);
    }
}

} // namespace maxchat::ui
