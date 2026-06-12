#include "ui/ThemeCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>

#include <cmath>

namespace maxchat::ui {

namespace {

QColor rgb(const int r, const int g, const int b) {
    return QColor(r, g, b);
}

QColor colorFromJson(const QJsonValue& value, const QColor& fallback = {}) {
    const QJsonArray array = value.toArray();
    if (array.size() != 3) {
        return fallback;
    }
    const int red = array.at(0).toInt(-1);
    const int green = array.at(1).toInt(-1);
    const int blue = array.at(2).toInt(-1);
    if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255) {
        return fallback;
    }
    return QColor(red, green, blue);
}

double luminance(const QColor& color) {
    return 0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue();
}

QString cssRgb(const QColor& color) {
    return QStringLiteral("rgb(%1,%2,%3)").arg(color.red()).arg(color.green()).arg(color.blue());
}

QString cssRgba(const QColor& color, const double alpha) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha, 0, 'f', 2);
}

QString contrastColor(const QColor& color) {
    return luminance(color) > 150.0 ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
}

QString borderColor(const QColor& color) {
    return QStringLiteral("rgba(%1,%2,%3,0.45)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue());
}

QString displayNameFromFileName(const QString& fileName) {
    QString label = QFileInfo(fileName).completeBaseName();
    label.replace(QLatin1Char('-'), QLatin1Char(' '));
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    QStringList words = label.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString& word : words) {
        word = word.left(1).toUpper() + word.mid(1);
    }
    return words.join(QLatin1Char(' '));
}

QString assetDirectory(const QString& name) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("assets/%1").arg(name)),
        QDir(appDir).filePath(QStringLiteral("../assets/%1").arg(name)),
        QDir::current().filePath(QStringLiteral("assets/%1").arg(name)),
    };
    for (const QString& candidate : candidates) {
        if (QDir(candidate).exists()) {
            return QDir(candidate).absolutePath();
        }
    }
    return {};
}

QString themeDirectory() {
    return assetDirectory(QStringLiteral("themes"));
}

QString userConfigDirectory() {
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (root.isEmpty()) {
        return {};
    }
    return QDir(root).filePath(QStringLiteral("maxchat"));
}

QString userThemeDirectory() {
    const QString config = userConfigDirectory();
    return config.isEmpty() ? QString() : QDir(config).filePath(QStringLiteral("themes"));
}

// Drop a copyable starter file in the user theme folder, like the Python app.
// Never fatal: a read-only config dir must not break startup.
void ensureUserThemeTemplate(const QString& dir) {
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        return;
    }
    QFile file(QDir(dir).filePath(QStringLiteral("_example.json")));
    if (file.exists() || !file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    file.write("{\n"
               "  \"name\": \"My Theme (copy me, drop the underscore)\",\n"
               "  \"dark\": true,\n"
               "  \"on_text\": \"white\",\n"
               "  \"bg\": [40, 42, 54],\n"
               "  \"panel\": [52, 55, 70],\n"
               "  \"panel2\": [68, 71, 90],\n"
               "  \"text\": [248, 248, 242],\n"
               "  \"on\": [98, 114, 164],\n"
               "  \"accent\": [80, 250, 123],\n"
               "  \"groove\": [33, 34, 44],\n"
               "  \"scroll\": [68, 71, 90],\n"
               "  \"scroll_hi\": [98, 114, 164]\n"
               "}\n");
}

// User app themes: <config>/maxchat/themes/*.json in the Python app's format
// (one theme per file, id = file stem, "name" = label, "_"-prefixed files are
// templates). A bad file is skipped, never fatal.
QList<QJsonObject> userThemeObjects() {
    QList<QJsonObject> objects;
    const QString dir = userThemeDirectory();
    if (dir.isEmpty()) {
        return objects;
    }
    ensureUserThemeTemplate(dir);

    const QStringList files =
        QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString& fileName : files) {
        if (fileName.startsWith(QLatin1Char('_'))) {
            continue;
        }
        QFile file(QDir(dir).filePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject()) {
            continue;
        }
        QJsonObject object = document.object();
        if (!object.contains(QStringLiteral("id"))) {
            object.insert(QStringLiteral("id"),
                          QFileInfo(fileName).completeBaseName().trimmed().toLower());
        }
        if (!object.contains(QStringLiteral("label"))) {
            object.insert(QStringLiteral("label"),
                          object.value(QStringLiteral("name"))
                              .toString(displayNameFromFileName(fileName)));
        }
        objects.append(object);
    }
    return objects;
}

