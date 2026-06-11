#include "ui/MainWindow.h"

#include "app/AppInfo.h"
#include "core/CommandAlias.h"
#include "core/ConnectionPlan.h"
#include "core/DefaultNetworks.h"
#include "core/SettingsStore.h"
#include "core/UrlDetector.h"
#include "irc/CommandParser.h"
#include "irc/IrcRedaction.h"
#include "irc/ReconnectPlanner.h"
#include "services/LinkPreviewClassifier.h"
#include "services/LinkPreviewPolicy.h"
#include "services/LinkPreviewRenderer.h"
#include "spell/SpellcheckDictionaryCatalog.h"
#include "ui/AliasEditorDialog.h"
#include "ui/BanListDialog.h"
#include "ui/ChannelListDialog.h"
#include "ui/ChannelModesDialog.h"
#include "irc/IrcFormat.h"
#include "ui/AudioPlayerBar.h"
#include "ui/ChatFindDialog.h"
#include "ui/ColorPickerDialog.h"
#include "comic/ComicArt.h"
#include "comic/ComicCharacter.h"
#include "comic/ComicRenderer.h"
#include "ui/ComicSettingsDialog.h"
#include "ui/ComicView.h"
#include "ui/DccManager.h"
#include "ui/DccTransfersDialog.h"
#include "ui/ImageViewerDialog.h"
#include "ui/MediaPlayerDialog.h"
#include "ui/ShortcutEditorDialog.h"
#include "ui/FriendsNotifyDialog.h"
#include "ui/IgnoreListDialog.h"
#include "ui/PreferencesDialog.h"
#include "ui/QuickConnectDialog.h"
#include "ui/RawLogDialog.h"
#include "ui/ServerListDialog.h"
#include "ui/SpellcheckHighlighter.h"
#include "ui/SystemInfo.h"
#include "ui/ThemeCatalog.h"
#include "ui/UrlListDialog.h"
#include "ui/AppIcon.h"
#include "ui/Notifier.h"
#include <QSystemTrayIcon>
#include <QMenu>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QStyle>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDesktopServices>
#include <QColor>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMetaType>
#include <QMouseEvent>
#include <QPainter>
#include <QPair>
#include <QPalette>
#include <QPushButton>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QSysInfo>
#include <QTabBar>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariantList>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace maxchat::ui {

namespace {

QString nickWithoutPrefix(QString nick) {
    static const QString prefixes = QStringLiteral("~&@%+");
    while (!nick.isEmpty() && prefixes.contains(nick.front())) {
        nick.remove(0, 1);
    }
    return nick;
}

bool memberMatchesNick(const QString& memberText, const QString& nick) {
    return nickWithoutPrefix(memberText).compare(nick, Qt::CaseInsensitive) == 0;
}

bool removeMember(QListWidget* memberList, const QString& nick) {
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

void addMember(QListWidget* memberList, const QString& nick) {
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

bool isChannelTarget(const QString& target) {
    return target.startsWith(QLatin1Char('#')) || target.startsWith(QLatin1Char('&'));
}

bool isLikelyServerNotice(const QString& sender, const QString& target, const QString& ownNick) {
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

QString cleanedChannelToken(QString token) {
    token = token.trimmed();
    while (!token.isEmpty() && QStringLiteral(":,.;)]}").contains(token.back())) {
        token.chop(1);
    }
    return isChannelTarget(token) ? token : QString();
}

QString firstChannelToken(const QString& text) {
    static const QRegularExpression channelToken(QStringLiteral(R"((^|\s)([#&][^\s,.;:\)\]\}]+))"));
    const QRegularExpressionMatch match = channelToken.match(text);
    return match.hasMatch() ? cleanedChannelToken(match.captured(2)) : QString();
}

QString replyTextTarget(const QString& line) {
    static const QRegularExpression labelPattern(QStringLiteral(R"(^\[([^\]]+)\]\s*(.*)$)"));
    const QRegularExpressionMatch match = labelPattern.match(line.trimmed());
    if (!match.hasMatch()) {
        return QStringLiteral("server");
    }

    const QString label = match.captured(1).toLower();
    const QString body = match.captured(2).trimmed();
    if (label == QStringLiteral("topic") || label == QStringLiteral("channel")) {
        return firstChannelToken(body);
    }
    if (label != QStringLiteral("error")) {
        return QStringLiteral("server");
    }

    if (body.startsWith(QStringLiteral("Cannot send to ")) ||
        body.startsWith(QStringLiteral("Cannot join ")) ||
        body.startsWith(QStringLiteral("No such channel: ")) ||
        body.startsWith(QStringLiteral("You are not on "))) {
        return firstChannelToken(body);
    }

    const QString target = firstChannelToken(body);
    if (!target.isEmpty() && (body.contains(QStringLiteral(" is not on ")) ||
                              body.contains(QStringLiteral(" is already on ")))) {
        return target;
    }
    return body.startsWith(target) ? target : QStringLiteral("server");
}

QString replyTextLabel(const QString& line) {
    static const QRegularExpression labelPattern(QStringLiteral(R"(^\[([^\]]+)\])"));
    const QRegularExpressionMatch match = labelPattern.match(line.trimmed());
    return match.hasMatch() ? match.captured(1).toLower() : QString();
}

QString normalizeIgnoreMask(QString mask) {
    mask = mask.trimmed();
    if (mask.isEmpty()) {
        return {};
    }
    return mask.contains(QLatin1Char('!')) || mask.contains(QLatin1Char('@'))
               ? mask
               : QStringLiteral("%1!*@*").arg(mask);
}

QStringList variantStringList(const QVariant& value) {
    if (value.metaType().id() == QMetaType::QStringList) {
        return value.toStringList();
    }

    QStringList strings;
    const QVariantList values = value.toList();
    for (const QVariant& item : values) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) {
            strings.append(text);
        }
    }
    return strings;
}

bool containsCaseInsensitive(const QStringList& values, const QString& needle) {
    return std::any_of(values.cbegin(), values.cend(), [&needle](const QString& value) {
        return value.compare(needle, Qt::CaseInsensitive) == 0;
    });
}

QString formatDurationMs(const qint64 milliseconds) {
    qint64 seconds = std::max<qint64>(0, milliseconds / 1000);
    const qint64 days = seconds / 86400;
    seconds %= 86400;
    const qint64 hours = seconds / 3600;
    seconds %= 3600;
    const qint64 minutes = seconds / 60;
    seconds %= 60;

    QStringList parts;
    if (days > 0) {
        parts.append(QStringLiteral("%1d").arg(days));
    }
    if (hours > 0 || !parts.isEmpty()) {
        parts.append(QStringLiteral("%1h").arg(hours));
    }
    if (minutes > 0 || !parts.isEmpty()) {
        parts.append(QStringLiteral("%1m").arg(minutes));
    }
    parts.append(QStringLiteral("%1s").arg(seconds));
    return parts.join(QLatin1Char(' '));
}

QString qtDateTimeFormat(const QString& strftimeFormat) {
    const QString fallback = QStringLiteral("hh:mm AP");
    const QString source = strftimeFormat.trimmed();
    if (source.isEmpty()) {
        return fallback;
    }

    QString out;
    out.reserve(source.size() * 2);
    for (qsizetype i = 0; i < source.size(); ++i) {
        const QChar ch = source.at(i);
        if (ch != QLatin1Char('%') || i + 1 >= source.size()) {
            out.append(ch);
            continue;
        }

        const QChar code = source.at(++i);
        if (code == QLatin1Char('%')) {
            out.append(QLatin1Char('%'));
        } else if (code == QLatin1Char('I')) {
            out.append(QStringLiteral("hh"));
        } else if (code == QLatin1Char('H')) {
            out.append(QStringLiteral("HH"));
        } else if (code == QLatin1Char('M')) {
            out.append(QStringLiteral("mm"));
        } else if (code == QLatin1Char('S')) {
            out.append(QStringLiteral("ss"));
        } else if (code == QLatin1Char('p')) {
            out.append(QStringLiteral("AP"));
        } else if (code == QLatin1Char('Y')) {
            out.append(QStringLiteral("yyyy"));
        } else if (code == QLatin1Char('y')) {
            out.append(QStringLiteral("yy"));
        } else if (code == QLatin1Char('m')) {
            out.append(QStringLiteral("MM"));
        } else if (code == QLatin1Char('d')) {
            out.append(QStringLiteral("dd"));
        } else if (code == QLatin1Char('b')) {
            out.append(QStringLiteral("MMM"));
        } else if (code == QLatin1Char('B')) {
            out.append(QStringLiteral("MMMM"));
        } else if (code == QLatin1Char('a')) {
            out.append(QStringLiteral("ddd"));
        } else if (code == QLatin1Char('A')) {
            out.append(QStringLiteral("dddd"));
        } else {
            out.append(QLatin1Char('%'));
            out.append(code);
        }
    }
    return out.isEmpty() ? fallback : out;
}

QStringList removeCaseInsensitive(QStringList values, const QString& needle) {
    for (int index = values.size() - 1; index >= 0; --index) {
        if (values.at(index).compare(needle, Qt::CaseInsensitive) == 0) {
            values.removeAt(index);
        }
    }
    return values;
}

QString previewKey(const QUrl& url) {
    return url.toString(QUrl::FullyEncoded).toCaseFolded();
}

QString inputText(const QTextEdit* input) {
    return input == nullptr ? QString() : input->toPlainText();
}

struct CommandServerSpec {
    bool valid = false;
    QString errorText;
    QString host;
    int port = 6697;
    bool tls = true;
    QString serverPassword;
};

bool tokenIsPort(QString token) {
    token = token.trimmed();
    if (token.startsWith(QLatin1Char('+'))) {
        token.remove(0, 1);
    }
    bool ok = false;
    const int port = token.toInt(&ok);
    return ok && port > 0 && port <= 65535;
}

CommandServerSpec parseCommandServerSpec(const QString& firstToken, const QString& rest) {
    QStringList tokens = QStringLiteral("%1 %2")
                             .arg(firstToken, rest)
                             .trimmed()
                             .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

    CommandServerSpec spec;
    if (tokens.isEmpty()) {
        spec.errorText = QStringLiteral("Usage: /server server[:port] "
                                        "[server-password]");
        return spec;
    }

    bool tls = true;
    bool tlsOptionSeen = false;
    int index = 0;
    while (index < tokens.size()) {
        const QString option = tokens.at(index).toLower();
        if (option == QStringLiteral("-ssl") || option == QStringLiteral("--ssl") ||
            option == QStringLiteral("-tls") || option == QStringLiteral("--tls")) {
            tls = true;
            tlsOptionSeen = true;
            ++index;
            continue;
        }
        if (option == QStringLiteral("-nossl") || option == QStringLiteral("--nossl") ||
            option == QStringLiteral("-notls") || option == QStringLiteral("--notls")) {
            tls = false;
            tlsOptionSeen = true;
            ++index;
            continue;
        }
        break;
    }

    if (index >= tokens.size()) {
        spec.errorText = QStringLiteral("Usage: /server server[:port] "
                                        "[server-password]");
        return spec;
    }

    maxchat::irc::ServerEndpoint endpoint =
        maxchat::core::parseServerSpec(tokens.at(index), 6697, tls);
    if (tlsOptionSeen) {
        endpoint.tls = tls;
    }
    ++index;

    if (index < tokens.size() && tokenIsPort(tokens.at(index))) {
        QString portText = tokens.at(index);
        if (portText.startsWith(QLatin1Char('+'))) {
            endpoint.tls = true;
            portText.remove(0, 1);
        } else if (!tlsOptionSeen) {
            endpoint.tls = false;
        }
        endpoint.port = portText.toInt();
        ++index;
    }

    endpoint.host = endpoint.host.trimmed();
    if (endpoint.host.isEmpty()) {
        spec.errorText = QStringLiteral("Usage: /server server[:port] "
                                        "[server-password]");
        return spec;
    }
    if (endpoint.port <= 0 || endpoint.port > 65535) {
        spec.errorText = QStringLiteral("! Invalid server port.");
        return spec;
    }

    spec.valid = true;
    spec.host = endpoint.host;
    spec.port = endpoint.port;
    spec.tls = endpoint.tls;
    if (index < tokens.size()) {
        spec.serverPassword = tokens.mid(index).join(QLatin1Char(' '));
    }
    return spec;
}

bool networkMatchesCommandTarget(const maxchat::core::NetworkConfig& network,
                                 const QString& target) {
    const QString cleanTarget = target.trimmed();
    if (cleanTarget.isEmpty()) {
        return false;
    }

    if (network.value(QStringLiteral("name"))
                .toString()
                .compare(cleanTarget, Qt::CaseInsensitive) == 0 ||
        network.value(QStringLiteral("host"))
                .toString()
                .compare(cleanTarget, Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QStringList servers = variantStringList(network.value(QStringLiteral("servers")));
    for (const QString& server : servers) {
        if (server.compare(cleanTarget, Qt::CaseInsensitive) == 0) {
            return true;
        }
        const maxchat::irc::ServerEndpoint endpoint = maxchat::core::parseServerSpec(
            server, network.value(QStringLiteral("port"), 6667).toInt(),
            network.value(QStringLiteral("tls")).toBool());
        if (endpoint.host.compare(cleanTarget, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return false;
}

maxchat::core::NetworkConfig findCommandNetwork(const maxchat::core::NetworkConfigList& networks,
                                                const QString& target, const QString& rest) {
    const QString fullTarget = QStringLiteral("%1 %2").arg(target, rest).trimmed();
    if (!fullTarget.isEmpty()) {
        for (const maxchat::core::NetworkConfig& network : networks) {
            if (networkMatchesCommandTarget(network, fullTarget)) {
                return network;
            }
        }
    }

    for (const maxchat::core::NetworkConfig& network : networks) {
        if (networkMatchesCommandTarget(network, target)) {
            return network;
        }
    }
    return {};
}

struct ReplayLogLine {
    QDateTime timestamp;
    QString body;
};

bool looksLikeReplayTimestampPrefix(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    static const QRegularExpression timestampLike(QStringLiteral(R"(^[0-9A-Za-z ,:/.\-+]+$)"));
    return timestampLike.match(trimmed).hasMatch() &&
           trimmed.contains(QRegularExpression(QStringLiteral(R"(\d)")));
}

QString unwrapAlignedReplayBody(QString body) {
    body = body.trimmed();
    const int separator = body.indexOf(QStringLiteral(" | "));
    if (separator < 0) {
        return body;
    }

    const QString left = body.left(separator).trimmed();
    const QString right = body.mid(separator + 3).trimmed();
    if (right.isEmpty()) {
        return body;
    }

    const int nickStart = left.lastIndexOf(QLatin1Char('<'));
    if (nickStart >= 0 && left.endsWith(QLatin1Char('>'))) {
        return QStringLiteral("%1 %2").arg(left.mid(nickStart), right);
    }

    static const QRegularExpression noticeLabel(QStringLiteral(R"((-[^\s-][^-]*-)$)"));
    const QRegularExpressionMatch noticeMatch = noticeLabel.match(left);
    if (noticeMatch.hasMatch()) {
        return QStringLiteral("%1 %2").arg(noticeMatch.captured(1), right);
    }

    const int actionStart = left.lastIndexOf(QStringLiteral("* "));
    if (actionStart >= 0) {
        const QString actionLabel = left.mid(actionStart).trimmed();
        static const QRegularExpression actionLabelPattern(QStringLiteral(R"(^\* \S+$)"));
        if (actionLabelPattern.match(actionLabel).hasMatch()) {
            return QStringLiteral("%1 %2").arg(actionLabel, right);
        }
    }

    if (left.isEmpty() || looksLikeReplayTimestampPrefix(left)) {
        return right;
    }
    return body;
}

ReplayLogLine parseReplayLogLine(const QString& line) {
    static const QRegularExpression timestampPrefix(
        QStringLiteral(R"(^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s+(.*)$)"));
    const QRegularExpressionMatch match = timestampPrefix.match(line);
    if (!match.hasMatch()) {
        return {{}, unwrapAlignedReplayBody(line)};
    }

    return {QDateTime::fromString(match.captured(1), QStringLiteral("yyyy-MM-dd HH:mm:ss")),
            unwrapAlignedReplayBody(match.captured(2))};
}

void setInputText(QTextEdit* input, const QString& text) {
    if (input == nullptr) {
        return;
    }
    input->setPlainText(text);
    QTextCursor cursor = input->textCursor();
    cursor.movePosition(QTextCursor::End);
    input->setTextCursor(cursor);
}

int inputCursorPosition(const QTextEdit* input) {
    return input == nullptr ? 0 : input->textCursor().position();
}

void setInputCursorPosition(QTextEdit* input, const int position) {
    if (input == nullptr) {
        return;
    }
    QTextCursor cursor = input->textCursor();
    cursor.setPosition(std::clamp(position, 0, static_cast<int>(input->toPlainText().size())));
    input->setTextCursor(cursor);
}

constexpr int MaxRawLogLines = 5000;
constexpr int MaxUrlListItems = 1000;
constexpr int FriendPollIntervalMs = 60000;
constexpr int TreeTargetRole = Qt::UserRole;
constexpr int TreeNetworkRole = Qt::UserRole + 1;

QTreeWidgetItem* newTreeItem(const QString& label, const QString& target = {},
                             const QString& network = {}) {
    auto* item = new QTreeWidgetItem(QStringList{label});
    if (!target.trimmed().isEmpty()) {
        item->setData(0, TreeTargetRole, target.trimmed());
    }
    if (!network.trimmed().isEmpty()) {
        item->setData(0, TreeNetworkRole, network.trimmed());
    }
    return item;
}

QString treeItemTarget(const QTreeWidgetItem* item) {
    if (item == nullptr) {
        return {};
    }

    const QString storedTarget = item->data(0, TreeTargetRole).toString().trimmed();
    return storedTarget.isEmpty() ? item->text(0).trimmed() : storedTarget;
}

QString treeItemNetwork(const QTreeWidgetItem* item) {
    if (item == nullptr) {
        return {};
    }

    const QString storedNetwork = item->data(0, TreeNetworkRole).toString().trimmed();
    if (!storedNetwork.isEmpty()) {
        return storedNetwork;
    }

    const QTreeWidgetItem* root = item;
    while (root->parent() != nullptr) {
        root = root->parent();
    }
    return root->data(0, TreeNetworkRole).toString().trimmed();
}

bool isTreeStatusTarget(const QString& target) {
    return target.compare(QStringLiteral("server"), Qt::CaseInsensitive) == 0 ||
           target == QStringLiteral("Connected") || target == QStringLiteral("Connecting") ||
           target == QStringLiteral("Disconnected");
}

QString labelWithTreeCounts(const QString& label, const int unreadCount, const int highlightCount) {
    if (unreadCount <= 0) {
        return label;
    }

    if (highlightCount > 0) {
        if (highlightCount == unreadCount) {
            return QStringLiteral("%1 [!%2]").arg(label).arg(highlightCount);
        }
        return QStringLiteral("%1 [!%2/%3]").arg(label).arg(highlightCount).arg(unreadCount);
    }
    return QStringLiteral("%1 [%2]").arg(label).arg(unreadCount);
}

class ChatTextView final : public QTextBrowser {
  public:
    using SeparatorMovedHandler = std::function<void(int)>;

    explicit ChatTextView(QWidget* parent = nullptr) : QTextBrowser(parent) {}

    void setSeparatorMovedHandler(SeparatorMovedHandler handler) {
        separatorMovedHandler_ = std::move(handler);
    }

    // When true, copying yields plain text only (no rich-text colour runs),
    // matching the Python "strip colors on copy" option.
    void setStripColorsOnCopy(bool strip) { stripColorsOnCopy_ = strip; }

    void setSeparatorGuide(double timestampColumns, int nickWidth, bool visible, QColor color) {
        timestampColumns_ = std::max(0.0, timestampColumns);
        nickWidth_ = std::clamp(nickWidth, 4, 40);
        separatorColumns_ =
            visible ? timestampColumns_ + static_cast<double>(nickWidth_) + 1.0 : 0.0;
        color.setAlpha(130);
        separatorColor_ = color;
        viewport()->update();
    }

  protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && nearSeparator(event->position().x())) {
            draggingSeparator_ = true;
            event->accept();
            return;
        }
        QTextBrowser::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (draggingSeparator_) {
            const double spaceWidth =
                std::max(1.0, QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')));
            const double columns =
                (event->position().x() - document()->documentMargin()) / spaceWidth;
            separatorColumns_ = std::max(timestampColumns_ + 5.0, columns);
            viewport()->update();
            event->accept();
            return;
        }
        if (nearSeparator(event->position().x())) {
            viewport()->setCursor(Qt::SplitHCursor);
            event->accept();
            return;
        }
        viewport()->setCursor(Qt::IBeamCursor);
        QTextBrowser::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (draggingSeparator_) {
            draggingSeparator_ = false;
            const int nextWidth = std::clamp(
                static_cast<int>(std::lround(separatorColumns_ - timestampColumns_ - 1.0)), 4, 40);
            if (separatorMovedHandler_) {
                separatorMovedHandler_(nextWidth);
            }
            event->accept();
            return;
        }
        QTextBrowser::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        if (!draggingSeparator_) {
            viewport()->unsetCursor();
        }
        QTextBrowser::leaveEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        QTextBrowser::paintEvent(event);
        if (separatorColumns_ <= 0.0) {
            return;
        }
        QPainter painter(viewport());
        painter.setPen(separatorColor_);
        const int x = static_cast<int>(std::lround(separatorX()));
        painter.drawLine(x, 0, x, viewport()->height());
    }

    QMimeData* createMimeDataFromSelection() const override {
        QMimeData* mime = QTextBrowser::createMimeDataFromSelection();
        if (stripColorsOnCopy_ && mime != nullptr) {
            // Drop the HTML/colour payload; keep the plain text only.
            const QString plain = mime->text();
            mime->clear();
            mime->setText(plain);
        }
        return mime;
    }

  private:
    [[nodiscard]] double separatorX() const {
        return document()->documentMargin() +
               QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')) * separatorColumns_;
    }

    [[nodiscard]] bool nearSeparator(double x) const {
        return separatorColumns_ > 0.0 && std::abs(x - separatorX()) <= 5.0;
    }

    bool stripColorsOnCopy_ = true;
    SeparatorMovedHandler separatorMovedHandler_;
    double timestampColumns_ = 0.0;
    double separatorColumns_ = 0.0;
    int nickWidth_ = 16;
    QColor separatorColor_ = QColor(127, 127, 127, 130);
    bool draggingSeparator_ = false;
};

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_chatLogStore(QDir(m_settings.paths().configDir).filePath(QStringLiteral("logs"))),
      m_openGraphFetcher(&m_previewNetworkManager),
      m_osNotifyAvailable(false) {
    m_appUptime.start();
    loadFonts();
    buildMenus();
    buildLayout();
    setupConnectionSignals();
    setupTrayIcon();
    m_notifier = new Notifier(this);
    // OS native notifications post through the VISIBLE tray icon (showMessage on
    // a never-shown tray is a silent no-op), so they require m_tray to exist.
    m_osNotifyAvailable = (m_tray != nullptr) && ::QSystemTrayIcon::supportsMessages();
#ifdef Q_OS_LINUX
    // Wayland/WSLg reports supportsMessages()=true but often has no notification
    // daemon; confirm one actually owns the D-Bus name, else fall back to toast.
    if (m_osNotifyAvailable) {
        QProcess probe;
        probe.start(QStringLiteral("dbus-send"),
                    {QStringLiteral("--session"),
                     QStringLiteral("--dest=org.freedesktop.DBus"),
                     QStringLiteral("--type=method_call"),
                     QStringLiteral("--print-reply"),
                     QStringLiteral("/org/freedesktop/DBus"),
                     QStringLiteral("org.freedesktop.DBus.GetNameOwner"),
                     QStringLiteral("string:org.freedesktop.Notifications")});
        const bool finished = probe.waitForFinished(3000);
        m_osNotifyAvailable = finished && probe.exitStatus() == QProcess::NormalExit &&
                              probe.exitCode() == 0;
    }
#endif
    connect(&m_openGraphFetcher, &maxchat::services::OpenGraphFetcher::cardFetched, this,
            &MainWindow::handlePreviewCardFetched);
    connect(&m_openGraphFetcher, &maxchat::services::OpenGraphFetcher::fetchFailed, this,
            &MainWindow::handlePreviewFetchFailed);
    m_friendPollTimer.setInterval(FriendPollIntervalMs);
    connect(&m_friendPollTimer, &QTimer::timeout, this, &MainWindow::pollFriends);
    m_autoAwayTimer.setSingleShot(true);
    connect(&m_autoAwayTimer, &QTimer::timeout, this, &MainWindow::triggerAutoAway);

    m_dccManager = new DccManager(this);
    connect(m_dccManager, &DccManager::ctcpToSend, this,
            [this](const QString& peer, const QString& ctcpArgs) {
                connection().ctcp(peer, QStringLiteral("DCC"), ctcpArgs);
                appendSystemLineToTarget(peer, QStringLiteral("-> [dcc] %1").arg(ctcpArgs), true,
                                         true, false, false);
            });
    connect(m_dccManager, &DccManager::status, this,
            [this](const QString& message) { appendSystemLine(QStringLiteral("! %1").arg(message)); });
    connect(m_dccManager, &DccManager::chatLineReceived, this,
            [this](const QString& peer, const QString& line) {
                appendSystemLineToTarget(QStringLiteral("=%1").arg(peer),
                                         QStringLiteral("<%1> %2").arg(peer, line), true, false,
                                         false, false);
            });
    connect(m_dccManager, &DccManager::chatStateChanged, this,
            [this](const QString& peer, int state) {
                const QString target = QStringLiteral("=%1").arg(peer);
                const QString note = state == 0   ? QStringLiteral("connecting...")
                                     : state == 1 ? QStringLiteral("connected.")
                                                  : QStringLiteral("closed.");
                rememberTarget(target);
                appendSystemLineToTarget(
                    target, QStringLiteral("* DCC chat with %1 %2").arg(peer, note), true, false,
                    false);
                rebuildNetworkTree();
            });

    setupNavShortcuts();
    applyCurrentSettings();
    if (m_settings.loadWithDefaults().value(QStringLiteral("connect_on_start"), false).toBool()) {
        QTimer::singleShot(0, this, [this]() { startConfiguredStartupConnection(); });
    }

    setWindowTitle(QStringLiteral("%1 %2").arg(app::displayName(), app::version()));
    setWindowIcon(QIcon(QStringLiteral(":/icons/maxchat.ico")));
    resize(1100, 720);

    if (m_settings.loadWithDefaults().value(QStringLiteral("update_check"), false).toBool()) {
        QTimer::singleShot(3500, this, [this]() { checkForUpdates(/*manual=*/false); });
    }

    // Scripting: load the user's Lua scripts (no-op when built without Lua).
    const QString scriptsDir =
        QDir(m_settings.paths().configDir).filePath(QStringLiteral("scripts"));
    m_lua = new maxchat::scripting::LuaEngine(
        this, scriptsDir, QDir(scriptsDir).filePath(QStringLiteral("data")), this);
    m_lua->setPermissions(buildScriptPermissions());
    if (maxchat::scripting::LuaEngine::available()) {
        QDir().mkpath(scriptsDir);
        seedBundledScripts(scriptsDir);
        m_lua->loadAll();
    }
}

bool MainWindow::selfTest() const {
    return m_networkTree != nullptr && m_chatView != nullptr && m_bufferTabBar != nullptr &&
           m_memberList != nullptr && m_channelModesButton != nullptr && m_topicLabel != nullptr &&
           m_input != nullptr && !windowTitle().isEmpty();
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange && isMinimized() && m_minimizeToTray && m_tray) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::changeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            handleInputSubmitted();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Up) {
            return showHistoryEntry(-1);
        }
        if (keyEvent->key() == Qt::Key_Down) {
            return showHistoryEntry(1);
        }
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
            return completeInput();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void maxchat::ui::MainWindow::buildMenus() {
    const QVariantMap initialSettings = m_settings.loadWithDefaults();

    auto* serverMenu = menuBar()->addMenu(QStringLiteral("&Server"));
    QAction* serverListAction =
        serverMenu->addAction(QStringLiteral("Server List..."), this, &MainWindow::openServerList);
    serverListAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    serverMenu->addAction(QStringLiteral("Quick Connect..."), this, &MainWindow::openQuickConnect);
    serverMenu->addAction(QStringLiteral("Disconnect"), this,
                          &MainWindow::disconnectFromCurrentServer);
    serverMenu->addAction(QStringLiteral("Disconnect All"), this,
                          &MainWindow::disconnectFromCurrentServer);
    serverMenu->addAction(QStringLiteral("Reconnect All"), this,
                          &MainWindow::reconnectCurrentServer);
    serverMenu->addSeparator();
    QAction* joinAction =
        serverMenu->addAction(QStringLiteral("Join..."), this, &MainWindow::openJoinDialog);
    joinAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    serverMenu->addAction(QStringLiteral("Leave Channel"), this, &MainWindow::leaveCurrentChannel);
    serverMenu->addSeparator();
    QAction* channelListAction = serverMenu->addAction(QStringLiteral("Channels..."), this,
                                                       [this]() { openChannelList(true); });
    serverMenu->addSeparator();
    serverMenu->addAction(QStringLiteral("Quit"), qApp, &QApplication::quit);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    m_buttonBarAction = viewMenu->addAction(QStringLiteral("Button Bar"));
    m_buttonBarAction->setCheckable(true);
    m_buttonBarAction->setChecked(true);
    connect(m_buttonBarAction, &QAction::toggled, this,
            [this](const bool visible) { setButtonBarVisible(visible, true); });
    viewMenu->addSeparator();
    m_serverListVisibleAction = viewMenu->addAction(QStringLiteral("Server List"));
    m_serverListVisibleAction->setCheckable(true);
    m_serverListVisibleAction->setChecked(true);
    connect(m_serverListVisibleAction, &QAction::toggled, this,
            [this](const bool visible) { setServerListVisible(visible, true); });
    m_membersVisibleAction = viewMenu->addAction(QStringLiteral("Member List"));
    m_membersVisibleAction->setCheckable(true);
    m_membersVisibleAction->setChecked(true);
    connect(m_membersVisibleAction, &QAction::toggled, this,
            [this](const bool visible) { setMembersVisible(visible, true); });
    m_buttonsAsTabsAction = viewMenu->addAction(QStringLiteral("Buttons as Tabs"));
    m_buttonsAsTabsAction->setCheckable(true);
    m_buttonsAsTabsAction->setChecked(
        initialSettings.value(QStringLiteral("buffer_tabs"), false).toBool());
    connect(m_buttonsAsTabsAction, &QAction::toggled, this,
            [this](const bool visible) { setBufferTabsVisible(visible, true); });
    m_chatSeparatorAction = viewMenu->addAction(QStringLiteral("Chat Separator"));
    m_chatSeparatorAction->setCheckable(true);
    m_chatSeparatorAction->setChecked(true);
    connect(m_chatSeparatorAction, &QAction::toggled, this,
            [this](const bool visible) { setChatSeparatorVisible(visible, true); });
    viewMenu->addSeparator();
    QMenu* themeMenu = viewMenu->addMenu(QStringLiteral("Theme"));
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    for (const AppThemeDefinition& theme : appThemes()) {
        QAction* action = themeMenu->addAction(theme.label);
        action->setCheckable(true);
        action->setData(theme.id);
        themeGroup->addAction(action);
        m_themeActions.append(action);
        connect(action, &QAction::triggered, this, [this, id = theme.id]() { setTheme(id, true); });
    }

    QMenu* chatThemeMenu = viewMenu->addMenu(QStringLiteral("Chat Theme"));
    auto* chatThemeGroup = new QActionGroup(this);
    chatThemeGroup->setExclusive(true);
    for (const ChatThemeDefinition& theme : chatThemes()) {
        QAction* action = chatThemeMenu->addAction(theme.label);
        action->setCheckable(true);
        action->setData(theme.id);
        chatThemeGroup->addAction(action);
        m_chatThemeActions.append(action);
        connect(action, &QAction::triggered, this,
                [this, id = theme.id]() { setChatTheme(id, true); });
    }

    QMenu* wallpaperMenu = viewMenu->addMenu(QStringLiteral("Wallpaper"));
    auto* wallpaperGroup = new QActionGroup(this);
    wallpaperGroup->setExclusive(true);
    for (const WallpaperDefinition& wallpaper : wallpaperChoices()) {
        QAction* action = wallpaperMenu->addAction(wallpaper.label);
        action->setCheckable(true);
        action->setData(wallpaper.value);
        wallpaperGroup->addAction(action);
        m_wallpaperActions.append(action);
        connect(action, &QAction::triggered, this,
                [this, value = wallpaper.value]() { setWallpaper(value, true); });
    }
    wallpaperMenu->addSeparator();
    wallpaperMenu->addAction(QStringLiteral("Load Image..."), this, [this]() {
        const QString path =
            QFileDialog::getOpenFileName(this, QStringLiteral("Choose Wallpaper"), QString(),
                                         QStringLiteral("Images (*.png *.jpg *.jpeg *.webp)"));
        if (!path.isEmpty()) {
            setWallpaper(path, true);
        }
    });
    m_looksMenu = viewMenu->addMenu(QStringLiteral("Saved Looks"));
    rebuildLooksMenu();
    viewMenu->addSeparator();
    viewMenu->addAction(QStringLiteral("Clear Current Chat"), this, &MainWindow::clearCurrentChat);
    viewMenu->addAction(QStringLiteral("Mark All Read"), this, &MainWindow::markAllRead);

    auto* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
    auto* findAction =
        toolsMenu->addAction(QStringLiteral("Find in Chat..."), this, &MainWindow::openChatFind);
    findAction->setShortcut(QKeySequence::Find);
    toolsMenu->addAction(QStringLiteral("Replay Current Log"), this, &MainWindow::replayCurrentLog);
    toolsMenu->addSeparator();
    QAction* urlListAction =
        toolsMenu->addAction(QStringLiteral("URL List..."), this, &MainWindow::openUrlList);
    toolsMenu->addAction(QStringLiteral("Raw Log..."), this, &MainWindow::openRawLog);
    toolsMenu->addSeparator();
    m_doNotDisturbAction = toolsMenu->addAction(QStringLiteral("Do Not Disturb"));
    m_doNotDisturbAction->setCheckable(true);
    m_doNotDisturbAction->setChecked(initialSettings.value(QStringLiteral("dnd"), false).toBool());
    m_doNotDisturbAction->setToolTip(
        QStringLiteral("Suppress toast, flash, tray, beep, and sound notifications."));
    connect(m_doNotDisturbAction, &QAction::toggled, this, [this](const bool enabled) {
        QVariantMap settings = m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("dnd"), enabled);
        if (!m_settings.saveRaw(settings)) {
            appendSystemLine(QStringLiteral("! Could not save Do Not Disturb."));
            return;
        }
        appendSystemLine(enabled ? QStringLiteral("! Do Not Disturb enabled.")
                                 : QStringLiteral("! Do Not Disturb disabled."));
    });

    auto* settingsMenu = menuBar()->addMenu(QStringLiteral("&Settings"));
    QAction* prefsAction = settingsMenu->addAction(QStringLiteral("Preferences..."), this,
                                                   &MainWindow::openPreferences);
    prefsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    settingsMenu->addAction(QStringLiteral("Ignore List..."), this, &MainWindow::openIgnoreList);
    settingsMenu->addAction(QStringLiteral("Aliases..."), this, &MainWindow::openAliases);
    settingsMenu->addAction(QStringLiteral("Keyboard Shortcuts..."), this,
                            &MainWindow::openShortcutEditor);
    settingsMenu->addAction(QStringLiteral("Friends / Notify..."), this,
                            &MainWindow::openFriendsNotify);
    settingsMenu->addAction(QStringLiteral("Scripts..."), this, &MainWindow::openScriptsManager);
    QAction* transfersAction =
        settingsMenu->addAction(QStringLiteral("File Transfers..."), this,
                                &MainWindow::openDccTransfers);
    settingsMenu->addSeparator();
    settingsMenu->addAction(QStringLiteral("Import Settings..."), this,
                            &MainWindow::importSettings);
    settingsMenu->addAction(QStringLiteral("Export Settings..."), this,
                            &MainWindow::exportSettings);
    settingsMenu->addAction(QStringLiteral("Reset Server List"), this,
                            &MainWindow::resetServerList);

    auto* comicMenu = menuBar()->addMenu(QStringLiteral("&Comic"));
    m_comicModeAction = comicMenu->addAction(QStringLiteral("Comic Mode"));
    m_comicModeAction->setCheckable(true);
    connect(m_comicModeAction, &QAction::toggled, this, &MainWindow::setComicMode);
    QAction* comicModeAction = m_comicModeAction;
    comicModeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    QAction* emotionAction =
        comicMenu->addAction(QStringLiteral("Emotion..."), this, &MainWindow::openEmotionPicker);
    comicMenu->addSeparator();
    comicMenu->addAction(QStringLiteral("Comic Settings..."), this, &MainWindow::openComicSettings);
    comicMenu->addAction(QStringLiteral("Browse Characters..."), this,
                         &MainWindow::openCharacterGallery);
    comicMenu->addAction(QStringLiteral("Save Comic..."), this, &MainWindow::saveComic);
    comicMenu->addSeparator();
    m_comicCaptionsAction = comicMenu->addAction(QStringLiteral("Character Names"));
    m_comicCaptionsAction->setCheckable(true);
    m_comicCaptionsAction->setChecked(
        initialSettings.value(QStringLiteral("comic_captions"), true).toBool());
    connect(m_comicCaptionsAction, &QAction::toggled, this, [this](const bool enabled) {
        QVariantMap settings = m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("comic_captions"), enabled);
        if (!m_settings.saveRaw(settings)) {
            appendSystemLine(QStringLiteral("! Could not save comic caption setting."));
            return;
        }
        showFeaturePlanned(QStringLiteral("Character Names"));
    });

    auto* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    QAction* helpAction = helpMenu->addAction(QStringLiteral("Commands && Shortcuts..."), this,
                                              &MainWindow::openCommandHelp);
    helpAction->setShortcut(QKeySequence(Qt::Key_F1));
    helpMenu->addSeparator();
    helpMenu->addAction(QStringLiteral("Check for Updates..."), this,
                        [this]() { checkForUpdates(/*manual=*/true); });
    helpMenu->addAction(QStringLiteral("About"), this, &MainWindow::openAbout);

    m_buttonBar = addToolBar(QStringLiteral("Button Bar"));
    m_buttonBar->setObjectName(QStringLiteral("mainToolbar"));
    m_buttonBar->setMovable(false);
    m_buttonBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_buttonBar->addAction(m_serverListVisibleAction);
    m_buttonBar->addAction(m_membersVisibleAction);
    m_buttonBar->addSeparator();
    m_buttonBar->addAction(channelListAction);
    m_buttonBar->addAction(joinAction);
    m_buttonBar->addSeparator();
    m_buttonBar->addAction(comicModeAction);
    m_buttonBar->addAction(emotionAction);
    m_buttonBar->addSeparator();
    m_buttonBar->addAction(urlListAction);
    m_buttonBar->addAction(transfersAction);
    m_buttonBar->addAction(prefsAction);
    const auto setToolbarLabel = [this](QAction* action, const QString& label) {
        if (auto* button = m_buttonBar->widgetForAction(action); button != nullptr) {
            button->setProperty("text", label);
            if (auto* toolButton = qobject_cast<QToolButton*>(button); toolButton != nullptr) {
                toolButton->setText(label);
            }
        }
    };
    const auto reapplyToolbarLabels = [this, setToolbarLabel, channelListAction, joinAction,
                                       comicModeAction, emotionAction, urlListAction,
                                       transfersAction, prefsAction]() {
        setToolbarLabel(m_serverListVisibleAction, QStringLiteral("Servers"));
        setToolbarLabel(m_membersVisibleAction, QStringLiteral("Members"));
        setToolbarLabel(channelListAction, QStringLiteral("Channels"));
        setToolbarLabel(joinAction, QStringLiteral("Join"));
        setToolbarLabel(comicModeAction, QStringLiteral("Comic"));
        setToolbarLabel(emotionAction, QStringLiteral("Emotion"));
        setToolbarLabel(urlListAction, QStringLiteral("URLs"));
        setToolbarLabel(transfersAction, QStringLiteral("Transfers"));
        setToolbarLabel(prefsAction, QStringLiteral("Prefs"));
    };
    reapplyToolbarLabels();
    connect(m_serverListVisibleAction, &QAction::changed, this, reapplyToolbarLabels);
    connect(m_membersVisibleAction, &QAction::changed, this, reapplyToolbarLabels);
}

void maxchat::ui::MainWindow::buildLayout() {
    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("centralRoot"));
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(4);

    m_topicLabel = new QLabel(QString(), root); // topic bar starts empty (no channel yet)
    m_topicLabel->setObjectName(QStringLiteral("topicLabel"));
    m_topicLabel->setMinimumHeight(30);
    m_topicLabel->setAlignment(Qt::AlignCenter); // centred in the bar, Python parity
    // The topic is attacker-controlled (anyone can set a channel TOPIC); force
    // plain text so a topic like "<img src=...>" can't render as rich text.
    m_topicLabel->setTextFormat(Qt::PlainText);

    m_bufferTabBar = new QTabBar(root);
    m_bufferTabBar->setObjectName(QStringLiteral("bufferTabBar"));
    m_bufferTabBar->setDrawBase(false);
    m_bufferTabBar->setExpanding(false);
    m_bufferTabBar->setUsesScrollButtons(true);
    m_bufferTabBar->setTabsClosable(false);
    m_bufferTabBar->setFocusPolicy(Qt::NoFocus);
    m_bufferTabBar->addTab(QStringLiteral("Server"));
    m_bufferTabBar->setVisible(false);
    connect(m_bufferTabBar, &QTabBar::tabBarClicked, this, [this](const int index) {
        QTreeWidgetItem* item = treeItemForTabIndex(index);
        if (item != nullptr && m_networkTree != nullptr) {
            m_networkTree->setCurrentItem(item);
        }
    });

    m_networkTree = new QTreeWidget(root);
    m_networkTree->setObjectName(QStringLiteral("networkTree"));
    m_networkTree->setMinimumWidth(0);
    m_networkTree->setHeaderHidden(true);
    m_networkTree->setContextMenuPolicy(Qt::CustomContextMenu);
    auto* rootItem = newTreeItem(QStringLiteral("Server"), QStringLiteral("server"));
    m_networkTree->addTopLevelItem(rootItem);
    m_networkTree->expandAll();
    m_networkTree->setCurrentItem(rootItem);
    connect(m_networkTree, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::showNetworkTreeContextMenu);
    connect(m_networkTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                // Roots are server-buffer rows; children are channels/queries.
                if (current == nullptr || !m_hasConnectionPlan) {
                    return;
                }

                const QString network = treeItemNetwork(current);
                const QString target = treeItemTarget(current);
                if (target.isEmpty()) {
                    return;
                }

                if (!network.isEmpty() &&
                    network.compare(activeNetworkName(), Qt::CaseInsensitive) != 0) {
                    setActiveNetwork(network);
                }

                if (isTreeStatusTarget(target)) {
                    activateBufferTarget({});
                    updateChannelModeButton();
                    return;
                }

                activateBufferTarget(target);
                showConnectionStatus(
                    QStringLiteral("%1 - %2").arg(m_connectionPlan.networkName, target));
                updateChannelModeButton();
            });

    auto* chatColumn = new QWidget(root);
    chatColumn->setObjectName(QStringLiteral("chatColumn"));
    auto* chatLayout = new QVBoxLayout(chatColumn);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(4);

    auto* chatView = new ChatTextView(chatColumn);
    chatView->setSeparatorMovedHandler(
        [this](const int nickWidth) { setNickColumnWidth(nickWidth, true); });
    m_chatView = chatView;
    m_chatView->setObjectName(QStringLiteral("chatView"));
    m_chatView->setReadOnly(true);
    // Anchor clicks route through handleChatAnchorClicked so image/audio/video
    // links open the inline viewers instead of an external browser.
    m_chatView->setOpenExternalLinks(false);
    m_chatView->setOpenLinks(false);
    connect(m_chatView, &QTextBrowser::anchorClicked, this,
            &MainWindow::handleChatAnchorClicked);
    m_chatView->setPlainText(
        QStringLiteral("Not connected - Server > Server List... or Quick Connect...\n\n"
                       "Native C++/Qt port skeleton is running.\n"
                       "Server List now loads and saves the native settings store."));

    m_memberPanel = new QWidget(root);
    m_memberPanel->setObjectName(QStringLiteral("memberPanel"));
    m_memberPanel->setMinimumWidth(0);
    auto* memberPanelLayout = new QVBoxLayout(m_memberPanel);
    memberPanelLayout->setContentsMargins(0, 0, 0, 0);
    memberPanelLayout->setSpacing(4);

    m_channelModesButton = new QPushButton(QStringLiteral("Channel Modes..."), m_memberPanel);
    m_channelModesButton->setObjectName(QStringLiteral("channelModesButton"));
    m_channelModesButton->setEnabled(false);
    connect(m_channelModesButton, &QPushButton::clicked, this, &MainWindow::openChannelModes);

    m_membersHeader = new QLabel(QStringLiteral("Members"), m_memberPanel);
    m_membersHeader->setObjectName(QStringLiteral("membersHeader"));
    m_memberList = new QListWidget(m_memberPanel);
    m_memberList->setObjectName(QStringLiteral("memberList"));
    m_memberList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_memberList, &QListWidget::customContextMenuRequested, this,
            &MainWindow::showMemberContextMenu);
    connect(m_memberList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item == nullptr || item->data(Qt::UserRole + 1).toBool()) {
            return;
        }
        openQueryForNick(nickWithoutPrefix(item->text()));
    });
    memberPanelLayout->addWidget(m_channelModesButton);
    memberPanelLayout->addWidget(m_membersHeader);
    memberPanelLayout->addWidget(m_memberList, 1);

    m_input = new QTextEdit(root);
    m_input->setObjectName(QStringLiteral("messageInput"));
    m_input->setPlaceholderText(QStringLiteral("Message"));
    m_input->setAcceptRichText(false);
    m_input->setLineWrapMode(QTextEdit::NoWrap);
    m_input->setTabChangesFocus(false);
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_input->document()->setDocumentMargin(4);
    resizeMessageInput();
    m_spellcheckHighlighter = new SpellcheckHighlighter(m_input->document());
    m_input->installEventFilter(this);

    // mIRC formatting: Ctrl+B/I/U insert the control codes, Ctrl+K opens the
    // colour picker. Widget-scoped so they only fire while typing.
    const auto addFormattingShortcut = [this](const QString& key, const ushort code) {
        auto* shortcut = new QShortcut(QKeySequence(key), m_input);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this,
                [this, code]() { m_input->textCursor().insertText(QString(QChar(code))); });
    };
    addFormattingShortcut(QStringLiteral("Ctrl+B"), 0x02);
    addFormattingShortcut(QStringLiteral("Ctrl+I"), 0x1D);
    addFormattingShortcut(QStringLiteral("Ctrl+U"), 0x1F);
    addFormattingShortcut(QStringLiteral("Ctrl+R"), 0x16); // reverse
    addFormattingShortcut(QStringLiteral("Ctrl+O"), 0x0F); // reset/plain
    auto* colorShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), m_input);
    colorShortcut->setContext(Qt::WidgetShortcut);
    connect(colorShortcut, &QShortcut::activated, this, [this]() {
        ColorPickerDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedCode().isEmpty()) {
            m_input->textCursor().insertText(QString(QChar(0x03)) + dialog.selectedCode());
            m_input->setFocus();
        }
    });

    // Comic panels sit ABOVE the chat in a vertical splitter so the chat stays
    // visible beneath them when Comic Mode is on (MS Comic Chat style).
    m_comicView = new ComicView(chatColumn);
    connect(m_comicView, &ComicView::saveRequested, this, &MainWindow::saveComic);
    m_comicView->setVisible(false);
    m_chatSplitter = new QSplitter(Qt::Vertical, chatColumn);
    m_chatSplitter->setObjectName(QStringLiteral("chatSplitter"));
    m_chatSplitter->addWidget(m_comicView);
    m_chatSplitter->addWidget(m_chatView);
    m_chatSplitter->setCollapsible(0, false);
    m_chatSplitter->setCollapsible(1, false);
    m_chatSplitter->setStretchFactor(0, 1);
    m_chatSplitter->setStretchFactor(1, 1);
    chatLayout->addWidget(m_chatSplitter, 1);
    m_audioBar = new AudioPlayerBar(chatColumn);
    chatLayout->addWidget(m_audioBar);
    auto* inputRow = new QWidget(chatColumn);
    auto* inputRowLayout = new QHBoxLayout(inputRow);
    inputRowLayout->setContentsMargins(0, 0, 0, 0);
    inputRowLayout->setSpacing(4);
    m_nickLabel = new QLabel(inputRow);
    m_nickLabel->setObjectName(QStringLiteral("nickLabel"));
    m_nickLabel->setVisible(false);
    inputRowLayout->addWidget(m_nickLabel);
    inputRowLayout->addWidget(m_input, 1);
    chatLayout->addWidget(inputRow);

    m_mainSplitter = new QSplitter(Qt::Horizontal, root);
    m_mainSplitter->setObjectName(QStringLiteral("mainSplitter"));
    m_mainSplitter->addWidget(m_networkTree);
    m_mainSplitter->addWidget(chatColumn);
    m_mainSplitter->addWidget(m_memberPanel);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setChildrenCollapsible(true);
    m_mainSplitter->setCollapsible(0, true);
    m_mainSplitter->setCollapsible(2, true);
    m_mainSplitter->setSizes({180, 720, 190});
    connect(m_mainSplitter, &QSplitter::splitterMoved, this,
            [this]() { syncPanelActionsFromSplitter(true); });

    rootLayout->addWidget(m_topicLabel);
    rootLayout->addWidget(m_bufferTabBar);
    rootLayout->addWidget(m_mainSplitter, 1);

    setCentralWidget(root);
    statusBar()->showMessage(
        QStringLiteral("Not connected - Server > Server List... or Quick Connect..."));
}

