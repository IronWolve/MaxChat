#pragma once

#include <algorithm>

#include <QLatin1Char>
#include <QListWidget>
#include <QListWidgetItem>
#include <QString>
#include <QStringList>
#include <Qt>

// Small pure helpers shared by the IRC handlers (IrcRouter) and the rest of
// MainWindow: nick-prefix stripping, member-list add/remove, target classification
// and ignore-mask normalisation. `inline` + namespaced so both translation units
// share one definition (was duplicated during the IrcRouter extraction).
namespace maxchat::ui {

inline QString nickWithoutPrefix(QString nick) {
    static const QString prefixes = QStringLiteral("~&@%+");
    while (!nick.isEmpty() && prefixes.contains(nick.front())) {
        nick.remove(0, 1);
    }
    return nick;
}

inline bool memberMatchesNick(const QString& memberText, const QString& nick) {
    return nickWithoutPrefix(memberText).compare(nick, Qt::CaseInsensitive) == 0;
}

inline bool removeMember(QListWidget* memberList, const QString& nick) {
    if (memberList == nullptr || nick.trimmed().isEmpty()) {
        return false;
    }
    bool removed = false;
    for (int row = memberList->count() - 1; row >= 0; --row) {
        QListWidgetItem* item = memberList->item(row);
        if (item != nullptr && memberMatchesNick(item->text(), nick)) {
            delete memberList->takeItem(row);
            removed = true;
        }
    }
    return removed;
}

inline void addMember(QListWidget* memberList, const QString& nick) {
    const QString trimmed = nick.trimmed();
    if (memberList == nullptr || trimmed.isEmpty()) {
        return;
    }
    for (int row = 0; row < memberList->count(); ++row) {
        const QListWidgetItem* item = memberList->item(row);
        if (item != nullptr && memberMatchesNick(item->text(), trimmed)) {
            return;
        }
    }
    memberList->addItem(trimmed);
    memberList->sortItems(Qt::AscendingOrder);
}

inline bool isChannelTarget(const QString& target) {
    return target.startsWith(QLatin1Char('#')) || target.startsWith(QLatin1Char('&'));
}

inline bool isLikelyServerNotice(const QString& sender, const QString& target,
                                 const QString& ownNick) {
    const QString cleanTarget = target.trimmed();
    if (isChannelTarget(cleanTarget)) {
        return false;
    }
    if (cleanTarget.isEmpty() || cleanTarget == QStringLiteral("*")) {
        return true;
    }
    if (sender.trimmed().isEmpty()) {
        return true;
    }
    if (sender.contains(QLatin1Char('.'))) {
        return true;
    }
    Q_UNUSED(ownNick);
    return false;
}

inline QString normalizeIgnoreMask(QString mask) {
    mask = mask.trimmed();
    if (mask.isEmpty()) {
        return {};
    }
    return mask.contains(QLatin1Char('!')) || mask.contains(QLatin1Char('@'))
               ? mask
               : QStringLiteral("%1!*@*").arg(mask);
}

inline bool containsCaseInsensitive(const QStringList& values, const QString& needle) {
    return std::any_of(values.cbegin(), values.cend(), [&needle](const QString& value) {
        return value.compare(needle, Qt::CaseInsensitive) == 0;
    });
}

} // namespace maxchat::ui
