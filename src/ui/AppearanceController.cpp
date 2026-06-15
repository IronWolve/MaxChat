#include "ui/AppearanceController.h"

#include "ui/MainWindowHost.h"

#include <QFontDatabase>

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

} // namespace maxchat::ui