void maxchat::ui::MainWindow::openServerList() {
    const bool mergedDefaults = m_settings.mergeDefaultNetworks();
    QVariantMap settings = m_settings.loadWithDefaults();
    auto networks =
        maxchat::core::networkConfigListFromVariant(settings.value(QStringLiteral("networks")));

    ServerListDialog dialog(networks, settings.value(QStringLiteral("connect_on_start")).toBool(),
                            this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    settings.insert(QStringLiteral("networks"),
                    maxchat::core::networkConfigListToVariantList(dialog.networks()));
    settings.insert(QStringLiteral("connect_on_start"), dialog.connectOnStart());
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save server list."));
        return;
    }

    appendSystemLine(
        QStringLiteral("! Server list saved (%1 networks).").arg(dialog.networks().size()));
    if (dialog.connectWasRequested()) {
        if (mergedDefaults) {
            appendSystemLine(QStringLiteral("! Server list was updated with bundled defaults."));
        }
        const auto network = dialog.selectedNetwork();
        startConnection(network);
    }
}

void maxchat::ui::MainWindow::openQuickConnect() {
    QuickConnectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    startConnection(dialog.network());
}

void maxchat::ui::MainWindow::openJoinDialog() {
    bool accepted = false;
    const QString channel =
        QInputDialog::getText(this, QStringLiteral("Join Channel"), QStringLiteral("Channel"),
                              QLineEdit::Normal, QString(), &accepted);
    if (!accepted || channel.trimmed().isEmpty()) {
        return;
    }
    sendCommandOrMessage(QStringLiteral("/join %1").arg(channel.trimmed()));
}

void maxchat::ui::MainWindow::openPreferences() {
    PreferencesDialog dialog(m_settings.loadWithDefaults(), this);
    connect(&dialog, &PreferencesDialog::exportSettingsRequested, this,
            &MainWindow::exportSettings);
    connect(&dialog, &PreferencesDialog::importSettingsRequested, this, [this, &dialog]() {
        dialog.reject();
        importSettings();
    });
    connect(&dialog, &PreferencesDialog::resetServerListRequested, this, [this, &dialog]() {
        dialog.reject();
        resetServerList();
    });
    connect(&dialog, &PreferencesDialog::resetAllSettingsRequested, this, [this, &dialog]() {
        dialog.reject();
        resetAllSettings();
    });
    connect(&dialog, &PreferencesDialog::openComicSettingsRequested, this,
            &MainWindow::openComicSettings);
    connect(&dialog, &PreferencesDialog::browseCharactersRequested, this,
            &MainWindow::openCharacterGallery);
    connect(&dialog, &PreferencesDialog::testNotificationRequested, this, [this, &dialog]() {
        // Use the dialog's current (unsaved) settings, not the cached m_notify*
        QVariantMap testSettings = dialog.settings();
        const QString style = testSettings.value(QStringLiteral("notify_popup"), QStringLiteral("custom")).toString();
        const bool flash = testSettings.value(QStringLiteral("notify_flash"), true).toBool();
        const bool sound = testSettings.value(QStringLiteral("notify_sound"), false).toBool();
        const int durationMs = testSettings.value(QStringLiteral("notify_duration"), 6).toInt() * 1000;
        const QString corner = testSettings.value(QStringLiteral("notify_corner"), QStringLiteral("br")).toString();
        const QString theme = testSettings.value(QStringLiteral("notify_theme"), QStringLiteral("follow")).toString();

        // Don't notify if window is active or DND
        if (isActiveWindow()) return;
        if (testSettings.value(QStringLiteral("dnd"), false).toBool()) return;

        if (flash) QApplication::alert(this, 0);
        if (sound) {
            const QString soundsDir =
                QDir(m_settings.paths().configDir).filePath(QStringLiteral("sounds"));
            const QString bundled = QDir(QCoreApplication::applicationDirPath())
                                        .filePath(QStringLiteral("assets/sounds"));
            if (!m_soundPlayer.play(notifySoundPath(soundsDir, bundled))) {
                QApplication::beep();
            }
        }

        if (style == QLatin1String("off")) return;

        // OS native - post through the visible tray icon.
        if (style == QLatin1String("system") && m_osNotifyAvailable && m_tray != nullptr) {
            m_tray->showMessage(
                QStringLiteral("Test \u00b7 MaxChat"),
                QStringLiteral("This is what a notification looks like \u2014 click to open the chat."),
                m_tray->icon(), 5000);
            return;
        }

        // Custom toast
        const AppThemeDefinition& themeDef = appThemeById(m_currentTheme);
        QColor followBg = themeDef.panel.isValid() ? themeDef.panel : QColor(QStringLiteral("#2b2b2b"));
        QColor followFg = themeDef.text.isValid() ? themeDef.text : QColor(QStringLiteral("#e8e8e8"));
        QColor followAccent = themeDef.accent.isValid() ? themeDef.accent : QColor(QStringLiteral("#4a9eff"));

        QColor bg = Notifier::paletteBg(theme, followBg);
        QColor fg = Notifier::paletteFg(theme, followFg);
        QColor accent = Notifier::paletteAccent(theme, followAccent);

        QIcon icon = ui::AppIcon::makeIcon(
            testSettings.value(QStringLiteral("tray_icon"), QStringLiteral("bubble")).toString(), accent);

        m_notifier->show(
            QStringLiteral("Test \u00b7 MaxChat"),
            QStringLiteral("This is what a notification looks like \u2014 click to open the chat."),
            bg, fg, accent, corner, durationMs, icon);
    });
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (!m_settings.saveRaw(dialog.settings())) {
        appendSystemLine(QStringLiteral("! Could not save preferences."));
        return;
    }
    applyCurrentSettings();
    appendSystemLine(QStringLiteral("! Preferences saved."));
}

void maxchat::ui::MainWindow::openAliases() {
    AliasEditorDialog dialog(m_commandAliases, {}, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_commandAliases = dialog.aliases();
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("command_aliases"), m_commandAliases);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save command aliases."));
        return;
    }
    appendSystemLine(
        QStringLiteral("! Command aliases saved (%1 aliases).").arg(m_commandAliases.size()));
}

void maxchat::ui::MainWindow::openIgnoreList() {
    IgnoreListDialog dialog(
        m_ignoreMasks,
        [this](const QStringList& masks) {
            QStringList normalized;
            for (const QString& mask : masks) {
                const QString value = normalizeIgnoreMask(mask);
                if (!value.isEmpty() && !containsCaseInsensitive(normalized, value)) {
                    normalized.append(value);
                }
            }
            m_ignoreMasks = normalized;
            QVariantMap settings = m_settings.loadWithDefaults();
            settings.insert(QStringLiteral("ignores"), m_ignoreMasks);
            if (!m_settings.saveRaw(settings)) {
                appendSystemLine(QStringLiteral("! Could not save ignore list."));
                return;
            }
            m_connection.setIgnoreMasks(m_ignoreMasks);
            for (auto* irc : std::as_const(m_connectionsByNetwork)) {
                if (irc != nullptr) {
                    irc->setIgnoreMasks(m_ignoreMasks);
                }
            }
            appendSystemLine(
                QStringLiteral("! Ignore list saved (%1 masks).").arg(m_ignoreMasks.size()));
        },
        this);
    dialog.exec();
}

void maxchat::ui::MainWindow::openFriendsNotify() {
    FriendsNotifyDialog dialog(
        m_friendNicks,
        [this](const QStringList& friends) {
            QStringList cleaned;
            for (const QString& nick : friends) {
                const QString value = nickWithoutPrefix(nick).trimmed();
                if (!value.isEmpty() && !containsCaseInsensitive(cleaned, value)) {
                    cleaned.append(value);
                }
            }
            m_friendNicks = cleaned;
            QVariantMap settings = m_settings.loadWithDefaults();
            settings.insert(QStringLiteral("friends"), m_friendNicks);
            if (!m_settings.saveRaw(settings)) {
                appendSystemLine(QStringLiteral("! Could not save notify list."));
                return;
            }
            m_onlineFriends.clear();
            m_haveFriendSnapshot = false;
            if (m_friendNicks.isEmpty()) {
                m_friendPollTimer.stop();
            } else {
                pollFriends();
                if (anyNetworkConnectionIsConnected()) {
                    m_friendPollTimer.start();
                }
            }
            appendSystemLine(
                QStringLiteral("! Notify list saved (%1 nicks).").arg(m_friendNicks.size()));
        },
        this);
    dialog.exec();
}

void maxchat::ui::MainWindow::openChannelModes() {
    const QString channel = m_currentTarget.trimmed();
    if (!connection().isConnected() || !isChannelTarget(channel)) {
        appendSystemLine(QStringLiteral("! Select a channel before opening channel modes."));
        return;
    }

    connection().sendRaw(QStringLiteral("MODE %1").arg(channel));
    ChannelModesDialog dialog(
        channel, m_connectionPlan.networkName,
        m_channelModeLines.value(QStringLiteral("%1/%2").arg(activeNetworkName().toCaseFolded(),
                                                             channel.toCaseFolded())),
        [this, channel](const QString& change) { sendModeChange(channel, change); }, this);
    dialog.exec();
}

void maxchat::ui::MainWindow::openBanList(const QString& channel) {
    const QString target =
        channel.trimmed().isEmpty() ? m_currentTarget.trimmed() : channel.trimmed();
    if (!connection().isConnected() || !isChannelTarget(target)) {
        appendSystemLine(QStringLiteral("! Select a channel before opening the ban list."));
        return;
    }

    if (m_banListDialog != nullptr) {
        if (m_banListDialog->channel().compare(target, Qt::CaseInsensitive) == 0) {
            m_banListDialog->raise();
            m_banListDialog->activateWindow();
            return;
        }
        m_banListDialog->close();
    }

    m_banListDialog = new BanListDialog(
        target, m_connectionPlan.networkName,
        [this, target](const QString& mask) {
            connection().sendRaw(QStringLiteral("MODE %1 +b %2").arg(target, mask.trimmed()));
        },
        [this, target](const QString& mask) {
            connection().sendRaw(QStringLiteral("MODE %1 -b %2").arg(target, mask.trimmed()));
        },
        this);
    m_banListDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_banListDialog->show();
    m_banListDialog->clearBans();
    m_banListDialog->setStatusText(QStringLiteral("Requesting ban list..."));
    connection().sendRaw(QStringLiteral("MODE %1 +b").arg(target));
}

void maxchat::ui::MainWindow::openChannelList(bool reset) {
    if (m_channelListDialog == nullptr) {
        m_channelListDialog = new ChannelListDialog(this);
        m_channelListDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_channelListDialog, &ChannelListDialog::joinRequested, this,
                [this](const QString& channel) {
                    sendCommandOrMessage(QStringLiteral("/join %1").arg(channel));
                });
    }

    if (reset) {
        m_channelListDialog->clearChannels();
    }
    m_channelListDialog->show();
    m_channelListDialog->raise();
    m_channelListDialog->activateWindow();
}

void maxchat::ui::MainWindow::openChatFind() {
    if (m_chatFindDialog != nullptr) {
        m_chatFindDialog->raise();
        m_chatFindDialog->activateWindow();
        return;
    }

    m_chatFindDialog = new ChatFindDialog(this);
    m_chatFindDialog->setAttribute(Qt::WA_DeleteOnClose);
    if (m_chatView != nullptr) {
        const QString selectedText = m_chatView->textCursor().selectedText();
        if (!selectedText.trimmed().isEmpty()) {
            m_chatFindDialog->setSearchText(selectedText);
        }
    }
    connect(m_chatFindDialog, &ChatFindDialog::findNextRequested, this,
            [this](const QString& text, bool caseSensitive, bool wrapSearch) {
                findInChat(text, false, caseSensitive, wrapSearch);
            });
    connect(m_chatFindDialog, &ChatFindDialog::findPreviousRequested, this,
            [this](const QString& text, bool caseSensitive, bool wrapSearch) {
                findInChat(text, true, caseSensitive, wrapSearch);
            });
    m_chatFindDialog->show();
}

void maxchat::ui::MainWindow::openRawLog() {
    if (m_rawLogDialog != nullptr) {
        m_rawLogDialog->raise();
        m_rawLogDialog->activateWindow();
        return;
    }

    m_rawLogDialog = new RawLogDialog(this);
    m_rawLogDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_rawLogDialog->setLines(m_rawLogLines);
    connect(m_rawLogDialog, &RawLogDialog::clearRequested, this, [this]() {
        m_rawLogLines.clear();
        appendSystemLine(QStringLiteral("! Raw log cleared."));
    });
    m_rawLogDialog->show();
}

void maxchat::ui::MainWindow::openUrlList() {
    if (m_urlListDialog != nullptr) {
        m_urlListDialog->raise();
        m_urlListDialog->activateWindow();
        return;
    }

    m_urlListDialog = new UrlListDialog(this);
    m_urlListDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_urlListDialog->setUrls(m_urlList);
    connect(m_urlListDialog, &UrlListDialog::clearRequested, this, [this]() {
        m_urlList.clear();
        appendSystemLine(QStringLiteral("! URL list cleared."));
    });
    m_urlListDialog->show();
}

void maxchat::ui::MainWindow::openCommandHelp() {
    QMessageBox::information(
        this, QStringLiteral("Commands & Shortcuts"),
        QStringList{
            QStringLiteral("Core: /join, /part, /cycle, /msg, /query, /notice, /me, /nick, /topic, "
                           "/names, /list"),
            QStringLiteral("Info: /whois, /who, /whowas, /lag, /uptime, /netinfo, /ctcp"),
            QStringLiteral("Local: /alias, /unalias, /ignore, /unignore, /notify, /unnotify, "
                           "/mute, /unmute, /clear, /clearall, /close, /sysinfo"),
            QStringLiteral("Connection: /connect, /server, /reconnect, /disconnect, /quit, /raw, "
                           "/quote, /away, /back"),
            QStringLiteral("Shortcuts: Ctrl+S server list, Ctrl+J join, Ctrl+P preferences, Ctrl+F "
                           "find, F1 help"),
        }
            .join(QStringLiteral("\n\n")));
}

void maxchat::ui::MainWindow::openAbout() {
    QMessageBox::about(
        this, QStringLiteral("About %1").arg(app::displayName()),
        QStringLiteral("<b>%1 %2</b><br><br>"
                       "Native C++/Qt port of MaxChat.<br><br>"
                       "IRC core, server list, link previews, spellcheck, logging, and "
                       "daily chat tools are in progress. Comic mode, DCC, and media "
                       "playback are still deferred.")
            .arg(app::displayName().toHtmlEscaped(), app::version().toHtmlEscaped()));
}

namespace {
// Compare two version strings by their integer components (Python parity:
// pull out every digit-run and compare them in order). Missing trailing
// components count as 0, so "1.0" and "1" are equal. Returns true if
// `remote` is strictly newer than `local`.
bool versionIsNewer(const QString& remote, const QString& local) {
    const QRegularExpression digits(QStringLiteral("\\d+"));
    const auto parts = [&](const QString& s) {
        QList<int> out;
        auto it = digits.globalMatch(s);
        while (it.hasNext()) {
            out.append(it.next().captured(0).toInt());
        }
        return out;
    };
    const QList<int> r = parts(remote);
    const QList<int> l = parts(local);
    const int n = std::max(r.size(), l.size());
    for (int i = 0; i < n; ++i) {
        const int rv = i < r.size() ? r.at(i) : 0;
        const int lv = i < l.size() ? l.at(i) : 0;
        if (rv != lv) {
            return rv > lv;
        }
    }
    return false;
}
} // namespace

void maxchat::ui::MainWindow::checkForUpdates(bool manual) {
    QNetworkRequest req(QUrl(
        QStringLiteral("https://api.github.com/repos/IronWolve/MaxChat/releases/latest")));
    req.setRawHeader("User-Agent", "MaxChat-update-check");
    req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply* reply = m_updateNetworkManager.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual]() {
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError;
        QString latest;
        QString url = QStringLiteral("https://github.com/IronWolve/MaxChat/releases");
        bool parsed = ok;
        if (ok) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                latest = obj.value(QStringLiteral("tag_name")).toString();
                while (latest.startsWith(QLatin1Char('v')) || latest.startsWith(QLatin1Char('V'))) {
                    latest.remove(0, 1);
                }
                const QString html = obj.value(QStringLiteral("html_url")).toString();
                if (!html.isEmpty()) {
                    url = html;
                }
            } else {
                parsed = false;
            }
        }
        if (!latest.isEmpty() && versionIsNewer(latest, app::version())) {
            const auto choice = QMessageBox::question(
                this, QStringLiteral("Update available"),
                QStringLiteral("%1 v%2 is available — you have v%3.\n\nOpen the releases page?")
                    .arg(app::displayName(), latest, app::version()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (choice == QMessageBox::Yes) {
                QDesktopServices::openUrl(QUrl(url));
            }
        } else if (manual) {
            const QString msg =
                parsed ? QStringLiteral("You're on the latest version (v%1).").arg(app::version())
                       : QStringLiteral(
                             "Couldn't check for updates (no releases yet, or no connection).");
            QMessageBox::information(this, QStringLiteral("Check for Updates"), msg);
        }
    });
}

void maxchat::ui::MainWindow::handleCtcpSound(const QString& network, const QString& sender,
                                              const QString& target, const QString& file,
                                              const QString& text) {
    // A PM SOUND lands under the sender's buffer; a channel SOUND under the
    // channel (Python parity).
    const bool isPm = !isChannelTarget(target);
    const QString buffer = isPm ? sender : (target.isEmpty() ? QStringLiteral("server") : target);

    // Always show the action line — even with playback off or the file missing.
    const QString action =
        !text.isEmpty()
            ? text
            : (file.isEmpty() ? QStringLiteral("plays a sound") : QStringLiteral("plays %1").arg(file));
    appendSystemLineToNetworkTarget(network, buffer,
                                    QStringLiteral("* %1 %2").arg(sender, action));

    // Play only when the feature is on AND the named .wav already exists locally.
    const QVariantMap settings = m_settings.loadWithDefaults();
    if (file.isEmpty() || !settings.value(QStringLiteral("ctcp_sound"), false).toBool()) {
        return;
    }
    const QString soundsDir = QDir(m_settings.paths().configDir).filePath(QStringLiteral("sounds"));
    const QString path = resolveSoundPath(soundsDir, file);
    if (!path.isEmpty()) {
        m_soundPlayer.play(path);
    }
}

// --- maxchat::scripting::ScriptHost -----------------------------------------

void maxchat::ui::MainWindow::scriptEcho(const QString& network, const QString& text) {
    if (network.isEmpty() || network.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
        appendSystemLine(text);
    } else {
        appendSystemLineToNetworkTarget(network, QStringLiteral("server"), text);
    }
}

void maxchat::ui::MainWindow::scriptSay(const QString& network, const QString& target,
                                        const QString& text) {
    const QString net = network.isEmpty() ? activeNetworkName() : network;
    maxchat::irc::IrcConnection* conn = connectionForNetwork(net);
    if (conn == nullptr || target.trimmed().isEmpty() || text.isEmpty()) {
        return;
    }
    if (conn->privmsg(target, text)) {
        appendSystemLineToNetworkTarget(
            net, target, QStringLiteral("<%1> %2").arg(currentNickForNetwork(net), text), true, true);
    }
}

void maxchat::ui::MainWindow::scriptSendRaw(const QString& network, const QString& line) {
    const QString net = network.isEmpty() ? activeNetworkName() : network;
    if (maxchat::irc::IrcConnection* conn = connectionForNetwork(net); conn != nullptr) {
        conn->sendRaw(line); // sendRaw already strips CR/LF
    }
}

void maxchat::ui::MainWindow::scriptInsertInput(const QString& text) {
    if (m_input != nullptr) {
        m_input->textCursor().insertText(text);
    }
}

void maxchat::ui::MainWindow::scriptNotify(const QString& title, const QString& text) {
    notify(title, text, activeNetworkName(), m_currentTarget);
}

QString maxchat::ui::MainWindow::scriptMe(const QString& network) {
    return currentNickForNetwork(network.isEmpty() ? activeNetworkName() : network);
}

QString maxchat::ui::MainWindow::scriptTarget() {
    return m_currentTarget.trimmed().isEmpty() ? QStringLiteral("(server)") : m_currentTarget;
}

QString maxchat::ui::MainWindow::scriptNetwork() {
    return activeNetworkName();
}

QStringList maxchat::ui::MainWindow::scriptChannels(const QString& network) {
    const QString net = network.isEmpty() ? activeNetworkName() : network;
    QStringList channels;
    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffers()) {
        if (id.kind == maxchat::core::ChatBufferKind::Channel &&
            id.network.compare(net, Qt::CaseInsensitive) == 0) {
            channels.append(id.target);
        }
    }
    return channels;
}

QStringList maxchat::ui::MainWindow::scriptNicks(const QString& network, const QString& target) {
    const QString net = network.isEmpty() ? activeNetworkName() : network;
    const QString tgt = target.trimmed().isEmpty() ? m_currentTarget : target;
    return m_chatBuffers.snapshot(bufferIdForNetworkTarget(net, tgt)).members;
}

QString maxchat::ui::MainWindow::scriptHttpGet(const QString& url) {
    const QUrl parsed(url);
    if (!parsed.isValid() || (parsed.scheme() != QLatin1String("http") &&
                              parsed.scheme() != QLatin1String("https"))) {
        return {};
    }
    QNetworkRequest request(parsed);
    request.setRawHeader("User-Agent", "MaxChat-script");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_updateNetworkManager.get(request);

    // Block (with a timeout) until the request finishes — scripts opt into this
    // by enabling the network permission, and accept the synchronous wait.
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(10000);
    loop.exec();

    QString body;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        body = QString::fromUtf8(reply->readAll());
    } else if (!reply->isFinished()) {
        reply->abort();
    }
    reply->deleteLater();
    return body;
}

maxchat::scripting::ScriptPermissions maxchat::ui::MainWindow::buildScriptPermissions() const {
    const QVariantMap settings = m_settings.loadWithDefaults();
    const QVariantMap perms = settings.value(QStringLiteral("script_perms")).toMap();
    maxchat::scripting::ScriptPermissions out;
    out.readFiles = perms.value(QStringLiteral("read"), false).toBool();
    out.writeFiles = perms.value(QStringLiteral("write"), false).toBool();
    out.runPrograms = perms.value(QStringLiteral("exec"), false).toBool();
    out.loadModules = perms.value(QStringLiteral("modules"), false).toBool();
    out.network = perms.value(QStringLiteral("network"), false).toBool();
    for (const QVariant& dir : settings.value(QStringLiteral("script_dirs")).toList()) {
        const QString path = dir.toString().trimmed();
        if (!path.isEmpty()) {
            out.allowedDirs << path;
        }
    }
    return out;
}

void maxchat::ui::MainWindow::applyScriptPermissions() {
    if (m_lua == nullptr) {
        return;
    }
    const maxchat::scripting::ScriptPermissions next = buildScriptPermissions();
    if (next == m_lua->permissions()) {
        return; // nothing changed — don't disturb running scripts
    }
    m_lua->setPermissions(next);
    if (maxchat::scripting::LuaEngine::available()) {
        const QStringList loaded = m_lua->loaded();
        for (const QString& name : loaded) {
            m_lua->reload(name); // re-sandbox each script under the new capabilities
        }
        appendSystemLine(QStringLiteral("! Script permissions updated; reloaded %1 script(s).")
                             .arg(loaded.size()));
    }
}