// User chat themes: <config>/maxchat/chat_themes.json — a map of id -> theme
// (the Python app's save format).
QList<QJsonObject> userChatThemeObjects() {
    QList<QJsonObject> objects;
    const QString config = userConfigDirectory();
    if (config.isEmpty()) {
        return objects;
    }
    QFile file(QDir(config).filePath(QStringLiteral("chat_themes.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return objects;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return objects;
    }
    const QJsonObject root = document.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        QJsonObject object = it.value().toObject();
        object.insert(QStringLiteral("id"), it.key().trimmed().toLower());
        if (!object.contains(QStringLiteral("label"))) {
            object.insert(QStringLiteral("label"), it.key());
        }
        objects.append(object);
    }
    return objects;
}

QString wallpaperDirectory() {
    return assetDirectory(QStringLiteral("wallpapers"));
}

QList<QPair<double, QColor>> gradientFromJson(const QJsonValue& value) {
    QList<QPair<double, QColor>> gradient;
    const QJsonArray array = value.toArray();
    gradient.reserve(array.size());
    for (const QJsonValue& entryValue : array) {
        const QJsonObject entry = entryValue.toObject();
        const QColor color = colorFromJson(entry.value(QStringLiteral("color")));
        if (!color.isValid()) {
            continue;
        }
        gradient.append({entry.value(QStringLiteral("stop")).toDouble(), color});
    }
    return gradient;
}

AppThemeDefinition parseAppTheme(const QJsonObject& object) {
    AppThemeDefinition theme;
    theme.id = object.value(QStringLiteral("id")).toString().trimmed().toLower();
    theme.label = object.value(QStringLiteral("label")).toString(theme.id);
    if (theme.id.isEmpty()) {
        return {};
    }
    if (theme.id == systemThemeId() || object.value(QStringLiteral("system")).toBool(false)) {
        theme.id = systemThemeId();
        theme.label = theme.label.isEmpty() ? QStringLiteral("Themes Off") : theme.label;
        theme.dark = false;
        return theme;
    }

    theme.dark = object.value(QStringLiteral("dark")).toBool(true);
    theme.bg = colorFromJson(object.value(QStringLiteral("bg")));
    theme.panel = colorFromJson(object.value(QStringLiteral("panel")));
    theme.panel2 = colorFromJson(object.value(QStringLiteral("panel2")));
    theme.text = colorFromJson(object.value(QStringLiteral("text")));
    theme.on = colorFromJson(object.value(QStringLiteral("on")));
    theme.onText = object.value(QStringLiteral("on_text"))
                       .toString(luminance(theme.on) < 140.0 ? QStringLiteral("white")
                                                             : QStringLiteral("black"));
    theme.accent = colorFromJson(object.value(QStringLiteral("accent")));
    theme.groove = colorFromJson(object.value(QStringLiteral("groove")));
    theme.scroll = colorFromJson(object.value(QStringLiteral("scroll")));
    theme.scrollHi = colorFromJson(object.value(QStringLiteral("scroll_hi")));
    theme.chatBg = colorFromJson(object.value(QStringLiteral("chat_bg")));
    theme.chatFg = colorFromJson(object.value(QStringLiteral("chat_fg")));
    theme.wallpaper = object.value(QStringLiteral("wallpaper")).toString();
    theme.bgGradient = gradientFromJson(object.value(QStringLiteral("bg_gradient")));
    // Whitelist the bundled font keys: theme files are shareable, and parsing
    // arbitrary keys here would let an imported theme inject other settings.
    const QJsonObject fonts = object.value(QStringLiteral("fonts")).toObject();
    if (!fonts.isEmpty()) {
        for (const QString& key : themeFontKeys()) {
            if (fonts.contains(key)) {
                theme.fonts.insert(key, fonts.value(key).toVariant());
            }
        }
    }

    if (!theme.bg.isValid() || !theme.panel.isValid() || !theme.panel2.isValid() ||
        !theme.text.isValid() || !theme.on.isValid() || !theme.accent.isValid() ||
        !theme.groove.isValid() || !theme.scroll.isValid() || !theme.scrollHi.isValid()) {
        return {};
    }
    return theme;
}

ChatThemeDefinition parseChatTheme(const QJsonObject& object) {
    ChatThemeDefinition theme;
    theme.id = object.value(QStringLiteral("id")).toString().trimmed().toLower();
    theme.label = object.value(QStringLiteral("label")).toString(theme.id);
    if (theme.id.isEmpty()) {
        return {};
    }
    if (theme.id == QStringLiteral("follow")) {
        return theme;
    }
    theme.bg = colorFromJson(object.value(QStringLiteral("bg")));
    theme.fg = colorFromJson(object.value(QStringLiteral("fg")));
    theme.fixedFont = object.value(QStringLiteral("fixed")).toBool(true);
    theme.timestamp = colorFromJson(object.value(QStringLiteral("ts")));
    theme.bracket = colorFromJson(object.value(QStringLiteral("bracket")));
    theme.system = colorFromJson(object.value(QStringLiteral("system")));
    const QJsonValue nicks = object.value(QStringLiteral("nicks"));
    if (nicks.isString()) {
        theme.monoNicks = nicks.toString() == QStringLiteral("mono");
    } else if (nicks.isArray()) {
        const QJsonArray palette = nicks.toArray();
        for (const QJsonValue& entry : palette) {
            const QColor color = colorFromJson(entry);
            if (color.isValid()) {
                theme.nickPalette.append(color);
            }
        }
    }
    if (!theme.bg.isValid() || !theme.fg.isValid()) {
        return {};
    }
    return theme;
}

QList<QJsonObject> jsonThemeObjects(const QString& kind) {
    QList<QJsonObject> objects;
    const QString dir = themeDirectory();
    if (dir.isEmpty()) {
        return objects;
    }

    const QStringList files =
        QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString& fileName : files) {
        QFile file(QDir(dir).filePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject()) {
            continue;
        }

        const QJsonObject root = document.object();
        const QString fileKind = root.value(QStringLiteral("kind")).toString().trimmed().toLower();
        if (!fileKind.isEmpty() && !fileKind.contains(kind)) {
            continue;
        }

        if (root.contains(QStringLiteral("themes"))) {
            const QJsonArray themes = root.value(QStringLiteral("themes")).toArray();
            for (const QJsonValue& value : themes) {
                if (value.isObject()) {
                    objects.append(value.toObject());
                }
            }
        } else if (root.contains(QStringLiteral("id"))) {
            objects.append(root);
        }
    }
    return objects;
}

