#include "comic/ComicEmotion.h"

#include <initializer_list>

namespace maxchat::comic {

namespace {
bool hasAny(const QString& haystack, std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (haystack.contains(QString::fromUtf8(needle))) {
            return true;
        }
    }
    return false;
}
} // namespace

QString guessEmotion(const QString& body) {
    const QString low = body.toLower();

    // Order matters: the louder/more specific signals win over the generic
    // smile/frown (e.g. ">:(" must hit angry before ":(" hits sad).
    if (hasAny(low, {":d", ":-d", "xd", "lol", "lmao", "rofl", "rotfl", "haha", "hehe",
                     "\xF0\x9F\x98\x82", "\xF0\x9F\xA4\xA3"})) { // 😂 🤣
        return QStringLiteral("laughing");
    }
    if (hasAny(low, {">:(", "grr", "wtf", "ffs", "angry", "furious", "pissed", "hate this",
                     "\xF0\x9F\x98\xA1", "\xF0\x9F\xA4\xAC"})) { // 😡 🤬
        return QStringLiteral("angry");
    }
    if (hasAny(low, {"zzz", "yawn", "boring", "bored", "meh",
                     "\xF0\x9F\xA5\xB1", "\xF0\x9F\x98\xB4"})) { // 🥱 😴
        return QStringLiteral("bored");
    }
    if (hasAny(body, {":'(", ":(", ":-(", "):", "=("}) ||
        hasAny(low, {"\xF0\x9F\x98\xA2", "\xF0\x9F\x98\xAD", "\xE2\x98\xB9"})) { // 😢 😭 ☹
        return QStringLiteral("sad");
    }
    if (hasAny(low, {"?!", "!?", "yikes", "omg", "uh oh", "eek",
                     "\xF0\x9F\x98\xB1", "\xF0\x9F\x98\xA8"})) { // 😱 😨
        return QStringLiteral("scared");
    }
    if (hasAny(body, {";)", ";-)", ":p", ":-p", ":P", ":-P"}) ||
        hasAny(low, {"\xF0\x9F\x98\x89", "\xF0\x9F\x98\x9C"})) { // 😉 😜
        return QStringLiteral("coy");
    }
    if (hasAny(body, {":)", ":-)", "(:", "=)", ":3"}) ||
        hasAny(low, {"yay", "woot", "\xF0\x9F\x99\x82", "\xF0\x9F\x98\x8A",
                     "\xF0\x9F\x98\x80", "\xF0\x9F\x98\x84"})) { // 🙂 😊 😀 😄
        return QStringLiteral("happy");
    }

    if (body.contains(QStringLiteral("!!!"))) {
        return QStringLiteral("shouting");
    }
    int letters = 0;
    bool allUpper = true;
    for (const QChar c : body) {
        if (c.isLetter()) {
            ++letters;
            if (!c.isUpper()) {
                allUpper = false;
            }
        }
    }
    if (letters >= 3 && allUpper) {
        return QStringLiteral("shouting");
    }
    return QStringLiteral("neutral");
}

} // namespace maxchat::comic