void maxchat::ui::MainWindow::handleScriptsCommand(const QString& command, const QString& arg) {
    if (!maxchat::scripting::LuaEngine::available()) {
        appendSystemLine(QStringLiteral("! This build has no scripting support."));
        return;
    }
    const QString scriptsDir =
        QDir(m_settings.paths().configDir).filePath(QStringLiteral("scripts"));

    if (command == QStringLiteral("scripts")) {
        const QStringList names = m_lua->loaded();
        appendSystemLine(names.isEmpty()
                             ? QStringLiteral("* No scripts loaded.")
                             : QStringLiteral("* Loaded scripts: %1").arg(names.join(QStringLiteral(", "))));
        appendSystemLine(QStringLiteral("* Scripts folder: %1").arg(scriptsDir));
        return;
    }

    if (arg.isEmpty()) {
        appendSystemLine(QStringLiteral("! Usage: /%1 <script>").arg(command));
        return;
    }
    const QString name = QFileInfo(arg).completeBaseName(); // tolerate "foo" or "foo.lua"
    if (command == QStringLiteral("load")) {
        const QString path = QDir(scriptsDir).filePath(name + QStringLiteral(".lua"));
        appendSystemLine(m_lua->load(path) ? QStringLiteral("* Loaded %1.").arg(name)
                                           : QStringLiteral("! Could not load %1.").arg(name));
    } else if (command == QStringLiteral("unload")) {
        appendSystemLine(m_lua->unload(name) ? QStringLiteral("* Unloaded %1.").arg(name)
                                             : QStringLiteral("! %1 is not loaded.").arg(name));
    } else if (command == QStringLiteral("reload")) {
        appendSystemLine(m_lua->reload(name) ? QStringLiteral("* Reloaded %1.").arg(name)
                                             : QStringLiteral("! Could not reload %1.").arg(name));
    }
}

void maxchat::ui::MainWindow::seedBundledScripts(const QString& destDir) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {QDir(appDir).filePath(QStringLiteral("assets/scripts")),
                                    QDir(appDir).filePath(QStringLiteral("../assets/scripts")),
                                    QDir::current().filePath(QStringLiteral("assets/scripts"))};
    QString src;
    for (const QString& candidate : candidates) {
        if (QDir(candidate).exists()) {
            src = candidate;
            break;
        }
    }
    if (src.isEmpty()) {
        return;
    }
    const QFileInfoList examples =
        QDir(src).entryInfoList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : examples) {
        const QString dest = QDir(destDir).filePath(fi.fileName());
        if (!QFile::exists(dest)) {
            QFile::copy(fi.absoluteFilePath(), dest); // never overwrite user edits
        }
    }
}

void maxchat::ui::MainWindow::openScriptsManager() {
    if (!maxchat::scripting::LuaEngine::available()) {
        showFeaturePlanned(QStringLiteral("Scripts"),
                           QStringLiteral("This build was compiled without scripting support."));
        return;
    }
    const QString scriptsDir =
        QDir(m_settings.paths().configDir).filePath(QStringLiteral("scripts"));

    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Scripts"));
    dialog->resize(460, 380);
    auto* root = new QVBoxLayout(dialog);
    root->addWidget(new QLabel(QStringLiteral("Lua scripts in %1").arg(scriptsDir), dialog));
    auto* list = new QListWidget(dialog);
    root->addWidget(list, 1);

    const auto refresh = [this, list, scriptsDir]() {
        list->clear();
        const QStringList loaded = m_lua->loaded();
        const QFileInfoList files =
            QDir(scriptsDir).entryInfoList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
        for (const QFileInfo& fi : files) {
            const QString name = fi.completeBaseName();
            auto* item = new QListWidgetItem(
                loaded.contains(name) ? QStringLiteral("%1   [loaded]").arg(name) : name);
            item->setData(Qt::UserRole, name);
            list->addItem(item);
        }
    };
    refresh();

    const auto selectedName = [list]() -> QString {
        QListWidgetItem* item = list->currentItem();
        return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
    };

    auto* buttons = new QHBoxLayout();
    const auto addButton = [&](const QString& label, std::function<void()> handler) {
        auto* button = new QPushButton(label, dialog);
        connect(button, &QPushButton::clicked, dialog, std::move(handler));
        buttons->addWidget(button);
    };
    addButton(QStringLiteral("Load"), [this, selectedName, refresh]() {
        const QString name = selectedName();
        if (!name.isEmpty()) {
            handleScriptsCommand(QStringLiteral("load"), name);
            refresh();
        }
    });
    addButton(QStringLiteral("Unload"), [this, selectedName, refresh]() {
        const QString name = selectedName();
        if (!name.isEmpty()) {
            handleScriptsCommand(QStringLiteral("unload"), name);
            refresh();
        }
    });
    addButton(QStringLiteral("Reload"), [this, selectedName, refresh]() {
        const QString name = selectedName();
        if (!name.isEmpty()) {
            handleScriptsCommand(QStringLiteral("reload"), name);
            refresh();
        }
    });
    addButton(QStringLiteral("Open folder"),
              [scriptsDir]() { QDesktopServices::openUrl(QUrl::fromLocalFile(scriptsDir)); });
    buttons->addStretch(1);
    addButton(QStringLiteral("Close"), [dialog]() { dialog->accept(); });
    root->addLayout(buttons);
    dialog->show();
}

void maxchat::ui::MainWindow::leaveCurrentChannel() {
    const QString channel = m_currentTarget.trimmed();
    if (!connection().isConnected()) {
        appendSystemLine(QStringLiteral("! Not connected."));
        return;
    }
    if (!isChannelTarget(channel)) {
        appendSystemLine(QStringLiteral("! Select a channel before leaving."));
        return;
    }
    sendCommandOrMessage(QStringLiteral("/part %1").arg(channel));
}

void maxchat::ui::MainWindow::replayCurrentLog() {
    if (m_chatView == nullptr) {
        return;
    }

    const QString network = currentLogNetwork();
    const QString target = currentLogTarget();
    // replay_lines == 0 means "a sensible default", not "everything" — Python
    // uses 50. Dumping an entire multi-thousand-line log on every buffer open is
    // both slow and unhelpful.
    constexpr int DefaultReplayLines = 50;
    const QStringList lines = m_chatLogStore.recentLines(
        network, target, m_replayLines > 0 ? m_replayLines : DefaultReplayLines);
    if (lines.isEmpty()) {
        statusBar()->showMessage(
            QStringLiteral("No saved log lines for %1/%2.").arg(network, target));
        return;
    }

    m_replayingLog = true;

    // Replayed history is rendered dimmed (whole line in the timestamp grey, no
    // nick colours, formatting stripped) so it reads as "the past" — aligned to
    // the live chat, Python parity.
    const maxchat::core::ChatLineFormatOptions base = chatLineFormatOptions();
    const QString dim = base.timestampColor.isEmpty() ? QStringLiteral("#8a8a8a") : base.timestampColor;
    maxchat::core::ChatLineFormatOptions dimOptions = base;
    dimOptions.defaultForeground = dim;
    dimOptions.systemColor = dim;
    dimOptions.bracketColor = dim;
    dimOptions.colorNicks = false;
    dimOptions.renderFormatting = false;

    QDateTime lastWhen;
    for (const QString& line : lines) {
        const ReplayLogLine replayLine = parseReplayLogLine(line);
        if (replayLine.body.trimmed().isEmpty()) {
            continue;
        }

        maxchat::core::ChatLineFormatOptions options = dimOptions;
        if (replayLine.timestamp.isValid()) {
            options.timestamp = replayLine.timestamp.toString(qtDateTimeFormat(m_timestampFormat));
            lastWhen = replayLine.timestamp;
        }
        const maxchat::core::FormattedChatLine display =
            maxchat::core::formatChatLine(replayLine.body, options);
        appendFormattedChatLine(display);
    }

    // A dimmed divider marks where the previous session left off.
    maxchat::core::ChatLineFormatOptions ruleOptions = dimOptions;
    ruleOptions.systemLine = true;
    ruleOptions.showTimestamp = false;
    const QString when =
        lastWhen.isValid() ? lastWhen.toString(qtDateTimeFormat(m_timestampFormat)) : QString();
    const QString endLabel =
        when.isEmpty() ? QStringLiteral("--- Chat ended ---")
                       : QStringLiteral("--- Chat ended %1 ---").arg(when);
    appendFormattedChatLine(maxchat::core::formatChatLine(endLabel, ruleOptions));
    m_replayingLog = false;
    statusBar()->showMessage(
        QStringLiteral("Replayed %1 log lines for %2/%3.").arg(lines.size()).arg(network, target));
}

void maxchat::ui::MainWindow::markAllRead() {
    const int changed = m_chatBuffers.markAllReadForNetwork(currentLogNetwork());
    updateNetworkTreeLabels();
    statusBar()->showMessage(changed == 1 ? QStringLiteral("Marked 1 chat read.")
                                          : QStringLiteral("Marked %1 chats read.").arg(changed));
}

void maxchat::ui::MainWindow::clearCurrentChat() {
    if (m_chatView != nullptr) {
        m_chatView->clear();
    }
    const bool cleared = m_chatBuffers.clearLines(bufferIdForTarget(m_currentTarget));
    Q_UNUSED(cleared);
    updateNetworkTreeLabels();
    statusBar()->showMessage(QStringLiteral("Cleared current chat view."));
}

void maxchat::ui::MainWindow::clearAllChats() {
    for (const maxchat::core::ChatBufferId& id :
         m_chatBuffers.buffersForNetwork(currentLogNetwork())) {
        const bool cleared = m_chatBuffers.clearLines(id);
        Q_UNUSED(cleared);
    }
    renderActiveBuffer();
    updateNetworkTreeLabels();
    appendSystemLine(QStringLiteral("* All chats cleared."));
}

void maxchat::ui::MainWindow::showUptime() {
    QString line =
        QStringLiteral("* MaxChat uptime %1").arg(formatDurationMs(m_appUptime.elapsed()));
    const qint64 startedAt = m_connectionUptimeStartMsByNetwork.value(activeNetworkName(), 0);
    if (startedAt > 0) {
        const qint64 elapsed = std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAt);
        line += QStringLiteral(" - connected to %1 for %2")
                    .arg(m_connectionPlan.networkName, formatDurationMs(elapsed));
    } else if (m_connectionUptimeRunning) {
        line +=
            QStringLiteral(" - connected to %1 for %2")
                .arg(m_connectionPlan.networkName, formatDurationMs(m_connectionUptime.elapsed()));
    }
    appendSystemLine(line);
}

void maxchat::ui::MainWindow::showNetInfo() {
    if (!m_hasConnectionPlan) {
        appendSystemLine(QStringLiteral("* No network selected."));
        return;
    }

    const QHash<QString, QString> info = connection().isupport();
    const maxchat::irc::ServerEndpoint server =
        maxchat::irc::currentServer(m_connectionPlan.reconnect);
    const QString serverLabel = server.host.trimmed().isEmpty()
                                    ? m_connectionPlan.networkName
                                    : QStringLiteral("%1:%2").arg(server.host).arg(server.port);

    if (info.isEmpty()) {
        appendSystemLine(QStringLiteral("* %1 (%2): no ISUPPORT received yet")
                             .arg(m_connectionPlan.networkName, serverLabel));
        return;
    }

    const QStringList preferredKeys = {
        QStringLiteral("NETWORK"),    QStringLiteral("CHANTYPES"),   QStringLiteral("PREFIX"),
        QStringLiteral("CHANMODES"),  QStringLiteral("CASEMAPPING"), QStringLiteral("NICKLEN"),
        QStringLiteral("CHANNELLEN"), QStringLiteral("TOPICLEN"),    QStringLiteral("MODES"),
    };

    QStringList preferred;
    for (const QString& key : preferredKeys) {
        if (!info.contains(key)) {
            continue;
        }
        const QString value = info.value(key);
        preferred.append(value.isEmpty() ? key : QStringLiteral("%1=%2").arg(key, value));
    }

    QStringList extraKeys = info.keys();
    extraKeys.sort(Qt::CaseInsensitive);
    for (const QString& key : extraKeys) {
        if (containsCaseInsensitive(preferredKeys, key)) {
            continue;
        }
        const QString value = info.value(key);
        preferred.append(value.isEmpty() ? key : QStringLiteral("%1=%2").arg(key, value));
    }

    appendSystemLine(
        QStringLiteral("* %1 (%2): %3")
            .arg(m_connectionPlan.networkName, serverLabel, preferred.join(QStringLiteral(" - "))));
}

void maxchat::ui::MainWindow::showFeaturePlanned(const QString& feature, const QString& detail) {
    const QString cleanFeature =
        feature.trimmed().isEmpty() ? QStringLiteral("This feature") : feature.trimmed();
    QString line = QStringLiteral("! %1 is planned for the C++ port.").arg(cleanFeature);
    if (!detail.trimmed().isEmpty()) {
        line += QStringLiteral(" %1").arg(detail.trimmed());
    }
    appendSystemLine(line);
    statusBar()->showMessage(line.mid(2));
}

QString MainWindow::systemInfoText() const {
    return maxchat::ui::systemInfoLine();
}

void maxchat::ui::MainWindow::showCommandHelp(const QString& topic) {
    const QString cleanTopic = topic.trimmed().toLower();
    if (cleanTopic == QStringLiteral("services")) {
        appendSystemLine(
            QStringLiteral("! Services: /nickserv, /ns, /identify, /ghost, /chanserv, /cs, "
                           "/memoserv, /ms, /operserv, /os, /hostserv, /hs"));
        return;
    }
    if (cleanTopic == QStringLiteral("ops") || cleanTopic == QStringLiteral("operators")) {
        appendSystemLine(
            QStringLiteral("! Operator/channel tools: /op, /deop, /voice, /devoice, /halfop, "
                           "/dehalfop, /mode, /kick, /ban, /kickban, /onotice"));
        return;
    }
    if (cleanTopic == QStringLiteral("local")) {
        appendSystemLine(
            QStringLiteral("! Local tools: /alias, /unalias, /ignore, /unignore, /notify, "
                           "/unnotify, /mute, /unmute, /clear, /clearall, /close, /sysinfo"));
        return;
    }

    appendSystemLine(
        QStringLiteral("! Core: /join, /part, /cycle, /msg, /query, /notice, /me, /nick, "
                       "/topic, /names, /list"));
    appendSystemLine(
        QStringLiteral("! Info: /whois, /who, /whowas, /lag, /uptime, /netinfo, /ctcp"));
    appendSystemLine(
        QStringLiteral("! Local: /help local, /help services, /help ops, /alias, /ignore, "
                       "/notify, /mute"));
    appendSystemLine(
        QStringLiteral("! Connection/raw: /connect, /server, /reconnect, /disconnect, /quit, "
                       "/raw, /quote, /away, /back"));
}

void maxchat::ui::MainWindow::appendReplyLine(const QString& line) {
    appendReplyLineForNetwork(currentLogNetwork(), line);
}

void maxchat::ui::MainWindow::appendReplyLineForNetwork(const QString& network, const QString& line) {
    const QString label = replyTextLabel(line);
    if (label == QStringLiteral("topic") || label == QStringLiteral("channel")) {
        return;
    }

    QString target = replyTextTarget(line);
    if (target.trimmed().isEmpty()) {
        target = QStringLiteral("server");
    }
    appendSystemLineToNetworkTarget(network, target, line);
}

void maxchat::ui::MainWindow::closeTarget(const QString& target) {
    const QString cleanTarget = target.trimmed();
    if (cleanTarget.isEmpty() || isTreeStatusTarget(cleanTarget)) {
        appendSystemLine(QStringLiteral("! No channel or query is selected to close."));
        return;
    }

    forgetTarget(cleanTarget);
    if (cleanTarget.compare(m_currentTarget, Qt::CaseInsensitive) == 0) {
        activateBufferTarget({});
        updateChannelModeButton();
        showConnectionStatus(m_hasConnectionPlan
                               ? QStringLiteral("%1 - Connected").arg(m_connectionPlan.networkName)
                               : QStringLiteral("Not connected"));
    }
    rebuildNetworkTree();
    statusBar()->showMessage(QStringLiteral("Closed %1.").arg(cleanTarget));
}

void maxchat::ui::MainWindow::setServerListVisible(const bool visible, const bool save) {
    setSplitterPanelVisible(0, visible, save);
    if (save) {
        statusBar()->showMessage(visible ? QStringLiteral("Server list shown.")
                                         : QStringLiteral("Server list hidden."));
    }
}

void maxchat::ui::MainWindow::setMembersVisible(const bool visible, const bool save) {
    setSplitterPanelVisible(2, visible, save);
    if (save) {
        statusBar()->showMessage(visible ? QStringLiteral("Members list shown.")
                                         : QStringLiteral("Members list hidden."));
    }
}

void maxchat::ui::MainWindow::setButtonBarVisible(const bool visible, const bool save) {
    if (m_buttonBar != nullptr) {
        m_buttonBar->setVisible(visible);
    }
    if (m_buttonBarAction != nullptr && m_buttonBarAction->isChecked() != visible) {
        const QSignalBlocker blocker(m_buttonBarAction);
        m_buttonBarAction->setChecked(visible);
    }
    if (save) {
        saveViewVisibilitySetting(QStringLiteral("show_button_bar"), visible);
        statusBar()->showMessage(visible ? QStringLiteral("Button bar shown.")
                                         : QStringLiteral("Button bar hidden."));
    }
}

void maxchat::ui::MainWindow::setBufferTabsVisible(const bool visible, const bool save) {
    if (m_bufferTabBar != nullptr) {
        syncBufferTabs();
        m_bufferTabBar->setVisible(visible);
    }
    // Buttons-as-tabs is an alternative buffer navigator: when on it replaces the
    // server-list tree so the same "Server"/buffer rows aren't shown twice. Only act
    // on a real transition (don't disturb the splitter at startup when tabs are off
    // and the tree already shows): hide the tree when tabs go on, restore it via the
    // splitter mechanism (preserving sizing) when tabs go off.
    if (m_networkTree != nullptr) {
        if (visible && m_networkTree->isVisible()) {
            m_networkTree->setVisible(false);
        } else if (!visible && !m_networkTree->isVisible()) {
            const bool serverListPref = m_settings.loadWithDefaults()
                                            .value(QStringLiteral("server_list_visible"), true)
                                            .toBool();
            setServerListVisible(serverListPref, false);
        }
    }
    if (m_buttonsAsTabsAction != nullptr && m_buttonsAsTabsAction->isChecked() != visible) {
        const QSignalBlocker blocker(m_buttonsAsTabsAction);
        m_buttonsAsTabsAction->setChecked(visible);
    }
    if (save) {
        saveViewVisibilitySetting(QStringLiteral("buffer_tabs"), visible);
        statusBar()->showMessage(visible ? QStringLiteral("Buttons as tabs shown.")
                                         : QStringLiteral("Buttons as tabs hidden."));
    }
}

void maxchat::ui::MainWindow::setChatSeparatorVisible(const bool visible, const bool save) {
    if (m_separatorLine == visible) {
        if (m_chatSeparatorAction != nullptr && m_chatSeparatorAction->isChecked() != visible) {
            const QSignalBlocker blocker(m_chatSeparatorAction);
            m_chatSeparatorAction->setChecked(visible);
        }
        updateChatSeparatorGuide();
        return;
    }

    m_separatorLine = visible;
    if (m_chatSeparatorAction != nullptr && m_chatSeparatorAction->isChecked() != visible) {
        const QSignalBlocker blocker(m_chatSeparatorAction);
        m_chatSeparatorAction->setChecked(visible);
    }
    if (save) {
        saveViewVisibilitySetting(QStringLiteral("separator_line"), visible);
    }
    renderActiveBuffer();
    updateChatSeparatorGuide();
    statusBar()->showMessage(visible ? QStringLiteral("Chat separator shown.")
                                     : QStringLiteral("Chat separator hidden."));
}

void maxchat::ui::MainWindow::setNickColumnWidth(const int nickWidth, const bool save) {
    const int cleanWidth = std::clamp(nickWidth, 4, 40);
    if (cleanWidth == m_nickColumnWidth) {
        updateChatSeparatorGuide();
        return;
    }

    m_nickColumnWidth = cleanWidth;
    if (save) {
        QVariantMap settings = m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("nick_width"), m_nickColumnWidth);
        if (!m_settings.saveRaw(settings)) {
            statusBar()->showMessage(QStringLiteral("Could not save nick column width."));
        }
    }
    renderActiveBuffer();
    updateChatSeparatorGuide();
    statusBar()->showMessage(QStringLiteral("Nick column width: %1").arg(m_nickColumnWidth));
}

void maxchat::ui::MainWindow::setSplitterPanelVisible(const int index, const bool visible, const bool save) {
    QAction* action = nullptr;
    const QString prefKey = index == 0   ? QStringLiteral("server_list_visible")
                            : index == 2 ? QStringLiteral("member_list_visible")
                                         : QString();
    const int fallbackWidth = index == 2 ? 190 : 180;
    if (index == 0) {
        action = m_serverListVisibleAction;
    } else if (index == 2) {
        action = m_membersVisibleAction;
    }

    if (m_mainSplitter != nullptr && index >= 0 && index < m_mainSplitter->count()) {
        QList<int> sizes = m_mainSplitter->sizes();
        if (visible && sizes.at(index) <= 0) {
            const int restored = fallbackWidth;
            sizes[index] = restored;
            if (sizes.size() > 1) {
                sizes[1] = std::max(160, sizes.at(1) - restored);
            }
            m_mainSplitter->setSizes(sizes);
        } else if (!visible && sizes.at(index) > 0) {
            if (sizes.size() > 1) {
                sizes[1] += sizes.at(index);
            }
            sizes[index] = 0;
            m_mainSplitter->setSizes(sizes);
        }
    }

    if (action != nullptr && action->isChecked() != visible) {
        const QSignalBlocker blocker(action);
        action->setChecked(visible);
    }
    if (save && !prefKey.isEmpty()) {
        saveViewVisibilitySetting(prefKey, visible);
        saveSplitterSizes();
    }
}

void maxchat::ui::MainWindow::syncBufferTabs() {
    if (m_bufferTabBar == nullptr || m_networkTree == nullptr) {
        return;
    }

    // One tab per server-tree row (network roots + their channels/queries),
    // mirroring the tree's order and labels. Clicking a tab selects the
    // matching tree item, which drives all the buffer-switch logic.
    const QSignalBlocker blocker(m_bufferTabBar);
    while (m_bufferTabBar->count() > 0) {
        m_bufferTabBar->removeTab(0);
    }
    int currentIndex = -1;
    const QTreeWidgetItem* currentItem = m_networkTree->currentItem();
    for (int top = 0; top < m_networkTree->topLevelItemCount(); ++top) {
        QTreeWidgetItem* root = m_networkTree->topLevelItem(top);
        for (int child = -1; child < root->childCount(); ++child) {
            QTreeWidgetItem* item = child < 0 ? root : root->child(child);
            const int tabIndex = m_bufferTabBar->addTab(item->text(0));
            if (item == currentItem) {
                currentIndex = tabIndex;
            }
        }
    }
    if (currentIndex >= 0) {
        m_bufferTabBar->setCurrentIndex(currentIndex);
    }
}

QTreeWidgetItem* MainWindow::treeItemForTabIndex(const int index) const {
    if (m_networkTree == nullptr || index < 0) {
        return nullptr;
    }
    int seen = 0;
    for (int top = 0; top < m_networkTree->topLevelItemCount(); ++top) {
        QTreeWidgetItem* root = m_networkTree->topLevelItem(top);
        for (int child = -1; child < root->childCount(); ++child) {
            QTreeWidgetItem* item = child < 0 ? root : root->child(child);
            if (seen++ == index) {
                return item;
            }
        }
    }
    return nullptr;
}

void maxchat::ui::MainWindow::syncPanelActionsFromSplitter(const bool save) {
    if (m_mainSplitter == nullptr) {
        return;
    }

    const QList<int> sizes = m_mainSplitter->sizes();
    const bool serverVisible = sizes.size() > 0 && sizes.at(0) > 0;
    const bool membersVisible = sizes.size() > 2 && sizes.at(2) > 0;
    if (m_serverListVisibleAction != nullptr &&
        m_serverListVisibleAction->isChecked() != serverVisible) {
        const QSignalBlocker blocker(m_serverListVisibleAction);
        m_serverListVisibleAction->setChecked(serverVisible);
    }
    if (m_membersVisibleAction != nullptr &&
        m_membersVisibleAction->isChecked() != membersVisible) {
        const QSignalBlocker blocker(m_membersVisibleAction);
        m_membersVisibleAction->setChecked(membersVisible);
    }

    if (save) {
        QVariantMap settings = m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("server_list_visible"), serverVisible);
        settings.insert(QStringLiteral("member_list_visible"), membersVisible);
        QVariantList savedSizes;
        savedSizes.reserve(sizes.size());
        for (const int size : sizes) {
            savedSizes.append(size);
        }
        settings.insert(QStringLiteral("splitter_sizes"), savedSizes);
        if (!m_settings.saveRaw(settings)) {
            statusBar()->showMessage(QStringLiteral("Could not save panel layout."));
        }
    }
}

void maxchat::ui::MainWindow::updateChatSeparatorGuide() {
    auto* chatView = dynamic_cast<ChatTextView*>(m_chatView);
    if (chatView == nullptr) {
        return;
    }

    const QString timestamp = m_showTimestamps ? timestampText() : QString();
    const double timestampColumns =
        timestamp.isEmpty() ? 0.0 : static_cast<double>(timestamp.size() + 1);
    QColor color = m_chatView->palette().color(QPalette::ColorRole::Text);
    chatView->setSeparatorGuide(timestampColumns, m_nickColumnWidth,
                                m_separatorLine && m_alignNicks, color);
}

void maxchat::ui::MainWindow::saveViewVisibilitySetting(const QString& key, const bool visible) {
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(key, visible);
    if (!m_settings.saveRaw(settings)) {
        statusBar()->showMessage(QStringLiteral("Could not save view setting."));
    }
}

void maxchat::ui::MainWindow::saveSplitterSizes() {
    if (m_mainSplitter == nullptr) {
        return;
    }

    const QList<int> sizes = m_mainSplitter->sizes();
    QVariantList savedSizes;
    savedSizes.reserve(sizes.size());
    for (const int size : sizes) {
        savedSizes.append(size);
    }
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("splitter_sizes"), savedSizes);
    if (!m_settings.saveRaw(settings)) {
        statusBar()->showMessage(QStringLiteral("Could not save panel layout."));
    }
}

void maxchat::ui::MainWindow::exportSettings() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Settings"), QStringLiteral("maxchat-settings.json"),
        QStringLiteral("JSON files (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(
        QStringLiteral("networks"),
        maxchat::core::networkConfigListToVariantList(maxchat::core::networkConfigListFromVariant(
            settings.value(QStringLiteral("networks")))));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendSystemLine(QStringLiteral("! Could not open settings export file."));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromVariant(settings);
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        appendSystemLine(QStringLiteral("! Could not write settings export file."));
        return;
    }
    appendSystemLine(QStringLiteral("! Settings exported to %1.").arg(path));
}

void maxchat::ui::MainWindow::importSettings() {
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Import Settings"), QString(),
                                     QStringLiteral("JSON files (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        appendSystemLine(QStringLiteral("! Could not open settings import file."));
        return;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        appendSystemLine(QStringLiteral("! Settings import file is not valid JSON."));
        return;
    }

    const QVariantMap prepared =
        m_settings.prepareImportedSettings(document.object().toVariantMap());
    if (!m_settings.saveRaw(prepared)) {
        appendSystemLine(QStringLiteral("! Could not save imported settings."));
        return;
    }
    applyCurrentSettings();
    appendSystemLine(QStringLiteral("! Settings imported from %1.").arg(path));
}

void maxchat::ui::MainWindow::resetServerList() {
    if (!m_settings.resetServerList()) {
        appendSystemLine(QStringLiteral("! Could not reset server list."));
        return;
    }
    appendSystemLine(QStringLiteral("! Server list reset to bundled defaults."));
}

void maxchat::ui::MainWindow::saveActiveNetworkState() {
    if (!m_hasConnectionPlan) {
        return;
    }
    const QString network = activeNetworkName();
    rememberNetwork(network);
    m_connectionPlansByNetwork.insert(network, m_connectionPlan);
    m_currentTargetByNetwork.insert(network, m_currentTarget);
    m_openTargetsByNetwork.insert(network, m_openTargets);
    m_registeredByNetwork.insert(network, m_registered);
    m_initialConnectAttemptsByNetwork.insert(network, m_initialConnectAttempts);
    m_manualDisconnectByNetwork.insert(network, m_manualDisconnect);
    m_reconnectRequestedByNetwork.insert(network, m_reconnectRequested);
}

maxchat::irc::IrcConnection* MainWindow::connectionForNetwork(const QString& network) const {
    const QString normalized = network.trimmed();
    if (normalized.isEmpty()) {
        return nullptr;
    }
    return m_connectionsByNetwork.value(normalized, nullptr);
}

maxchat::irc::IrcConnection* MainWindow::ensureConnectionForNetwork(const QString& network) {
    const QString normalized = network.trimmed();
    if (normalized.isEmpty()) {
        return &m_connection;
    }

    if (auto* existing = connectionForNetwork(normalized); existing != nullptr) {
        return existing;
    }

    auto* created = new maxchat::irc::IrcConnection(this);
    created->setIgnoreMasks(m_ignoreMasks);
    const QVariantMap settings = m_settings.loadWithDefaults();
    created->setCtcpVersion(settings.value(QStringLiteral("hide_version"), false).toBool(),
                            settings.value(QStringLiteral("ctcp_version")).toString());
    m_connectionsByNetwork.insert(normalized, created);
    setupConnectionSignals(normalized, created);
    return created;
}

maxchat::irc::IrcConnection& MainWindow::connection() {
    if (auto* existing = connectionForNetwork(activeNetworkName()); existing != nullptr) {
        return *existing;
    }
    return m_connection;
}

const maxchat::irc::IrcConnection& MainWindow::connection() const {
    if (auto* existing = connectionForNetwork(activeNetworkName()); existing != nullptr) {
        return *existing;
    }
    return m_connection;
}

bool MainWindow::anyNetworkConnectionIsConnected() const {
    if (m_connection.isConnected()) {
        return true;
    }
    for (auto it = m_connectionsByNetwork.cbegin(); it != m_connectionsByNetwork.cend(); ++it) {
        if (it.value() != nullptr && it.value()->isConnected()) {
            return true;
        }
    }
    return false;
}

void maxchat::ui::MainWindow::withNetworkContext(const QString& network, const std::function<void()>& body) {
    const QString normalized = network.trimmed();
    if (normalized.isEmpty() || normalized.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
        body();
        return;
    }

    const bool hadConnectionPlan = m_hasConnectionPlan;
    const maxchat::core::NetworkConnectionPlan previousPlan = m_connectionPlan;
    const QString previousTarget = m_currentTarget;
    const QStringList previousOpenTargets = m_openTargets;
    const bool previousRegistered = m_registered;
    const bool previousBackgroundContext = m_backgroundNetworkContext;

    saveActiveNetworkState();
    rememberNetwork(normalized);
    m_hasConnectionPlan = true;
    if (m_connectionPlansByNetwork.contains(normalized)) {
        m_connectionPlan = m_connectionPlansByNetwork.value(normalized);
    } else {
        m_connectionPlan = {};
        m_connectionPlan.networkName = normalized;
    }
    m_currentTarget = m_currentTargetByNetwork.value(normalized);
    m_openTargets = m_openTargetsByNetwork.value(normalized);
    m_registered = m_registeredByNetwork.value(normalized, false);

    m_backgroundNetworkContext = true;
    body();
    m_backgroundNetworkContext = previousBackgroundContext;

    saveActiveNetworkState();
    m_hasConnectionPlan = hadConnectionPlan;
    m_connectionPlan = previousPlan;
    m_currentTarget = previousTarget;
    m_openTargets = previousOpenTargets;
    m_registered = previousRegistered;
    if (hadConnectionPlan) {
        const bool activeSet = m_chatBuffers.setActiveBuffer(
            bufferIdForNetworkTarget(activeNetworkName(), m_currentTarget));
        Q_UNUSED(activeSet);
    }
    renderActiveBuffer();
    renderActiveBufferMetadata();
    rebuildNetworkTree();
    updateChannelModeButton();
}

void maxchat::ui::MainWindow::startConfiguredStartupConnection() {
    if (m_hasConnectionPlan || anyNetworkConnectionIsConnected()) {
        return;
    }

    const bool mergedDefaults = m_settings.mergeDefaultNetworks();
    const QVariantMap settings = m_settings.loadWithDefaults();
    const auto networks =
        maxchat::core::networkConfigListFromVariant(settings.value(QStringLiteral("networks")));
    if (mergedDefaults) {
        appendSystemLine(QStringLiteral("! Server list was updated with bundled defaults."));
    }

    // Networks flagged "Connect on startup" all come up (staggered, in Server
    // List order). With none flagged, fall back to the first connectable one.
    QList<maxchat::core::NetworkConfig> autoconnects;
    for (const maxchat::core::NetworkConfig& network : networks) {
        if (network.value(QStringLiteral("autoconnect")).toBool() &&
            maxchat::core::hasConnectableServer(
                maxchat::core::connectionPlanFromNetwork(network))) {
            autoconnects.append(network);
        }
    }
    if (!autoconnects.isEmpty()) {
        int delayMs = 0;
        for (const maxchat::core::NetworkConfig& network : autoconnects) {
            QTimer::singleShot(delayMs, this, [this, network]() { startConnection(network); });
            delayMs += 1500;
        }
        return;
    }

    for (const maxchat::core::NetworkConfig& network : networks) {
        if (maxchat::core::hasConnectableServer(
                maxchat::core::connectionPlanFromNetwork(network))) {
            startConnection(network);
            return;
        }
    }

    appendSystemLine(QStringLiteral("! Auto-connect is enabled, but no server is available."));
}

void maxchat::ui::MainWindow::setupConnectionSignals() {
    setupConnectionSignals({}, &m_connection);
}