QList<AppThemeDefinition> fallbackAppThemes() {
    return {{QStringLiteral("dark"),
             QStringLiteral("Default"),
             true,
             rgb(28, 30, 33),
             rgb(37, 40, 44),
             rgb(44, 48, 53),
             rgb(224, 226, 229),
             rgb(40, 95, 60),
             QStringLiteral("white"),
             rgb(80, 230, 150),
             rgb(18, 20, 23),
             rgb(70, 75, 82),
             rgb(98, 104, 112),
             {},
             {},
             {},
             {}},
            {systemThemeId(), QStringLiteral("Themes Off"), false}};
}

QList<ChatThemeDefinition> fallbackChatThemes() {
    return {{QStringLiteral("follow"), QStringLiteral("Follow app theme")},
            {QStringLiteral("terminal"), QStringLiteral("Terminal - grey on black"),
             rgb(10, 10, 10), rgb(208, 208, 208), true}};
}

QList<AppThemeDefinition> buildAppThemes() {
    QList<AppThemeDefinition> loaded;
    QSet<QString> ids;
    for (const QJsonObject& object : jsonThemeObjects(QStringLiteral("app"))) {
        const AppThemeDefinition theme = parseAppTheme(object);
        if (theme.id.isEmpty() || ids.contains(theme.id)) {
            continue;
        }
        ids.insert(theme.id);
        loaded.append(theme);
    }

    // User themes load after the bundled set; built-in ids win.
    for (const QJsonObject& object : userThemeObjects()) {
        const AppThemeDefinition theme = parseAppTheme(object);
        if (theme.id.isEmpty() || ids.contains(theme.id)) {
            continue;
        }
        ids.insert(theme.id);
        loaded.append(theme);
    }

    if (loaded.isEmpty()) {
        return fallbackAppThemes();
    }
    if (!ids.contains(defaultThemeId())) {
        loaded.prepend(fallbackAppThemes().first());
    }
    if (!ids.contains(systemThemeId())) {
        loaded.insert(loaded.isEmpty() ? 0 : 1,
                      {systemThemeId(), QStringLiteral("Themes Off"), false});
    }
    return loaded;
}

QList<ChatThemeDefinition> buildChatThemes() {
    QList<ChatThemeDefinition> loaded;
    QSet<QString> ids;
    for (const QJsonObject& object : jsonThemeObjects(QStringLiteral("chat"))) {
        const ChatThemeDefinition theme = parseChatTheme(object);
        if (theme.id.isEmpty() || ids.contains(theme.id)) {
            continue;
        }
        ids.insert(theme.id);
        loaded.append(theme);
    }

    // User chat themes from <config>/maxchat/chat_themes.json; built-in ids win.
    for (const QJsonObject& object : userChatThemeObjects()) {
        const ChatThemeDefinition theme = parseChatTheme(object);
        if (theme.id.isEmpty() || ids.contains(theme.id)) {
            continue;
        }
        ids.insert(theme.id);
        loaded.append(theme);
    }

    if (loaded.isEmpty()) {
        return fallbackChatThemes();
    }
    if (!ids.contains(QStringLiteral("follow"))) {
        loaded.prepend(fallbackChatThemes().first());
    }
    return loaded;
}