void maxchat::ui::MainWindow::setupConnectionSignals(const QString& network, maxchat::irc::IrcConnection* irc) {
    if (irc == nullptr) {
        return;
    }
    const QString signalNetwork = network.trimmed();
    const auto runInContext = [this, signalNetwork](const auto& body) {
        if (signalNetwork.isEmpty()) {
            body();
        } else {
            withNetworkContext(signalNetwork, body);
        }
    };

    connect(irc, &maxchat::irc::IrcConnection::connected, this, [this, runInContext]() {
        runInContext([&]() {
            appendSystemLineToTarget(
                QStringLiteral("server"),
                QStringLiteral("! Socket connected. Registering with IRC server..."));
            showConnectionStatus(QStringLiteral("Registering with IRC server..."));
        });
    });
    connect(irc, &maxchat::irc::IrcConnection::registered, this, [this, runInContext]() {
        runInContext([&]() {
            m_registered = true;
            m_registeredByNetwork.insert(activeNetworkName(), true);
            m_connectionUptime.start();
            m_connectionUptimeRunning = true;
            m_initialConnectAttempts = 0;
            m_initialConnectAttemptsByNetwork.insert(activeNetworkName(), 0);
            m_connectionUptimeStartMsByNetwork.insert(activeNetworkName(),
                                                      QDateTime::currentMSecsSinceEpoch());
            if (m_currentTarget.isEmpty() && !m_connectionPlan.autojoin.isEmpty()) {
                m_currentTarget = m_connectionPlan.autojoin.first();
            }
            const QString nick =
                connection().nick().isEmpty() ? m_connectionPlan.nick : connection().nick();
            appendSystemLineToTarget(
                QStringLiteral("server"),
                QStringLiteral("! Connected to %1 as %2.").arg(m_connectionPlan.networkName, nick));
            statusBar()->showMessage(
                QStringLiteral("Connected to %1 as %2").arg(m_connectionPlan.networkName, nick));
            // First connect only: widen the nick column to fit your nick (never
            // shrinks a column you've already widened; runs once). Python parity.
            if (m_alignNicks && !nick.isEmpty() &&
                !m_settings.loadWithDefaults()
                     .value(QStringLiteral("nick_width_autoset"), false)
                     .toBool()) {
                static_cast<void>(
                    m_settings.setValue(QStringLiteral("nick_width_autoset"), true));
                const int want = static_cast<int>(nick.size()) + 2; // < > around it
                if (want > m_nickColumnWidth) {
                    setNickColumnWidth(want, true);
                }
            }
            for (const QString& performLine : m_connectionPlan.perform) {
                const QString trimmedPerform = performLine.trimmed();
                if (trimmedPerform.isEmpty()) {
                    continue;
                }
                if (trimmedPerform.startsWith(QLatin1Char('/'))) {
                    sendCommandOrMessage(trimmedPerform);
                } else {
                    connection().sendRaw(trimmedPerform);
                }
            }
            showConnectionStatus(m_currentTarget.isEmpty()
                                   ? QStringLiteral("Connected to %1 as %2")
                                         .arg(m_connectionPlan.networkName, nick)
                                   : QStringLiteral("%1 - %2 as %3")
                                         .arg(m_connectionPlan.networkName, m_currentTarget, nick));
            rebuildNetworkTree();
            updateChannelModeButton();
            m_haveFriendSnapshot = false;
            pollFriends();
            if (!m_friendNicks.isEmpty()) {
                m_friendPollTimer.start();
            }
        });
    });
    connect(irc, &maxchat::irc::IrcConnection::disconnected, this,
            [this, signalNetwork](const QString& reason) {
                if (signalNetwork.isEmpty()) {
                    handleDisconnected(reason);
                } else {
                    handleDisconnected(signalNetwork, reason);
                }
            });
    connect(irc, &maxchat::irc::IrcConnection::errorOccurred, this,
            [this, runInContext](const QString& message) {
                runInContext([&]() {
                    appendSystemLineToTarget(QStringLiteral("server"),
                                             QStringLiteral("! Error: %1").arg(message));
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::systemText, this,
            [this, runInContext](const QString& line) {
                runInContext([&]() {
                    appendSystemLineToTarget(QStringLiteral("server"),
                                             QStringLiteral("! %1").arg(line));
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::rawLine, this,
            [this, signalNetwork](const QString& direction, const QString& line) {
                const QString rawLine =
                    signalNetwork.isEmpty()
                        ? QStringLiteral("%1 %2").arg(direction, line)
                        : QStringLiteral("[%1] %2 %3").arg(signalNetwork, direction, line);
                appendRawLogLine(rawLine);
            });
    connect(irc, &maxchat::irc::IrcConnection::replyText, this,
            [this, signalNetwork](const QString& line) {
                if (signalNetwork.isEmpty()) {
                    appendReplyLine(line);
                } else {
                    appendReplyLineForNetwork(signalNetwork, line);
                }
            });
    connect(
        irc, &maxchat::irc::IrcConnection::lagMeasured, this, [this, runInContext](double seconds) {
            runInContext([&]() {
                appendSystemLine(QStringLiteral("* Lag: %1 ms").arg(qRound64(seconds * 1000.0)));
            });
        });
    connect(irc, &maxchat::irc::IrcConnection::listReply, this,
            [this, runInContext](const QString& channel, int users, const QString& topic) {
                runInContext([&]() {
                    if (m_channelListDialog != nullptr) {
                        m_channelListDialog->addChannel(channel, users, topic);
                        return;
                    }
                    appendSystemLineToTarget(
                        QStringLiteral("server"),
                        topic.trimmed().isEmpty()
                            ? QStringLiteral("[list] %1 (%2 users)").arg(channel).arg(users)
                            : QStringLiteral("[list] %1 (%2 users): %3")
                                  .arg(channel)
                                  .arg(users)
                                  .arg(topic));
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::listEnd, this, [this, runInContext]() {
        runInContext([&]() {
            if (m_channelListDialog != nullptr) {
                m_channelListDialog->setComplete(true);
                return;
            }
            appendSystemLineToTarget(QStringLiteral("server"),
                                     QStringLiteral("[list] End of /LIST."));
        });
    });
    connect(irc, &maxchat::irc::IrcConnection::messageReceived, this,
            [this, runInContext, signalNetwork](const QString& sender, const QString& target,
                                                const QString& text, bool notice, bool action) {
                runInContext([&]() {
                    const QString nick =
                        connection().nick().isEmpty() ? m_connectionPlan.nick : connection().nick();
                    if (shouldDropForFlood(sender, nick)) {
                        return;
                    }
                    const bool serverNotice = notice && isLikelyServerNotice(sender, target, nick);
                    const bool privateToMe =
                        !serverNotice && target.compare(nick, Qt::CaseInsensitive) == 0;
                    const QString conversation =
                        serverNotice ? QStringLiteral("server") : (privateToMe ? sender : target);
                    const bool activeConversation =
                        conversation.compare(m_currentTarget, Qt::CaseInsensitive) == 0;
                    if (privateToMe) {
                        rememberTarget(conversation);
                    }
                    if (privateToMe && !activeConversation) {
                        const maxchat::core::ChatBufferId queryBuffer =
                            bufferIdForTarget(conversation);
                        Q_UNUSED(queryBuffer);
                        rebuildNetworkTree();
                    } else if (activeConversation) {
                        renderActiveBufferMetadata();
                    }

                    const bool highlight = sender.compare(nick, Qt::CaseInsensitive) != 0 &&
                                           textHighlightsMe(text, nick) &&
                                           !isMutedChannel(conversation);
                    if (action) {
                        appendSystemLineToTarget(conversation,
                                                 QStringLiteral("* %1 %2").arg(sender, text), true,
                                                 false, highlight, false);
                    } else if (notice) {
                        appendSystemLineToTarget(conversation,
                                                 QStringLiteral("-%1- %2").arg(sender, text), true,
                                                 false, highlight, false);
                    } else {
                        appendSystemLineToTarget(conversation,
                                                 QStringLiteral("<%1> %2").arg(sender, text), true,
                                                 false, highlight);
                    }

                    // Notify on PMs and highlights
                    if (!isActiveWindow()) {
                        if (privateToMe && m_notifyPm) {
                            const QString stripped = sender;  // stripFormatting TBD
                            QString title = QStringLiteral("Private message \u00b7 %1").arg(stripped);
                            notify(title, text, activeNetworkName(), conversation);
                        } else if (highlight && m_notifyHighlight) {
                            QString title = QStringLiteral("%1 mentioned you").arg(sender);
                            notify(title, text, activeNetworkName(), conversation);
                        }
                        if ((privateToMe || highlight) && m_beepHighlight) {
                            QApplication::beep();
                        }
                    }

                    if (!action) {
                        m_lua->dispatch(notice ? QStringLiteral("on_notice")
                                               : QStringLiteral("on_message"),
                                        signalNetwork, {signalNetwork, target, sender, text});
                    }
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::nickChanged, this,
            [this, runInContext, signalNetwork](const QString& oldNick, const QString& newNick) {
                runInContext([&]() {
                    m_lua->dispatch(QStringLiteral("on_nick"), signalNetwork,
                                    {signalNetwork, oldNick, newNick});
                    const QStringList affectedChannels = channelTargetsContainingMember(oldNick);
                    renameMemberInChannelBuffers(oldNick, newNick);
                    if (m_memberList != nullptr) {
                        if (removeMember(m_memberList, oldNick)) {
                            addMember(m_memberList, newNick);
                            memberListChanged();
                        }
                    }
                    const QString line =
                        QStringLiteral("* %1 is now known as %2").arg(oldNick, newNick);
                    if (affectedChannels.isEmpty()) {
                        appendSystemLineToTarget(QStringLiteral("server"), line);
                        return;
                    }
                    for (const QString& channel : affectedChannels) {
                        appendSystemLineToTarget(channel, line);
                    }
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::userJoined, this,
            [this, runInContext, signalNetwork](const QString& channel, const QString& nick) {
                runInContext([&]() {
                    m_lua->dispatch(QStringLiteral("on_join"), signalNetwork,
                                    {signalNetwork, channel, nick});
                    const QString currentNick =
                        connection().nick().isEmpty() ? m_connectionPlan.nick : connection().nick();
                    const bool ownJoin = nick.compare(currentNick, Qt::CaseInsensitive) == 0;
                    const maxchat::core::ChatBufferId channelBuffer = bufferIdForTarget(channel);
                    const bool memberAdded = m_chatBuffers.addMember(channelBuffer, nick);
                    Q_UNUSED(memberAdded);
                    if (ownJoin || m_currentTarget.isEmpty()) {
                        activateBufferTarget(channel);
                        const bool joinedSet = m_chatBuffers.setJoined(channelBuffer, true);
                        Q_UNUSED(joinedSet);
                        showConnectionStatus(
                            QStringLiteral("%1 - %2").arg(m_connectionPlan.networkName, channel));
                        rebuildNetworkTree();
                        updateChannelModeButton();
                        if (connection().isConnected()) {
                            connection().sendRaw(QStringLiteral("MODE %1").arg(channel));
                        }
                    }
                    if (channel.compare(m_currentTarget, Qt::CaseInsensitive) == 0) {
                        addMember(m_memberList, nick);
                        memberListChanged();
                    }
                    if (!m_hideJoinPart || ownJoin) {
                        appendSystemLineToTarget(
                            channel, QStringLiteral("* %1 joined %2").arg(nick, channel));
                    }
                });
            });
    connect(
        irc, &maxchat::irc::IrcConnection::userParted, this,
        [this, runInContext, signalNetwork](const QString& channel, const QString& nick,
                                            const QString& reason) {
            runInContext([&]() {
                m_lua->dispatch(QStringLiteral("on_part"), signalNetwork,
                                {signalNetwork, channel, nick});
                const QString currentNick =
                    connection().nick().isEmpty() ? m_connectionPlan.nick : connection().nick();
                const bool ownPart = nick.compare(currentNick, Qt::CaseInsensitive) == 0;
                const maxchat::core::ChatBufferId channelBuffer = bufferIdForTarget(channel);
                const bool memberRemoved = m_chatBuffers.removeMember(channelBuffer, nick);
                Q_UNUSED(memberRemoved);
                if (channel.compare(m_currentTarget, Qt::CaseInsensitive) == 0) {
                    removeMember(m_memberList, nick);
                    memberListChanged();
                }
                if (!m_hideJoinPart || ownPart) {
                    appendSystemLineToTarget(
                        channel,
                        reason.isEmpty()
                            ? QStringLiteral("* %1 left %2").arg(nick, channel)
                            : QStringLiteral("* %1 left %2 (%3)").arg(nick, channel, reason));
                }
                if (ownPart && channel.compare(m_currentTarget, Qt::CaseInsensitive) == 0) {
                    const bool joinedSet = m_chatBuffers.setJoined(channelBuffer, false);
                    Q_UNUSED(joinedSet);
                    forgetTarget(channel);
                    activateBufferTarget({});
                    m_pendingNamesByChannel.remove(QStringLiteral("%1/%2").arg(
                        activeNetworkName().toCaseFolded(), channel.toCaseFolded()));
                    if (m_memberList != nullptr) {
                        m_memberList->clear();
                    }
                    showConnectionStatus(
                        QStringLiteral("%1 - Connected").arg(m_connectionPlan.networkName));
                    rebuildNetworkTree();
                    updateChannelModeButton();
                }
            });
        });
    connect(irc, &maxchat::irc::IrcConnection::userQuit, this,
            [this, runInContext, signalNetwork](const QString& nick, const QString& reason) {
                runInContext([&]() {
                    m_lua->dispatch(QStringLiteral("on_quit"), signalNetwork, {signalNetwork, nick});
                    const QStringList affectedChannels = channelTargetsContainingMember(nick);
                    removeMemberFromChannelBuffers(nick);
                    removeMember(m_memberList, nick);
                    memberListChanged();
                    if (!m_hideJoinPart) {
                        const QString line =
                            reason.isEmpty() ? QStringLiteral("* %1 quit").arg(nick)
                                             : QStringLiteral("* %1 quit (%2)").arg(nick, reason);
                        if (affectedChannels.isEmpty()) {
                            appendSystemLineToTarget(QStringLiteral("server"), line);
                            return;
                        }
                        for (const QString& channel : affectedChannels) {
                            appendSystemLineToTarget(channel, line);
                        }
                    }
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::topicChanged, this,
            [this, runInContext](const QString& channel, const QString& topic) {
                runInContext([&]() {
                    const maxchat::core::ChatBufferId channelBuffer = bufferIdForTarget(channel);
                    const QString previousTopic = m_chatBuffers.snapshot(channelBuffer).topic;
                    const bool topicSet = m_chatBuffers.setTopic(channelBuffer, topic);
                    Q_UNUSED(topicSet);
                    const bool active = channel.compare(m_currentTarget, Qt::CaseInsensitive) == 0;
                    if (active) {
                        renderActiveBufferMetadata();
                    }
                    if (previousTopic == topic) {
                        return;
                    }
                    appendSystemLineToTarget(
                        channel, topic.isEmpty()
                                     ? QStringLiteral("! %1 has no topic.").arg(channel)
                                     : QStringLiteral("! Topic for %1: %2").arg(channel, topic));
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::namesReceived, this,
            [this, runInContext](const QString& channel, const QStringList& names) {
                runInContext([&]() {
                    const QString key = QStringLiteral("%1/%2").arg(
                        activeNetworkName().toCaseFolded(), channel.toCaseFolded());
                    QStringList& pendingNames = m_pendingNamesByChannel[key];
                    pendingNames.append(names);
                    const bool membersSet =
                        m_chatBuffers.setMembers(bufferIdForTarget(channel), pendingNames);
                    Q_UNUSED(membersSet);
                    if (channel.compare(m_currentTarget, Qt::CaseInsensitive) != 0) {
                        return;
                    }
                    renderActiveBufferMetadata();
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::namesEnd, this,
            [this, runInContext](const QString& channel) {
                runInContext([&]() {
                    m_pendingNamesByChannel.remove(QStringLiteral("%1/%2").arg(
                        activeNetworkName().toCaseFolded(), channel.toCaseFolded()));
                    if (channel.compare(m_currentTarget, Qt::CaseInsensitive) == 0) {
                        renderActiveBufferMetadata();
                    }
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::dccRequest, this,
            [this, runInContext](const QString& sender, const QString& args) {
                runInContext([&]() { m_dccManager->handleIncoming(sender, args); });
            });
    connect(irc, &maxchat::irc::IrcConnection::ctcpSound, this,
            [this, signalNetwork](const QString& sender, const QString& target, const QString& file,
                                  const QString& text) {
                handleCtcpSound(signalNetwork, sender, target, file, text);
            });
    connect(irc, &maxchat::irc::IrcConnection::invited, this,
            [this, runInContext](const QString& sender, const QString& channel,
                                 const QString& mask) {
                runInContext([&]() {
                    if (m_inviteProtect && !containsCaseInsensitive(m_friendNicks, sender)) {
                        // Treat invite spam from non-friends as an auto-ignore.
                        addIgnoreMask(normalizeIgnoreMask(mask.isEmpty() ? sender : mask));
                    }
                    if (m_ignoreInvites) {
                        return; // silently drop
                    }
                    appendSystemLineToTarget(
                        QStringLiteral("server"),
                        QStringLiteral("[invite] %1 invited you to %2").arg(sender, channel));
                });
            });
    connect(
        irc, &maxchat::irc::IrcConnection::kicked, this,
        [this, runInContext](const QString& channel, const QString& nick, const QString& reason) {
            runInContext([&]() {
                appendSystemLineToTarget(
                    channel, reason.isEmpty()
                                 ? QStringLiteral("* %1 was kicked from %2").arg(nick, channel)
                                 : QStringLiteral("* %1 was kicked from %2 (%3)")
                                       .arg(nick, channel, reason));
                const QString ownNick =
                    connection().nick().isEmpty() ? m_connectionPlan.nick : connection().nick();
                if (m_autoRejoin && nick.compare(ownNick, Qt::CaseInsensitive) == 0) {
                    const QString net = activeNetworkName();
                    QTimer::singleShot(std::max(0, m_rejoinDelay) * 1000, this, [this, net, channel]() {
                        withNetworkContext(net, [&]() {
                            if (connection().isConnected()) {
                                connection().sendRaw(QStringLiteral("JOIN %1").arg(channel));
                            }
                        });
                    });
                }
                if (channel.compare(m_currentTarget, Qt::CaseInsensitive) == 0) {
                    const QString currentNick =
                        connection().nick().isEmpty() ? m_connectionPlan.nick : connection().nick();
                    if (nick.compare(currentNick, Qt::CaseInsensitive) == 0) {
                        const bool joinedSet =
                            m_chatBuffers.setJoined(bufferIdForTarget(channel), false);
                        Q_UNUSED(joinedSet);
                        forgetTarget(channel);
                        activateBufferTarget({});
                        m_pendingNamesByChannel.remove(QStringLiteral("%1/%2").arg(
                            activeNetworkName().toCaseFolded(), channel.toCaseFolded()));
                        if (m_memberList != nullptr) {
                            m_memberList->clear();
                        }
                        showConnectionStatus(
                            QStringLiteral("%1 - Connected").arg(m_connectionPlan.networkName));
                        rebuildNetworkTree();
                        updateChannelModeButton();
                    } else {
                        const bool memberRemoved =
                            m_chatBuffers.removeMember(bufferIdForTarget(channel), nick);
                        Q_UNUSED(memberRemoved);
                        removeMember(m_memberList, nick);
                        memberListChanged();
                    }
                }
            });
        });
    connect(irc, &maxchat::irc::IrcConnection::channelModeIs, this,
            [this, runInContext](const QString& channel, const QString& modeLine) {
                runInContext([&]() {
                    const QString key = QStringLiteral("%1/%2").arg(
                        activeNetworkName().toCaseFolded(), channel.toCaseFolded());
                    const QString previousModeLine = m_channelModeLines.value(key);
                    m_channelModeLines.insert(key, modeLine);
                    if (previousModeLine == modeLine) {
                        return;
                    }
                    appendSystemLineToTarget(
                        channel, modeLine.isEmpty()
                                     ? QStringLiteral("! Modes for %1 are not set.").arg(channel)
                                     : QStringLiteral("! Modes for %1: %2").arg(channel, modeLine));
                });
            });
    connect(
        irc, &maxchat::irc::IrcConnection::modeChanged, this,
        [this, runInContext](const QString& target, const QString& by, const QString& modeLine) {
            runInContext([&]() {
                if (!m_showMode) {
                    return;
                }
                const QString actor = by.isEmpty() ? QStringLiteral("server") : by;
                appendSystemLineToTarget(
                    isChannelTarget(target) ? target : QStringLiteral("server"),
                    modeLine.isEmpty()
                        ? QStringLiteral("* %1 changed modes for %2").arg(actor, target)
                        : QStringLiteral("* %1 sets mode %2 on %3").arg(actor, modeLine, target));
            });
        });
    connect(irc, &maxchat::irc::IrcConnection::awayChanged, this,
            [this, signalNetwork](const QString& nick, const bool away) {
                const QString network =
                    signalNetwork.isEmpty() ? activeNetworkName() : signalNetwork;
                const QString key = nick.trimmed().toLower();
                if (key.isEmpty()) {
                    return;
                }
                QSet<QString>& awayNicks = m_awayNicksByNetwork[network];
                if (away) {
                    awayNicks.insert(key);
                } else {
                    awayNicks.remove(key);
                }
                if (network.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
                    recolorMemberList();
                }
            });
    connect(irc, &maxchat::irc::IrcConnection::isonReply, this,
            [this, signalNetwork](const QStringList& onlineNicks) {
                if (signalNetwork.isEmpty()) {
                    handleIsonReply(onlineNicks);
                } else {
                    handleIsonReplyForNetwork(signalNetwork, onlineNicks);
                }
            });
    connect(
        irc, &maxchat::irc::IrcConnection::banList, this,
        [this, runInContext](const QString& channel, const QString& mask, const QString& setter) {
            runInContext([&]() {
                if (m_banListDialog != nullptr &&
                    m_banListDialog->channel().compare(channel, Qt::CaseInsensitive) == 0) {
                    m_banListDialog->addBan(mask, setter);
                    return;
                }
                appendSystemLineToTarget(
                    QStringLiteral("server"),
                    setter.trimmed().isEmpty()
                        ? QStringLiteral("[ban] %1 %2").arg(channel, mask)
                        : QStringLiteral("[ban] %1 %2 set by %3").arg(channel, mask, setter));
            });
        });
    connect(irc, &maxchat::irc::IrcConnection::banListEnd, this,
            [this, runInContext](const QString& channel) {
                runInContext([&]() {
                    if (m_banListDialog != nullptr &&
                        m_banListDialog->channel().compare(channel, Qt::CaseInsensitive) == 0) {
                        m_banListDialog->setStatusText(
                            QStringLiteral("Ban list loaded for %1.").arg(channel));
                        return;
                    }
                    appendSystemLineToTarget(
                        QStringLiteral("server"),
                        QStringLiteral("[ban] End of ban list for %1.").arg(channel));
                });
            });
}

void maxchat::ui::MainWindow::startConnection(const maxchat::core::NetworkConfig& network) {
    saveActiveNetworkState();

    const maxchat::core::NetworkConnectionPlan plan =
        maxchat::core::connectionPlanFromNetwork(network);
    if (!maxchat::core::hasConnectableServer(plan)) {
        appendSystemLine(QStringLiteral("! Saved network has no usable server."));
        return;
    }

    const QVariantMap settings = m_settings.loadWithDefaults();
    m_autoReconnect = settings.value(QStringLiteral("auto_reconnect")).toBool();
    m_hasConnectionPlan = true;
    m_connectionPlan = plan;
    m_connectionPlansByNetwork.insert(m_connectionPlan.networkName, m_connectionPlan);
    m_registered = false;
    m_connectionUptimeRunning = false;
    m_manualDisconnect = false;
    m_initialConnectAttempts = 0;
    m_initialConnectAttemptsByNetwork.insert(m_connectionPlan.networkName, 0);
    m_manualDisconnectByNetwork.insert(m_connectionPlan.networkName, false);
    m_reconnectRequestedByNetwork.insert(m_connectionPlan.networkName, false);
    m_connectionUptimeStartMsByNetwork.remove(m_connectionPlan.networkName);
    const QString networkKeyPrefix =
        QStringLiteral("%1/").arg(m_connectionPlan.networkName.toCaseFolded());
    for (const QString& key : m_pendingNamesByChannel.keys()) {
        if (key.startsWith(networkKeyPrefix)) {
            m_pendingNamesByChannel.remove(key);
        }
    }
    for (const QString& key : m_channelModeLines.keys()) {
        if (key.startsWith(networkKeyPrefix)) {
            m_channelModeLines.remove(key);
        }
    }
    rememberNetwork(m_connectionPlan.networkName);
    maxchat::irc::IrcConnection* irc = ensureConnectionForNetwork(m_connectionPlan.networkName);
    Q_UNUSED(irc);
    const maxchat::core::ChatBufferId serverBuffer =
        m_chatBuffers.ensureServerBuffer(m_connectionPlan.networkName);
    Q_UNUSED(serverBuffer);
    m_floodGuard.clear();
    m_openTargets = m_connectionPlan.autojoin;
    for (const QString& target : m_openTargets) {
        const maxchat::core::ChatBufferId targetBuffer = bufferIdForTarget(target);
        Q_UNUSED(targetBuffer);
    }
    m_currentTarget =
        m_connectionPlan.autojoin.isEmpty() ? QString() : m_connectionPlan.autojoin.first();
    m_currentTargetByNetwork.insert(m_connectionPlan.networkName, m_currentTarget);
    m_openTargetsByNetwork.insert(m_connectionPlan.networkName, m_openTargets);
    m_registeredByNetwork.insert(m_connectionPlan.networkName, false);
    activateBufferTarget(m_currentTarget);

    rebuildNetworkTree();
    updateChannelModeButton();
    if (m_replayLogEnabled) {
        replayCurrentLog();
    }
    connectNextServer(m_connectionPlan.networkName);
}

void maxchat::ui::MainWindow::connectNextServer(const QString& network, const bool forceNext) {
    withNetworkContext(network, [this, network, forceNext]() {
        if (!m_hasConnectionPlan || !maxchat::core::hasConnectableServer(m_connectionPlan)) {
            appendSystemLine(QStringLiteral("! No server is available to connect."));
            return;
        }

        const QString signalNetwork = activeNetworkName();
        const int attempts = m_initialConnectAttemptsByNetwork.value(signalNetwork, 0);
        if (!m_registered && attempts >= maxInitialConnectAttempts(signalNetwork)) {
            appendSystemLine(QStringLiteral("! Connection attempts exhausted for %1.")
                                 .arg(m_connectionPlan.networkName));
            showConnectionStatus(QStringLiteral("Not connected"));
            return;
        }

        maxchat::core::NetworkConnectionPlan plan = m_connectionPlan;
        const maxchat::irc::ServerEndpoint server =
            maxchat::irc::chooseReconnectServer(plan.reconnect, forceNext);
        if (server.host.trimmed().isEmpty()) {
            appendSystemLine(QStringLiteral("! No server is available to connect."));
            return;
        }
        m_connectionPlan = plan;
        m_connectionPlansByNetwork.insert(signalNetwork, m_connectionPlan);

        const int nextAttempts = attempts + 1;
        m_initialConnectAttempts = nextAttempts;
        m_initialConnectAttemptsByNetwork.insert(signalNetwork, nextAttempts);
        const QString tlsText = server.tls ? QStringLiteral(" SSL/TLS") : QString();
        appendSystemLine(QStringLiteral("! Connecting to %1 (%2:%3%4), attempt %5 of %6.")
                             .arg(m_connectionPlan.networkName, server.host)
                             .arg(server.port)
                             .arg(tlsText)
                             .arg(nextAttempts)
                             .arg(maxInitialConnectAttempts(signalNetwork)));
        showConnectionStatus(
            QStringLiteral("Connecting to %1:%2...").arg(server.host).arg(server.port));

        maxchat::irc::IrcConnection* irc = ensureConnectionForNetwork(signalNetwork);
        if (irc == nullptr) {
            appendSystemLine(QStringLiteral("! Could not create network connection."));
            return;
        }
        irc->connectTo(connectConfigFor(server, m_connectionPlan));
    });
}

void maxchat::ui::MainWindow::connectNextServer(bool forceNext) {
    connectNextServer(activeNetworkName(), forceNext);
}

void maxchat::ui::MainWindow::reconnectNetwork(const QString& network) {
    withNetworkContext(network, [this]() {
        if (!m_hasConnectionPlan || !maxchat::core::hasConnectableServer(m_connectionPlan)) {
            appendSystemLine(QStringLiteral("! No saved connection is available to reconnect."));
            return;
        }

        const QString signalNetwork = activeNetworkName();
        m_initialConnectAttempts = 0;
        m_initialConnectAttemptsByNetwork.insert(signalNetwork, 0);
        m_reconnectRequested = true;
        m_reconnectRequestedByNetwork.insert(signalNetwork, true);
        maxchat::irc::IrcConnection* irc = ensureConnectionForNetwork(signalNetwork);
        if (irc != nullptr && irc->isConnected()) {
            appendSystemLine(
                QStringLiteral("! Reconnecting to %1.").arg(m_connectionPlan.networkName));
            m_manualDisconnect = true;
            m_manualDisconnectByNetwork.insert(signalNetwork, true);
            irc->disconnectFromServer();
            return;
        }

        m_reconnectRequested = false;
        m_reconnectRequestedByNetwork.insert(signalNetwork, false);
        m_manualDisconnect = false;
        m_manualDisconnectByNetwork.insert(signalNetwork, false);
        connectNextServer(signalNetwork, true);
    });
}

void maxchat::ui::MainWindow::reconnectCurrentServer() {
    reconnectNetwork(activeNetworkName());
}

void maxchat::ui::MainWindow::disconnectNetwork(const QString& network) {
    withNetworkContext(network, [this]() {
        const QString signalNetwork = activeNetworkName();
        m_manualDisconnect = true;
        m_reconnectRequested = false;
        m_manualDisconnectByNetwork.insert(signalNetwork, true);
        m_reconnectRequestedByNetwork.insert(signalNetwork, false);
        if (m_hasConnectionPlan) {
            appendSystemLine(
                QStringLiteral("! Disconnecting from %1.").arg(m_connectionPlan.networkName));
            if (auto* irc = connectionForNetwork(signalNetwork); irc != nullptr) {
                irc->disconnectFromServer();
            } else {
                connection().disconnectFromServer();
            }
        } else {
            appendSystemLine(QStringLiteral("! Not connected."));
            showConnectionStatus(QStringLiteral("Not connected"));
        }
    });
}

void maxchat::ui::MainWindow::disconnectFromCurrentServer() {
    disconnectNetwork(activeNetworkName());
}

void maxchat::ui::MainWindow::handleDisconnected(const QString& network, const QString& reason) {
    withNetworkContext(network, [this, reason]() { handleDisconnected(reason); });
}

void maxchat::ui::MainWindow::handleDisconnected(const QString& reason) {
    appendSystemLine(QStringLiteral("! Disconnected: %1").arg(reason));
    showConnectionStatus(QStringLiteral("Not connected"));
    const QString networkKeyPrefix = QStringLiteral("%1/").arg(activeNetworkName().toCaseFolded());
    for (const QString& key : m_pendingNamesByChannel.keys()) {
        if (key.startsWith(networkKeyPrefix)) {
            m_pendingNamesByChannel.remove(key);
        }
    }
    if (!anyNetworkConnectionIsConnected()) {
        m_friendPollTimer.stop();
    }
    m_onlineFriendsByNetwork.remove(activeNetworkName());
    m_haveFriendSnapshotByNetwork.insert(activeNetworkName(), false);
    m_onlineFriends.clear();
    m_haveFriendSnapshot = false;
    updateChannelModeButton();
    const bool wasRegistered = m_registered;
    m_registered = false;
    m_registeredByNetwork.insert(activeNetworkName(), false);
    m_connectionUptimeRunning = false;
    m_connectionUptimeStartMsByNetwork.remove(activeNetworkName());
    m_connectionPlansByNetwork.insert(activeNetworkName(), m_connectionPlan);

    const QString network = activeNetworkName();
    const bool reconnectRequested =
        m_reconnectRequestedByNetwork.value(network, m_reconnectRequested);
    const bool manualDisconnect = m_manualDisconnectByNetwork.value(network, m_manualDisconnect);

    if (reconnectRequested) {
        m_reconnectRequested = false;
        m_manualDisconnect = false;
        m_reconnectRequestedByNetwork.insert(network, false);
        m_manualDisconnectByNetwork.insert(network, false);
        connectNextServer(network, true);
        return;
    }

    if (manualDisconnect || !m_hasConnectionPlan) {
        return;
    }
    if (wasRegistered && !m_autoReconnect) {
        appendSystemLine(QStringLiteral("! Auto reconnect is disabled."));
        return;
    }
    if (wasRegistered) {
        m_initialConnectAttempts = 0;
        m_initialConnectAttemptsByNetwork.insert(network, 0);
        m_connectionPlan.reconnect.serverAttempt = 0;
        m_connectionPlansByNetwork.insert(network, m_connectionPlan);
        appendSystemLine(QStringLiteral("! Reconnecting to %1.").arg(m_connectionPlan.networkName));
    }
    connectNextServer(network);
}

void maxchat::ui::MainWindow::connectFromCommand(const QString& command, const QStringList& targets,
                                    const QString& text) {
    if (targets.isEmpty() || targets.first().trimmed().isEmpty()) {
        appendSystemLine(command == QStringLiteral("connect")
                             ? QStringLiteral("! Usage: /connect network-or-server "
                                              "[port] [server-password]")
                             : QStringLiteral("! Usage: /server server[:port] "
                                              "[server-password]"));
        return;
    }

    const QString firstTarget = targets.first().trimmed();
    const bool mergedDefaults = m_settings.mergeDefaultNetworks();
    Q_UNUSED(mergedDefaults);
    const QVariantMap settings = m_settings.loadWithDefaults();
    const maxchat::core::NetworkConfigList networks =
        maxchat::core::networkConfigListFromVariant(settings.value(QStringLiteral("networks")));

    const maxchat::core::NetworkConfig savedNetwork =
        findCommandNetwork(networks, firstTarget, text);
    if (!savedNetwork.isEmpty()) {
        startConnection(savedNetwork);
        return;
    }

    const CommandServerSpec serverSpec = parseCommandServerSpec(firstTarget, text);
    if (!serverSpec.valid) {
        appendSystemLine(serverSpec.errorText.startsWith(QLatin1Char('!'))
                             ? serverSpec.errorText
                             : QStringLiteral("! %1").arg(serverSpec.errorText));
        return;
    }

    const QString currentNick = !connection().nick().isEmpty() ? connection().nick()
                                : !m_connectionPlan.nick.trimmed().isEmpty()
                                    ? m_connectionPlan.nick.trimmed()
                                    : QStringLiteral("comicfan");

    maxchat::core::NetworkConfig network;
    network.insert(QStringLiteral("name"), serverSpec.host);
    network.insert(QStringLiteral("host"), serverSpec.host);
    network.insert(QStringLiteral("port"), serverSpec.port);
    network.insert(QStringLiteral("tls"), serverSpec.tls);
    network.insert(QStringLiteral("nick"), currentNick);
    network.insert(QStringLiteral("username"), m_connectionPlan.username);
    network.insert(QStringLiteral("realname"), m_connectionPlan.realname);
    network.insert(QStringLiteral("channels"), QString());
    network.insert(QStringLiteral("server_pass"), serverSpec.serverPassword);
    network.insert(QStringLiteral("servers"), QStringList{});
    startConnection(network);
}

void maxchat::ui::MainWindow::handleInputSubmitted() {
    noteUserActivity();
    const QString raw = inputText(m_input);
    const QStringList lines = raw.split(QLatin1Char('\n'));
    int lineCount = 0;
    for (const QString& line : lines) {
        if (!line.trimmed().isEmpty()) {
            ++lineCount;
        }
    }
    if (lineCount == 0) {
        return;
    }

    // Paste guard: confirm before firing a big multi-line block at the network.
    if (m_pasteGuard && lineCount >= m_pasteLines) {
        const QString where =
            m_currentTarget.trimmed().isEmpty() ? QStringLiteral("the server") : m_currentTarget;
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Paste guard"),
            QStringLiteral("Send %1 lines to %2?").arg(lineCount).arg(where));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    addInputHistory(raw.trimmed());
    m_input->clear();
    if (lines.size() <= 1) {
        sendCommandOrMessage(raw.trimmed());
    } else {
        for (const QString& line : lines) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) {
                sendCommandOrMessage(trimmed);
            }
        }
    }
}

void maxchat::ui::MainWindow::sendCommandOrMessage(const QString& text) {
    const QString aliasChannel = m_currentTarget.startsWith(QLatin1Char('#')) ||
                                         m_currentTarget.startsWith(QLatin1Char('&'))
                                     ? m_currentTarget
                                     : QString();
    const auto aliasExpansion = maxchat::core::expandCommandAliases(
        text, m_commandAliases, currentNickForNetwork(activeNetworkName()), aliasChannel);

    // /dcc is handled by the DCC manager, not the IRC command parser.
    if (aliasExpansion.commandLine.startsWith(QStringLiteral("/dcc"), Qt::CaseInsensitive)) {
        const QStringList parts =
            aliasExpansion.commandLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        handleDccCommand(parts.mid(1));
        return;
    }

    // Plain text in a "=peer" buffer is a DCC CHAT line, not an IRC message.
    if (m_currentTarget.startsWith(QLatin1Char('=')) &&
        !aliasExpansion.commandLine.startsWith(QLatin1Char('/'))) {
        const QString peer = m_currentTarget.mid(1);
        m_dccManager->sendChatLine(peer, text);
        appendSystemLineToTarget(m_currentTarget,
                                 QStringLiteral("<%1> %2").arg(currentNickForNetwork(activeNetworkName()), text),
                                 false, true, false, false);
        return;
    }

    // Give scripts first crack at any /command (after alias expansion). A
    // script's on_command returning true consumes it.
    if (aliasExpansion.commandLine.startsWith(QLatin1Char('/'))) {
        const QString afterSlash = aliasExpansion.commandLine.mid(1);
        const int space = afterSlash.indexOf(QLatin1Char(' '));
        const QString cmd = space >= 0 ? afterSlash.left(space) : afterSlash;
        const QString args = space >= 0 ? afterSlash.mid(space + 1) : QString();
        if (!cmd.isEmpty() &&
            m_lua->dispatch(QStringLiteral("on_command"), activeNetworkName(), {cmd, args})) {
            return;
        }
    }

    const auto parsed = maxchat::irc::parseUserCommand(aliasExpansion.commandLine, m_currentTarget);

    if (parsed.type == maxchat::irc::UserCommandType::Clear) {
        clearCurrentChat();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::ClearAll) {
        clearAllChats();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Close) {
        closeTarget(m_currentTarget);
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Reconnect) {
        reconnectCurrentServer();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Disconnect) {
        if (!connection().isConnected()) {
            appendSystemLine(QStringLiteral("! Not connected."));
            return;
        }
        disconnectFromCurrentServer();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Connect) {
        connectFromCommand(parsed.command, parsed.targets, parsed.text);
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Uptime) {
        showUptime();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::NetInfo) {
        showNetInfo();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Alias) {
        if (parsed.targets.isEmpty()) {
            showAliasList();
            return;
        }

        const QString name = parsed.targets.first().toLower();
        if (parsed.text.trimmed().isEmpty()) {
            const QVariant value = m_commandAliases.value(name);
            appendSystemLine(value.isValid()
                                 ? QStringLiteral("! /%1 = %2").arg(name, value.toString())
                                 : QStringLiteral("! /%1 is not defined.").arg(name));
            return;
        }
        setAliasCommand(name, parsed.text);
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Unalias) {
        removeAliasCommand(parsed.targets.first());
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Mute) {
        addMutedChannel(parsed.targets.first());
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Unmute) {
        removeMutedChannel(parsed.targets.first());
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::SysInfo) {
        const QString info = systemInfoText();
        const QStringList args =
            parsed.text.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // "/sysinfo send [#chan|nick]" broadcasts; a bare "/sysinfo" shows it to you.
        if (!args.isEmpty() && args.first().compare(QStringLiteral("send"), Qt::CaseInsensitive) == 0) {
            const QString target =
                args.size() > 1 ? args.at(1).trimmed() : m_currentTarget.trimmed();
            if (!connection().isConnected() || target.isEmpty() || isTreeStatusTarget(target)) {
                appendSystemLine(QStringLiteral("! Usage: /sysinfo send [#channel|nick] "
                                                "(needs a connection and a target)."));
                return;
            }
            if (connection().privmsg(target, info)) {
                const QString nick =
                    connection().nick().isEmpty() ? m_connectionPlan.nick : connection().nick();
                appendSystemLineToTarget(target, QStringLiteral("<%1> %2").arg(nick, info), true,
                                         true);
            } else {
                appendSystemLine(QStringLiteral("! Could not send sysinfo."));
            }
            return;
        }
        appendSystemLine(QStringLiteral("* %1").arg(info));
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Scripts) {
        handleScriptsCommand(parsed.command, parsed.text.trimmed());
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Help) {
        showCommandHelp(parsed.text);
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Error) {
        appendSystemLine(QStringLiteral("! %1").arg(parsed.errorText));
        return;
    }
    if (!connection().isConnected()) {
        appendSystemLine(QStringLiteral("! Not connected."));
        return;
    }

    const QString nick =
        connection().nick().isEmpty() ? m_connectionPlan.nick : connection().nick();

    switch (parsed.type) {
    case maxchat::irc::UserCommandType::Empty:
        return;
    case maxchat::irc::UserCommandType::Error:
        appendSystemLine(QStringLiteral("! %1").arg(parsed.errorText));
        return;
    case maxchat::irc::UserCommandType::Clear:
        clearCurrentChat();
        return;
    case maxchat::irc::UserCommandType::ClearAll:
        clearAllChats();
        return;
    case maxchat::irc::UserCommandType::Close:
        closeTarget(m_currentTarget);
        return;
    case maxchat::irc::UserCommandType::Disconnect:
        disconnectFromCurrentServer();
        return;
    case maxchat::irc::UserCommandType::Reconnect:
        reconnectCurrentServer();
        return;
    case maxchat::irc::UserCommandType::Connect:
        connectFromCommand(parsed.command, parsed.targets, parsed.text);
        return;
    case maxchat::irc::UserCommandType::Lag:
        if (connection().measureLag()) {
            appendSystemLine(QStringLiteral("* Measuring lag..."));
        } else {
            appendSystemLine(QStringLiteral("! Could not send lag probe."));
        }
        return;
    case maxchat::irc::UserCommandType::Uptime:
        showUptime();
        return;
    case maxchat::irc::UserCommandType::NetInfo:
        showNetInfo();
        return;
    case maxchat::irc::UserCommandType::Alias:
    case maxchat::irc::UserCommandType::Unalias:
    case maxchat::irc::UserCommandType::Mute:
    case maxchat::irc::UserCommandType::Unmute:
    case maxchat::irc::UserCommandType::SysInfo:
    case maxchat::irc::UserCommandType::Scripts:
    case maxchat::irc::UserCommandType::Help:
        return;
    case maxchat::irc::UserCommandType::Text:
        if (parsed.targets.isEmpty()) {
            appendSystemLine(QStringLiteral("! Join a channel or use /msg before sending text."));
            return;
        }
        if (connection().privmsg(parsed.targets.first(), parsed.text)) {
            appendSystemLineToTarget(parsed.targets.first(),
                                     QStringLiteral("<%1> %2").arg(nick, parsed.text), true, true);
        }
        return;
    case maxchat::irc::UserCommandType::Join:
        activateBufferTarget(parsed.targets.first());
        for (const QString& target : parsed.targets) {
            rememberTarget(target);
        }
        rebuildNetworkTree();
        if (!connection().sendRaw(
                QStringLiteral("JOIN %1").arg(parsed.targets.join(QLatin1Char(','))))) {
            appendSystemLine(QStringLiteral("! Could not send JOIN."));
        }
        return;
    case maxchat::irc::UserCommandType::Part: {
        const QString target = parsed.targets.first();
        QString raw = QStringLiteral("PART %1").arg(target);
        if (!parsed.text.isEmpty()) {
            raw += QStringLiteral(" :%1").arg(parsed.text);
        }
        if (connection().sendRaw(raw)) {
            forgetTarget(target);
            if (target.compare(m_currentTarget, Qt::CaseInsensitive) == 0) {
                activateBufferTarget({});
                showConnectionStatus(
                    QStringLiteral("%1 - Connected").arg(m_connectionPlan.networkName));
            }
            rebuildNetworkTree();
        }
        return;
    }
    case maxchat::irc::UserCommandType::Cycle: {
        const QString target = parsed.targets.first();
        QString raw = QStringLiteral("PART %1").arg(target);
        if (!parsed.text.isEmpty()) {
            raw += QStringLiteral(" :%1").arg(parsed.text);
        }
        if (!connection().sendRaw(raw)) {
            appendSystemLine(QStringLiteral("! Could not send PART."));
            return;
        }
        if (!connection().sendRaw(QStringLiteral("JOIN %1").arg(target))) {
            appendSystemLine(QStringLiteral("! Could not send JOIN."));
            return;
        }
        rememberTarget(target);
        activateBufferTarget(target);
        rebuildNetworkTree();
        appendSystemLineToTarget(target, QStringLiteral("! Cycling %1.").arg(target), true, true);
        return;
    }
    case maxchat::irc::UserCommandType::PrivateMessage:
        if (connection().privmsg(parsed.targets.first(), parsed.text)) {
            rememberTarget(parsed.targets.first());
            if (m_pmEcho || isChannelTarget(parsed.targets.first())) {
                appendSystemLineToTarget(parsed.targets.first(),
                                         QStringLiteral("<%1> %2").arg(nick, parsed.text), true,
                                         true);
            }
            rebuildNetworkTree();
        }
        return;
    case maxchat::irc::UserCommandType::Query:
        activateBufferTarget(parsed.targets.first());
        showConnectionStatus(QStringLiteral("%1 - %2 as %3")
                               .arg(m_connectionPlan.networkName, m_currentTarget, nick));
        rebuildNetworkTree();
        if (!parsed.text.isEmpty() && connection().privmsg(parsed.targets.first(), parsed.text) &&
            m_pmEcho) {
            appendSystemLineToTarget(parsed.targets.first(),
                                     QStringLiteral("<%1> %2").arg(nick, parsed.text), true, true);
        }
        return;
    case maxchat::irc::UserCommandType::Notice:
        if (connection().sendRaw(
                QStringLiteral("NOTICE %1 :%2").arg(parsed.targets.first(), parsed.text))) {
            appendSystemLineToTarget(
                parsed.targets.first(),
                QStringLiteral("-%1:%2- %3").arg(nick, parsed.targets.first(), parsed.text), true,
                true);
        } else {
            appendSystemLine(QStringLiteral("! Could not send NOTICE."));
        }
        return;
    case maxchat::irc::UserCommandType::BroadcastMessage: {
        const QStringList channels = joinedChannelTargets();
        if (channels.isEmpty()) {
            appendSystemLine(QStringLiteral("! No joined channels to message."));
            return;
        }
        for (const QString& channel : channels) {
            if (connection().privmsg(channel, parsed.text)) {
                appendSystemLineToTarget(
                    channel, QStringLiteral("<%1:%2> %3").arg(nick, channel, parsed.text), true,
                    true);
            } else {
                appendSystemLineToTarget(
                    channel, QStringLiteral("! Could not send message to %1.").arg(channel), true,
                    true);
            }
        }
        return;
    }
    case maxchat::irc::UserCommandType::BroadcastAction: {
        const QStringList channels = joinedChannelTargets();
        if (channels.isEmpty()) {
            appendSystemLine(QStringLiteral("! No joined channels for action."));
            return;
        }
        for (const QString& channel : channels) {
            if (connection().action(channel, parsed.text)) {
                appendSystemLineToTarget(channel, QStringLiteral("* %1 %2").arg(nick, parsed.text),
                                         true, true, false, false);
            } else {
                appendSystemLineToTarget(
                    channel, QStringLiteral("! Could not send action to %1.").arg(channel), true,
                    true);
            }
        }
        return;
    }
    case maxchat::irc::UserCommandType::OpNotice: {
        const QString channel = parsed.targets.first();
        const QString target = QStringLiteral("@%1").arg(channel);
        if (connection().sendRaw(QStringLiteral("NOTICE %1 :%2").arg(target, parsed.text))) {
            appendSystemLineToTarget(channel, QStringLiteral("-> -%1- %2").arg(target, parsed.text),
                                     true, true, false, false);
        } else {
            appendSystemLine(QStringLiteral("! Could not send op notice."));
        }
        return;
    }
    case maxchat::irc::UserCommandType::Action:
        if (connection().action(parsed.targets.first(), parsed.text)) {
            appendSystemLineToTarget(parsed.targets.first(),
                                     QStringLiteral("* %1 %2").arg(nick, parsed.text), true, true,
                                     false, false);
        }
        return;
    case maxchat::irc::UserCommandType::Ctcp:
        if (connection().ctcp(parsed.targets.first(), parsed.rawLine, parsed.text)) {
            appendSystemLineToTarget(
                parsed.targets.first(),
                parsed.text.isEmpty()
                    ? QStringLiteral("* CTCP %1 sent to %2")
                          .arg(parsed.rawLine, parsed.targets.first())
                    : QStringLiteral("* CTCP %1 %2 sent to %3")
                          .arg(parsed.rawLine, parsed.text, parsed.targets.first()),
                true, true);
        } else {
            appendSystemLine(QStringLiteral("! Could not send CTCP."));
        }
        return;
    case maxchat::irc::UserCommandType::Sound: {
        const QString soundTarget = parsed.targets.first();
        if (isTreeStatusTarget(soundTarget)) {
            appendSystemLine(QStringLiteral("! /sound needs a channel or query."));
            return;
        }
        // Send the CTCP SOUND to the room (wire parity with Python). Local
        // playback (self + received sounds) arrives with the sound subsystem
        // (audit Phase 7 / S3).
        const QString soundArgs =
            QStringLiteral("%1 %2").arg(parsed.rawLine, parsed.text).trimmed();
        if (connection().ctcp(soundTarget, QStringLiteral("SOUND"), soundArgs)) {
            appendSystemLineToTarget(
                soundTarget,
                QStringLiteral("* [sound] %1").arg(soundArgs), true, true, false,
                false);
        } else {
            appendSystemLine(QStringLiteral("! Could not send sound."));
        }
        return;
    }
    case maxchat::irc::UserCommandType::ServiceMessage:
        if (connection().privmsg(parsed.targets.first(), parsed.text)) {
            const QString redactedLine = maxchat::irc::redactLine(
                QStringLiteral("PRIVMSG %1 :%2").arg(parsed.targets.first(), parsed.text));
            const int textStart = redactedLine.indexOf(QStringLiteral(" :"));
            const QString displayText =
                textStart < 0 ? parsed.text : redactedLine.mid(textStart + 2);
            appendSystemLine(QStringLiteral("-> %1: %2").arg(parsed.targets.first(), displayText));
        } else {
            appendSystemLine(QStringLiteral("! Could not message %1.").arg(parsed.targets.first()));
        }
        return;
    case maxchat::irc::UserCommandType::Nick:
        if (!connection().sendRaw(QStringLiteral("NICK %1").arg(parsed.text))) {
            appendSystemLine(QStringLiteral("! Could not change nick."));
        }
        return;
    case maxchat::irc::UserCommandType::Whois:
        if (!connection().sendRaw(QStringLiteral("WHOIS %1").arg(parsed.targets.first()))) {
            appendSystemLine(QStringLiteral("! Could not send WHOIS."));
        }
        return;
    case maxchat::irc::UserCommandType::Who:
        if (!connection().sendRaw(QStringLiteral("WHO %1").arg(parsed.targets.first()))) {
            appendSystemLine(QStringLiteral("! Could not send WHO."));
        }
        return;
    case maxchat::irc::UserCommandType::Whowas:
        if (!connection().sendRaw(QStringLiteral("WHOWAS %1").arg(parsed.targets.first()))) {
            appendSystemLine(QStringLiteral("! Could not send WHOWAS."));
        }
        return;
    case maxchat::irc::UserCommandType::Names:
        if (!connection().sendRaw(QStringLiteral("NAMES %1").arg(parsed.targets.first()))) {
            appendSystemLine(QStringLiteral("! Could not send NAMES."));
        }
        return;
    case maxchat::irc::UserCommandType::ChannelList:
        openChannelList(true);
        if (!connection().sendRaw(parsed.text.trimmed().isEmpty()
                                      ? QStringLiteral("LIST")
                                      : QStringLiteral("LIST %1").arg(parsed.text.trimmed()))) {
            appendSystemLine(QStringLiteral("! Could not send LIST."));
            if (m_channelListDialog != nullptr) {
                m_channelListDialog->setComplete(true);
            }
        }
        return;
    case maxchat::irc::UserCommandType::Topic: {
        const QString target = parsed.targets.first();
        QString raw = QStringLiteral("TOPIC %1").arg(target);
        if (!parsed.text.isEmpty()) {
            raw += QStringLiteral(" :%1").arg(parsed.text);
        }
        if (!connection().sendRaw(raw)) {
            appendSystemLine(QStringLiteral("! Could not send TOPIC."));
        }
        return;
    }
    case maxchat::irc::UserCommandType::Mode:
        if (!connection().sendRaw(parsed.rawLine)) {
            appendSystemLine(QStringLiteral("! Could not send MODE."));
        }
        return;
    case maxchat::irc::UserCommandType::Invite:
        if (!connection().sendRaw(
                QStringLiteral("INVITE %1 %2").arg(parsed.targets.at(0), parsed.targets.at(1)))) {
            appendSystemLine(QStringLiteral("! Could not send INVITE."));
        }
        return;
    case maxchat::irc::UserCommandType::Kick: {
        QString raw = QStringLiteral("KICK %1 %2").arg(parsed.targets.at(0), parsed.targets.at(1));
        if (!parsed.text.isEmpty()) {
            raw += QStringLiteral(" :%1").arg(parsed.text);
        }
        if (!connection().sendRaw(raw)) {
            appendSystemLine(QStringLiteral("! Could not send KICK."));
        }
        return;
    }
    case maxchat::irc::UserCommandType::Ban:
        if (!connection().sendRaw(
                QStringLiteral("MODE %1 +b %2").arg(parsed.targets.at(0), parsed.targets.at(2)))) {
            appendSystemLine(QStringLiteral("! Could not send ban."));
        }
        return;
    case maxchat::irc::UserCommandType::KickBan: {
        const QString channel = parsed.targets.at(0);
        const QString nickOrMask = parsed.targets.at(1);
        const QString mask = parsed.targets.at(2);
        if (!connection().sendRaw(QStringLiteral("MODE %1 +b %2").arg(channel, mask))) {
            appendSystemLine(QStringLiteral("! Could not send ban."));
            return;
        }
        const QString reason = parsed.text.isEmpty() ? nickOrMask : parsed.text;
        if (!connection().sendRaw(
                QStringLiteral("KICK %1 %2 :%3").arg(channel, nickOrMask, reason))) {
            appendSystemLine(QStringLiteral("! Could not send KICK."));
        }
        return;
    }
    case maxchat::irc::UserCommandType::Away:
        if (!connection().sendRaw(parsed.text.isEmpty()
                                      ? QStringLiteral("AWAY")
                                      : QStringLiteral("AWAY :%1").arg(parsed.text))) {
            appendSystemLine(QStringLiteral("! Could not send AWAY."));
        }
        return;
    case maxchat::irc::UserCommandType::Ignore:
        if (parsed.text.trimmed().isEmpty()) {
            appendSystemLine(m_ignoreMasks.isEmpty()
                                 ? QStringLiteral("! Ignore list is empty.")
                                 : QStringLiteral("! Ignoring: %1")
                                       .arg(m_ignoreMasks.join(QStringLiteral(", "))));
        } else {
            addIgnoreMask(parsed.text);
        }
        return;
    case maxchat::irc::UserCommandType::Unignore:
        removeIgnoreMask(parsed.text);
        return;
    case maxchat::irc::UserCommandType::Notify:
        if (parsed.text.trimmed().isEmpty()) {
            appendSystemLine(m_friendNicks.isEmpty()
                                 ? QStringLiteral("! Notify list is empty.")
                                 : QStringLiteral("! Notify list: %1")
                                       .arg(m_friendNicks.join(QStringLiteral(", "))));
        } else {
            addFriendNick(parsed.text);
        }
        return;
    case maxchat::irc::UserCommandType::Unnotify:
        removeFriendNick(parsed.text);
        return;
    case maxchat::irc::UserCommandType::Raw:
        if (!connection().sendRaw(parsed.rawLine)) {
            appendSystemLine(QStringLiteral("! Could not send raw command."));
        }
        return;
    case maxchat::irc::UserCommandType::Quit:
        if (!parsed.text.isEmpty()) {
            connection().sendRaw(QStringLiteral("QUIT :%1").arg(parsed.text));
        } else {
            connection().sendRaw(QStringLiteral("QUIT"));
        }
        disconnectFromCurrentServer();
        return;
    }
}

void maxchat::ui::MainWindow::addInputHistory(const QString& text) {
    if (text.trimmed().isEmpty()) {
        return;
    }
    if (m_inputHistory.isEmpty() || m_inputHistory.last() != text) {
        m_inputHistory.append(text);
    }

    constexpr int MaxInputHistory = 200;
    while (m_inputHistory.size() > MaxInputHistory) {
        m_inputHistory.removeFirst();
    }
    m_inputHistoryIndex = m_inputHistory.size();
}

bool MainWindow::showHistoryEntry(int delta) {
    if (m_input == nullptr || m_inputHistory.isEmpty()) {
        return false;
    }

    if (delta < 0) {
        m_inputHistoryIndex = std::max(0, m_inputHistoryIndex - 1);
        setInputText(m_input, m_inputHistory.at(m_inputHistoryIndex));
    } else {
        if (m_inputHistoryIndex >= m_inputHistory.size() - 1) {
            m_inputHistoryIndex = m_inputHistory.size();
            m_input->clear();
        } else {
            ++m_inputHistoryIndex;
            setInputText(m_input, m_inputHistory.at(m_inputHistoryIndex));
        }
    }
    setInputCursorPosition(m_input, inputText(m_input).size());
    return true;
}

bool MainWindow::completeInput() {
    if (m_input == nullptr) {
        return false;
    }

    const QString text = inputText(m_input);
    const int cursor = inputCursorPosition(m_input);
    const QString beforeCursor = text.left(cursor);
    const int lastSpace = std::max(beforeCursor.lastIndexOf(QLatin1Char(' ')),
                                   beforeCursor.lastIndexOf(QLatin1Char('\t')));
    const int tokenStart = lastSpace < 0 ? 0 : lastSpace + 1;
    const QString prefix = beforeCursor.mid(tokenStart);
    if (prefix.isEmpty()) {
        return true;
    }

    const bool commandCompletion = prefix.startsWith(QLatin1Char('/')) && tokenStart == 0;
    const QStringList candidates = completionCandidates(commandCompletion);
    for (const QString& candidate : candidates) {
        if (!candidate.startsWith(prefix, Qt::CaseInsensitive) || candidate == prefix) {
            continue;
        }

        const bool nickStyleSuffix = !commandCompletion && tokenStart == 0 &&
                                     !candidate.startsWith(QLatin1Char('#')) &&
                                     !candidate.startsWith(QLatin1Char('&'));
        const QString suffix = nickStyleSuffix ? QStringLiteral(": ") : QStringLiteral(" ");
        const QString completed = text.left(tokenStart) + candidate + suffix + text.mid(cursor);
        setInputText(m_input, completed);
        setInputCursorPosition(m_input, tokenStart + candidate.size() + suffix.size());
        return true;
    }
    return true;
}

void maxchat::ui::MainWindow::showNetworkTreeContextMenu(const QPoint& pos) {
    if (m_networkTree == nullptr) {
        return;
    }

    QTreeWidgetItem* item = m_networkTree->itemAt(pos);
    if (item == nullptr) {
        return;
    }

    const QString target = treeItemTarget(item);
    if (target.isEmpty()) {
        return;
    }
    const QString itemNetwork = treeItemNetwork(item).trimmed().isEmpty()
                                    ? activeNetworkName()
                                    : treeItemNetwork(item).trimmed();

    QMenu menu(this);
    if (item->parent() == nullptr || !m_hasConnectionPlan || isTreeStatusTarget(target)) {
        menu.addAction(QStringLiteral("Disconnect"), this,
                       [this, itemNetwork]() { disconnectNetwork(itemNetwork); });
        menu.addAction(QStringLiteral("Reconnect Now"), this,
                       [this, itemNetwork]() { reconnectNetwork(itemNetwork); });
        menu.addAction(QStringLiteral("Server List..."), this, &MainWindow::openServerList);
        menu.addAction(QStringLiteral("Quick Connect..."), this, &MainWindow::openQuickConnect);
    } else {
        menu.addAction(QStringLiteral("Select"), this, [this, item]() {
            if (m_networkTree != nullptr) {
                m_networkTree->setCurrentItem(item);
            }
        });
        menu.addAction(QStringLiteral("Copy Name"), this,
                       [target]() { QApplication::clipboard()->setText(target); });

        if (isChannelTarget(target)) {
            menu.addSeparator();
            menu.addAction(QStringLiteral("Refresh Topic / Names / Modes"), this,
                           [this, itemNetwork, target]() {
                               setActiveNetwork(itemNetwork);
                               connection().sendRaw(QStringLiteral("TOPIC %1").arg(target));
                               connection().sendRaw(QStringLiteral("NAMES %1").arg(target));
                               connection().sendRaw(QStringLiteral("MODE %1").arg(target));
                           });
            menu.addAction(QStringLiteral("Set Topic..."), this, [this, itemNetwork, target]() {
                setActiveNetwork(itemNetwork);
                const QString currentTopic =
                    m_chatBuffers.snapshot(bufferIdForNetworkTarget(itemNetwork, target)).topic;
                bool ok = false;
                const QString topic = QInputDialog::getText(this, QStringLiteral("Set Topic"),
                                                            QStringLiteral("Topic:"),
                                                            QLineEdit::Normal, currentTopic, &ok);
                if (ok) {
                    QString raw = QStringLiteral("TOPIC %1").arg(target);
                    if (!topic.trimmed().isEmpty()) {
                        raw += QStringLiteral(" :%1").arg(topic.trimmed());
                    }
                    connection().sendRaw(raw);
                }
            });
            menu.addAction(QStringLiteral("Channel Modes..."), this, [this, itemNetwork, target]() {
                setActiveNetwork(itemNetwork);
                activateBufferTarget(target);
                rebuildNetworkTree();
                updateChannelModeButton();
                openChannelModes();
            });
            menu.addAction(QStringLiteral("Ban List..."), this, [this, itemNetwork, target]() {
                setActiveNetwork(itemNetwork);
                openBanList(target);
            });
            menu.addAction(QStringLiteral("Leave Channel"), this, [this, itemNetwork, target]() {
                setActiveNetwork(itemNetwork);
                sendCommandOrMessage(QStringLiteral("/part %1").arg(target));
            });
        }

        menu.addSeparator();
        menu.addAction(QStringLiteral("Close"), this, [this, itemNetwork, target]() {
            setActiveNetwork(itemNetwork);
            closeTarget(target);
        });
    }

    menu.exec(m_networkTree->viewport()->mapToGlobal(pos));
}

void maxchat::ui::MainWindow::showMemberContextMenu(const QPoint& pos) {
    if (m_memberList == nullptr || !connection().isConnected()) {
        return;
    }

    QListWidgetItem* item = m_memberList->itemAt(pos);
    if (item == nullptr || item->data(Qt::UserRole + 1).toBool()) {
        return;
    }

    const QString nick = nickWithoutPrefix(item->text()).trimmed();
    if (nick.isEmpty() || nick == QStringLiteral("Members")) {
        return;
    }

    QMenu menu(this);
    menu.addAction(QStringLiteral("WhoIs %1").arg(nick), this,
                   [this, nick]() { sendCommandOrMessage(QStringLiteral("/whois %1").arg(nick)); });
    menu.addAction(QStringLiteral("Message %1").arg(nick), this,
                   [this, nick]() { openQueryForNick(nick); });
    menu.addAction(QStringLiteral("Copy Nick"), this,
                   [nick]() { QApplication::clipboard()->setText(nick); });
    menu.addAction(QStringLiteral("Send File to %1...").arg(nick), this, [this, nick]() {
        configureDcc();
        const QString path =
            QFileDialog::getOpenFileName(this, QStringLiteral("Send file to %1").arg(nick));
        if (!path.isEmpty()) {
            m_dccManager->offerSend(nick, path);
        }
    });
    menu.addAction(QStringLiteral("DCC Chat with %1").arg(nick), this, [this, nick]() {
        configureDcc();
        m_dccManager->offerChat(nick);
    });
    menu.addAction(QStringLiteral("Set Color..."), this,
                   [this, nick]() { setNickColorOverride(nick); });
    if (m_nickColorOverrides.contains(nick.toLower())) {
        menu.addAction(QStringLiteral("Reset Color"), this,
                       [this, nick]() { clearNickColorOverride(nick); });
    }
    if (!m_comicCharacterPaths.isEmpty()) {
        menu.addAction(QStringLiteral("Assign Comic Character..."), this, [this, nick]() {
            QStringList stems = m_comicCharacterPaths.keys();
            std::sort(stems.begin(), stems.end());
            QStringList options;
            options << QStringLiteral("(default / random)") << stems;
            QVariantMap settings = m_settings.loadWithDefaults();
            QVariantMap chars = settings.value(QStringLiteral("comic_chars")).toMap();
            const QString current = chars.value(nick.toLower()).toString();
            const int idx =
                current.isEmpty() ? 0 : std::max(0, static_cast<int>(stems.indexOf(current)) + 1);
            bool ok = false;
            const QString choice = QInputDialog::getItem(
                this, QStringLiteral("Assign comic character"),
                QStringLiteral("Comic character for %1:").arg(nick), options, idx, false, &ok);
            if (!ok) {
                return;
            }
            if (choice == options.first()) {
                chars.remove(nick.toLower());
            } else {
                chars.insert(nick.toLower(), choice);
            }
            settings.insert(QStringLiteral("comic_chars"), chars);
            if (!m_settings.saveRaw(settings)) {
                appendSystemLine(QStringLiteral("! Could not save comic character."));
                return;
            }
            appendSystemLine(choice == options.first()
                                 ? QStringLiteral("! %1: comic character reset to default.").arg(nick)
                                 : QStringLiteral("! %1: comic character set to %2.").arg(nick, choice));
            refreshComic();
        });
    }
    const QString ignoreMask = normalizeIgnoreMask(nick);
    if (containsCaseInsensitive(m_ignoreMasks, ignoreMask)) {
        menu.addAction(QStringLiteral("Unignore %1").arg(nick), this,
                       [this, ignoreMask]() { removeIgnoreMask(ignoreMask); });
    } else {
        menu.addAction(QStringLiteral("Ignore %1").arg(nick), this,
                       [this, ignoreMask]() { addIgnoreMask(ignoreMask); });
    }
    if (containsCaseInsensitive(m_friendNicks, nick)) {
        menu.addAction(QStringLiteral("Remove %1 from Notify").arg(nick), this,
                       [this, nick]() { removeFriendNick(nick); });
    } else {
        menu.addAction(QStringLiteral("Add %1 to Notify").arg(nick), this,
                       [this, nick]() { addFriendNick(nick); });
    }

    const QString channel = m_currentTarget.trimmed();
    if (isChannelTarget(channel)) {
        menu.addSeparator();
        QMenu* operatorMenu = menu.addMenu(QStringLiteral("Operator"));
        const QList<QPair<QString, QString>> modeActions = {
            {QStringLiteral("Owner"), QStringLiteral("q")},
            {QStringLiteral("Admin"), QStringLiteral("a")},
            {QStringLiteral("Op"), QStringLiteral("o")},
            {QStringLiteral("Half-Op"), QStringLiteral("h")},
            {QStringLiteral("Voice"), QStringLiteral("v")},
        };
        for (const auto& modeAction : modeActions) {
            const QString label = modeAction.first;
            const QString mode = modeAction.second;
            operatorMenu->addAction(QStringLiteral("Give %1 (+%2)").arg(label, mode), this,
                                    [this, channel, mode, nick]() {
                                        sendModeChange(channel,
                                                       QStringLiteral("+%1 %2").arg(mode, nick));
                                    });
            operatorMenu->addAction(QStringLiteral("Take %1 (-%2)").arg(label, mode), this,
                                    [this, channel, mode, nick]() {
                                        sendModeChange(channel,
                                                       QStringLiteral("-%1 %2").arg(mode, nick));
                                    });
            operatorMenu->addSeparator();
        }

        QMenu* kickBanMenu = menu.addMenu(QStringLiteral("Kick / Ban"));
        kickBanMenu->addAction(QStringLiteral("Kick"), this,
                               [this, channel, nick]() { kickNick(channel, nick); });
        kickBanMenu->addAction(
            QStringLiteral("Kick With Reason..."), this, [this, channel, nick]() {
                bool ok = false;
                const QString reason = QInputDialog::getText(
                    this, QStringLiteral("Kick %1").arg(nick), QStringLiteral("Reason:"),
                    QLineEdit::Normal, QString(), &ok);
                if (ok) {
                    kickNick(channel, nick, reason);
                }
            });
        kickBanMenu->addAction(QStringLiteral("Ban"), this,
                               [this, channel, nick]() { banNick(channel, nick); });
        kickBanMenu->addAction(QStringLiteral("Kick + Ban"), this,
                               [this, channel, nick]() { kickBanNick(channel, nick); });
    }

    menu.addSeparator();
    QMenu* ctcpMenu = menu.addMenu(QStringLiteral("CTCP"));
    for (const QString& command : {QStringLiteral("PING"), QStringLiteral("VERSION"),
                                   QStringLiteral("TIME"), QStringLiteral("CLIENTINFO")}) {
        ctcpMenu->addAction(command, this,
                            [this, nick, command]() { connection().ctcp(nick, command); });
    }

    menu.exec(m_memberList->viewport()->mapToGlobal(pos));
}

void maxchat::ui::MainWindow::openQueryForNick(const QString& nick) {
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (cleanNick.isEmpty() || cleanNick == QStringLiteral("Members")) {
        return;
    }

    activateBufferTarget(cleanNick);
    rebuildNetworkTree();
    updateChannelModeButton();
    showConnectionStatus(
        QStringLiteral("%1 - private chat with %2").arg(m_connectionPlan.networkName, cleanNick));
}

void maxchat::ui::MainWindow::sendModeChange(const QString& channel, const QString& change) {
    if (channel.trimmed().isEmpty() || change.trimmed().isEmpty()) {
        return;
    }
    if (!connection().sendRaw(QStringLiteral("MODE %1 %2").arg(channel, change.trimmed()))) {
        appendSystemLine(QStringLiteral("! Could not send MODE."));
    }
}

void maxchat::ui::MainWindow::kickNick(const QString& channel, const QString& nick, const QString& reason) {
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (channel.trimmed().isEmpty() || cleanNick.isEmpty()) {
        return;
    }
    const QString kickReason = reason.trimmed().isEmpty() ? cleanNick : reason.trimmed();
    if (!connection().sendRaw(
            QStringLiteral("KICK %1 %2 :%3").arg(channel, cleanNick, kickReason))) {
        appendSystemLine(QStringLiteral("! Could not send KICK."));
    }
}

void maxchat::ui::MainWindow::banNick(const QString& channel, const QString& nick) {
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (channel.trimmed().isEmpty() || cleanNick.isEmpty()) {
        return;
    }
    sendModeChange(channel, QStringLiteral("+b %1!*@*").arg(cleanNick));
}

void maxchat::ui::MainWindow::kickBanNick(const QString& channel, const QString& nick) {
    banNick(channel, nick);
    kickNick(channel, nick);
}

void maxchat::ui::MainWindow::addIgnoreMask(const QString& mask) {
    const QString normalized = normalizeIgnoreMask(mask);
    if (normalized.isEmpty()) {
        return;
    }
    if (containsCaseInsensitive(m_ignoreMasks, normalized)) {
        appendSystemLine(QStringLiteral("! Already ignoring %1.").arg(normalized));
        return;
    }

    m_ignoreMasks.append(normalized);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("ignores"), m_ignoreMasks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save ignore list."));
        return;
    }
    m_connection.setIgnoreMasks(m_ignoreMasks);
    for (auto* irc : std::as_const(m_connectionsByNetwork)) {
        if (irc != nullptr) {
            irc->setIgnoreMasks(m_ignoreMasks);
        }
    }
    appendSystemLine(QStringLiteral("! Ignoring %1.").arg(normalized));
}

void maxchat::ui::MainWindow::removeIgnoreMask(const QString& mask) {
    const QString normalized = normalizeIgnoreMask(mask);
    if (normalized.isEmpty()) {
        return;
    }
    if (!containsCaseInsensitive(m_ignoreMasks, normalized)) {
        appendSystemLine(QStringLiteral("! %1 is not in the ignore list.").arg(normalized));
        return;
    }

    m_ignoreMasks = removeCaseInsensitive(m_ignoreMasks, normalized);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("ignores"), m_ignoreMasks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save ignore list."));
        return;
    }
    m_connection.setIgnoreMasks(m_ignoreMasks);
    for (auto* irc : std::as_const(m_connectionsByNetwork)) {
        if (irc != nullptr) {
            irc->setIgnoreMasks(m_ignoreMasks);
        }
    }
    appendSystemLine(QStringLiteral("! No longer ignoring %1.").arg(normalized));
}

void maxchat::ui::MainWindow::addMutedChannel(const QString& channel) {
    const QString key = mutedChannelKey(channel);
    if (key.isEmpty()) {
        appendSystemLine(QStringLiteral("! Usage: /mute [#channel]"));
        return;
    }
    if (containsCaseInsensitive(m_mutedChannelKeys, key)) {
        appendSystemLine(
            QStringLiteral("! Highlights are already muted for %1.").arg(channel.trimmed()));
        return;
    }

    QStringList next = m_mutedChannelKeys;
    next.append(key);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("muted_channels"), next);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save muted channels."));
        return;
    }

    m_mutedChannelKeys = next;
    appendSystemLine(QStringLiteral("! Muted highlights for %1 on %2.")
                         .arg(channel.trimmed(), activeNetworkName()));
}

void maxchat::ui::MainWindow::removeMutedChannel(const QString& channel) {
    const QString key = mutedChannelKey(channel);
    if (key.isEmpty()) {
        appendSystemLine(QStringLiteral("! Usage: /unmute [#channel]"));
        return;
    }
    if (!containsCaseInsensitive(m_mutedChannelKeys, key)) {
        appendSystemLine(
            QStringLiteral("! Highlights are not muted for %1.").arg(channel.trimmed()));
        return;
    }

    const QStringList next = removeCaseInsensitive(m_mutedChannelKeys, key);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("muted_channels"), next);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save muted channels."));
        return;
    }

    m_mutedChannelKeys = next;
    appendSystemLine(QStringLiteral("! Unmuted highlights for %1 on %2.")
                         .arg(channel.trimmed(), activeNetworkName()));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_confirmQuit && anyNetworkConnectionIsConnected()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Quit MaxChat"),
            QStringLiteral("You are still connected. Quit anyway?"));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::noteUserActivity() {
    // Any typed input resets the idle clock and clears an auto-set away.
    if (m_autoAwayMins > 0) {
        m_autoAwayTimer.start(m_autoAwayMins * 60 * 1000);
    }
    if (m_autoAwayActive) {
        m_autoAwayActive = false;
        for (auto* irc : std::as_const(m_connectionsByNetwork)) {
            if (irc != nullptr && irc->isConnected()) {
                irc->sendRaw(QStringLiteral("AWAY"));
            }
        }
    }
}

void MainWindow::triggerAutoAway() {
    if (m_autoAwayMins <= 0 || m_autoAwayActive || !anyNetworkConnectionIsConnected()) {
        return;
    }
    m_autoAwayActive = true;
    for (auto* irc : std::as_const(m_connectionsByNetwork)) {
        if (irc != nullptr && irc->isConnected()) {
            irc->sendRaw(QStringLiteral("AWAY :Auto-away (idle)"));
        }
    }
}

void MainWindow::applyCtcpVersion(const QVariantMap& settings) {
    const bool hide = settings.value(QStringLiteral("hide_version"), false).toBool();
    const QString custom = settings.value(QStringLiteral("ctcp_version")).toString();
    for (auto* irc : std::as_const(m_connectionsByNetwork)) {
        if (irc != nullptr) {
            irc->setCtcpVersion(hide, custom);
        }
    }
    m_connection.setCtcpVersion(hide, custom);
}

bool MainWindow::textHighlightsMe(const QString& text, const QString& nick) const {
    // Match against the plain text (Python strips mIRC codes first) so a colour
    // or bold code adjacent to / inside your nick can't hide a highlight.
    const QString plain = maxchat::irc::stripFormatting(text);
    if (!nick.isEmpty() && plain.contains(nick, Qt::CaseInsensitive)) {
        return true;
    }
    for (const QString& word : m_highlightWords) {
        const QString trimmed = word.trimmed();
        if (!trimmed.isEmpty() && plain.contains(trimmed, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool MainWindow::isMutedChannel(const QString& channel) const {
    const QString key = mutedChannelKey(channel);
    return !key.isEmpty() && containsCaseInsensitive(m_mutedChannelKeys, key);
}

QString MainWindow::mutedChannelKey(const QString& channel) const {
    const QString cleanChannel = channel.trimmed();
    if (!isChannelTarget(cleanChannel)) {
        return {};
    }
    return QStringLiteral("%1/%2").arg(activeNetworkName().toCaseFolded(),
                                       cleanChannel.toCaseFolded());
}

void maxchat::ui::MainWindow::showAliasList() {
    if (m_commandAliases.isEmpty()) {
        appendSystemLine(QStringLiteral("! No command aliases are defined."));
        return;
    }

    QStringList keys = m_commandAliases.keys();
    keys.sort(Qt::CaseInsensitive);
    appendSystemLine(QStringLiteral("! Command aliases:"));
    for (const QString& key : keys) {
        appendSystemLine(
            QStringLiteral("! /%1 = %2").arg(key, m_commandAliases.value(key).toString()));
    }
}

void maxchat::ui::MainWindow::setAliasCommand(const QString& name, const QString& expansion) {
    QString cleanName = name.trimmed().toLower();
    if (cleanName.startsWith(QLatin1Char('/'))) {
        cleanName.remove(0, 1);
    }
    const QString cleanExpansion = expansion.trimmed();
    if (cleanName.isEmpty() || cleanExpansion.isEmpty()) {
        appendSystemLine(QStringLiteral("! Usage: /alias name command"));
        return;
    }

    m_commandAliases.insert(cleanName, cleanExpansion);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("command_aliases"), m_commandAliases);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save command aliases."));
        return;
    }
    appendSystemLine(QStringLiteral("! Alias /%1 = %2").arg(cleanName, cleanExpansion));
}

void maxchat::ui::MainWindow::removeAliasCommand(const QString& name) {
    QString cleanName = name.trimmed().toLower();
    if (cleanName.startsWith(QLatin1Char('/'))) {
        cleanName.remove(0, 1);
    }
    if (cleanName.isEmpty()) {
        appendSystemLine(QStringLiteral("! Usage: /unalias name"));
        return;
    }
    if (!m_commandAliases.contains(cleanName)) {
        appendSystemLine(QStringLiteral("! Alias /%1 is not defined.").arg(cleanName));
        return;
    }

    m_commandAliases.remove(cleanName);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("command_aliases"), m_commandAliases);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save command aliases."));
        return;
    }
    appendSystemLine(QStringLiteral("! Removed alias /%1.").arg(cleanName));
}

void maxchat::ui::MainWindow::addFriendNick(const QString& nick) {
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (cleanNick.isEmpty()) {
        return;
    }
    if (containsCaseInsensitive(m_friendNicks, cleanNick)) {
        appendSystemLine(QStringLiteral("! %1 is already on the notify list.").arg(cleanNick));
        return;
    }

    m_friendNicks.append(cleanNick);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("friends"), m_friendNicks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save notify list."));
        return;
    }
    m_haveFriendSnapshot = false;
    m_haveFriendSnapshotByNetwork.clear();
    pollFriends();
    if (anyNetworkConnectionIsConnected()) {
        m_friendPollTimer.start();
    }
    appendSystemLine(QStringLiteral("! Added %1 to the notify list.").arg(cleanNick));
}

void maxchat::ui::MainWindow::removeFriendNick(const QString& nick) {
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (cleanNick.isEmpty()) {
        return;
    }
    if (!containsCaseInsensitive(m_friendNicks, cleanNick)) {
        appendSystemLine(QStringLiteral("! %1 is not on the notify list.").arg(cleanNick));
        return;
    }

    m_friendNicks = removeCaseInsensitive(m_friendNicks, cleanNick);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("friends"), m_friendNicks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save notify list."));
        return;
    }
    m_onlineFriends.remove(cleanNick.toCaseFolded());
    for (auto it = m_onlineFriendsByNetwork.begin(); it != m_onlineFriendsByNetwork.end(); ++it) {
        it.value().remove(cleanNick.toCaseFolded());
    }
    m_haveFriendSnapshot = false;
    m_haveFriendSnapshotByNetwork.clear();
    if (m_friendNicks.isEmpty()) {
        m_friendPollTimer.stop();
    } else {
        pollFriends();
    }
    appendSystemLine(QStringLiteral("! Removed %1 from the notify list.").arg(cleanNick));
}

void maxchat::ui::MainWindow::pollFriends() {
    if (m_friendNicks.isEmpty()) {
        return;
    }
    const QString raw = QStringLiteral("ISON %1").arg(m_friendNicks.join(QLatin1Char(' ')));
    bool sent = false;
    if (m_connection.isConnected()) {
        m_connection.sendRaw(raw);
        sent = true;
    }
    for (auto it = m_connectionsByNetwork.cbegin(); it != m_connectionsByNetwork.cend(); ++it) {
        if (it.value() != nullptr && it.value()->isConnected()) {
            it.value()->sendRaw(raw);
            sent = true;
        }
    }
    if (!sent) {
        m_friendPollTimer.stop();
    }
}

void maxchat::ui::MainWindow::handleIsonReply(const QStringList& onlineNicks) {
    handleIsonReplyForNetwork(currentLogNetwork(), onlineNicks);
}

void maxchat::ui::MainWindow::handleIsonReplyForNetwork(const QString& network, const QStringList& onlineNicks) {
    QSet<QString> nowOnline;
    for (const QString& nick : onlineNicks) {
        if (!nick.trimmed().isEmpty()) {
            nowOnline.insert(nick.toCaseFolded());
        }
    }

    const QString cleanNetwork =
        network.trimmed().isEmpty() ? QStringLiteral("Server") : network.trimmed();
    if (!m_haveFriendSnapshotByNetwork.value(cleanNetwork, false)) {
        m_onlineFriends = nowOnline;
        m_onlineFriendsByNetwork.insert(cleanNetwork, nowOnline);
        m_haveFriendSnapshot = true;
        m_haveFriendSnapshotByNetwork.insert(cleanNetwork, true);
        QStringList onlineDisplay;
        for (const QString& friendNick : m_friendNicks) {
            if (nowOnline.contains(friendNick.toCaseFolded())) {
                onlineDisplay.append(friendNick);
            }
        }
        if (!onlineDisplay.isEmpty()) {
            appendSystemLineToNetworkTarget(cleanNetwork, QStringLiteral("server"),
                                            QStringLiteral("! Notify online: %1")
                                                .arg(onlineDisplay.join(QStringLiteral(", "))));
        }
        return;
    }

    const QSet<QString> previousOnline = m_onlineFriendsByNetwork.value(cleanNetwork);
    for (const QString& friendNick : m_friendNicks) {
        const QString key = friendNick.toCaseFolded();
        const bool online = nowOnline.contains(key);
        const bool wasOnline = previousOnline.contains(key);
        if (online && !wasOnline) {
            appendSystemLineToNetworkTarget(cleanNetwork, QStringLiteral("server"),
                                            QStringLiteral("! %1 is online.").arg(friendNick));
        } else if (!online && wasOnline) {
            appendSystemLineToNetworkTarget(cleanNetwork, QStringLiteral("server"),
                                            QStringLiteral("! %1 went offline.").arg(friendNick));
        }
    }
    m_onlineFriends = nowOnline;
    m_onlineFriendsByNetwork.insert(cleanNetwork, nowOnline);
}

bool MainWindow::shouldDropForFlood(const QString& sender, const QString& ownNick) {
    const QString cleanSender = nickWithoutPrefix(sender).trimmed();
    if (cleanSender.isEmpty() || cleanSender.compare(ownNick, Qt::CaseInsensitive) == 0 ||
        containsCaseInsensitive(m_friendNicks, cleanSender)) {
        return false;
    }

    const QString key =
        QStringLiteral("%1/%2").arg(currentLogNetwork(), cleanSender.toCaseFolded());
    if (!m_floodGuard.recordMessage(key, QDateTime::currentMSecsSinceEpoch())) {
        return false;
    }

    addIgnoreMask(cleanSender);
    appendSystemLine(QStringLiteral("! Auto-ignored %1 for flooding. Use /unignore %1 to undo.")
                         .arg(cleanSender));
    return true;
}

bool MainWindow::findInChat(const QString& text, bool backwards, bool caseSensitive,
                            bool wrapSearch) {
    const QString needle = text.trimmed();
    if (m_chatView == nullptr || needle.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Enter text to find."));
        return false;
    }

    QTextDocument::FindFlags flags;
    if (backwards) {
        flags |= QTextDocument::FindBackward;
    }
    if (caseSensitive) {
        flags |= QTextDocument::FindCaseSensitively;
    }

    const QTextCursor originalCursor = m_chatView->textCursor();
    if (m_chatView->find(needle, flags)) {
        statusBar()->showMessage(QStringLiteral("Found \"%1\".").arg(needle));
        return true;
    }

    if (wrapSearch) {
        QTextCursor cursor(m_chatView->document());
        if (backwards) {
            cursor.movePosition(QTextCursor::End);
        } else {
            cursor.movePosition(QTextCursor::Start);
        }
        m_chatView->setTextCursor(cursor);
        if (m_chatView->find(needle, flags)) {
            statusBar()->showMessage(QStringLiteral("Found \"%1\".").arg(needle));
            return true;
        }
    }

    m_chatView->setTextCursor(originalCursor);
    statusBar()->showMessage(QStringLiteral("No matches for \"%1\".").arg(needle));
    return false;
}

QStringList MainWindow::completionCandidates(bool commandCompletion) const {
    QStringList candidates;
    QSet<QString> seen;
    const auto appendUnique = [&candidates, &seen](const QString& value) {
        const QString trimmed = value.trimmed();
        if (trimmed.isEmpty()) {
            return;
        }
        const QString key = trimmed.toCaseFolded();
        if (seen.contains(key)) {
            return;
        }
        seen.insert(key);
        candidates.append(trimmed);
    };

    if (commandCompletion) {
        for (const QString& command : maxchat::irc::supportedCommandNames()) {
            appendUnique(QStringLiteral("/%1").arg(command));
        }
        for (auto it = m_commandAliases.cbegin(); it != m_commandAliases.cend(); ++it) {
            appendUnique(QStringLiteral("/%1").arg(it.key()));
        }
        return candidates;
    }

    appendUnique(m_currentTarget);
    for (const QString& target : m_openTargets) {
        appendUnique(target);
    }
    for (const QString& channel : m_connectionPlan.autojoin) {
        appendUnique(channel);
    }
    if (m_memberList != nullptr) {
        for (int row = 0; row < m_memberList->count(); ++row) {
            const QListWidgetItem* item = m_memberList->item(row);
            if (item == nullptr || item->text() == QStringLiteral("Members")) {
                continue;
            }
            appendUnique(nickWithoutPrefix(item->text()));
        }
    }
    return candidates;
}

maxchat::core::ChatLineFormatOptions MainWindow::chatLineFormatOptions() const {
    maxchat::core::ChatLineFormatOptions options;
    options.showTimestamp = m_showTimestamps;
    options.alignNicks = m_alignNicks;
    options.separatorLine = m_separatorLine;
    options.renderFormatting = m_showFormatting;
    options.colorNicks = m_coloredNicks;
    options.nickColumnWidth = m_nickColumnWidth;
    options.timestamp = timestampText();

    const maxchat::ui::ChatThemeDefinition chatTheme = chatThemeById(m_currentChatTheme);
    if (chatTheme.timestamp.isValid()) {
        options.timestampColor = chatTheme.timestamp.name();
    } else {
        QColor chatBg = chatTheme.bg;
        if (chatTheme.id == QStringLiteral("follow")) {
            const maxchat::ui::AppThemeDefinition appTheme = appThemeById(m_currentTheme);
            chatBg = appTheme.chatBg.isValid() ? appTheme.chatBg : appTheme.panel;
        }
        const bool darkChat =
            !chatBg.isValid() ||
            (0.299 * chatBg.red() + 0.587 * chatBg.green() + 0.114 * chatBg.blue()) < 150.0;
        options.timestampColor =
            darkChat ? QStringLiteral("#8a8a8a") : QStringLiteral("#6f6f6f");
    }
    if (chatTheme.bracket.isValid()) {
        options.bracketColor = chatTheme.bracket.name();
    }
    if (chatTheme.system.isValid()) {
        options.systemColor = chatTheme.system.name();
    }
    if (!m_eventColor.isEmpty()) {
        options.systemColor = m_eventColor; // Fonts-page override wins
    }
    options.monoNicks = chatTheme.monoNicks;
    for (const QColor& color : chatTheme.nickPalette) {
        options.nickPalette.append(color.name());
    }
    for (auto it = m_nickColorOverrides.constBegin(); it != m_nickColorOverrides.constEnd();
         ++it) {
        options.nickColorOverrides.insert(it.key(), it.value().toString());
    }
    return options;
}

QString MainWindow::timestampText() const {
    return QDateTime::currentDateTime().toString(qtDateTimeFormat(m_timestampFormat));
}

void maxchat::ui::MainWindow::appendPlainChatLine(const QString& line) {
    if (m_chatView == nullptr) {
        return;
    }

    QTextCursor cursor = m_chatView->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat blockFormat;
    if (!m_chatView->document()->isEmpty()) {
        cursor.insertBlock(blockFormat);
    } else {
        cursor.setBlockFormat(blockFormat);
    }
    cursor.insertText(line);
    m_chatView->setTextCursor(cursor);
    m_chatView->ensureCursorVisible();
}

void maxchat::ui::MainWindow::appendHtmlChatLine(const QString& html) {
    if (m_chatView == nullptr) {
        return;
    }

    QTextCursor cursor = m_chatView->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat blockFormat;
    if (!m_chatView->document()->isEmpty()) {
        cursor.insertBlock(blockFormat);
    } else {
        cursor.setBlockFormat(blockFormat);
    }
    cursor.insertHtml(html);
    m_chatView->setTextCursor(cursor);
    m_chatView->ensureCursorVisible();
}

void maxchat::ui::MainWindow::appendFormattedChatLine(const maxchat::core::FormattedChatLine& line) {
    if (m_chatView == nullptr) {
        return;
    }

    QTextCursor cursor = m_chatView->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextBlockFormat blockFormat;
    if (m_indentWrap && line.hangingIndent && !line.prefixPlain.isEmpty()) {
        const int indent = QFontMetrics(m_chatView->font()).horizontalAdvance(line.prefixPlain);
        if (indent > 0) {
            blockFormat.setLeftMargin(indent);
            blockFormat.setTextIndent(-indent);
        }
    }

    if (!m_chatView->document()->isEmpty()) {
        cursor.insertBlock(blockFormat);
    } else {
        cursor.setBlockFormat(blockFormat);
    }
    cursor.insertHtml(line.html);
    cursor.mergeBlockFormat(blockFormat);
    m_chatView->setTextCursor(cursor);
    m_chatView->ensureCursorVisible();
}

void maxchat::ui::MainWindow::appendPreviewHtmlLine(const QString& html) {
    if (m_chatView == nullptr) {
        return;
    }

    QTextCursor cursor = m_chatView->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextBlockFormat blockFormat;
    const maxchat::core::FormattedChatLine column =
        maxchat::core::formatChatLine(QString(), chatLineFormatOptions());
    if (!column.prefixPlain.isEmpty()) {
        const int indent = QFontMetrics(m_chatView->font()).horizontalAdvance(column.prefixPlain);
        if (indent > 0) {
            blockFormat.setLeftMargin(indent);
        }
    }

    if (!m_chatView->document()->isEmpty()) {
        cursor.insertBlock(blockFormat);
    } else {
        cursor.setBlockFormat(blockFormat);
    }
    cursor.insertHtml(html);
    cursor.mergeBlockFormat(blockFormat);
    m_chatView->setTextCursor(cursor);
    m_chatView->ensureCursorVisible();
}

void maxchat::ui::MainWindow::appendPreviewHtml(const QString& html) {
    if (html.isEmpty()) {
        return;
    }
    appendPreviewHtmlToNetworkTarget(currentLogNetwork(), m_currentTarget, html);
}

void maxchat::ui::MainWindow::appendPreviewHtmlToNetworkTarget(const QString& network, const QString& target,
                                                  const QString& html) {
    if (html.isEmpty()) {
        return;
    }

    maxchat::core::ChatBufferLine line;
    line.htmlText = html;
    line.localEcho = true;
    const bool stored = m_chatBuffers.appendLine(bufferIdForNetworkTarget(network, target), line);
    Q_UNUSED(stored);
    if (isActiveBufferTarget(network, target)) {
        appendPreviewHtmlLine(html);
    }
}

void maxchat::ui::MainWindow::appendSystemLine(const QString& line, bool logLine) {
    appendSystemLineToTarget(m_currentTarget, line, logLine);
}

void maxchat::ui::MainWindow::appendSystemLineToTarget(const QString& target, const QString& line, bool logLine,
                                          bool localEcho, bool highlight, bool systemStyling) {
    appendSystemLineToNetworkTarget(currentLogNetwork(), target, line, logLine, localEcho,
                                    highlight, systemStyling);
}

void maxchat::ui::MainWindow::appendSystemLineToNetworkTarget(const QString& network, const QString& target,
                                                 const QString& line, bool logLine, bool localEcho,
                                                 bool highlight, bool systemStyling) {
    rememberUrlsFromLine(line);
    maxchat::core::ChatLineFormatOptions formatOptions = chatLineFormatOptions();
    formatOptions.systemLine = systemStyling;
    const maxchat::core::FormattedChatLine display =
        maxchat::core::formatChatLine(line, formatOptions);
    const bool shouldQueuePreviews = logLine && !m_replayingLog;
    if (logLine) {
        appendChatLogLineForNetworkTarget(network, target, line);
    }

    maxchat::core::ChatBufferLine bufferLine;
    bufferLine.plainText = display.plainText;
    bufferLine.htmlText = display.html;
    bufferLine.sourceText = line;
    bufferLine.localEcho = localEcho;
    bufferLine.highlight = highlight;
    bufferLine.systemLine = systemStyling;
    const bool stored =
        m_chatBuffers.appendLine(bufferIdForNetworkTarget(network, target), bufferLine);
    Q_UNUSED(stored);
    if (stored) {
        updateNetworkTreeLabels();
    }

    const bool active = isActiveBufferTarget(network, target);
    if (active) {
        appendFormattedChatLine(display);
        if (m_comicMode) {
            refreshComic();
        }
    }
    if (active && shouldQueuePreviews) {
        queueLinkPreviewsFromLine(display.plainText);
    }
    // The status bar is for transient status (connect/find/etc.), not a mirror of
    // chat — echoing every appended line here is what put your own "<nick> msg"
    // in the bottom bar.
}

void maxchat::ui::MainWindow::appendRawLogLine(const QString& line) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    m_rawLogLines.append(trimmed);
    while (m_rawLogLines.size() > MaxRawLogLines) {
        m_rawLogLines.removeFirst();
    }

    if (m_rawLogDialog != nullptr) {
        m_rawLogDialog->appendLine(trimmed);
    }
}

void maxchat::ui::MainWindow::appendChatLogLine(const QString& line) {
    appendChatLogLineForTarget(m_currentTarget, line);
}

void maxchat::ui::MainWindow::appendChatLogLineForTarget(const QString& target, const QString& line) {
    appendChatLogLineForNetworkTarget(currentLogNetwork(), target, line);
}

void maxchat::ui::MainWindow::appendChatLogLineForNetworkTarget(const QString& network, const QString& target,
                                                   const QString& line) {
    if (!m_loggingEnabled || m_replayingLog || line.trimmed().isEmpty()) {
        return;
    }
    const bool logged = m_chatLogStore.appendLine(
        network.trimmed().isEmpty() ? QStringLiteral("Server") : network.trimmed(),
        target.trimmed().isEmpty() ? QStringLiteral("server") : target.trimmed(), line);
    Q_UNUSED(logged);
}

void maxchat::ui::MainWindow::rememberUrlsFromLine(const QString& line) {
    const QStringList urls = maxchat::core::extractUrls(line);
    if (urls.isEmpty()) {
        return;
    }

    QStringList newUrls;
    for (const QString& url : urls) {
        bool seen = false;
        for (const QString& existing : m_urlList) {
            if (existing.compare(url, Qt::CaseInsensitive) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            m_urlList.append(url);
            newUrls.append(url);
        }
    }

    if (newUrls.isEmpty()) {
        return;
    }

    bool trimmed = false;
    while (m_urlList.size() > MaxUrlListItems) {
        m_urlList.removeFirst();
        trimmed = true;
    }

    if (m_urlListDialog != nullptr) {
        if (trimmed) {
            m_urlListDialog->setUrls(m_urlList);
        } else {
            m_urlListDialog->appendUrls(newUrls);
        }
    }
}

void maxchat::ui::MainWindow::queueLinkPreviewsFromLine(const QString& line) {
    if (m_chatView == nullptr || m_replayingLog) {
        return;
    }

    const QStringList urls = maxchat::core::extractUrls(line);
    for (const QString& urlText : urls) {
        const maxchat::services::LinkPreviewCandidate candidate =
            maxchat::services::classifyLinkPreview(urlText);
        if (!candidate.isPreviewable() ||
            !maxchat::services::isLinkPreviewEnabled(candidate, m_linkPreviewToggles)) {
            continue;
        }

        switch (candidate.kind) {
        case maxchat::services::LinkPreviewKind::DirectImage:
            appendPreviewHtml(maxchat::services::renderDirectImagePreviewHtml(candidate));
            break;
        case maxchat::services::LinkPreviewKind::DirectAudio:
        case maxchat::services::LinkPreviewKind::DirectVideo:
            appendPreviewHtml(maxchat::services::renderDirectMediaPreviewHtml(candidate));
            break;
        case maxchat::services::LinkPreviewKind::OpenGraph:
        case maxchat::services::LinkPreviewKind::XPost:
        case maxchat::services::LinkPreviewKind::MastodonPost: {
            const QString key = previewKey(candidate.fetchUrl);
            if (m_pendingPreviewCandidates.contains(key)) {
                break;
            }
            m_pendingPreviewCandidates.insert(key, candidate);
            m_openGraphFetcher.fetch(candidate.fetchUrl);
            break;
        }
        case maxchat::services::LinkPreviewKind::None:
            break;
        }
    }
}

void maxchat::ui::MainWindow::handlePreviewCardFetched(const QUrl& url,
                                          const maxchat::services::OpenGraphCard& card) {
    const QString key = previewKey(url);
    const auto iterator = m_pendingPreviewCandidates.find(key);
    if (iterator == m_pendingPreviewCandidates.end()) {
        return;
    }

    const maxchat::services::LinkPreviewCandidate candidate = iterator.value();
    m_pendingPreviewCandidates.erase(iterator);
    if (!maxchat::services::isLinkPreviewEnabled(candidate, m_linkPreviewToggles)) {
        return;
    }

    maxchat::services::OpenGraphCard displayCard = card;
    if (displayCard.imageUrl.isValid() &&
        !maxchat::services::isAllowedPreviewFetchUrl(displayCard.imageUrl)) {
        displayCard.imageUrl = QUrl();
    }
    appendPreviewHtml(maxchat::services::renderOpenGraphPreviewHtml(candidate, displayCard));
}

void maxchat::ui::MainWindow::handlePreviewFetchFailed(const QUrl& url, const QString& reason) {
    Q_UNUSED(reason);
    m_pendingPreviewCandidates.remove(previewKey(url));
}

void maxchat::ui::MainWindow::showConnectionStatus(const QString& line) {
    // Connection lifecycle / context goes to the bottom status bar. The topic bar
    // is reserved for the channel topic (set in renderActiveBufferMetadata).
    statusBar()->showMessage(line);
}

void maxchat::ui::MainWindow::updateWindowTitle() {
    // Active network/channel context lives in the window title (UI_SURFACES.md),
    // not the topic or status bars.
    QString title = QStringLiteral("%1 %2").arg(app::displayName(), app::version());
    if (m_hasConnectionPlan && !m_connectionPlan.networkName.isEmpty()) {
        title += QStringLiteral(" — %1").arg(m_connectionPlan.networkName);
        const QString target = m_currentTarget.trimmed();
        if (!target.isEmpty() && !isTreeStatusTarget(target)) {
            title += QStringLiteral(" / %1").arg(target);
        }
    }
    setWindowTitle(title);
}

maxchat::core::ChatBufferId MainWindow::bufferIdForTarget(const QString& target) {
    return bufferIdForNetworkTarget(currentLogNetwork(), target);
}

maxchat::core::ChatBufferId MainWindow::bufferIdForNetworkTarget(const QString& network,
                                                                 const QString& target) {
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("server"), Qt::CaseInsensitive) == 0) {
        return m_chatBuffers.ensureServerBuffer(
            network.trimmed().isEmpty() ? QStringLiteral("Server") : network);
    }
    if (isChannelTarget(trimmed)) {
        return m_chatBuffers.ensureChannelBuffer(
            network.trimmed().isEmpty() ? QStringLiteral("Server") : network, trimmed);
    }
    return m_chatBuffers.ensureQueryBuffer(
        network.trimmed().isEmpty() ? QStringLiteral("Server") : network, trimmed);
}

bool MainWindow::isActiveBufferTarget(const QString& target) const {
    return isActiveBufferTarget(currentLogNetwork(), target);
}

bool MainWindow::isActiveBufferTarget(const QString& network, const QString& target) const {
    if (m_backgroundNetworkContext) {
        return false;
    }
    if (network.trimmed().compare(activeNetworkName(), Qt::CaseInsensitive) != 0) {
        return false;
    }
    const QString trimmed = target.trimmed();
    const QString current = m_currentTarget.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("server"), Qt::CaseInsensitive) == 0) {
        return current.isEmpty();
    }
    return current.compare(trimmed, Qt::CaseInsensitive) == 0;
}

void maxchat::ui::MainWindow::activateBufferTarget(const QString& target) {
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("server"), Qt::CaseInsensitive) == 0) {
        m_currentTarget.clear();
    } else {
        rememberTarget(trimmed);
        m_currentTarget = trimmed;
    }
    rememberNetwork(activeNetworkName());
    m_currentTargetByNetwork.insert(activeNetworkName(), m_currentTarget);
    m_openTargetsByNetwork.insert(activeNetworkName(), m_openTargets);

    const int unreadBefore =
        m_chatBuffers.snapshot(bufferIdForTarget(m_currentTarget)).unreadCount;
    const bool activeSet = m_chatBuffers.setActiveBuffer(bufferIdForTarget(m_currentTarget));
    Q_UNUSED(activeSet);
    renderActiveBuffer(unreadBefore);
    renderActiveBufferMetadata();
    updateNetworkTreeLabels();
    syncBufferTabs();
}

void maxchat::ui::MainWindow::renderActiveBuffer(const int unreadMarkerFromEnd) {
    if (m_chatView == nullptr) {
        return;
    }

    m_chatView->clear();
    const maxchat::core::ChatBufferSnapshot snapshot =
        m_chatBuffers.snapshot(bufferIdForTarget(m_currentTarget));
    const qsizetype markerIndex =
        m_markerLine && unreadMarkerFromEnd > 0 && unreadMarkerFromEnd < snapshot.lines.size()
            ? snapshot.lines.size() - unreadMarkerFromEnd
            : -1;
    qsizetype lineIndex = 0;
    const maxchat::core::ChatLineFormatOptions baseOptions = chatLineFormatOptions();
    for (const maxchat::core::ChatBufferLine& line : snapshot.lines) {
        if (lineIndex++ == markerIndex) {
            appendUnreadMarkerLine();
        }
        if (!line.sourceText.isEmpty()) {
            maxchat::core::ChatLineFormatOptions options = baseOptions;
            options.systemLine = line.systemLine;
            const maxchat::core::FormattedChatLine display =
                maxchat::core::formatChatLine(line.sourceText, options);
            appendFormattedChatLine(display);
        } else if (!line.htmlText.isEmpty()) {
            appendPreviewHtmlLine(line.htmlText);
        } else if (!line.plainText.isEmpty()) {
            appendPlainChatLine(line.plainText);
        }
    }
    refreshComic();
}

void maxchat::ui::MainWindow::renderActiveBufferMetadata() {
    const maxchat::core::ChatBufferSnapshot snapshot =
        m_chatBuffers.snapshot(bufferIdForTarget(m_currentTarget));

    if (m_memberList != nullptr) {
        m_memberList->clear();
        if (snapshot.id.kind == maxchat::core::ChatBufferKind::Channel) {
            if (m_membersHeader != nullptr) {
                m_membersHeader->setText(
                    QStringLiteral("%1 users").arg(snapshot.members.size()));
            }
            if (m_coloredNicks) {
                const auto roleRank = [](const QString& member) {
                    const int idx =
                        QStringLiteral("~&@%+").indexOf(member.isEmpty() ? QChar() : member.front());
                    return idx < 0 ? 5 : idx;
                };
                QStringList members = snapshot.members;
                std::sort(members.begin(), members.end(),
                          [&](const QString& a, const QString& b) {
                              if (m_sortByStatus && roleRank(a) != roleRank(b)) {
                                  return roleRank(a) < roleRank(b);
                              }
                              return nickWithoutPrefix(a).compare(nickWithoutPrefix(b),
                                                                  Qt::CaseInsensitive) < 0;
                          });
                for (const QString& member : members) {
                    m_memberList->addItem(member);
                }
            } else {
                // Colours off: IRCCloud-style sections grouped by channel role.
                const QList<QPair<QString, QString>> memberGroups = {
                    {QStringLiteral("~"), QStringLiteral("Owners")},
                    {QStringLiteral("&"), QStringLiteral("Admins")},
                    {QStringLiteral("@"), QStringLiteral("Operators")},
                    {QStringLiteral("%"), QStringLiteral("Half-ops")},
                    {QStringLiteral("+"), QStringLiteral("Voiced")},
                    {QString(), QStringLiteral("Members")},
                };
                QHash<QString, QStringList> grouped;
                for (const QString& member : snapshot.members) {
                    const QString prefix = member.left(1);
                    grouped[QStringLiteral("~&@%+").contains(prefix) ? prefix : QString()]
                        .append(member);
                }
                for (const auto& group : memberGroups) {
                    QStringList names = grouped.value(group.first);
                    if (names.isEmpty()) {
                        continue;
                    }
                    std::sort(names.begin(), names.end(),
                              [](const QString& a, const QString& b) {
                                  return nickWithoutPrefix(a).compare(
                                             nickWithoutPrefix(b), Qt::CaseInsensitive) < 0;
                              });
                    auto* header = new QListWidgetItem(QStringLiteral("%1   %2")
                                                           .arg(group.second.toUpper())
                                                           .arg(names.size()));
                    header->setFlags(Qt::ItemIsEnabled);
                    QFont headerFont = header->font();
                    headerFont.setBold(true);
                    header->setFont(headerFont);
                    header->setData(Qt::UserRole + 1, true);
                    m_memberList->addItem(header);
                    for (const QString& name : names) {
                        m_memberList->addItem(name);
                    }
                }
            }
            recolorMemberList();
        } else if (m_membersHeader != nullptr) {
            m_membersHeader->setText(QStringLiteral("Members"));
        }
    }

    // The topic bar shows ONLY the active channel's topic — empty on the server
    // tab, in a PM, when not connected, or when the channel has no topic.
    // Connection state and network/channel context belong to the status bar and
    // the window title, not here.
    if (m_topicLabel != nullptr) {
        const QString topic =
            (m_hasConnectionPlan && snapshot.id.kind == maxchat::core::ChatBufferKind::Channel)
                ? snapshot.topic.trimmed()
                : QString();
        m_topicLabel->setText(topic);
    }
    updateWindowTitle();
}

namespace {

const QStringList& savedLookKeys() {
    static const QStringList keys = {
        QStringLiteral("theme"),           QStringLiteral("chat_theme"),
        QStringLiteral("wallpaper"),       QStringLiteral("app_font_family"),
        QStringLiteral("app_font_size"),   QStringLiteral("app_font_bold"),
        QStringLiteral("chat_font_family"), QStringLiteral("chat_font_size"),
        QStringLiteral("chat_font_bold"),
    };
    return keys;
}

} // namespace

void maxchat::ui::MainWindow::handleChatAnchorClicked(const QUrl& url) {
    if (!url.isValid()) {
        return;
    }

    using maxchat::services::LinkPreviewKind;
    const auto candidate = maxchat::services::classifyLinkPreview(url);
    switch (candidate.kind) {
    case LinkPreviewKind::DirectImage: {
        auto* viewer = new ImageViewerDialog(
            candidate.fetchUrl.isValid() ? candidate.fetchUrl : url, this);
        viewer->show();
        return;
    }
    case LinkPreviewKind::DirectAudio:
        if (m_audioBar != nullptr) {
            m_audioBar->playUrl(candidate.fetchUrl.isValid() ? candidate.fetchUrl : url);
            return;
        }
        break;
    case LinkPreviewKind::DirectVideo: {
        auto* player =
            new MediaPlayerDialog(candidate.fetchUrl.isValid() ? candidate.fetchUrl : url, this);
        player->show();
        return;
    }
    default:
        break;
    }
    QDesktopServices::openUrl(url);
}

void maxchat::ui::MainWindow::setupNavShortcuts() {
    for (const NavShortcutSpec& spec : navShortcutSpecs()) {
        auto* shortcut = new QShortcut(QKeySequence(spec.defaultKey), this);
        if (spec.id == QStringLiteral("navActivity")) {
            connect(shortcut, &QShortcut::activated, this, &MainWindow::jumpToNextActivity);
        } else {
            const int index = spec.id.mid(3).toInt() - 1;
            connect(shortcut, &QShortcut::activated, this,
                    [this, index]() { jumpToBufferIndex(index); });
        }
        m_navShortcuts.insert(spec.id, shortcut);
    }
}

void maxchat::ui::MainWindow::applyNavShortcutOverrides(const QVariantMap& settings) {
    const QVariantMap overrides = settings.value(QStringLiteral("shortcuts")).toMap();
    for (const NavShortcutSpec& spec : navShortcutSpecs()) {
        QShortcut* shortcut = m_navShortcuts.value(spec.id);
        if (shortcut == nullptr) {
            continue;
        }
        const QString bound = overrides.value(spec.id, spec.defaultKey).toString();
        shortcut->setKey(QKeySequence(bound));
    }
}

void maxchat::ui::MainWindow::jumpToBufferIndex(const int index) {
    if (m_networkTree == nullptr || index < 0) {
        return;
    }
    int seen = 0;
    for (int top = 0; top < m_networkTree->topLevelItemCount(); ++top) {
        QTreeWidgetItem* root = m_networkTree->topLevelItem(top);
        if (seen++ == index) {
            m_networkTree->setCurrentItem(root);
            return;
        }
        for (int child = 0; child < root->childCount(); ++child) {
            if (seen++ == index) {
                m_networkTree->setCurrentItem(root->child(child));
                return;
            }
        }
    }
}

void maxchat::ui::MainWindow::jumpToNextActivity() {
    if (m_networkTree == nullptr) {
        return;
    }
    for (int top = 0; top < m_networkTree->topLevelItemCount(); ++top) {
        QTreeWidgetItem* root = m_networkTree->topLevelItem(top);
        for (int child = -1; child < root->childCount(); ++child) {
            QTreeWidgetItem* item = child < 0 ? root : root->child(child);
            const QString network = treeItemNetwork(item);
            QString target = treeItemTarget(item);
            if (isTreeStatusTarget(target)) {
                target.clear(); // the root row = the network's server buffer
            } else if (target.isEmpty() && child >= 0) {
                continue;
            }
            if (m_chatBuffers.snapshot(bufferIdForNetworkTarget(network, target)).unreadCount >
                0) {
                m_networkTree->setCurrentItem(item);
                return;
            }
        }
    }
    statusBar()->showMessage(QStringLiteral("No unread activity."));
}

void maxchat::ui::MainWindow::openShortcutEditor() {
    ShortcutEditorDialog dialog(
        m_settings.loadWithDefaults().value(QStringLiteral("shortcuts")).toMap(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("shortcuts"), dialog.overrides());
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save shortcuts."));
        return;
    }
    applyNavShortcutOverrides(settings);
    appendSystemLine(QStringLiteral("! Shortcuts saved."));
}

void maxchat::ui::MainWindow::rebuildLooksMenu() {
    if (m_looksMenu == nullptr) {
        return;
    }

    m_looksMenu->clear();
    m_looksMenu->addAction(QStringLiteral("Save Current Look..."), this,
                           &MainWindow::saveCurrentLook);
    const QVariantMap looks =
        m_settings.loadWithDefaults().value(QStringLiteral("looks")).toMap();
    if (looks.isEmpty()) {
        QAction* none = m_looksMenu->addAction(QStringLiteral("No saved looks"));
        none->setEnabled(false);
        return;
    }

    m_looksMenu->addSeparator();
    for (auto it = looks.constBegin(); it != looks.constEnd(); ++it) {
        const QString name = it.key();
        m_looksMenu->addAction(name, this, [this, name]() { applyLook(name); });
    }
    m_looksMenu->addSeparator();
    QMenu* deleteMenu = m_looksMenu->addMenu(QStringLiteral("Delete"));
    for (auto it = looks.constBegin(); it != looks.constEnd(); ++it) {
        const QString name = it.key();
        deleteMenu->addAction(name, this, [this, name]() { deleteLook(name); });
    }
}

void maxchat::ui::MainWindow::saveCurrentLook() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Save Look"),
                                               QStringLiteral("Name for this look:"),
                                               QLineEdit::Normal, QString(), &accepted)
                             .trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }

    QVariantMap settings = m_settings.loadWithDefaults();
    QVariantMap look;
    for (const QString& key : savedLookKeys()) {
        look.insert(key, settings.value(key));
    }
    QVariantMap looks = settings.value(QStringLiteral("looks")).toMap();
    looks.insert(name, look);
    settings.insert(QStringLiteral("looks"), looks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save look."));
        return;
    }
    rebuildLooksMenu();
    appendSystemLine(QStringLiteral("! Look \"%1\" saved.").arg(name));
}

void maxchat::ui::MainWindow::applyLook(const QString& name) {
    QVariantMap settings = m_settings.loadWithDefaults();
    const QVariantMap look =
        settings.value(QStringLiteral("looks")).toMap().value(name).toMap();
    if (look.isEmpty()) {
        appendSystemLine(QStringLiteral("! Look \"%1\" was not found.").arg(name));
        rebuildLooksMenu();
        return;
    }

    for (auto it = look.constBegin(); it != look.constEnd(); ++it) {
        settings.insert(it.key(), it.value());
    }
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not apply look."));
        return;
    }
    applyCurrentSettings();
    appendSystemLine(QStringLiteral("! Look \"%1\" applied.").arg(name));
}

void maxchat::ui::MainWindow::deleteLook(const QString& name) {
    QVariantMap settings = m_settings.loadWithDefaults();
    QVariantMap looks = settings.value(QStringLiteral("looks")).toMap();
    if (looks.remove(name) == 0) {
        rebuildLooksMenu();
        return;
    }
    settings.insert(QStringLiteral("looks"), looks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not delete look."));
        return;
    }
    rebuildLooksMenu();
    appendSystemLine(QStringLiteral("! Look \"%1\" deleted.").arg(name));
}

void maxchat::ui::MainWindow::appendUnreadMarkerLine() {
    if (m_chatView == nullptr) {
        return;
    }
    QTextCursor cursor = m_chatView->textCursor();
    cursor.movePosition(QTextCursor::End);
    if (!m_chatView->document()->isEmpty()) {
        cursor.insertBlock(QTextBlockFormat());
    }
    cursor.insertHtml(QStringLiteral("<hr/>"));
    m_chatView->setTextCursor(cursor);
}

void maxchat::ui::MainWindow::configureDcc() {
    const QVariantMap settings = m_settings.loadWithDefaults();
    QString dir = settings.value(QStringLiteral("dcc_dir")).toString().trimmed();
    if (dir.isEmpty()) {
        dir = QDir(m_settings.paths().configDir).filePath(QStringLiteral("downloads"));
    }
    m_dccManager->setDownloadDir(dir);
    m_dccManager->setPassive(settings.value(QStringLiteral("dcc_passive"), true).toBool());
    m_dccManager->setPortRange(settings.value(QStringLiteral("dcc_port_first"), 0).toInt(),
                               settings.value(QStringLiteral("dcc_port_last"), 0).toInt());
    m_dccManager->setAcceptPolicy(
        settings.value(QStringLiteral("dcc_accept"), QStringLiteral("ask")).toString(),
        settings.value(QStringLiteral("dcc_trusted")).toStringList());
    // Advertised IP: pref override, else the active connection's local IPv4.
    QString ip = settings.value(QStringLiteral("dcc_ip")).toString().trimmed();
    if (ip.isEmpty()) {
        ip = connection().localAddress();
    }
    m_dccManager->setAdvertisedIp(ip);
}

void maxchat::ui::MainWindow::openDccTransfers() {
    configureDcc();
    DccTransfersDialog dialog(m_dccManager, this);
    dialog.exec();
}

void maxchat::ui::MainWindow::handleDccCommand(const QStringList& args) {
    configureDcc();

    const QString sub = args.isEmpty() ? QString() : args.first().toLower();
    if (sub == QStringLiteral("send") && args.size() >= 3) {
        const QString peer = args.at(1);
        const QString path = args.mid(2).join(QLatin1Char(' '));
        m_dccManager->offerSend(peer, path);
    } else if (sub == QStringLiteral("send") && args.size() == 2) {
        const QString peer = args.at(1);
        const QString path =
            QFileDialog::getOpenFileName(this, QStringLiteral("Send file to %1").arg(peer));
        if (!path.isEmpty()) {
            m_dccManager->offerSend(peer, path);
        }
    } else if (sub == QStringLiteral("chat") && args.size() >= 2) {
        m_dccManager->offerChat(args.at(1));
    } else if (sub == QStringLiteral("list")) {
        const auto transfers = m_dccManager->transfers();
        appendSystemLine(QStringLiteral("! %1 DCC transfer(s).").arg(transfers.size()));
        openDccTransfers();
    } else if (sub == QStringLiteral("close") || sub == QStringLiteral("cancel")) {
        const QString target = m_currentTarget.trimmed();
        if (target.startsWith(QLatin1Char('='))) {
            m_dccManager->closeChat(target.mid(1));
        } else {
            for (const auto& t : m_dccManager->transfers()) {
                m_dccManager->cancelTransfer(t.id);
            }
            appendSystemLine(QStringLiteral("! Cancelled active DCC transfers."));
        }
    } else {
        appendSystemLine(QStringLiteral(
            "! Usage: /dcc send <nick> [file] | /dcc chat <nick> | /dcc list | /dcc close"));
    }
}

void maxchat::ui::MainWindow::openComicSettings() {
    ensureComicArt();
    QStringList stems = m_comicCharacterPaths.keys();
    std::sort(stems.begin(), stems.end());
    QStringList bgs;
    for (auto it = m_comicBackgroundPaths.constBegin(); it != m_comicBackgroundPaths.constEnd();
         ++it) {
        bgs.append(it.key());
    }
    std::sort(bgs.begin(), bgs.end());
    // Per-channel overrides editor: offer every open channel as "net/target".
    QStringList channelKeys;
    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffers()) {
        if (id.kind == maxchat::core::ChatBufferKind::Channel) {
            channelKeys.append(id.network + QStringLiteral("/") + id.target);
        }
    }
    std::sort(channelKeys.begin(), channelKeys.end());
    ComicSettingsDialog dialog(m_settings.loadWithDefaults(), stems, bgs, channelKeys, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (!m_settings.saveRaw(dialog.settings())) {
        appendSystemLine(QStringLiteral("! Could not save comic settings."));
        return;
    }
    m_comicArtDirLoaded.clear(); // force art rescan
    m_comicBgCache.clear();
    refreshComic();
    appendSystemLine(QStringLiteral("! Comic settings saved."));
}

void maxchat::ui::MainWindow::openCharacterGallery() {
    ensureComicArt();
    if (m_comicCharacterPaths.isEmpty()) {
        showFeaturePlanned(QStringLiteral("Browse Characters"),
                           QStringLiteral("No comic art found - set a comic art folder in "
                                          "Comic Settings."));
        return;
    }
    QStringList stems = m_comicCharacterPaths.keys();
    std::sort(stems.begin(), stems.end());
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Comic Characters"));
    dialog->resize(560, 480);
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(
        QStringLiteral("%1 characters - assign them to nicks in Comic Settings.").arg(stems.size()),
        dialog));
    auto* list = new QListWidget(dialog);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(96, 110));
    list->setResizeMode(QListView::Adjust);
    list->setSpacing(8);
    for (const QString& stem : stems) {
        maxchat::comic::Character* ch = maxchat::comic::loadCharacter(m_comicCharacterPaths.value(stem));
        auto* item = new QListWidgetItem(stem, list);
        if (ch != nullptr) {
            const QImage im = ch->imageTrimmed(QStringLiteral("neutral"), QStringLiteral("right"), 0);
            if (!im.isNull()) {
                item->setIcon(QIcon(QPixmap::fromImage(
                    im.scaledToHeight(110, Qt::SmoothTransformation))));
            }
        }
    }
    layout->addWidget(list, 1);
    auto* close = new QPushButton(QStringLiteral("Close"), dialog);
    connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
    layout->addWidget(close, 0, Qt::AlignRight);
    dialog->show();
}

void maxchat::ui::MainWindow::openEmotionPicker() {
    // Set the expression used for YOUR comic panels (overrides the text guess);
    // "Auto" returns to guessing from your message text. Mirrors the Python
    // emotion wheel without the live-preview popup.
    const QStringList options = {QStringLiteral("Auto"),    QStringLiteral("neutral"),
                                 QStringLiteral("happy"),   QStringLiteral("laughing"),
                                 QStringLiteral("coy"),     QStringLiteral("scared"),
                                 QStringLiteral("bored"),   QStringLiteral("angry"),
                                 QStringLiteral("shouting"), QStringLiteral("sad")};
    const QString currentLabel =
        m_comicSelfEmotion == QStringLiteral("auto") ? QStringLiteral("Auto") : m_comicSelfEmotion;
    const int current = std::max(0, static_cast<int>(options.indexOf(currentLabel)));
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, QStringLiteral("Comic emotion"),
        QStringLiteral("Expression for your comic panels (Auto = guess from your text):"), options,
        current, false, &ok);
    if (!ok) {
        return;
    }
    m_comicSelfEmotion =
        choice == QStringLiteral("Auto") ? QStringLiteral("auto") : choice.toLower();
    appendSystemLine(m_comicSelfEmotion == QStringLiteral("auto")
                         ? QStringLiteral("! Comic emotion: auto (guess from text).")
                         : QStringLiteral("! Comic emotion set to %1.").arg(m_comicSelfEmotion));
    refreshComic();
}


void maxchat::ui::MainWindow::saveComic() {
    if (m_comicView == nullptr || !m_comicView->hasPanels()) {
        appendSystemLine(QStringLiteral("! No comic to save yet."));
        return;
    }
    const QPixmap sheet = m_comicView->sheet();
    if (sheet.isNull()) {
        return;
    }
    QString defaultName =
        QStringLiteral("comic-%1.png")
            .arg(QString(m_currentTarget).remove(QLatin1Char('#')).remove(QLatin1Char('&')).replace(
                QLatin1Char('/'), QLatin1Char('_')));
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save comic image"),
                                                defaultName, QStringLiteral("PNG image (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".png");
    }
    if (sheet.save(path, "PNG")) {
        appendSystemLine(QStringLiteral("! Comic saved to %1").arg(path));
    } else {
        appendSystemLine(QStringLiteral("! Could not save the comic image."));
    }
}

void maxchat::ui::MainWindow::setComicMode(bool enabled) {
    m_comicMode = enabled;
    if (m_comicModeAction != nullptr && m_comicModeAction->isChecked() != enabled) {
        const QSignalBlocker blocker(m_comicModeAction);
        m_comicModeAction->setChecked(enabled);
    }
    // Show the comic pane above the chat; the chat stays visible beneath it.
    if (m_comicView != nullptr) {
        m_comicView->setVisible(enabled);
    }
    if (enabled && m_chatSplitter != nullptr) {
        const QList<int> sizes = m_chatSplitter->sizes();
        const int total = sizes.value(0) + sizes.value(1);
        if (total > 0 && sizes.value(0) <= 0) {
            m_chatSplitter->setSizes({total / 2, total - total / 2});
        }
    }
    if (enabled) {
        ensureComicArt();
        refreshComic();
        if (m_comicCharacterPaths.isEmpty()) {
            appendSystemLine(QStringLiteral(
                "! Comic Mode: no art loaded. Set your Comic Chat art folder in "
                "Comic > Comic Settings (the folder with the .avb/.bgb files)."));
        }
    }
    statusBar()->showMessage(enabled ? QStringLiteral("Comic Mode on.")
                                     : QStringLiteral("Comic Mode off."));
}

namespace {

// Java-style deterministic hash used by the Python comic for character/pose.
quint32 comicHash(const QString& s) {
    quint32 h = 0;
    for (const QChar c : s) {
        h = h * 31u + c.unicode();
    }
    return h;
}

// Text-based emotion guess (port of _emotion_for).
QString comicEmotionFor(const QString& body) {
    const QString low = body.toLower();
    if (low.contains(QStringLiteral(":d")) || low.contains(QStringLiteral(":-d")) ||
        low.contains(QStringLiteral("lol")) || low.contains(QStringLiteral("rotfl")) ||
        low.contains(QStringLiteral("haha"))) {
        return QStringLiteral("laughing");
    }
    if (body.contains(QStringLiteral(":)")) || body.contains(QStringLiteral(":-)")) ||
        body.contains(QStringLiteral("(:")) || body.contains(QStringLiteral("=)"))) {
        return QStringLiteral("happy");
    }
    if (body.contains(QStringLiteral(":(")) || body.contains(QStringLiteral(":-(")) ||
        body.contains(QStringLiteral("):")) || body.contains(QStringLiteral("=("))) {
        return QStringLiteral("sad");
    }
    if (body.contains(QStringLiteral(";)")) || body.contains(QStringLiteral(";-)"))) {
        return QStringLiteral("coy");
    }
    if (body.contains(QStringLiteral("?!")) || body.contains(QStringLiteral("!?"))) {
        return QStringLiteral("scared");
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


struct ComicMsg {
    QString nick;
    QString text;
    bool think = false;
    bool action = false;
};

} // namespace

void maxchat::ui::MainWindow::ensureComicArt() {
    const QVariantMap settings = m_settings.loadWithDefaults();
    const QString dir = settings.value(QStringLiteral("comic_art_dir")).toString().trimmed();
    const QString resolved = dir.isEmpty() ? maxchat::comic::bundledArtDir() : dir;
    if (resolved == m_comicArtDirLoaded) {
        return;
    }
    m_comicArtDirLoaded = resolved;
    m_comicCharacterPaths.clear();
    m_comicBackgroundPaths.clear();
    m_comicAutoChars.clear();
    if (resolved.isEmpty()) {
        return;
    }
    QStringList backgrounds;
    QStringList characters;
    maxchat::comic::scanArtDir(resolved, backgrounds, characters);
    for (const QString& path : characters) {
        m_comicCharacterPaths.insert(QFileInfo(path).completeBaseName().toLower(), path);
    }
    for (const QString& path : backgrounds) {
        m_comicBackgroundPaths.insert(QFileInfo(path).fileName().toLower(), path);
    }
}

maxchat::comic::Character* maxchat::ui::MainWindow::comicCharacterForNick(const QString& nick) {
    if (m_comicCharacterPaths.isEmpty()) {
        return nullptr;
    }
    const QString low = nick.trimmed().toLower();
    const QVariantMap settings = m_settings.loadWithDefaults();
    // A per-channel assignment (comic_channels[net/target].chars) wins over the
    // global comic_chars map.
    const QString chanKey =
        activeNetworkName().trimmed() + QStringLiteral("/") + m_currentTarget.trimmed();
    const QVariantMap chanChars = settings.value(QStringLiteral("comic_channels"))
                                      .toMap()
                                      .value(chanKey)
                                      .toMap()
                                      .value(QStringLiteral("chars"))
                                      .toMap();
    const QVariantMap manual = settings.value(QStringLiteral("comic_chars")).toMap();
    QString stem = chanChars.value(low).toString();
    if (stem.isEmpty()) {
        stem = manual.value(low).toString();
    }
    if (stem.isEmpty()) {
        const QString self = settings.value(QStringLiteral("comic_self_char")).toString();
        if (!self.isEmpty() && low == currentNickForNetwork(activeNetworkName()).toLower()) {
            stem = self;
        }
    }
    if (stem.isEmpty()) {
        if (m_comicAutoChars.contains(low)) {
            stem = m_comicAutoChars.value(low);
        } else {
            QStringList pool = m_comicCharacterPaths.keys();
            QStringList used = m_comicAutoChars.values();
            for (auto it = manual.constBegin(); it != manual.constEnd(); ++it) {
                used.append(it.value().toString().toLower());
            }
            QStringList freePool;
            for (const QString& s : pool) {
                if (!used.contains(s)) {
                    freePool.append(s);
                }
            }
            if (freePool.isEmpty()) {
                freePool = pool;
            }
            std::sort(freePool.begin(), freePool.end());
            stem = freePool.at(static_cast<int>(comicHash(low) % freePool.size()));
            m_comicAutoChars.insert(low, stem);
        }
    }
    const QString path = m_comicCharacterPaths.value(stem.toLower());
    return path.isEmpty() ? nullptr : maxchat::comic::loadCharacter(path);
}

QImage maxchat::ui::MainWindow::comicBackground() {
    const QVariantMap settings = m_settings.loadWithDefaults();
    // A per-channel background (comic_channels[net/target].bg) wins over the
    // global comic_bg.
    const QString chanKey =
        activeNetworkName().trimmed() + QStringLiteral("/") + m_currentTarget.trimmed();
    const QVariantMap chanCfg =
        settings.value(QStringLiteral("comic_channels")).toMap().value(chanKey).toMap();
    QString file = chanCfg.value(QStringLiteral("bg")).toString().toLower();
    if (file.isEmpty()) {
        file = settings.value(QStringLiteral("comic_bg")).toString().toLower();
    }
    QString path = m_comicBackgroundPaths.value(file);
    if (path.isEmpty() && !m_comicBackgroundPaths.isEmpty()) {
        if (file.isEmpty() &&
            settings.value(QStringLiteral("comic_random_bg"), false).toBool()) {
            // No configured background + random enabled → pick one that's stable
            // per channel (seed by the current target, sorted keys for determinism).
            QStringList keys = m_comicBackgroundPaths.keys();
            std::sort(keys.begin(), keys.end());
            const QString seed = m_currentTarget.isEmpty()
                                     ? QStringLiteral("default")
                                     : m_currentTarget.toLower();
            const int index = static_cast<int>(comicHash(seed) % static_cast<quint32>(keys.size()));
            path = m_comicBackgroundPaths.value(keys.at(index));
        } else {
            path = *m_comicBackgroundPaths.constBegin();
        }
    }
    if (path.isEmpty()) {
        return {};
    }
    if (!m_comicBgCache.contains(path)) {
        m_comicBgCache.insert(path, maxchat::comic::loadBackground(path));
    }
    return m_comicBgCache.value(path);
}

QString maxchat::ui::MainWindow::comicEmotionForMessage(const QString& nick,
                                                        const QString& text) {
    // Your own lines honour the emotion override (if not "auto"); everyone else
    // (and you, when auto) get the text-based guess.
    if (m_comicSelfEmotion != QStringLiteral("auto") &&
        nick.trimmed().toLower() ==
            currentNickForNetwork(activeNetworkName()).toLower()) {
        return m_comicSelfEmotion;
    }
    return comicEmotionFor(text);
}

void maxchat::ui::MainWindow::refreshComic() {
    if (m_comicView == nullptr || !m_comicMode) {
        return;
    }
    ensureComicArt();
    const QVariantMap settings = m_settings.loadWithDefaults();
    const bool captions = settings.value(QStringLiteral("comic_captions"), true).toBool();
    const double captionScale = settings.value(QStringLiteral("comic_caption_scale"), 1.0).toDouble();
    const int panelCount = std::clamp(settings.value(QStringLiteral("comic_panels"), 4).toInt(), 1, 6);
    const int perPanel = std::clamp(settings.value(QStringLiteral("comic_per_panel"), 4).toInt(), 1, 6);
    const int minFont = std::clamp(settings.value(QStringLiteral("comic_min_font"), 9).toInt(), 6, 13);
    QStringList comicIgnore = settings.value(QStringLiteral("comic_ignore")).toStringList();
    // Per-channel "hide from comic" nicks add to the global comic_ignore list.
    const QString comicChanKey =
        activeNetworkName().trimmed() + QStringLiteral("/") + m_currentTarget.trimmed();
    const QVariantList chanIgnore = settings.value(QStringLiteral("comic_channels"))
                                        .toMap()
                                        .value(comicChanKey)
                                        .toMap()
                                        .value(QStringLiteral("ignore"))
                                        .toList();
    for (const QVariant& entry : chanIgnore) {
        comicIgnore.append(entry.toString().toLower());
    }
    const bool ignoreCmds = settings.value(QStringLiteral("comic_ignore_cmds"), true).toBool();
    const QStringList botPatterns = settings.value(QStringLiteral("comic_bot_patterns")).toStringList();
    QRegularExpression excludeRe;
    const QString excludeStr = settings.value(QStringLiteral("comic_exclude_regex")).toString();
    if (!excludeStr.isEmpty()) {
        excludeRe = QRegularExpression(excludeStr, QRegularExpression::CaseInsensitiveOption);
    }

    const auto filtered = [&](const QString& nick, const QString& text) {
        if (comicIgnore.contains(nick.toLower())) {
            return true;
        }
        const QString stripped = maxchat::irc::stripFormatting(text).trimmed();
        if (stripped.isEmpty()) {
            return true;
        }
        if (ignoreCmds) {
            const QString low = stripped.toLower();
            for (const QString& pat : botPatterns) {
                if (pat.isEmpty() || !low.startsWith(pat)) {
                    continue;
                }
                if (pat.size() == 1) {
                    if (low.size() > 1 && low.at(1).isLetterOrNumber()) {
                        return true;
                    }
                } else {
                    return true;
                }
            }
        }
        if (!excludeStr.isEmpty() && excludeRe.isValid() && excludeRe.match(stripped).hasMatch()) {
            return true;
        }
        return false;
    };

    // Collect eligible speech messages from the active buffer.
    const maxchat::core::ChatBufferSnapshot snapshot =
        m_chatBuffers.snapshot(bufferIdForTarget(m_currentTarget));
    QVector<ComicMsg> msgs;
    for (const maxchat::core::ChatBufferLine& line : snapshot.lines) {
        const QString src = line.sourceText;
        if (src.isEmpty()) {
            continue;
        }
        ComicMsg msg;
        if (src.startsWith(QLatin1Char('<'))) {
            const int end = src.indexOf(QStringLiteral("> "));
            if (end <= 0) {
                continue;
            }
            msg.nick = src.mid(1, end - 1);
            msg.text = maxchat::irc::stripFormatting(src.mid(end + 2)).trimmed();
        } else if (!line.systemLine && src.startsWith(QStringLiteral("* "))) {
            const int sp = src.indexOf(QLatin1Char(' '), 2);
            if (sp <= 2) {
                continue;
            }
            msg.nick = src.mid(2, sp - 2);
            msg.text = maxchat::irc::stripFormatting(src.mid(sp + 1)).trimmed();
            msg.action = true;
        } else {
            continue;
        }
        if (msg.text.contains(QStringLiteral("http://")) ||
            msg.text.contains(QStringLiteral("https://")) ||
            msg.text.contains(QStringLiteral("www."))) {
            continue; // links are chat embeds, never panels
        }
        if (filtered(msg.nick, msg.text)) {
            continue;
        }
        if (!msg.action && msg.text.size() > 2 && msg.text.startsWith(QLatin1Char('(')) &&
            msg.text.endsWith(QLatin1Char(')'))) {
            msg.think = true;
            msg.text = msg.text.mid(1, msg.text.size() - 2);
        }
        msgs.append(msg);
    }

    // Group into panels (bubble cap + min-font spill).
    constexpr int kSize = 315;
    QVector<QVector<ComicMsg>> panels;
    for (const ComicMsg& msg : msgs) {
        bool extended = false;
        if (!panels.isEmpty() && panels.last().size() < perPanel) {
            QVector<ComicMsg> trial = panels.last();
            trial.append(msg);
            // Build actors/lines for the trial to check the resulting font size.
            QVector<maxchat::comic::ComicActor> actors;
            QHash<QString, int> actorIndex;
            QVector<maxchat::comic::ComicLineItem> rlines;
            for (const ComicMsg& m : trial) {
                if (!actorIndex.contains(m.nick.toLower())) {
                    maxchat::comic::ComicActor a;
                    a.nick = m.nick;
                    a.character = comicCharacterForNick(m.nick);
                    a.emotion = comicEmotionForMessage(m.nick, m.text);
                    a.pose = a.character && a.character->bodyCount() > 0
                                 ? static_cast<int>(comicHash(m.nick.toLower() + QStringLiteral("|") +
                                                              m.text) %
                                                    a.character->bodyCount())
                                 : 0;
                    actorIndex.insert(m.nick.toLower(), actors.size());
                    actors.append(a);
                }
                maxchat::comic::ComicLineItem li;
                li.actorIndex = actorIndex.value(m.nick.toLower());
                li.text = m.text;
                li.think = m.think;
                li.action = m.action;
                rlines.append(li);
            }
            if (maxchat::comic::panelMinFont(kSize, actors, rlines) >= minFont) {
                panels.last() = trial;
                extended = true;
            }
        }
        if (!extended) {
            panels.append({msg});
        }
    }
    while (panels.size() > 6) {
        panels.removeFirst();
    }

    // Caption colours: per-user nick_colors override else hashed nick colour.
    const QString captionMode = settings.value(QStringLiteral("comic_caption_mode"),
                                               QStringLiteral("nick")).toString();
    const QString fixedColor = settings.value(QStringLiteral("comic_caption_color"),
                                              QStringLiteral("#363636")).toString();
    const QImage background = comicBackground();

    QVector<QPixmap> rendered;
    const QVector<QVector<ComicMsg>> shown =
        panels.size() > panelCount ? panels.mid(panels.size() - panelCount) : panels;
    for (const QVector<ComicMsg>& panel : shown) {
        QVector<maxchat::comic::ComicActor> actors;
        QHash<QString, int> actorIndex;
        QVector<maxchat::comic::ComicLineItem> rlines;
        QHash<QString, QString> captionColors;
        for (const ComicMsg& m : panel) {
            const QString low = m.nick.toLower();
            if (!actorIndex.contains(low)) {
                maxchat::comic::ComicActor a;
                a.nick = m.nick;
                a.character = comicCharacterForNick(m.nick);
                if (a.character == nullptr) {
                    continue; // no art for this nick → skip (its lines too)
                }
                a.emotion = comicEmotionForMessage(m.nick, m.text);
                a.pose = a.character->bodyCount() > 0
                             ? static_cast<int>(comicHash(low + QStringLiteral("|") + m.text) %
                                                a.character->bodyCount())
                             : 0;
                actorIndex.insert(low, actors.size());
                actors.append(a);
                captionColors.insert(low, captionMode == QStringLiteral("fixed")
                                              ? fixedColor
                                              : (m_nickColorOverrides.value(low).toString().isEmpty()
                                                     ? QColor(maxchat::irc::nickColor(m.nick)).name()
                                                     : m_nickColorOverrides.value(low).toString()));
            }
            if (!actorIndex.contains(low)) {
                continue;
            }
            // Latest emotion/pose for the actor.
            maxchat::comic::ComicActor& a = actors[actorIndex.value(low)];
            a.emotion = comicEmotionForMessage(m.nick, m.text);
            if (a.character && a.character->bodyCount() > 0) {
                a.pose = static_cast<int>(comicHash(low + QStringLiteral("|") + m.text) %
                                          a.character->bodyCount());
            }
            maxchat::comic::ComicLineItem li;
            li.actorIndex = actorIndex.value(low);
            li.text = m.text;
            li.think = m.think;
            li.action = m.action;
            rlines.append(li);
        }
        if (actors.isEmpty()) {
            continue;
        }
        rendered.append(maxchat::comic::renderComicPanel(kSize, background, actors, rlines, captions,
                                                         captionScale, captionColors));
    }

    // Always present `panelCount` slots when art is available (Python parity):
    // pad with blank background panels so the configured number of panels is
    // visible even before enough messages arrive to fill them all. With no art at
    // all, leave it empty so ComicView shows its "set an art folder" hint.
    const bool haveArt = !m_comicCharacterPaths.isEmpty() || !background.isNull();
    if (haveArt && rendered.size() < panelCount) {
        const QPixmap blank =
            maxchat::comic::renderComicPanel(kSize, background, {}, {}, false, 1.0, {});
        while (rendered.size() < panelCount) {
            rendered.append(blank);
        }
    }
    m_comicView->setPanels(rendered);
}

void maxchat::ui::MainWindow::recolorMemberList() {
    if (m_memberList == nullptr) {
        return;
    }

    const ChatThemeDefinition chatTheme = chatThemeById(m_currentChatTheme);
    const bool plain = !m_coloredNicks || chatTheme.monoNicks;
    QStringList palette;
    palette.reserve(chatTheme.nickPalette.size());
    for (const QColor& color : chatTheme.nickPalette) {
        palette.append(color.name());
    }
    const QSet<QString> awayNicks = m_awayNicksByNetwork.value(activeNetworkName());

    for (int row = 0; row < m_memberList->count(); ++row) {
        QListWidgetItem* item = m_memberList->item(row);
        if (item == nullptr || item->data(Qt::UserRole + 1).toBool()) {
            continue; // group header rows keep their own styling
        }
        const QString nick = nickWithoutPrefix(item->text());
        const QString key = nick.toLower();
        const QString overrideColor = m_nickColorOverrides.value(key).toString();
        if (awayNicks.contains(key)) {
            item->setForeground(QColor(0x80, 0x80, 0x80));
        } else if (!overrideColor.isEmpty()) {
            // An explicit per-user colour wins, even in mono/colours-off modes.
            item->setForeground(QColor(overrideColor));
        } else if (plain || nick.isEmpty()) {
            item->setData(Qt::ForegroundRole, QVariant());
        } else {
            item->setForeground(QColor(maxchat::irc::nickColor(nick, palette)));
        }
    }
}

void maxchat::ui::MainWindow::resetAllSettings() {
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Reset Settings"),
        QStringLiteral("Reset ALL settings to defaults?\nNetworks, themes, colors and options "
                       "return to their first-run state."));
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (!m_settings.saveRaw(maxchat::core::SettingsStore::defaultSettings())) {
        appendSystemLine(QStringLiteral("! Could not reset settings."));
        return;
    }
    applyCurrentSettings();
    rebuildLooksMenu();
    appendSystemLine(QStringLiteral("! Settings were reset to defaults."));
}

void maxchat::ui::MainWindow::memberListChanged() {
    if (!m_coloredNicks) {
        renderActiveBufferMetadata(); // grouped view: rebuild groups + counts
    } else {
        recolorMemberList();
    }
}

void maxchat::ui::MainWindow::setNickColorOverride(const QString& nick) {
    const QString key = nick.trimmed().toLower();
    if (key.isEmpty()) {
        return;
    }
    const QColor current(m_nickColorOverrides.value(key).toString());
    const QColor picked =
        QColorDialog::getColor(current.isValid() ? current : QColor(Qt::white), this,
                               QStringLiteral("Color for %1").arg(nick));
    if (!picked.isValid()) {
        return;
    }
    m_nickColorOverrides.insert(key, picked.name());
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("nick_colors"), m_nickColorOverrides);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save nick colors."));
    }
    recolorMemberList();
    renderActiveBuffer();
}

void maxchat::ui::MainWindow::clearNickColorOverride(const QString& nick) {
    const QString key = nick.trimmed().toLower();
    if (m_nickColorOverrides.remove(key) == 0) {
        return;
    }
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("nick_colors"), m_nickColorOverrides);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(QStringLiteral("! Could not save nick colors."));
    }
    recolorMemberList();
    renderActiveBuffer();
}

void maxchat::ui::MainWindow::removeMemberFromChannelBuffers(const QString& nick) {
    removeMemberFromChannelBuffers(currentLogNetwork(), nick);
}

void maxchat::ui::MainWindow::removeMemberFromChannelBuffers(const QString& network, const QString& nick) {
    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffersForNetwork(network)) {
        if (id.kind == maxchat::core::ChatBufferKind::Channel) {
            const bool removed = m_chatBuffers.removeMember(id, nick);
            Q_UNUSED(removed);
        }
    }
}

void maxchat::ui::MainWindow::renameMemberInChannelBuffers(const QString& oldNick, const QString& newNick) {
    renameMemberInChannelBuffers(currentLogNetwork(), oldNick, newNick);
}

void maxchat::ui::MainWindow::renameMemberInChannelBuffers(const QString& network, const QString& oldNick,
                                              const QString& newNick) {
    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffersForNetwork(network)) {
        if (id.kind == maxchat::core::ChatBufferKind::Channel) {
            const bool renamed = m_chatBuffers.renameMember(id, oldNick, newNick);
            Q_UNUSED(renamed);
        }
    }
}

QStringList MainWindow::channelTargetsContainingMember(const QString& nick) const {
    return channelTargetsContainingMember(currentLogNetwork(), nick);
}

QStringList MainWindow::channelTargetsContainingMember(const QString& network,
                                                       const QString& nick) const {
    QStringList channels;
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (cleanNick.isEmpty()) {
        return channels;
    }

    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffersForNetwork(network)) {
        if (id.kind != maxchat::core::ChatBufferKind::Channel) {
            continue;
        }
        const maxchat::core::ChatBufferSnapshot snapshot = m_chatBuffers.snapshot(id);
        const auto memberIt = std::find_if(
            snapshot.members.cbegin(), snapshot.members.cend(),
            [&cleanNick](const QString& member) { return memberMatchesNick(member, cleanNick); });
        if (memberIt != snapshot.members.cend() && !snapshot.id.target.trimmed().isEmpty()) {
            channels.append(snapshot.id.target);
        }
    }
    return channels;
}