// Mutable singletons so saving a user theme can refresh them without a restart.
QList<AppThemeDefinition>& mutableAppThemes() {
    static QList<AppThemeDefinition> themes = buildAppThemes();
    return themes;
}

QList<ChatThemeDefinition>& mutableChatThemes() {
    static QList<ChatThemeDefinition> themes = buildChatThemes();
    return themes;
}

const QList<AppThemeDefinition>& themeRegistry() {
    return mutableAppThemes();
}

const QList<ChatThemeDefinition>& chatThemeRegistry() {
    return mutableChatThemes();
}

QString gradientCss(const AppThemeDefinition& theme) {
    if (theme.bgGradient.isEmpty()) {
        return cssRgb(theme.bg);
    }
    QStringList stops;
    stops.reserve(theme.bgGradient.size());
    for (const auto& stop : theme.bgGradient) {
        stops.append(
            QStringLiteral("stop:%1 %2").arg(stop.first, 0, 'f', 2).arg(cssRgb(stop.second)));
    }
    return QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, %1)")
        .arg(stops.join(QStringLiteral(", ")));
}

QString bundledWallpaperPath(const QString& filename) {
    if (filename.isEmpty()) {
        return {};
    }
    const QString dir = wallpaperDirectory();
    if (dir.isEmpty()) {
        return {};
    }
    const QString path = QDir(dir).filePath(filename);
    return QFileInfo(path).isFile() ? path : QString();
}

} // namespace

QString defaultThemeId() {
    return QStringLiteral("dark");
}

QString systemThemeId() {
    return QStringLiteral("system");
}

QString normalizeThemeId(const QString& theme) {
    const QString id = theme.trimmed().toLower();
    if (id == QStringLiteral("default")) {
        return defaultThemeId();
    }
    if (id == QStringLiteral("none") || id == QStringLiteral("no-theme") ||
        id == QStringLiteral("off")) {
        return systemThemeId();
    }
    for (const AppThemeDefinition& definition : themeRegistry()) {
        if (definition.id == id) {
            return id;
        }
    }
    return defaultThemeId();
}

QString normalizeChatThemeId(const QString& chatTheme) {
    const QString id = chatTheme.trimmed().toLower();
    for (const ChatThemeDefinition& definition : chatThemeRegistry()) {
        if (definition.id == id) {
            return id;
        }
    }
    return QStringLiteral("follow");
}

QString normalizeWallpaperValue(const QString& wallpaper) {
    const QString value = wallpaper.trimmed();
    if (value.isEmpty() || value.compare(QStringLiteral("default"), Qt::CaseInsensitive) == 0) {
        return {};
    }
    if (value.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0 ||
        value.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("none");
    }
    const QFileInfo info(value);
    if (info.isAbsolute() && info.isFile()) {
        return info.absoluteFilePath();
    }
    if (!bundledWallpaperPath(value).isEmpty()) {
        return value;
    }
    return {};
}

QList<AppThemeDefinition> appThemes() {
    return themeRegistry();
}

QList<ChatThemeDefinition> chatThemes() {
    return chatThemeRegistry();
}

QList<WallpaperDefinition> wallpaperChoices() {
    QList<WallpaperDefinition> choices = {
        {QString(), QStringLiteral("Theme Default")},
        {QStringLiteral("none"), QStringLiteral("Off")},
    };

    const QString dir = wallpaperDirectory();
    if (dir.isEmpty()) {
        return choices;
    }

    const QStringList files =
        QDir(dir).entryList({QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                             QStringLiteral("*.jpeg"), QStringLiteral("*.webp")},
                            QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString& file : files) {
        choices.append({file, displayNameFromFileName(file)});
    }
    return choices;
}

AppThemeDefinition appThemeById(const QString& theme) {
    const QString id = normalizeThemeId(theme);
    for (const AppThemeDefinition& definition : themeRegistry()) {
        if (definition.id == id) {
            return definition;
        }
    }
    return themeRegistry().first();
}

ChatThemeDefinition chatThemeById(const QString& chatTheme) {
    const QString id = normalizeChatThemeId(chatTheme);
    for (const ChatThemeDefinition& definition : chatThemeRegistry()) {
        if (definition.id == id) {
            return definition;
        }
    }
    return chatThemeRegistry().first();
}