QStringList MainWindow::joinedChannelTargets() const {
    return joinedChannelTargets(currentLogNetwork());
}

QStringList MainWindow::joinedChannelTargets(const QString& network) const {
    QStringList channels;
    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffersForNetwork(network)) {
        if (id.kind != maxchat::core::ChatBufferKind::Channel) {
            continue;
        }
        const maxchat::core::ChatBufferSnapshot snapshot = m_chatBuffers.snapshot(id);
        if (snapshot.joined && !snapshot.id.target.trimmed().isEmpty()) {
            channels.append(snapshot.id.target);
        }
    }
    return channels;
}

QString MainWindow::activeNetworkName() const {
    const QString active = currentLogNetwork().trimmed();
    return active.isEmpty() ? QStringLiteral("Server") : active;
}

QString MainWindow::currentTargetForNetwork(const QString& network) const {
    const QString normalized = network.trimmed();
    if (normalized.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
        return m_currentTarget;
    }
    return m_currentTargetByNetwork.value(normalized);
}

QStringList MainWindow::openTargetsForNetwork(const QString& network) const {
    const QString normalized = network.trimmed();
    if (normalized.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
        return m_openTargets;
    }
    return m_openTargetsByNetwork.value(normalized);
}

bool MainWindow::networkRegistered(const QString& network) const {
    const QString normalized = network.trimmed();
    if (normalized.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
        return m_registered;
    }
    return m_registeredByNetwork.value(normalized, false);
}