QString effectiveWallpaperPath(const QString& theme, const QString& wallpaper) {
    const QString themeId = normalizeThemeId(theme);
    if (themeId == systemThemeId()) {
        return {};
    }

    const AppThemeDefinition definition = appThemeById(themeId);
    QString selected = normalizeWallpaperValue(wallpaper);
    if (selected == QStringLiteral("none")) {
        return {};
    }
    if (selected.isEmpty()) {
        selected = definition.wallpaper;
    }
    if (selected.isEmpty()) {
        return {};
    }
    // Plain forward-slash paths only: Qt style sheets do not resolve file://
    // URLs inside url(), so a QUrl here renders as no wallpaper at all.
    if (const QString bundled = bundledWallpaperPath(selected); !bundled.isEmpty()) {
        return QDir::fromNativeSeparators(bundled);
    }

    const QFileInfo info(selected);
    if (info.isAbsolute() && info.isFile()) {
        return QDir::fromNativeSeparators(info.absoluteFilePath());
    }
    return {};
}

QPalette paletteForAppearance(const QString& theme) {
    QPalette pal;
    const QString themeId = normalizeThemeId(theme);
    if (themeId == systemThemeId()) {
        return pal; // caller restores the platform palette for "Themes Off"
    }

    const AppThemeDefinition p = appThemeById(themeId);
    const QColor onText = p.onText.isEmpty()
                             ? (luminance(p.on) < 140.0 ? QColor(Qt::white) : QColor(Qt::black))
                             : QColor(p.onText);
    pal.setColor(QPalette::Window, p.bg);
    pal.setColor(QPalette::WindowText, p.text);
    pal.setColor(QPalette::Base, p.panel);
    pal.setColor(QPalette::AlternateBase, p.panel2);
    pal.setColor(QPalette::Text, p.text);
    pal.setColor(QPalette::Button, p.panel2);
    pal.setColor(QPalette::ButtonText, p.text);
    pal.setColor(QPalette::ToolTipBase, p.panel);
    pal.setColor(QPalette::ToolTipText, p.text);
    pal.setColor(QPalette::Highlight, p.on);
    pal.setColor(QPalette::HighlightedText, onText);
    pal.setColor(QPalette::PlaceholderText, QColor(p.text.red(), p.text.green(), p.text.blue(), 130));

    QColor dim = p.text;
    dim.setAlpha(110);
    for (const QPalette::ColorRole role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        pal.setColor(QPalette::Disabled, role, dim);
    }
    return pal;
}

QString styleSheetForAppearance(const QString& theme, const QString& chatTheme,
                                const QString& wallpaper) {
    const QString themeId = normalizeThemeId(theme);
    if (themeId == systemThemeId()) {
        return {};
    }

    const AppThemeDefinition p = appThemeById(themeId);
    QString wallpaperPath = effectiveWallpaperPath(themeId, wallpaper);
    // The path is interpolated into a quote-delimited QSS url("..."); a quote or
    // newline could break out and inject styles. Real wallpaper paths never
    // contain these, so just drop the wallpaper if one does.
    if (wallpaperPath.contains(QLatin1Char('"')) ||
        wallpaperPath.contains(QLatin1Char('\n')) ||
        wallpaperPath.contains(QLatin1Char('\r'))) {
        wallpaperPath.clear();
    }
    const bool hasWallpaper = !wallpaperPath.isEmpty();

    const QString background = gradientCss(p);
    const QString panel = hasWallpaper ? cssRgba(p.panel, 0.58) : cssRgb(p.panel);
    const QString panel2 = hasWallpaper ? cssRgba(p.panel2, 0.58) : cssRgb(p.panel2);
    const QString bg = cssRgb(p.bg);
    const QString text = cssRgb(p.text);
    const QString on = cssRgb(p.on);
    const QString onText = p.onText.isEmpty() ? contrastColor(p.on) : p.onText;
    const QString scroll = cssRgb(p.scroll);
    const QString scrollHi = cssRgb(p.scrollHi);
    const QString border = borderColor(p.text);

    const ChatThemeDefinition chat = chatThemeById(chatTheme);
    QColor chatBgColor = p.chatBg.isValid() ? p.chatBg : p.panel;
    QColor chatFgColor = p.chatFg.isValid() ? p.chatFg : p.text;
    if (chat.id != QStringLiteral("follow")) {
        chatBgColor = chat.bg;
        chatFgColor = chat.fg;
    }
    const QString chatBg = hasWallpaper && chat.id == QStringLiteral("follow")
                               ? cssRgba(chatBgColor, 0.80)
                               : cssRgb(chatBgColor);
    const QString chatFg = cssRgb(chatFgColor);
    const QString windowDecl =
        hasWallpaper
            ? QStringLiteral(R"(border-image: url("%1") 0 0 0 0 stretch stretch;)")
                  .arg(wallpaperPath)
            : QStringLiteral("background:%1;").arg(background);

    return QStringLiteral(R"(
      QMainWindow { %1 }
      QWidget { color:%2; }
      QWidget#centralRoot, QWidget#chatColumn, QWidget#memberPanel,
      QSplitter {
          background: transparent;
      }
      QMenuBar {
          background:%3;
          color:%2;
          spacing: 8px;
          border-bottom: 1px solid %4;
      }
      QMenuBar::item {
          background: transparent;
          padding: 6px 10px;
      }
      QMenuBar::item:selected, QMenu::item:selected, QListWidget::item:selected,
      QTreeWidget::item:selected, QTableWidget::item:selected {
          background:%5;
          color:%6;
          border: none;
          outline: 0;
      }
      QMenu {
          background:%7;
          color:%2;
          border: 1px solid %4;
          padding: 6px;
      }
      QMenu::item {
          padding: 6px 30px 6px 16px;
          border-radius: 4px;
      }
      QMenu::separator {
          height: 1px;
          background:%4;
          margin: 6px 10px;
      }
      QStatusBar, QToolBar#mainToolbar {
          background:%3;
          color:%2;
          border-top: 1px solid %4;
      }
      QToolBar#mainToolbar {
          border-top: 0;
          border-bottom: 1px solid %4;
          spacing: 3px;
          padding: 3px 6px;
      }
      QToolButton, QPushButton {
          background:%3;
          color:%2;
          border: 1px solid rgba(128,128,128,0.45);
          border-radius: 4px;
          padding: 5px 9px;
      }
      QToolButton:hover, QPushButton:hover {
          background:%5;
          color:%6;
      }
      QToolButton:checked, QPushButton:checked {
          background:%5;
          color:%6;
      }
      QToolBar#mainToolbar QToolButton:checked {
          background:%7;
          color:%2;
      }
      QTabBar#bufferTabBar {
          background:transparent;
      }
      QTabBar#bufferTabBar::tab {
          background:%3;
          color:%2;
          border: 1px solid %4;
          border-radius: 4px;
          padding: 5px 12px;
          margin-right: 3px;
      }
      QTabBar#bufferTabBar::tab:selected {
          background:%5;
          color:%6;
      }
      QTreeWidget, QListWidget, QTextBrowser, QTextEdit, QLineEdit,
      QPlainTextEdit, QTableWidget, QComboBox, QSpinBox {
          background:%7;
          color:%2;
          border: 1px solid %3;
          selection-background-color:%5;
          selection-color:%6;
          outline: 0;
      }
      QTextBrowser#chatView, QTextEdit#messageInput {
          background:%8;
          color:%9;
      }
      QTextEdit#messageInput {
          border: 1px solid %3;
          padding: 4px;
      }
      QTreeWidget#networkTree, QListWidget#memberList {
          background:%7;
      }
      QListWidget#preferencesButtons {
          background:%10;
          border: none;
          padding: 4px;
      }
      QListWidget#preferencesButtons::item {
          padding: 8px 10px;
      }
      QLabel#topicLabel {
          background:%3;
          border: 1px solid %4;
          padding: 4px 8px;
      }
      QGroupBox {
          background:%7;
          border: 1px solid %4;
          border-radius: 6px;
          margin-top: 10px;
          padding: 10px 8px 8px 8px;
      }
      QGroupBox::title {
          subcontrol-origin: margin;
          left: 8px;
          padding: 0 4px;
      }
      QHeaderView::section {
          background:%3;
          color:%2;
          border: 1px solid %4;
          padding: 4px 6px;
      }
      QSplitter::handle {
          background:%3;
      }
      QSplitter::handle:hover {
          background:%5;
      }
      QScrollBar:vertical, QScrollBar:horizontal {
          background:%10;
          border: 0;
      }
      QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
          background:%11;
          min-height: 20px;
          min-width: 20px;
          border-radius: 3px;
      }
      QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
          background:%12;
      }
      QScrollBar::add-line, QScrollBar::sub-line {
          width: 0;
          height: 0;
      }
      QDialogButtonBox QPushButton {
          min-width: 76px;
      }
  )")
        .arg(windowDecl, text, panel2, border, on, onText, panel, chatBg, chatFg, bg, scroll,
             scrollHi);
}

namespace {

QJsonArray colorArray(const QColor& color) {
    return QJsonArray{color.red(), color.green(), color.blue()};
}

QString slugify(const QString& name) {
    QString slug;
    for (const QChar ch : name.toLower()) {
        slug.append(ch.isLetterOrNumber() ? ch : QLatin1Char('-'));
    }
    const QStringList parts = slug.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    const QString joined = parts.join(QLatin1Char('-'));
    return joined.isEmpty() ? QStringLiteral("custom") : joined;
}

bool writeJsonFile(const QString& path, const QJsonObject& object) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return true;
}

} // namespace