void maxchat::ui::MainWindow::rememberNetwork(const QString& network) {
    const QString normalized = network.trimmed();
    if (normalized.isEmpty()) {
        return;
    }
    if (!containsCaseInsensitive(m_knownNetworks, normalized)) {
        m_knownNetworks.append(normalized);
    }
    if (!m_openTargetsByNetwork.contains(normalized)) {
        m_openTargetsByNetwork.insert(normalized, {});
    }
    if (!m_registeredByNetwork.contains(normalized)) {
        m_registeredByNetwork.insert(normalized, false);
    }
}

void maxchat::ui::MainWindow::setActiveNetwork(const QString& network) {
    const QString normalized = network.trimmed();
    if (normalized.isEmpty() || normalized.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
        return;
    }

    rememberNetwork(activeNetworkName());
    m_connectionPlansByNetwork.insert(activeNetworkName(), m_connectionPlan);
    m_currentTargetByNetwork.insert(activeNetworkName(), m_currentTarget);
    m_openTargetsByNetwork.insert(activeNetworkName(), m_openTargets);
    m_registeredByNetwork.insert(activeNetworkName(), m_registered);
    m_initialConnectAttemptsByNetwork.insert(activeNetworkName(), m_initialConnectAttempts);
    m_manualDisconnectByNetwork.insert(activeNetworkName(), m_manualDisconnect);
    m_reconnectRequestedByNetwork.insert(activeNetworkName(), m_reconnectRequested);

    rememberNetwork(normalized);
    if (m_connectionPlansByNetwork.contains(normalized)) {
        m_connectionPlan = m_connectionPlansByNetwork.value(normalized);
    } else {
        m_connectionPlan.networkName = normalized;
    }
    m_currentTarget = m_currentTargetByNetwork.value(normalized);
    m_openTargets = m_openTargetsByNetwork.value(normalized);
    m_registered = m_registeredByNetwork.value(normalized, false);
    m_initialConnectAttempts = m_initialConnectAttemptsByNetwork.value(normalized, 0);
    m_manualDisconnect = m_manualDisconnectByNetwork.value(normalized, false);
    m_reconnectRequested = m_reconnectRequestedByNetwork.value(normalized, false);
    m_connectionUptimeRunning = m_connectionUptimeStartMsByNetwork.contains(normalized);
    activateBufferTarget(m_currentTarget);
    rebuildNetworkTree();
    updateChannelModeButton();
}