void reloadThemes() {
    mutableAppThemes() = buildAppThemes();
    mutableChatThemes() = buildChatThemes();
}

namespace {
QJsonObject appThemeJson(const QString& name, const AppThemeDefinition& theme) {
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("dark"), theme.dark);
    object.insert(QStringLiteral("on_text"),
                  theme.onText.isEmpty() ? QStringLiteral("white") : theme.onText);
    object.insert(QStringLiteral("bg"), colorArray(theme.bg));
    object.insert(QStringLiteral("panel"), colorArray(theme.panel));
    object.insert(QStringLiteral("panel2"), colorArray(theme.panel2));
    object.insert(QStringLiteral("text"), colorArray(theme.text));
    object.insert(QStringLiteral("on"), colorArray(theme.on));
    object.insert(QStringLiteral("accent"), colorArray(theme.accent));
    object.insert(QStringLiteral("groove"), colorArray(theme.groove));
    object.insert(QStringLiteral("scroll"), colorArray(theme.scroll));
    object.insert(QStringLiteral("scroll_hi"), colorArray(theme.scrollHi));
    if (theme.chatBg.isValid()) {
        object.insert(QStringLiteral("chat_bg"), colorArray(theme.chatBg));
    }
    if (theme.chatFg.isValid()) {
        object.insert(QStringLiteral("chat_fg"), colorArray(theme.chatFg));
    }
    if (!theme.wallpaper.isEmpty()) {
        object.insert(QStringLiteral("wallpaper"), theme.wallpaper);
    }
    if (!theme.fonts.isEmpty()) {
        QJsonObject fonts;
        for (const QString& key : themeFontKeys()) {
            if (theme.fonts.contains(key)) {
                fonts.insert(key, QJsonValue::fromVariant(theme.fonts.value(key)));
            }
        }
        object.insert(QStringLiteral("fonts"), fonts);
    }
    return object;
}

QJsonObject chatThemeJson(const QString& name, const ChatThemeDefinition& theme) {
    QJsonObject entry;
    entry.insert(QStringLiteral("label"), name);
    entry.insert(QStringLiteral("bg"), colorArray(theme.bg));
    entry.insert(QStringLiteral("fg"), colorArray(theme.fg));
    entry.insert(QStringLiteral("fixed"), theme.fixedFont);
    if (theme.timestamp.isValid()) {
        entry.insert(QStringLiteral("ts"), colorArray(theme.timestamp));
    }
    if (theme.bracket.isValid()) {
        entry.insert(QStringLiteral("bracket"), colorArray(theme.bracket));
    }
    if (theme.system.isValid()) {
        entry.insert(QStringLiteral("system"), colorArray(theme.system));
    }
    if (theme.monoNicks) {
        entry.insert(QStringLiteral("nicks"), QStringLiteral("mono"));
    } else if (!theme.nickPalette.isEmpty()) {
        QJsonArray palette;
        for (const QColor& color : theme.nickPalette) {
            palette.append(colorArray(color));
        }
        entry.insert(QStringLiteral("nicks"), palette);
    }
    return entry;
}
} // namespace

QStringList themeFontKeys() {
    QStringList keys;
    for (const char* area : {"app", "chat", "list", "nick", "status", "topic"}) {
        const QString prefix = QString::fromLatin1(area);
        keys << prefix + QStringLiteral("_font_family") << prefix + QStringLiteral("_font_size")
             << prefix + QStringLiteral("_font_bold");
    }
    return keys;
}

QString userThemeDirectoryPath() {
    return userThemeDirectory();
}

bool isUserThemeId(const QString& id) {
    return id.startsWith(QStringLiteral("u-"));
}

bool deleteUserAppTheme(const QString& id) {
    if (!isUserThemeId(id)) {
        return false; // built-ins are not deletable
    }
    const QString dir = userThemeDirectory();
    if (dir.isEmpty()) {
        return false;
    }
    // id == file stem by construction (saveUserAppTheme); a plain "u-*" stem
    // contains no separators, so no traversal is possible.
    if (!QFile::remove(QDir(dir).filePath(id + QStringLiteral(".json")))) {
        return false;
    }
    reloadThemes();
    return true;
}

bool deleteUserChatTheme(const QString& id) {
    if (!isUserThemeId(id)) {
        return false;
    }
    const QString config = userConfigDirectory();
    if (config.isEmpty()) {
        return false;
    }
    const QString path = QDir(config).filePath(QStringLiteral("chat_themes.json"));
    QJsonObject root;
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) {
            return false;
        }
        root = doc.object();
    }
    if (!root.contains(id)) {
        return false;
    }
    root.remove(id);
    if (!writeJsonFile(path, root)) {
        return false;
    }
    reloadThemes();
    return true;
}

QString saveUserAppTheme(const QString& name, const AppThemeDefinition& theme) {
    const QString dir = userThemeDirectory();
    if (dir.isEmpty()) {
        return {};
    }
    const QString id = QStringLiteral("u-%1").arg(slugify(name));
    if (!writeJsonFile(QDir(dir).filePath(id + QStringLiteral(".json")),
                       appThemeJson(name, theme))) {
        return {};
    }
    reloadThemes();
    return id;
}

bool exportThemePack(const QString& path, const ThemePack& pack) {
    QJsonObject root;
    root.insert(QStringLiteral("kind"), QStringLiteral("maxchat-theme-pack"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("name"), pack.name);
    if (!pack.app.id.isEmpty()) {
        AppThemeDefinition app = pack.app;
        app.fonts = pack.fonts; // fonts ride inside the app theme
        root.insert(QStringLiteral("app"), appThemeJson(pack.name, app));
    }
    if (!pack.chat.id.isEmpty()) {
        root.insert(QStringLiteral("chat"), chatThemeJson(pack.name, pack.chat));
    }
    if (!pack.wallpaper.isEmpty()) {
        root.insert(QStringLiteral("wallpaper"), pack.wallpaper);
    }
    return writeJsonFile(path, root);
}

ThemePack importThemePack(const QString& path) {
    ThemePack result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("could not open the file");
        return result;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        result.error = QStringLiteral("not a theme JSON file");
        return result;
    }
    const QJsonObject root = document.object();
    const QString stem = QFileInfo(path).completeBaseName();
    result.name = root.value(QStringLiteral("name"))
                      .toString(root.value(QStringLiteral("label")).toString(stem));
    const QString kind = root.value(QStringLiteral("kind")).toString().trimmed().toLower();

    const auto installApp = [&result](QJsonObject object, const QString& name) {
        object.insert(QStringLiteral("id"), QStringLiteral("import"));
        object.insert(QStringLiteral("label"), name);
        AppThemeDefinition parsed = parseAppTheme(object);
        if (parsed.id.isEmpty()) {
            return false;
        }
        const QString id = saveUserAppTheme(name, parsed);
        if (id.isEmpty()) {
            return false;
        }
        parsed.id = id;
        result.app = parsed;
        result.fonts = parsed.fonts;
        return true;
    };
    const auto installChat = [&result](QJsonObject object, const QString& name) {
        object.insert(QStringLiteral("id"), QStringLiteral("import"));
        object.insert(QStringLiteral("label"), name);
        ChatThemeDefinition parsed = parseChatTheme(object);
        if (parsed.id.isEmpty()) {
            return false;
        }
        const QString id = saveUserChatTheme(name, parsed);
        if (id.isEmpty()) {
            return false;
        }
        parsed.id = id;
        result.chat = parsed;
        return true;
    };

    if (kind == QLatin1String("maxchat-theme-pack")) {
        bool any = false;
        if (root.value(QStringLiteral("app")).isObject()) {
            any = installApp(root.value(QStringLiteral("app")).toObject(), result.name) || any;
        }
        if (root.value(QStringLiteral("chat")).isObject()) {
            any = installChat(root.value(QStringLiteral("chat")).toObject(), result.name) || any;
        }
        result.wallpaper = root.value(QStringLiteral("wallpaper")).toString();
        if (!any) {
            result.error = QStringLiteral("the pack contains no usable theme");
        }
        return result;
    }

    // Bare single-theme files: chat themes have fg but no panel; app themes
    // have panel/accent. Try app first, fall back to chat.
    if (root.contains(QStringLiteral("panel")) || kind == QLatin1String("app")) {
        if (!installApp(root, result.name)) {
            result.error = QStringLiteral("invalid app theme file");
        }
        return result;
    }
    if (installChat(root, result.name)) {
        return result;
    }
    if (installApp(root, result.name)) {
        return result;
    }
    result.error = QStringLiteral("invalid theme file");
    return result;
}

QString saveUserChatTheme(const QString& name, const ChatThemeDefinition& theme) {
    const QString config = userConfigDirectory();
    if (config.isEmpty()) {
        return {};
    }
    const QString id = QStringLiteral("u-%1").arg(slugify(name));
    const QString path = QDir(config).filePath(QStringLiteral("chat_themes.json"));

    // Merge into the existing id -> theme map.
    QJsonObject root;
    QFile readFile(path);
    if (readFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(readFile.readAll());
        if (doc.isObject()) {
            root = doc.object();
        }
        readFile.close();
    }

    root.insert(id, chatThemeJson(name, theme));

    if (!writeJsonFile(path, root)) {
        return {};
    }
    reloadThemes();
    return id;
}

} // namespace maxchat::ui