QString MainWindow::treeDisplayLabelForNetwork(const QString& network) {
    const QString trimmed = network.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    int unreadCount = 0;
    int highlightCount = 0;
    for (const QString& target : visibleTreeTargets(trimmed)) {
        const maxchat::core::ChatBufferId id = bufferIdForNetworkTarget(trimmed, target);
        const maxchat::core::ChatBufferSnapshot snapshot = m_chatBuffers.snapshot(id);
        unreadCount += snapshot.unreadCount;
        highlightCount += snapshot.highlightCount;
    }

    return labelWithTreeCounts(trimmed, unreadCount, highlightCount);
}

QStringList MainWindow::visibleTreeTargets(const QString& network) const {
    QStringList targets;
    for (const QString& target : openTargetsForNetwork(network)) {
        const QString trimmed = target.trimmed();
        if (!trimmed.isEmpty() && !containsCaseInsensitive(targets, trimmed)) {
            targets.append(trimmed);
        }
    }

    const QString current = currentTargetForNetwork(network).trimmed();
    if (!current.isEmpty() && !containsCaseInsensitive(targets, current)) {
        targets.append(current);
    }
    return targets;
}

QStringList MainWindow::visibleTreeTargets() const {
    return visibleTreeTargets(activeNetworkName());
}

QString MainWindow::treeDisplayLabelForTarget(const QString& network, const QString& target) {
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    if (isTreeStatusTarget(trimmed)) {
        return QStringLiteral("Server");
    }

    const maxchat::core::ChatBufferSnapshot snapshot =
        m_chatBuffers.snapshot(bufferIdForNetworkTarget(network, trimmed));
    return labelWithTreeCounts(trimmed, snapshot.unreadCount, snapshot.highlightCount);
}

QString MainWindow::treeDisplayLabelForTarget(const QString& target) {
    return treeDisplayLabelForTarget(activeNetworkName(), target);
}

void maxchat::ui::MainWindow::updateNetworkTreeLabels() {
    if (m_networkTree == nullptr) {
        return;
    }

    for (int topIndex = 0; topIndex < m_networkTree->topLevelItemCount(); ++topIndex) {
        QTreeWidgetItem* rootItem = m_networkTree->topLevelItem(topIndex);
        if (rootItem == nullptr) {
            continue;
        }
        const QString network = treeItemNetwork(rootItem);
        if (!network.isEmpty()) {
            rootItem->setText(0, treeDisplayLabelForNetwork(network));
        }
        for (int childIndex = 0; childIndex < rootItem->childCount(); ++childIndex) {
            QTreeWidgetItem* item = rootItem->child(childIndex);
            const QString target = treeItemTarget(item);
            if (target.isEmpty()) {
                continue;
            }
            item->setText(0, treeDisplayLabelForTarget(network, target));
        }
    }
    syncBufferTabs();
}

void maxchat::ui::MainWindow::rememberTarget(const QString& target) {
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    for (const QString& openTarget : m_openTargets) {
        if (openTarget.compare(trimmed, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    m_openTargets.append(trimmed);
    m_openTargetsByNetwork.insert(activeNetworkName(), m_openTargets);
}

void maxchat::ui::MainWindow::forgetTarget(const QString& target) {
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    for (int index = m_openTargets.size() - 1; index >= 0; --index) {
        if (m_openTargets.at(index).compare(trimmed, Qt::CaseInsensitive) == 0) {
            m_openTargets.removeAt(index);
        }
    }
    m_openTargetsByNetwork.insert(activeNetworkName(), m_openTargets);
}

void maxchat::ui::MainWindow::rebuildNetworkTree() {
    if (m_networkTree == nullptr) {
        return;
    }
    const QSignalBlocker blocker(m_networkTree);
    m_networkTree->clear();

    if (!m_hasConnectionPlan && m_knownNetworks.isEmpty()) {
        // Network roots ARE the server buffer rows - no separate "Server" child.
        auto* rootItem = newTreeItem(QStringLiteral("Server"), QStringLiteral("server"));
        m_networkTree->addTopLevelItem(rootItem);
        m_networkTree->expandAll();
        m_networkTree->setCurrentItem(rootItem);
        return;
    }

    QStringList networks = m_knownNetworks;
    if (m_hasConnectionPlan && !containsCaseInsensitive(networks, m_connectionPlan.networkName)) {
        networks.append(m_connectionPlan.networkName);
    }

    for (const QString& network : networks) {
        const QString cleanNetwork = network.trimmed();
        if (cleanNetwork.isEmpty()) {
            continue;
        }
        const bool active = cleanNetwork.compare(activeNetworkName(), Qt::CaseInsensitive) == 0;
        const QString status = networkRegistered(cleanNetwork) ? QStringLiteral("Connected")
                               : active                        ? QStringLiteral("Connecting")
                                                               : QStringLiteral("Disconnected");
        auto* rootItem = newTreeItem(treeDisplayLabelForNetwork(cleanNetwork),
                                     QStringLiteral("server"), cleanNetwork);
        rootItem->setToolTip(0, status);
        if (active && currentTargetForNetwork(cleanNetwork).trimmed().isEmpty()) {
            m_networkTree->setCurrentItem(rootItem);
        }
        for (const QString& target : visibleTreeTargets(cleanNetwork)) {
            auto* item =
                newTreeItem(treeDisplayLabelForTarget(cleanNetwork, target), target, cleanNetwork);
            rootItem->addChild(item);
            if (active &&
                target.compare(currentTargetForNetwork(cleanNetwork), Qt::CaseInsensitive) == 0) {
                m_networkTree->setCurrentItem(item);
            }
        }
        m_networkTree->addTopLevelItem(rootItem);
    }
    m_networkTree->expandAll();
    if (m_networkTree->currentItem() == nullptr) {
        QTreeWidgetItem* rootItem = m_networkTree->topLevelItem(0);
        if (rootItem != nullptr) {
            m_networkTree->setCurrentItem(rootItem);
        }
    }
    syncBufferTabs();
}

void maxchat::ui::MainWindow::updateChannelModeButton() {
    if (m_channelModesButton != nullptr) {
        m_channelModesButton->setEnabled(connection().isConnected() &&
                                         isChannelTarget(m_currentTarget.trimmed()));
    }
}

QString MainWindow::currentLogNetwork() const {
    const QString network = m_connectionPlan.networkName.trimmed();
    return network.isEmpty() ? QStringLiteral("Server") : network;
}

QString MainWindow::currentLogTarget() const {
    const QString target = m_currentTarget.trimmed();
    return target.isEmpty() ? QStringLiteral("server") : target;
}

QString MainWindow::currentNickForNetwork(const QString& network) const {
    const QString cleanNetwork = network.trimmed();
    const maxchat::core::NetworkConnectionPlan plan =
        m_connectionPlansByNetwork.value(cleanNetwork, m_connectionPlan);
    const maxchat::irc::IrcConnection* irc = connectionForNetwork(cleanNetwork);
    if (irc != nullptr && !irc->nick().isEmpty()) {
        return irc->nick();
    }
    return plan.nick.trimmed().isEmpty() ? QStringLiteral("comicfan") : plan.nick.trimmed();
}

maxchat::irc::IrcConnection::ConnectConfig
MainWindow::connectConfigFor(const maxchat::irc::ServerEndpoint& server) const {
    return connectConfigFor(server, m_connectionPlan);
}

maxchat::irc::IrcConnection::ConnectConfig
MainWindow::connectConfigFor(const maxchat::irc::ServerEndpoint& server,
                             const maxchat::core::NetworkConnectionPlan& plan) const {
    maxchat::irc::IrcConnection::ConnectConfig config;
    config.host = server.host;
    config.port = server.port;
    config.tls = server.tls;
    config.nick = plan.nick;
    config.username = plan.username;
    config.realname = plan.realname;
    config.serverPassword = plan.serverPassword;
    config.saslPassword = plan.saslPassword;
    config.saslAccount = plan.saslAccount;
    config.acceptInvalidCertificate = plan.acceptInvalidCertificate;
    config.allowInsecureAuth = plan.allowInsecureAuth;
    config.autojoin = plan.autojoin;
    config.connectTimeoutMs = plan.connectTimeoutMs;
    config.registrationTimeoutMs = plan.connectTimeoutMs;
    config.proxyType = plan.proxyType;
    config.proxyHost = plan.proxyHost;
    config.proxyPort = plan.proxyPort;
    config.proxyUsername = plan.proxyUsername;
    config.proxyPassword = plan.proxyPassword;
    return config;
}

int MainWindow::maxInitialConnectAttempts() const {
    return maxInitialConnectAttempts(currentLogNetwork());
}

int MainWindow::maxInitialConnectAttempts(const QString& network) const {
    const maxchat::core::NetworkConnectionPlan plan =
        m_connectionPlansByNetwork.value(network.trimmed(), m_connectionPlan);
    return std::max(1, static_cast<int>(plan.reconnect.servers.size()) *
                           maxchat::irc::ServerRetryLimit);
}

void maxchat::ui::MainWindow::loadFonts() {
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMono-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMono-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/ComicRelief-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/ComicRelief-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/SymbolsNerdFontMono-Regular.ttf"));
}

void maxchat::ui::MainWindow::configureSpellcheck(const QVariantMap& settings) {
    if (m_spellcheckHighlighter == nullptr) {
        return;
    }

#ifdef MAXCHAT_WITH_HUNSPELL
    if (!settings.value(QStringLiteral("spellcheck_enabled"), true).toBool()) {
        m_spellcheckHighlighter->setWordChecker({});
        m_spellcheckHighlighter->setSpellcheckEnabled(false);
        if (m_spellchecker) {
            m_spellchecker->unload();
        }
        return;
    }

    const QString requestedLanguage =
        settings.value(QStringLiteral("spell_language"), QStringLiteral("en")).toString();
    const QList<maxchat::spell::SpellcheckLanguage> languages =
        maxchat::spell::spellcheckLanguages();
    auto languageIt =
        std::find_if(languages.cbegin(), languages.cend(),
                     [&requestedLanguage](const maxchat::spell::SpellcheckLanguage& language) {
                         return language.code.compare(requestedLanguage, Qt::CaseInsensitive) == 0;
                     });
    if (languageIt == languages.cend() || !languageIt->dictionaryAvailable()) {
        languageIt = std::find_if(languages.cbegin(), languages.cend(),
                                  [](const maxchat::spell::SpellcheckLanguage& language) {
                                      return language.code == QStringLiteral("en") &&
                                             language.dictionaryAvailable();
                                  });
    }

    if (languageIt == languages.cend() || !languageIt->dictionaryAvailable()) {
        m_spellcheckHighlighter->setWordChecker({});
        m_spellcheckHighlighter->setSpellcheckEnabled(false);
        if (m_spellchecker) {
            m_spellchecker->unload();
        }
        return;
    }

    if (!m_spellchecker) {
        m_spellchecker = std::make_unique<maxchat::spell::HunspellSpellchecker>();
    }
    if (!m_spellchecker->loadDictionary(languageIt->affPath, languageIt->dicPath)) {
        m_spellcheckHighlighter->setWordChecker({});
        m_spellcheckHighlighter->setSpellcheckEnabled(false);
        return;
    }

    m_spellcheckHighlighter->setWordChecker([this](const QString& word) {
        return m_spellchecker != nullptr && m_spellchecker->isLoaded() &&
               m_spellchecker->isCorrect(word);
    });
    m_spellcheckHighlighter->setSpellcheckEnabled(true);
#else
    Q_UNUSED(settings);
    m_spellcheckHighlighter->setWordChecker({});
    m_spellcheckHighlighter->setSpellcheckEnabled(false);
#endif
}

void maxchat::ui::MainWindow::resizeMessageInput() {
    if (m_input == nullptr) {
        return;
    }
    const int height = QFontMetrics(m_input->font()).lineSpacing() + 16;
    m_input->setMinimumHeight(height);
    m_input->setMaximumHeight(height);
}

void maxchat::ui::MainWindow::applyTheme(const QString& theme) {
    const QString normalized = normalizeThemeId(theme);
    const QString styleSheet =
        styleSheetForAppearance(normalized, m_currentChatTheme, m_currentWallpaper);
    // Apply palette + stylesheet app-wide so parentless dialogs are themed too,
    // and the OS palette can't bleed into widgets the QSS doesn't cover.
    if (normalized == systemThemeId()) {
        if (QStyle* style = QApplication::style()) {
            qApp->setPalette(style->standardPalette());
        }
        qApp->setStyleSheet(QString());
    } else {
        qApp->setPalette(paletteForAppearance(normalized));
        qApp->setStyleSheet(styleSheet);
    }
    updateTrayIcon();
    setWindowIcon(ui::AppIcon::makeIcon(
        m_settings.loadWithDefaults().value(QStringLiteral("tray_icon"), QStringLiteral("bubble")).toString(),
        appThemeById(normalized).accent));
}

void maxchat::ui::MainWindow::setTheme(const QString& theme, const bool save) {
    const QString normalized = normalizeThemeId(theme);
    if (save) {
        QVariantMap settings = m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("theme"), normalized);
        if (!m_settings.saveRaw(settings)) {
            appendSystemLine(QStringLiteral("! Could not save theme."));
        }
    }
    m_currentTheme = normalized;
    applyTheme(m_currentTheme);
    syncThemeActions(m_currentTheme);
    renderActiveBuffer();
    updateChatSeparatorGuide();
}

void maxchat::ui::MainWindow::syncThemeActions(const QString& theme) {
    const QString normalized = normalizeThemeId(theme);
    for (QAction* action : m_themeActions) {
        if (action == nullptr) {
            continue;
        }
        const QSignalBlocker blocker(action);
        action->setChecked(action->data().toString() == normalized);
    }
}

void maxchat::ui::MainWindow::setChatTheme(const QString& chatTheme, const bool save) {
    const QString normalized = normalizeChatThemeId(chatTheme);
    if (save) {
        QVariantMap settings = m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("chat_theme"), normalized);
        if (!m_settings.saveRaw(settings)) {
            appendSystemLine(QStringLiteral("! Could not save chat theme."));
        }
    }
    m_currentChatTheme = normalized;
    applyTheme(m_currentTheme);
    syncChatThemeActions(m_currentChatTheme);
    renderActiveBuffer();
    recolorMemberList();
    updateChatSeparatorGuide();
}

void maxchat::ui::MainWindow::syncChatThemeActions(const QString& chatTheme) {
    const QString normalized = normalizeChatThemeId(chatTheme);
    for (QAction* action : m_chatThemeActions) {
        if (action == nullptr) {
            continue;
        }
        const QSignalBlocker blocker(action);
        action->setChecked(action->data().toString() == normalized);
    }
}

void maxchat::ui::MainWindow::setWallpaper(const QString& wallpaper, const bool save) {
    const QString normalized = normalizeWallpaperValue(wallpaper);
    if (save) {
        QVariantMap settings = m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("wallpaper"), normalized);
        if (!m_settings.saveRaw(settings)) {
            appendSystemLine(QStringLiteral("! Could not save wallpaper."));
        }
    }
    m_currentWallpaper = normalized;
    applyTheme(m_currentTheme);
    syncWallpaperActions(m_currentWallpaper);
}

void maxchat::ui::MainWindow::syncWallpaperActions(const QString& wallpaper) {
    const QString normalized = normalizeWallpaperValue(wallpaper);
    for (QAction* action : m_wallpaperActions) {
        if (action == nullptr) {
            continue;
        }
        const QSignalBlocker blocker(action);
        action->setChecked(action->data().toString() == normalized);
    }
}

void maxchat::ui::MainWindow::applyCurrentSettings() {
    const QVariantMap settings = m_settings.loadWithDefaults();
    m_commandAliases = settings.value(QStringLiteral("command_aliases")).toMap();
    m_ignoreMasks = variantStringList(settings.value(QStringLiteral("ignores")));
    m_mutedChannelKeys = variantStringList(settings.value(QStringLiteral("muted_channels")));
    m_friendNicks = variantStringList(settings.value(QStringLiteral("friends")));
    m_autoReconnect = settings.value(QStringLiteral("auto_reconnect"), true).toBool();
    m_loggingEnabled = settings.value(QStringLiteral("logging"), true).toBool();
    m_replayLogEnabled = settings.value(QStringLiteral("replay_log"), true).toBool();
    m_showTimestamps = settings.value(QStringLiteral("show_timestamps"), true).toBool();
    m_timestampFormat =
        settings.value(QStringLiteral("timestamp_format"), QStringLiteral("%I:%M %p")).toString();
    m_alignNicks = settings.value(QStringLiteral("align_nicks"), true).toBool();
    m_separatorLine = settings.value(QStringLiteral("separator_line"), true).toBool();
    m_hideJoinPart = settings.value(QStringLiteral("hide_joinpart"), false).toBool();
    m_showFormatting = settings.value(QStringLiteral("show_formatting"), true).toBool();
    m_coloredNicks = settings.value(QStringLiteral("colored_nicks"), true).toBool();
    m_showMode = settings.value(QStringLiteral("show_mode"), true).toBool();
    m_pmEcho = settings.value(QStringLiteral("pm_echo"), true).toBool();
    m_indentWrap = settings.value(QStringLiteral("indent_wrap"), true).toBool();
    m_markerLine = settings.value(QStringLiteral("marker_line"), true).toBool();
    m_replayLines = settings.value(QStringLiteral("replay_lines"), 0).toInt();
    m_nickColorOverrides = settings.value(QStringLiteral("nick_colors")).toMap();
    m_sortByStatus =
        settings.value(QStringLiteral("sort_status"),
                       settings.value(QStringLiteral("sort_users_by_status"), true))
            .toBool();
    if (auto* chatView = dynamic_cast<ChatTextView*>(m_chatView)) {
        chatView->setStripColorsOnCopy(
            settings.value(QStringLiteral("strip_color_copy"), true).toBool());
    }
    m_pasteGuard = settings.value(QStringLiteral("paste_guard"), true).toBool();
    m_pasteLines = settings.value(QStringLiteral("paste_lines"), 4).toInt();
    m_autoRejoin = settings.value(QStringLiteral("auto_rejoin"), false).toBool();
    m_rejoinDelay = settings.value(QStringLiteral("rejoin_delay"), 2).toInt();
    m_ignoreInvites = settings.value(QStringLiteral("ignore_invites"), false).toBool();
    m_inviteProtect = settings.value(QStringLiteral("invite_protect"), true).toBool();
    m_confirmQuit = settings.value(QStringLiteral("confirm_quit"), true).toBool();
    m_scrollback = std::max(100, settings.value(QStringLiteral("scrollback"), 2000).toInt());
    m_chatBuffers.setMaxLinesPerBuffer(m_scrollback);
    m_autoAwayMins = settings.value(QStringLiteral("auto_away_mins"), 0).toInt();
    if (m_autoAwayMins > 0) {
        m_autoAwayTimer.start(m_autoAwayMins * 60 * 1000);
    } else {
        m_autoAwayTimer.stop();
    }
    applyCtcpVersion(settings);
    if (m_input != nullptr) {
        m_input->setPlaceholderText(
            settings.value(QStringLiteral("show_input_hint"), true).toBool()
                ? QStringLiteral(
                      "Message - Tab completes - Up/Down history - Ctrl+B/I/U/K format")
                : QString());
    }
    m_chatLogStore.setLogMask(settings.value(QStringLiteral("log_mask")).toString());
    applyNavShortcutOverrides(settings);
    recolorMemberList();
    m_linkPreviewToggles = maxchat::services::linkPreviewTogglesFromSettings(settings);
    if (m_mainSplitter != nullptr) {
        const QVariantList savedSizes = settings.value(QStringLiteral("splitter_sizes")).toList();
        if (savedSizes.size() >= 3) {
            QList<int> sizes;
            sizes.reserve(3);
            for (int index = 0; index < 3; ++index) {
                sizes.append(std::max(0, savedSizes.at(index).toInt()));
            }
            m_mainSplitter->setSizes(sizes);
        }
    }
    setButtonBarVisible(settings.value(QStringLiteral("show_button_bar"), true).toBool(), false);
    setBufferTabsVisible(settings.value(QStringLiteral("buffer_tabs"), false).toBool(), false);
    setServerListVisible(settings.value(QStringLiteral("server_list_visible"), true).toBool(),
                         false);
    setMembersVisible(settings.value(QStringLiteral("member_list_visible"), true).toBool(), false);
    m_nickColumnWidth = std::clamp(settings.value(QStringLiteral("nick_width"), 16).toInt(), 4, 40);
    if (m_chatSeparatorAction != nullptr && m_chatSeparatorAction->isChecked() != m_separatorLine) {
        const QSignalBlocker blocker(m_chatSeparatorAction);
        m_chatSeparatorAction->setChecked(m_separatorLine);
    }
    if (m_doNotDisturbAction != nullptr &&
        m_doNotDisturbAction->isChecked() !=
            settings.value(QStringLiteral("dnd"), false).toBool()) {
        const QSignalBlocker blocker(m_doNotDisturbAction);
        m_doNotDisturbAction->setChecked(settings.value(QStringLiteral("dnd"), false).toBool());
    }
    if (m_comicCaptionsAction != nullptr &&
        m_comicCaptionsAction->isChecked() !=
            settings.value(QStringLiteral("comic_captions"), true).toBool()) {
        const QSignalBlocker blocker(m_comicCaptionsAction);
        m_comicCaptionsAction->setChecked(
            settings.value(QStringLiteral("comic_captions"), true).toBool());
    }
    m_floodGuard.configure(settings.value(QStringLiteral("flood_protect"), false).toBool(),
                           settings.value(QStringLiteral("flood_msgs"), 10).toInt(),
                           settings.value(QStringLiteral("flood_secs"), 4).toInt());
    configureSpellcheck(settings);
    m_connection.setIgnoreMasks(m_ignoreMasks);
    for (auto* irc : std::as_const(m_connectionsByNetwork)) {
        if (irc != nullptr) {
            irc->setIgnoreMasks(m_ignoreMasks);
        }
    }
    if (m_friendNicks.isEmpty() || !anyNetworkConnectionIsConnected()) {
        m_friendPollTimer.stop();
    } else {
        m_haveFriendSnapshot = false;
        m_haveFriendSnapshotByNetwork.clear();
        pollFriends();
        m_friendPollTimer.start();
    }
    const auto configuredFont = [&settings](const QString& familyKey, const QString& sizeKey,
                                            const QString& boldKey) {
        QFont font(settings.value(familyKey, QStringLiteral("JetBrains Mono")).toString(),
                   settings.value(sizeKey, 14).toInt());
        font.setBold(settings.value(boldKey, true).toBool());
        return font;
    };

    const QFont appFont =
        configuredFont(QStringLiteral("app_font_family"), QStringLiteral("app_font_size"),
                       QStringLiteral("app_font_bold"));
    const QFont chatFont =
        configuredFont(QStringLiteral("chat_font_family"), QStringLiteral("chat_font_size"),
                       QStringLiteral("chat_font_bold"));
    const QFont listFont =
        configuredFont(QStringLiteral("list_font_family"), QStringLiteral("list_font_size"),
                       QStringLiteral("list_font_bold"));
    qApp->setFont(appFont);
    if (m_chatView != nullptr) {
        m_chatView->setFont(chatFont);
        m_chatView->setLineWrapMode(settings.value(QStringLiteral("word_wrap"), true).toBool()
                                        ? QTextEdit::WidgetWidth
                                        : QTextEdit::NoWrap);
    }
    if (m_input != nullptr) {
        m_input->setFont(chatFont);
        resizeMessageInput();
    }
    if (m_networkTree != nullptr) {
        m_networkTree->setFont(listFont);
    }
    if (m_memberList != nullptr) {
        m_memberList->setFont(listFont);
    }
    if (m_channelModesButton != nullptr) {
        m_channelModesButton->setFont(listFont);
    }

    m_currentTheme = normalizeThemeId(
        settings.value(QStringLiteral("theme"), QStringLiteral("synthwave")).toString());
    m_currentChatTheme = normalizeChatThemeId(
        settings.value(QStringLiteral("chat_theme"), QStringLiteral("follow")).toString());
    m_currentWallpaper =
        normalizeWallpaperValue(settings.value(QStringLiteral("wallpaper")).toString());
    applyTheme(m_currentTheme);
    // Re-apply the app font AFTER the theme: setting a global stylesheet re-polishes
    // widgets and drops qApp->setFont for chrome (menu bar, menus, dialogs), so the
    // window/menu font would otherwise ignore the configured app font. Set it on the
    // menu bar explicitly too (popup menus inherit from it).
    qApp->setFont(appFont);
    if (menuBar() != nullptr) {
        menuBar()->setFont(appFont);
    }
    syncThemeActions(m_currentTheme);
    syncChatThemeActions(m_currentChatTheme);
    syncWallpaperActions(m_currentWallpaper);

    // Per-area color overrides ride on widget-level style sheets so they win
    // over the window-level theme QSS; empty = follow the theme.
    const auto colorOverride = [&settings](const char* key) {
        return settings.value(QLatin1String(key)).toString().trimmed();
    };
    const QString chatTextColor = colorOverride("chat_text_color");
    if (m_chatView != nullptr) {
        m_chatView->setStyleSheet(
            chatTextColor.isEmpty()
                ? QString()
                : QStringLiteral("QTextBrowser#chatView{color:%1;}").arg(chatTextColor));
    }
    const QString treeColor = colorOverride("tree_color");
    if (m_networkTree != nullptr) {
        m_networkTree->setStyleSheet(
            treeColor.isEmpty() ? QString()
                                : QStringLiteral("QTreeWidget{color:%1;}").arg(treeColor));
    }
    const QString usersColor = colorOverride("userlist_color");
    if (m_memberList != nullptr) {
        m_memberList->setStyleSheet(
            usersColor.isEmpty() ? QString()
                                 : QStringLiteral("QListWidget{color:%1;}").arg(usersColor));
    }
    const QString statusColor = colorOverride("status_text_color");
    statusBar()->setStyleSheet(
        statusColor.isEmpty() ? QString()
                              : QStringLiteral("QStatusBar{color:%1;}").arg(statusColor));
    const QString topicColor = colorOverride("topic_color");
    if (m_topicLabel != nullptr) {
        m_topicLabel->setStyleSheet(
            topicColor.isEmpty() ? QString()
                                 : QStringLiteral("QLabel{color:%1;}").arg(topicColor));
    }
    m_eventColor = colorOverride("event_color");

    // Per-area fonts (list/nick/status/topic). Empty family / size 0 = inherit.
    const auto areaFont = [&settings](const char* familyKey, const char* sizeKey,
                                      const char* boldKey, const QFont& base) {
        QFont font = base;
        const QString family = settings.value(QLatin1String(familyKey)).toString().trimmed();
        if (!family.isEmpty()) {
            font.setFamily(family);
        }
        const int size = settings.value(QLatin1String(sizeKey)).toInt();
        if (size > 0) {
            font.setPointSize(size);
        }
        font.setBold(settings.value(QLatin1String(boldKey)).toBool());
        return font;
    };
    const QFont baseFont = qApp->font();
    if (m_networkTree != nullptr) {
        m_networkTree->setFont(
            areaFont("list_font_family", "list_font_size", "list_font_bold", baseFont));
    }
    if (m_memberList != nullptr) {
        m_memberList->setFont(
            areaFont("list_font_family", "list_font_size", "list_font_bold", baseFont));
    }
    if (m_topicLabel != nullptr) {
        m_topicLabel->setFont(
            areaFont("topic_font_family", "topic_font_size", "topic_font_bold", baseFont));
    }
    statusBar()->setFont(
        areaFont("status_font_family", "status_font_size", "status_font_bold", baseFont));

    // Your-nick label beside the input (text, font, colour).
    if (m_nickLabel != nullptr) {
        const QString nick = m_hasConnectionPlan ? currentNickForNetwork(activeNetworkName())
                                                 : QString();
        m_nickLabel->setText(nick.isEmpty() ? QString() : QStringLiteral("%1:").arg(nick));
        m_nickLabel->setVisible(!nick.isEmpty());
        m_nickLabel->setFont(
            areaFont("nick_font_family", "nick_font_size", "nick_font_bold", baseFont));
        const QString nickColor = colorOverride("nick_label_color");
        m_nickLabel->setStyleSheet(
            nickColor.isEmpty() ? QString() : QStringLiteral("QLabel{color:%1;}").arg(nickColor));
    }

    updateTrayIcon();
    updateMinimizeToTrayFromSettings();
    m_highlightWords = settings.value(QStringLiteral("highlight_words")).toString().split(
        QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    m_notifyPm = settings.value(QStringLiteral("notify_pm"), true).toBool();
    m_notifyHighlight = settings.value(QStringLiteral("notify_highlight"), true).toBool();
    m_beepHighlight = settings.value(QStringLiteral("beep_highlight"), true).toBool();
    m_notifyFlash = settings.value(QStringLiteral("notify_flash"), true).toBool();
    m_notifySound = settings.value(QStringLiteral("notify_sound"), false).toBool();
    m_notifyStyle = settings.value(QStringLiteral("notify_popup"), QStringLiteral("custom")).toString();
    m_notifyCorner = settings.value(QStringLiteral("notify_corner"), QStringLiteral("br")).toString();
    m_notifyDuration = settings.value(QStringLiteral("notify_duration"), 6).toInt();
    m_notifyTheme = settings.value(QStringLiteral("notify_theme"), QStringLiteral("follow")).toString();
    renderActiveBuffer();
    updateChatSeparatorGuide();
    renderActiveBufferMetadata();
    updateNetworkTreeLabels();
    updateChannelModeButton();
    applyScriptPermissions(); // re-sandbox scripts if the permission prefs changed
    }
void maxchat::ui::MainWindow::notify(const QString& title, const QString& text,
                        const QString& network, const QString& target) {
    // Don't notify if window is active or DND is on (matches Python)
    if (isActiveWindow() || !m_notifier) return;
    if (m_settings.loadWithDefaults().value(QStringLiteral("dnd"), false).toBool()) return;

    // Taskbar flash
    if (m_notifyFlash) {
        QApplication::alert(this, 0);
    }

    // Sound — the user's notify.wav (or a bundled default) via QSoundEffect,
    // falling back to the system beep if no .wav is available.
    if (m_notifySound) {
        const QString soundsDir =
            QDir(m_settings.paths().configDir).filePath(QStringLiteral("sounds"));
        const QString bundled = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("assets/sounds"));
        if (!m_soundPlayer.play(notifySoundPath(soundsDir, bundled))) {
            QApplication::beep();
        }
    }

    // Popup style
    if (m_notifyStyle == QLatin1String("off")) return;

    // OS native notification - post through the visible tray icon (showMessage
    // on a hidden tray is a no-op), available only when a daemon/tray accepts it.
    if (m_notifyStyle == QLatin1String("system") && m_osNotifyAvailable && m_tray != nullptr) {
        m_tray->showMessage(title, text, m_tray->icon(), 5000);
        return;
    }

    // Custom toast (also fallback when system tray unavailable)
    const AppThemeDefinition& themeDef = appThemeById(m_currentTheme);
    QColor followBg = themeDef.panel.isValid() ? themeDef.panel : QColor(QStringLiteral("#2b2b2b"));
    QColor followFg = themeDef.text.isValid() ? themeDef.text : QColor(QStringLiteral("#e8e8e8"));
    QColor followAccent = themeDef.accent.isValid() ? themeDef.accent : QColor(QStringLiteral("#4a9eff"));

    QColor bg = Notifier::paletteBg(m_notifyTheme, followBg);
    QColor fg = Notifier::paletteFg(m_notifyTheme, followFg);
    QColor accent = Notifier::paletteAccent(m_notifyTheme, followAccent);

    const int durationMs = m_notifyDuration * 1000;

    QIcon icon;
    icon = ui::AppIcon::makeIcon(
        m_settings.loadWithDefaults().value(QStringLiteral("tray_icon"), QStringLiteral("bubble")).toString(),
        accent);

    std::function<void()> onClick;
    if (!network.isEmpty() && !target.isEmpty()) {
        QString net = network;
        QString tgt = target;
        onClick = [this, net, tgt]() {
            show();
            raise();
            activateWindow();
            setActiveNetwork(net);
            activateBufferTarget(tgt);
        };
    } else {
        onClick = [this]() {
            show();
            raise();
            activateWindow();
        };
    }

    m_notifier->show(title, text, bg, fg, accent, m_notifyCorner, durationMs, icon, std::move(onClick));
}

void maxchat::ui::MainWindow::setupTrayIcon() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = nullptr;
        return;
    }

    m_tray = new ::QSystemTrayIcon(this);
    m_trayMenu = new QMenu(this);

    m_trayMenu->addAction(QStringLiteral("Show / Hide"), this, &MainWindow::toggleWindowVisibility);
    m_trayMenu->addSeparator();

    // Reuse the menu-bar DND action so both stay in sync (matches Python)
    if (m_doNotDisturbAction) {
        m_trayMenu->addAction(m_doNotDisturbAction);
    }
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(QStringLiteral("Quit"), this, &QWidget::close);

    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &::QSystemTrayIcon::activated, this, [this](::QSystemTrayIcon::ActivationReason r) {
        if (r == ::QSystemTrayIcon::Trigger) {
            toggleWindowVisibility();
        }
    });

    updateTrayIcon();
    m_tray->show();
}

void maxchat::ui::MainWindow::updateTrayIcon() {
    if (!m_tray) return;

    // Pull accent from current theme, fallback to default blue
    QColor accent = appThemeById(m_currentTheme).accent;
    if (!accent.isValid()) {
        accent = QColor(QStringLiteral("#4a9eff"));
    }

    QString choice = m_settings.loadWithDefaults()
        .value(QStringLiteral("tray_icon"), QStringLiteral("bubble")).toString();

    QIcon icon = ui::AppIcon::makeIcon(choice, accent);
    m_tray->setIcon(icon);
    m_tray->setToolTip(app::displayName());
}

void maxchat::ui::MainWindow::toggleWindowVisibility() {
    if (isVisible() && !isMinimized()) {
        hide();
    } else {
        show();
        raise();
        activateWindow();
    }
}


void maxchat::ui::MainWindow::updateMinimizeToTrayFromSettings() {
    auto s = m_settings.loadWithDefaults();
    m_minimizeToTray = s.value(QStringLiteral("minimize_to_tray"), false).toBool();
}

} // namespace maxchat::ui
