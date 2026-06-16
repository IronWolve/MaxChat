#include <QApplication>
#include <QAction>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QHostAddress>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabBar>
#include <QTextCursor>
#include <QTextEdit>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTime>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QWidget>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>

// MainWindow grants this test class friendship (see MainWindow.h) so it can
// reach private members without `#define private public` (an ODR hazard).
#include "ui/AppearanceController.h"
#include "ui/MainWindow.h"
#include "ui/SpellTextEdit.h"

using maxchat::ui::MainWindow;

namespace {

QAction* findMenuAction(QMenuBar* menuBar, const QString& text) {
    if (menuBar == nullptr) {
        return nullptr;
    }
    for (QAction* menuAction : menuBar->actions()) {
        QMenu* menu = menuAction == nullptr ? nullptr : menuAction->menu();
        if (menu == nullptr) {
            continue;
        }
        const auto findInMenu = [&](QMenu* rootMenu, const auto& findInMenuRef) -> QAction* {
            for (QAction* action : rootMenu->actions()) {
                if (action == nullptr) {
                    continue;
                }
                QString actionText = action->text();
                actionText.replace(QStringLiteral("&&"), QStringLiteral("\x1f"));
                actionText.remove(QLatin1Char('&'));
                actionText.replace(QStringLiteral("\x1f"), QStringLiteral("&"));
                if (actionText == text) {
                    return action;
                }
                if (QMenu* childMenu = action->menu(); childMenu != nullptr) {
                    if (QAction* match = findInMenuRef(childMenu, findInMenuRef);
                        match != nullptr) {
                        return match;
                    }
                }
            }
            return nullptr;
        };
        if (QAction* match = findInMenu(menu, findInMenu); match != nullptr) {
            return match;
        }
    }
    return nullptr;
}

QTreeWidgetItem* findTreeItemByTarget(QTreeWidget* tree, const QString& target) {
    if (tree == nullptr) {
        return nullptr;
    }
    for (int topIndex = 0; topIndex < tree->topLevelItemCount(); ++topIndex) {
        QTreeWidgetItem* root = tree->topLevelItem(topIndex);
        if (root == nullptr) {
            continue;
        }
        if (root->data(0, Qt::UserRole).toString() == target) {
            return root;
        }
        for (int childIndex = 0; childIndex < root->childCount(); ++childIndex) {
            QTreeWidgetItem* child = root->child(childIndex);
            if (child != nullptr && child->data(0, Qt::UserRole).toString() == target) {
                return child;
            }
        }
    }
    return nullptr;
}

QTreeWidgetItem* findTreeItemByNetworkTarget(QTreeWidget* tree, const QString& network,
                                             const QString& target) {
    if (tree == nullptr) {
        return nullptr;
    }
    for (int topIndex = 0; topIndex < tree->topLevelItemCount(); ++topIndex) {
        QTreeWidgetItem* root = tree->topLevelItem(topIndex);
        if (root == nullptr ||
            root->data(0, Qt::UserRole + 1).toString().compare(network, Qt::CaseInsensitive) != 0) {
            continue;
        }
        for (int childIndex = 0; childIndex < root->childCount(); ++childIndex) {
            QTreeWidgetItem* child = root->child(childIndex);
            if (child != nullptr &&
                child->data(0, Qt::UserRole).toString().compare(target, Qt::CaseInsensitive) == 0) {
                return child;
            }
        }
    }
    return nullptr;
}

bool waitForSocketText(QTcpSocket* socket, const QByteArray& needle, const int timeoutMs = 1000) {
    if (socket == nullptr) {
        return false;
    }

    QByteArray data = socket->readAll();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (data.contains(needle)) {
            return true;
        }
        QTest::qWait(10);
        socket->waitForReadyRead(25);
        data += socket->readAll();
    }
    return data.contains(needle);
}

} // namespace

class MainWindowLinkPreviewTest final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
    }

    void directImagePreviewRendersWhenEnabled() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_linkPreviewToggles.images = true;
        window.appendSystemLine(QStringLiteral("<nick> https://images.example.net/picture.jpg"));

        const QString html = chatView->toHtml();
        QVERIFY(html.contains(QStringLiteral("<img")));
        QVERIFY(html.contains(QStringLiteral("https://images.example.net/picture.jpg")));
    }

    void directImagePreviewDoesNotRenderWhenDisabled() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_linkPreviewToggles.images = false;
        window.appendSystemLine(QStringLiteral("<nick> https://images.example.net/picture.jpg"));

        QVERIFY(!chatView->toHtml().contains(QStringLiteral("<img")));
    }

    void rawLogDisplayDoesNotRenderPreview() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_linkPreviewToggles.images = true;
        window.appendSystemLine(QStringLiteral("<-- :server.example NOTICE * "
                                               ":https://images.example.net/raw.jpg"),
                                false);

        QVERIFY(!chatView->toHtml().contains(QStringLiteral("<img")));
    }

    void rawIrcLinesDoNotPrintInChatView() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        chatView->clear();
        QMetaObject::invokeMethod(&window.m_connection, "rawLine", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("<--")),
                                  Q_ARG(QString, QStringLiteral(":server 001 nick :welcome")));

        QVERIFY(chatView->toPlainText().trimmed().isEmpty());
        QCOMPARE(window.m_rawLogLines.size(), 1);
        QVERIFY(window.m_rawLogLines.first().contains(QStringLiteral("001 nick")));
    }

    void directMediaPreviewUsesMediaToggle() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_linkPreviewToggles.media = true;
        window.appendSystemLine(QStringLiteral("<nick> https://media.example.net/song.mp3"));
        QVERIFY(chatView->toHtml().contains(QStringLiteral("song.mp3")));

        window.m_linkPreviewToggles.media = false;
        window.appendSystemLine(QStringLiteral("<nick> https://media.example.net/clip.mp4"));
        QVERIFY(!chatView->toHtml().contains(QStringLiteral("Video")));
    }

    void linkPreviewUsesMessageColumnIndent() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.m_linkPreviewToggles.images = true;
        window.m_showTimestamps = false;
        window.m_alignNicks = true;
        window.m_separatorLine = true;
        window.m_nickColumnWidth = 8;
        window.activateBufferTarget(QStringLiteral("#chat"));
        chatView->clear();

        window.appendSystemLine(QStringLiteral("<nick> https://images.example.net/picture.jpg"));

        const QTextBlock chatBlock = chatView->document()->firstBlock();
        QVERIFY(chatBlock.isValid());
        const QTextBlock previewBlock = chatBlock.next();
        QVERIFY(previewBlock.isValid());

        const QTextBlockFormat chatFormat = chatBlock.blockFormat();
        const QTextBlockFormat previewFormat = previewBlock.blockFormat();
        QVERIFY(chatFormat.leftMargin() > 0.0);
        QCOMPARE(previewFormat.leftMargin(), chatFormat.leftMargin());
        QCOMPARE(previewFormat.textIndent(), 0.0);
    }

    void menuExposesPlannedFeatureStubs() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#chat"));

        QAction* buttonsAsTabs =
            findMenuAction(window.menuBar(), QStringLiteral("Buttons as Tabs"));
        QVERIFY(buttonsAsTabs != nullptr);
        QVERIFY(buttonsAsTabs->isEnabled());
        QVERIFY(buttonsAsTabs->isCheckable());

        QAction* comicMode = findMenuAction(window.menuBar(), QStringLiteral("Comic Mode"));
        QAction* comicSettings =
            findMenuAction(window.menuBar(), QStringLiteral("Comic Settings..."));
        QAction* browseCharacters =
            findMenuAction(window.menuBar(), QStringLiteral("Browse Characters..."));
        QAction* saveComic = findMenuAction(window.menuBar(), QStringLiteral("Save Comic..."));
        QAction* characterNames =
            findMenuAction(window.menuBar(), QStringLiteral("Character Names"));
        QAction* commands =
            findMenuAction(window.menuBar(), QStringLiteral("Commands & Shortcuts..."));
        QAction* updates = findMenuAction(window.menuBar(), QStringLiteral("Check for Updates..."));
        QAction* keyboard =
            findMenuAction(window.menuBar(), QStringLiteral("Keyboard Shortcuts..."));
        QAction* scripts = findMenuAction(window.menuBar(), QStringLiteral("Scripts..."));
        QAction* transfers = findMenuAction(window.menuBar(), QStringLiteral("File Transfers..."));
        QAction* dnd = findMenuAction(window.menuBar(), QStringLiteral("Do Not Disturb"));
        QAction* about = findMenuAction(window.menuBar(), QStringLiteral("About"));
        QAction* clearCurrentChat =
            findMenuAction(window.menuBar(), QStringLiteral("Clear Current Chat"));
        QVERIFY(comicMode != nullptr);
        QVERIFY(comicSettings != nullptr);
        QVERIFY(browseCharacters != nullptr);
        QVERIFY(saveComic != nullptr);
        QVERIFY(characterNames != nullptr);
        QVERIFY(commands != nullptr);
        QVERIFY(updates != nullptr);
        QVERIFY(keyboard != nullptr);
        QVERIFY(scripts != nullptr);
        QVERIFY(transfers != nullptr);
        QVERIFY(dnd != nullptr);
        QVERIFY(about != nullptr);
        QVERIFY(clearCurrentChat != nullptr);
        QVERIFY(comicMode->isEnabled());
        QVERIFY(comicMode->isCheckable());
        QVERIFY(comicSettings->isEnabled());
        QVERIFY(characterNames->isCheckable());
        QVERIFY(dnd->isCheckable());
        QVERIFY(commands->isEnabled());
        QVERIFY(updates->isEnabled());
        QVERIFY(about->isEnabled());
        QVERIFY(clearCurrentChat->isEnabled());
    }

    void viewMenuTogglesSidePanelsAndPersistsSettings() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* networkTree = window.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        auto* memberPanel = window.findChild<QWidget*>(QStringLiteral("memberPanel"));
        auto* splitter = window.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
        QAction* serverListAction = findMenuAction(window.menuBar(), QStringLiteral("Server List"));
        QAction* membersAction = findMenuAction(window.menuBar(), QStringLiteral("Member List"));
        QVERIFY(networkTree != nullptr);
        QVERIFY(memberPanel != nullptr);
        QVERIFY(splitter != nullptr);
        QVERIFY(serverListAction != nullptr);
        QVERIFY(membersAction != nullptr);
        QVERIFY(serverListAction->isChecked());
        QVERIFY(membersAction->isChecked());
        QVERIFY(splitter->sizes().at(0) > 0);
        QVERIFY(splitter->sizes().at(2) > 0);

        serverListAction->trigger();
        membersAction->trigger();
        QVERIFY(!serverListAction->isChecked());
        QVERIFY(!membersAction->isChecked());
        QCOMPARE(splitter->sizes().at(0), 0);
        QCOMPARE(splitter->sizes().at(2), 0);

        QVariantMap saved = window.m_settings.loadRaw();
        QCOMPARE(saved.value(QStringLiteral("server_list_visible")).toBool(), false);
        QCOMPARE(saved.value(QStringLiteral("member_list_visible")).toBool(), false);

        MainWindow restored;
        auto* restoredNetworkTree = restored.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        auto* restoredMemberPanel = restored.findChild<QWidget*>(QStringLiteral("memberPanel"));
        auto* restoredSplitter = restored.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
        QAction* restoredServerListAction =
            findMenuAction(restored.menuBar(), QStringLiteral("Server List"));
        QAction* restoredMembersAction =
            findMenuAction(restored.menuBar(), QStringLiteral("Member List"));
        QVERIFY(restoredNetworkTree != nullptr);
        QVERIFY(restoredMemberPanel != nullptr);
        QVERIFY(restoredSplitter != nullptr);
        QVERIFY(restoredServerListAction != nullptr);
        QVERIFY(restoredMembersAction != nullptr);
        QVERIFY(!restoredServerListAction->isChecked());
        QVERIFY(!restoredMembersAction->isChecked());
        QCOMPARE(restoredSplitter->sizes().at(0), 0);
        QCOMPARE(restoredSplitter->sizes().at(2), 0);

        restoredServerListAction->trigger();
        restoredMembersAction->trigger();
        QVERIFY(restoredSplitter->sizes().at(0) > 0);
        QVERIFY(restoredSplitter->sizes().at(2) > 0);
        saved = restored.m_settings.loadRaw();
        QCOMPARE(saved.value(QStringLiteral("server_list_visible")).toBool(), true);
        QCOMPARE(saved.value(QStringLiteral("member_list_visible")).toBool(), true);
    }

    void buttonBarAndChatSeparatorTogglePersistAndRerender() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("mainToolbar"));
        auto* tabBar = window.findChild<QTabBar*>(QStringLiteral("bufferTabBar"));
        QAction* buttonBarAction = findMenuAction(window.menuBar(), QStringLiteral("Button Bar"));
        QAction* tabsAction = findMenuAction(window.menuBar(), QStringLiteral("Buttons as Tabs"));
        QAction* separatorAction =
            findMenuAction(window.menuBar(), QStringLiteral("Chat Separator"));
        QVERIFY(chatView != nullptr);
        QVERIFY(toolbar != nullptr);
        QVERIFY(tabBar != nullptr);
        QVERIFY(buttonBarAction != nullptr);
        QVERIFY(tabsAction != nullptr);
        QVERIFY(separatorAction != nullptr);
        const QStringList expectedToolbarLabels = {
            QStringLiteral("Servers"), QStringLiteral("Members"),   QStringLiteral("Channels"),
            QStringLiteral("Join"),    QStringLiteral("Comic"),     QStringLiteral("Emotion"),
            QStringLiteral("URLs"),    QStringLiteral("Transfers"), QStringLiteral("Prefs"),
        };
        QStringList toolbarLabels;
        for (QAction* action : toolbar->actions()) {
            QWidget* widget = toolbar->widgetForAction(action);
            auto* button = qobject_cast<QToolButton*>(widget);
            if (button != nullptr) {
                toolbarLabels.append(button->text());
            }
        }
        QCOMPARE(toolbarLabels, expectedToolbarLabels);
        window.setButtonBarVisible(true, false);
        window.setBufferTabsVisible(false, false);
        window.setChatSeparatorVisible(true, false);
        QVERIFY(!toolbar->isHidden());
        QVERIFY(tabBar->isHidden());
        QVERIFY(buttonBarAction->isChecked());
        QVERIFY(!tabsAction->isChecked());
        QVERIFY(separatorAction->isChecked());

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.m_showTimestamps = false;
        window.m_alignNicks = true;
        window.m_separatorLine = true;
        window.m_nickColumnWidth = 8;
        window.activateBufferTarget(QStringLiteral("#chat"));
        window.rebuildNetworkTree();
        chatView->clear();
        window.appendSystemLine(QStringLiteral("<alice> hello there"));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral(" | ")));

        separatorAction->trigger();
        QVERIFY(!separatorAction->isChecked());
        QVERIFY(!window.m_separatorLine);
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral(" | ")));

        buttonBarAction->trigger();
        QVERIFY(!buttonBarAction->isChecked());
        QVERIFY(toolbar->isHidden());

        tabsAction->trigger();
        QVERIFY(tabsAction->isChecked());
        QVERIFY(!tabBar->isHidden());
        // One tab per tree row: the network root first, then its buffers; the
        // current tab tracks the active buffer.
        QVERIFY(tabBar->count() >= 2);
        QCOMPARE(tabBar->tabText(tabBar->currentIndex()), QStringLiteral("#chat"));

        const QVariantMap saved = window.m_settings.loadRaw();
        QCOMPARE(saved.value(QStringLiteral("separator_line")).toBool(), false);
        QCOMPARE(saved.value(QStringLiteral("show_button_bar")).toBool(), false);
        QCOMPARE(saved.value(QStringLiteral("buffer_tabs")).toBool(), true);
    }

    void themeMenuChangesSavedThemeAndApplicationStyle() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        QAction* synthwaveAction = findMenuAction(window.menuBar(), QStringLiteral("Synthwave"));
        QAction* vaporwaveAction = findMenuAction(window.menuBar(), QStringLiteral("Vaporwave"));
        QAction* defaultAction = findMenuAction(window.menuBar(), QStringLiteral("Default"));
        QAction* themesOffAction = findMenuAction(window.menuBar(), QStringLiteral("Themes Off"));
        QAction* greenChatAction =
            findMenuAction(window.menuBar(), QStringLiteral("Terminal - green on black"));
        QAction* vaporwaveWallpaperAction =
            findMenuAction(window.menuBar(), QStringLiteral("Vaporwave 2"));
        QVERIFY(synthwaveAction != nullptr);
        QVERIFY(vaporwaveAction != nullptr);
        QVERIFY(defaultAction != nullptr);
        QVERIFY(themesOffAction != nullptr);
        QVERIFY(greenChatAction != nullptr);
        QVERIFY(vaporwaveWallpaperAction != nullptr);
        QVERIFY(synthwaveAction->isCheckable());
        QVERIFY(defaultAction->isCheckable());
        QVERIFY(themesOffAction->isCheckable());
        window.m_appearance->setTheme(QStringLiteral("synthwave"), true);
        QVERIFY(synthwaveAction->isChecked());
        // Theming is applied application-wide (qApp) via QPalette + stylesheet.
        QVERIFY(!qApp->styleSheet().isEmpty());

        themesOffAction->trigger();
        QVERIFY(themesOffAction->isChecked());
        QVERIFY(qApp->styleSheet().isEmpty());
        QVariantMap saved = window.m_settings.loadRaw();
        QCOMPARE(saved.value(QStringLiteral("theme")).toString(), QStringLiteral("system"));

        vaporwaveAction->trigger();
        QVERIFY(vaporwaveAction->isChecked());
        QVERIFY(!qApp->styleSheet().isEmpty());
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("QToolBar#mainToolbar")));
        saved = window.m_settings.loadRaw();
        QCOMPARE(saved.value(QStringLiteral("theme")).toString(), QStringLiteral("vaporwave"));

        greenChatAction->trigger();
        QVERIFY(greenChatAction->isChecked());
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("rgb(8,12,8)")));
        saved = window.m_settings.loadRaw();
        QCOMPARE(saved.value(QStringLiteral("chat_theme")).toString(), QStringLiteral("green"));

        vaporwaveWallpaperAction->trigger();
        QVERIFY(vaporwaveWallpaperAction->isChecked());
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("vaporwave-2.jpg")));
        saved = window.m_settings.loadRaw();
        QCOMPARE(saved.value(QStringLiteral("wallpaper")).toString(),
                 QStringLiteral("vaporwave-2.jpg"));

        // Reset global app theming + saved state so later tests construct against a
        // clean qApp (each test shares one QApplication and the settings file).
        window.m_appearance->setTheme(QStringLiteral("system"), true);
        window.m_appearance->setWallpaper(QString(), true);
    }

    void draggingChatSeparatorChangesNickColumnWidth() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_showTimestamps = false;
        window.m_alignNicks = true;
        window.m_separatorLine = true;
        window.m_nickColumnWidth = 8;
        window.updateChatSeparatorGuide();

        chatView->resize(800, 300);
        const double spaceWidth =
            std::max(1.0, QFontMetricsF(chatView->font()).horizontalAdvance(QLatin1Char(' ')));
        const int startX = static_cast<int>(std::lround(
            chatView->document()->documentMargin() + spaceWidth * (8.0 + 0.5)));
        const int endX = startX + static_cast<int>(spaceWidth * 6.0);
        const QPoint start(startX, 12);
        const QPoint end(endX, 12);

        QTest::mousePress(chatView->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(chatView->viewport(), end);
        QTest::mouseRelease(chatView->viewport(), Qt::LeftButton, Qt::NoModifier, end);

        QVERIFY(window.m_nickColumnWidth > 8);
        const QVariantMap saved = window.m_settings.loadRaw();
        QVERIFY(saved.value(QStringLiteral("nick_width")).toInt() > 8);
    }

    void targetSwitchRestoresSeparateBufferHistory() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#one"));
        window.appendSystemLine(QStringLiteral("<alice> first room"));
        window.appendSystemLineToTarget(QStringLiteral("#two"),
                                        QStringLiteral("<bob> second room"));

        QString text = chatView->toPlainText();
        QVERIFY(text.contains(QStringLiteral("first room")));
        QVERIFY(!text.contains(QStringLiteral("second room")));

        window.activateBufferTarget(QStringLiteral("#two"));
        text = chatView->toPlainText();
        QVERIFY(!text.contains(QStringLiteral("first room")));
        QVERIFY(text.contains(QStringLiteral("second room")));

        const auto oneSnapshot =
            window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#one")));
        const auto twoSnapshot =
            window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#two")));
        QCOMPARE(oneSnapshot.lines.size(), 1);
        QCOMPARE(twoSnapshot.lines.size(), 1);
        QCOMPARE(twoSnapshot.unreadCount, 0);
    }

    void targetSwitchRestoresMembersAndTopic() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* memberList = window.findChild<QListWidget*>(QStringLiteral("memberList"));
        auto* topicLabel = window.findChild<QLabel*>(QStringLiteral("topicLabel"));
        QVERIFY(memberList != nullptr);
        QVERIFY(topicLabel != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");

        window.activateBufferTarget(QStringLiteral("#one"));
        const auto oneBuffer = window.bufferIdForTarget(QStringLiteral("#one"));
        QVERIFY(window.m_chatBuffers.setTopic(oneBuffer, QStringLiteral("First topic")));
        QVERIFY(window.m_chatBuffers.setMembers(
            oneBuffer, {QStringLiteral("@alice"), QStringLiteral("+bob")}));
        window.renderActiveBufferMetadata();

        QCOMPARE(topicLabel->toolTip(), QStringLiteral("First topic"));
        QCOMPARE(memberList->count(), 2);
        QCOMPARE(memberList->findItems(QStringLiteral("@alice"), Qt::MatchExactly).size(), 1);
        QCOMPARE(memberList->findItems(QStringLiteral("+bob"), Qt::MatchExactly).size(), 1);

        window.activateBufferTarget(QStringLiteral("#two"));
        const auto twoBuffer = window.bufferIdForTarget(QStringLiteral("#two"));
        QVERIFY(window.m_chatBuffers.setTopic(twoBuffer, QStringLiteral("Second topic")));
        QVERIFY(window.m_chatBuffers.setMembers(twoBuffer, {QStringLiteral("carol")}));
        window.renderActiveBufferMetadata();
        QCOMPARE(topicLabel->toolTip(), QStringLiteral("Second topic"));
        QCOMPARE(memberList->count(), 1);
        QCOMPARE(memberList->item(0)->text(), QStringLiteral("carol"));

        window.activateBufferTarget(QStringLiteral("#one"));
        QCOMPARE(topicLabel->toolTip(), QStringLiteral("First topic"));
        QCOMPARE(memberList->count(), 2);
        QCOMPARE(memberList->findItems(QStringLiteral("@alice"), Qt::MatchExactly).size(), 1);
        QCOMPARE(memberList->findItems(QStringLiteral("+bob"), Qt::MatchExactly).size(), 1);
    }

    void networkTreeSelectionUsesStoredTargetNotDisplayLabel() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* networkTree = window.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        QVERIFY(networkTree != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#one"));
        window.rebuildNetworkTree();

        auto* rootItem = networkTree->topLevelItem(0);
        QVERIFY(rootItem != nullptr);
        QVERIFY(rootItem->childCount() >= 1);
        auto* channelItem = rootItem->child(0);
        QCOMPARE(channelItem->data(0, Qt::UserRole).toString(), QStringLiteral("#one"));

        networkTree->setCurrentItem(rootItem);
        channelItem->setText(0, QStringLiteral("#one [2]"));
        networkTree->setCurrentItem(channelItem);

        QCOMPARE(window.m_currentTarget, QStringLiteral("#one"));
    }

    void networkTreeShowsServerBufferInsteadOfConnectionState() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* networkTree = window.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(networkTree != nullptr);
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.m_registered = true;
        window.activateBufferTarget(QStringLiteral("#chat"));
        window.appendSystemLine(QStringLiteral("<alice> channel line"));
        window.appendSystemLineToTarget(QString(), QStringLiteral("! server line"));
        window.rebuildNetworkTree();

        auto* rootItem = networkTree->topLevelItem(0);
        QVERIFY(rootItem != nullptr);
        QVERIFY(rootItem->childCount() >= 1);
        // The network root row IS the server buffer now - no separate child.
        QVERIFY(rootItem->text(0).startsWith(QStringLiteral("Libera.Chat")));
        QCOMPARE(rootItem->data(0, Qt::UserRole).toString(), QStringLiteral("server"));
        QCOMPARE(rootItem->toolTip(0), QStringLiteral("Connected"));

        networkTree->setCurrentItem(rootItem);

        QCOMPARE(window.m_currentTarget, QString());
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("server line")));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("channel line")));
    }

    void tabCompletionCyclesThroughCandidates() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        QVERIFY(window.m_input != nullptr);

        // "/" matches every slash command, so this is a guaranteed multi-candidate
        // cycle without needing a populated member list.
        window.m_input->setPlainText(QStringLiteral("/"));
        QTextCursor cursor = window.m_input->textCursor();
        cursor.movePosition(QTextCursor::End);
        window.m_input->setTextCursor(cursor);

        QVERIFY(window.completeInput(true));
        const QString first = window.m_input->toPlainText();
        QVERIFY(first.startsWith(QStringLiteral("/")));

        QVERIFY(window.completeInput(true)); // Tab again cycles to the next match
        const QString second = window.m_input->toPlainText();
        QVERIFY(second.startsWith(QStringLiteral("/")));
        QVERIFY(second != first);

        QVERIFY(window.completeInput(false)); // Backtab returns to the previous match
        QCOMPARE(window.m_input->toPlainText(), first);
    }

    void networkTreeShowsUnreadAndHighlightCounts() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* networkTree = window.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        QVERIFY(networkTree != nullptr);

        const auto channelItem = [networkTree](const QString& target) {
            auto* rootItem = networkTree->topLevelItem(0);
            if (rootItem == nullptr) {
                return static_cast<QTreeWidgetItem*>(nullptr);
            }
            for (int index = 0; index < rootItem->childCount(); ++index) {
                auto* item = rootItem->child(index);
                if (item != nullptr && item->data(0, Qt::UserRole).toString() == target) {
                    return item;
                }
            }
            return static_cast<QTreeWidgetItem*>(nullptr);
        };

        window.m_hasConnectionPlan = true;
        window.m_registered = true; // a connected network — not "(offline)"
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#one"));
        window.rememberTarget(QStringLiteral("#two"));
        window.rebuildNetworkTree();

        auto* twoItem = channelItem(QStringLiteral("#two"));
        QVERIFY(twoItem != nullptr);
        auto* rootItem = networkTree->topLevelItem(0);
        QVERIFY(rootItem != nullptr);
        QCOMPARE(rootItem->text(0), QStringLiteral("Libera.Chat"));
        QCOMPARE(twoItem->text(0), QStringLiteral("#two"));

        window.appendSystemLineToTarget(QStringLiteral("#two"), QStringLiteral("<bob> later"));
        QCOMPARE(rootItem->text(0), QStringLiteral("Libera.Chat [1]"));
        QCOMPARE(twoItem->text(0), QStringLiteral("#two [1]"));

        window.appendSystemLineToTarget(QStringLiteral("#two"),
                                        QStringLiteral("<bob> MaxChat: ping"), true, false, true);
        QCOMPARE(rootItem->text(0), QStringLiteral("Libera.Chat [!1/2]"));
        QCOMPARE(twoItem->text(0), QStringLiteral("#two [!1/2]"));

        window.activateBufferTarget(QStringLiteral("#two"));
        QCOMPARE(rootItem->text(0), QStringLiteral("Libera.Chat"));
        QCOMPARE(twoItem->text(0), QStringLiteral("#two"));
        QCOMPARE(window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#two")))
                     .unreadCount,
                 0);
    }

    void networkTreeKeepsSeparateNetworkBuffers() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* networkTree = window.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(networkTree != nullptr);
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.rememberNetwork(QStringLiteral("Libera.Chat"));
        window.activateBufferTarget(QStringLiteral("#maxchat"));
        window.appendSystemLine(QStringLiteral("<alice> libera line"));

        window.m_connectionPlan.networkName = QStringLiteral("EFNet");
        window.rememberNetwork(QStringLiteral("EFNet"));
        window.m_currentTarget.clear();
        window.m_openTargets.clear();
        window.activateBufferTarget(QStringLiteral("#maxchat"));
        window.appendSystemLine(QStringLiteral("<bob> efnet line"));
        window.rebuildNetworkTree();

        QCOMPARE(networkTree->topLevelItemCount(), 2);
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("efnet line")));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("libera line")));

        QTreeWidgetItem* liberaItem = findTreeItemByNetworkTarget(
            networkTree, QStringLiteral("Libera.Chat"), QStringLiteral("#maxchat"));
        QVERIFY(liberaItem != nullptr);
        networkTree->setCurrentItem(liberaItem);

        QCOMPARE(window.m_connectionPlan.networkName, QStringLiteral("Libera.Chat"));
        QCOMPARE(window.m_currentTarget, QStringLiteral("#maxchat"));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("libera line")));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("efnet line")));
    }

    void inactiveNetworkSignalUpdatesOnlyThatNetworkBuffer() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* networkTree = window.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(networkTree != nullptr);
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.m_connectionPlan.nick = QStringLiteral("libnick");
        window.rememberNetwork(QStringLiteral("Libera.Chat"));
        window.activateBufferTarget(QStringLiteral("#chat"));
        window.appendSystemLine(QStringLiteral("<alice> libera-only line"));
        const auto liberaBuffer =
            window.bufferIdForNetworkTarget(QStringLiteral("Libera.Chat"), QStringLiteral("#chat"));
        QCOMPARE(window.m_chatBuffers.snapshot(liberaBuffer).lines.size(), 1);

        maxchat::core::NetworkConnectionPlan efnetPlan;
        efnetPlan.networkName = QStringLiteral("EFNet");
        efnetPlan.nick = QStringLiteral("efnick");
        window.m_connectionPlansByNetwork.insert(QStringLiteral("EFNet"), efnetPlan);
        window.m_currentTargetByNetwork.insert(QStringLiteral("EFNet"), QStringLiteral("#chat"));
        window.m_openTargetsByNetwork.insert(QStringLiteral("EFNet"), {QStringLiteral("#chat")});
        window.m_registeredByNetwork.insert(QStringLiteral("EFNet"), true);
        window.rememberNetwork(QStringLiteral("EFNet"));
        maxchat::irc::IrcConnection* efnetConnection =
            window.ensureConnectionForNetwork(QStringLiteral("EFNet"));
        QVERIFY(efnetConnection != nullptr);
        window.rebuildNetworkTree();
        chatView->clear();
        window.renderActiveBuffer();

        emit efnetConnection->messageReceived(QStringLiteral("bob"), QStringLiteral("#chat"),
                                              QStringLiteral("efnet-only line"), false, false);

        QCOMPARE(window.m_connectionPlan.networkName, QStringLiteral("Libera.Chat"));
        QCOMPARE(window.m_currentTarget, QStringLiteral("#chat"));
        QCOMPARE(window.m_chatBuffers.snapshot(liberaBuffer).lines.size(), 1);
        QVERIFY2(chatView->toPlainText().contains(QStringLiteral("libera-only line")),
                 qPrintable(chatView->toPlainText()));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("efnet-only line")));

        const auto efnetSnapshot = window.m_chatBuffers.snapshot(
            window.bufferIdForNetworkTarget(QStringLiteral("EFNet"), QStringLiteral("#chat")));
        QCOMPARE(efnetSnapshot.lines.size(), 1);
        QCOMPARE(efnetSnapshot.unreadCount, 1);
        QVERIFY(efnetSnapshot.lines.first().plainText.contains(QStringLiteral("efnet-only line")));

        QTreeWidgetItem* efnetItem = findTreeItemByNetworkTarget(
            networkTree, QStringLiteral("EFNet"), QStringLiteral("#chat"));
        QVERIFY(efnetItem != nullptr);
        QCOMPARE(efnetItem->text(0), QStringLiteral("#chat [1]"));

        networkTree->setCurrentItem(efnetItem);
        QCOMPARE(window.m_connectionPlan.networkName, QStringLiteral("EFNet"));
        QCOMPARE(window.m_currentTarget, QStringLiteral("#chat"));
        QVERIFY2(chatView->toPlainText().contains(QStringLiteral("efnet-only line")),
                 qPrintable(chatView->toPlainText()));
        QVERIFY2(!chatView->toPlainText().contains(QStringLiteral("libera-only line")),
                 qPrintable(chatView->toPlainText()));
    }

    void incomingPrivateMessageOpensBackgroundQuery() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* networkTree = window.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(networkTree != nullptr);
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.m_connectionPlan.nick = QStringLiteral("bob");
        window.activateBufferTarget(QStringLiteral("#chat"));
        window.rebuildNetworkTree();

        emit window.m_connection.messageReceived(QStringLiteral("alice"), QStringLiteral("bob"),
                                                 QStringLiteral("hello privately"), false, false);

        QCOMPARE(window.m_currentTarget, QStringLiteral("#chat"));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("hello privately")));
        auto* aliceItem = findTreeItemByTarget(networkTree, QStringLiteral("alice"));
        QVERIFY(aliceItem != nullptr);
        QCOMPARE(aliceItem->text(0), QStringLiteral("alice [1]"));
        auto* rootItem = networkTree->topLevelItem(0);
        QVERIFY(rootItem != nullptr);
        QCOMPARE(rootItem->text(0), QStringLiteral("Libera.Chat [1]"));

        const auto aliceSnapshot =
            window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("alice")));
        QCOMPARE(aliceSnapshot.lines.size(), 1);
        QCOMPARE(aliceSnapshot.unreadCount, 1);
        QVERIFY(aliceSnapshot.lines.first().plainText.contains(QStringLiteral("hello privately")));

        window.activateBufferTarget(QStringLiteral("alice"));
        QCOMPARE(window.m_currentTarget, QStringLiteral("alice"));
        QCOMPARE(window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("alice")))
                     .unreadCount,
                 0);
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("hello privately")));
    }

    void messageQueryAndActionCommandsRouteCleanly() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* networkTree = window.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(networkTree != nullptr);
        QVERIFY(chatView != nullptr);

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.m_connectionPlan.nick = QStringLiteral("bob");
        window.activateBufferTarget(QStringLiteral("#chat"));
        window.rebuildNetworkTree();

        maxchat::irc::IrcConnection::ConnectConfig config;
        config.host = QStringLiteral("127.0.0.1");
        config.port = server.serverPort();
        config.tls = false;
        config.nick = QStringLiteral("bob");
        config.username = QStringLiteral("bob");
        config.realname = QStringLiteral("Bob");
        config.connectTimeoutMs = 5000;
        config.registrationTimeoutMs = 5000;
        window.m_connection.connectTo(config);

        QVERIFY(server.waitForNewConnection(1000));
        QTcpSocket* peer = server.nextPendingConnection();
        QVERIFY(peer != nullptr);
        QTRY_VERIFY(window.m_connection.isConnected());

        chatView->clear();
        window.sendCommandOrMessage(QStringLiteral("/msg alice hello quietly"));
        QVERIFY(waitForSocketText(peer, QByteArrayLiteral("PRIVMSG alice :hello quietly\r\n")));

        QCOMPARE(window.m_currentTarget, QStringLiteral("#chat"));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("hello quietly")));
        auto* aliceItem = findTreeItemByTarget(networkTree, QStringLiteral("alice"));
        QVERIFY(aliceItem != nullptr);
        QCOMPARE(aliceItem->text(0), QStringLiteral("alice"));
        const auto aliceSnapshot =
            window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("alice")));
        QCOMPARE(aliceSnapshot.unreadCount, 0);
        QCOMPARE(aliceSnapshot.lines.size(), 1);
        QCOMPARE(aliceSnapshot.lines.first().sourceText, QStringLiteral("<bob> hello quietly"));

        window.sendCommandOrMessage(QStringLiteral("/query alice query hello"));
        QVERIFY(waitForSocketText(peer, QByteArrayLiteral("PRIVMSG alice :query hello\r\n")));
        QCOMPARE(window.m_currentTarget, QStringLiteral("alice"));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("hello quietly")));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("query hello")));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("<bob:alice>")));

        window.activateBufferTarget(QStringLiteral("#chat"));
        chatView->clear();
        window.sendCommandOrMessage(QStringLiteral("/me waves"));
        QVERIFY(waitForSocketText(peer, QByteArray("PRIVMSG #chat :\001ACTION waves\001\r\n")));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("waves")));
        const auto chatSnapshot =
            window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#chat")));
        QVERIFY(!chatSnapshot.lines.isEmpty());
        QCOMPARE(chatSnapshot.lines.last().sourceText, QStringLiteral("* bob waves"));
    }

    void incomingChannelMessagesSetUnreadAndHighlightsInBackgroundOnly() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* networkTree = window.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        QVERIFY(networkTree != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.m_connectionPlan.nick = QStringLiteral("bob");
        window.activateBufferTarget(QStringLiteral("#one"));
        window.rememberTarget(QStringLiteral("#two"));
        window.rebuildNetworkTree();

        emit window.m_connection.messageReceived(QStringLiteral("alice"), QStringLiteral("#one"),
                                                 QStringLiteral("bob: active ping"), false, false);

        auto oneSnapshot =
            window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#one")));
        QCOMPARE(oneSnapshot.unreadCount, 0);
        QCOMPARE(oneSnapshot.highlightCount, 0);
        QCOMPARE(window.m_currentTarget, QStringLiteral("#one"));

        emit window.m_connection.messageReceived(QStringLiteral("carol"), QStringLiteral("#two"),
                                                 QStringLiteral("bob: background ping"), false,
                                                 false);

        auto twoSnapshot =
            window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#two")));
        QCOMPARE(twoSnapshot.unreadCount, 1);
        QCOMPARE(twoSnapshot.highlightCount, 1);
        QCOMPARE(window.m_currentTarget, QStringLiteral("#one"));
        auto* twoItem = findTreeItemByTarget(networkTree, QStringLiteral("#two"));
        QVERIFY(twoItem != nullptr);
        QCOMPARE(twoItem->text(0), QStringLiteral("#two [!1]"));

        window.m_mutedChannelKeys.append(window.mutedChannelKey(QStringLiteral("#two")));
        emit window.m_connection.messageReceived(QStringLiteral("dana"), QStringLiteral("#two"),
                                                 QStringLiteral("bob: muted background ping"),
                                                 false, false);

        twoSnapshot =
            window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#two")));
        QCOMPARE(twoSnapshot.unreadCount, 2);
        QCOMPARE(twoSnapshot.highlightCount, 1);
        QCOMPARE(twoItem->text(0), QStringLiteral("#two [!1/2]"));
    }

    void networkTreeRootIgnoresHiddenUnreadBuffers() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* networkTree = window.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        QVERIFY(networkTree != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_registered = true; // a connected network — not "(offline)"
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#one"));
        window.rememberTarget(QStringLiteral("#two"));
        window.rebuildNetworkTree();

        window.appendSystemLineToTarget(QStringLiteral("#two"),
                                        QStringLiteral("<bob> hidden later"));
        auto* rootItem = networkTree->topLevelItem(0);
        QVERIFY(rootItem != nullptr);
        QCOMPARE(rootItem->text(0), QStringLiteral("Libera.Chat [1]"));

        window.forgetTarget(QStringLiteral("#two"));
        window.rebuildNetworkTree();
        rootItem = networkTree->topLevelItem(0);
        QVERIFY(rootItem != nullptr);
        QCOMPARE(rootItem->text(0), QStringLiteral("Libera.Chat"));

        const auto hiddenSnapshot =
            window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#two")));
        QCOMPARE(hiddenSnapshot.unreadCount, 1);
    }

    void markAllReadClearsTreeCounters() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* networkTree = window.findChild<QTreeWidget*>(QStringLiteral("networkTree"));
        QVERIFY(networkTree != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_registered = true; // a connected network — not "(offline)"
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#one"));
        window.rememberTarget(QStringLiteral("#two"));
        window.rebuildNetworkTree();

        window.appendSystemLineToTarget(QStringLiteral("#two"),
                                        QStringLiteral("<bob> MaxChat: ping"), true, false, true);
        auto* rootItem = networkTree->topLevelItem(0);
        QVERIFY(rootItem != nullptr);
        QCOMPARE(rootItem->text(0), QStringLiteral("Libera.Chat [!1]"));

        window.markAllRead();

        QCOMPARE(rootItem->text(0), QStringLiteral("Libera.Chat"));
        const auto snapshot =
            window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#two")));
        QCOMPARE(snapshot.unreadCount, 0);
        QCOMPARE(snapshot.highlightCount, 0);
    }

    void clearCommandClearsCurrentViewAndStoredBuffer() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#chat"));
        window.appendSystemLine(QStringLiteral("<alice> keep this briefly"));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("keep this")));
        QCOMPARE(window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#chat")))
                     .lines.size(),
                 1);

        QAction* clearCurrentChat =
            findMenuAction(window.menuBar(), QStringLiteral("Clear Current Chat"));
        QVERIFY(clearCurrentChat != nullptr);
        clearCurrentChat->trigger();

        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("keep this")));
        QCOMPARE(window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#chat")))
                     .lines.size(),
                 0);

        window.appendSystemLine(QStringLiteral("<alice> clear this too"));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("clear this")));
        window.sendCommandOrMessage(QStringLiteral("/clear"));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("clear this")));
        QCOMPARE(window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#chat")))
                     .lines.size(),
                 0);
    }

    void localConnectionCommandsBypassGenericDisconnectedGuard() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.clearCurrentChat();
        window.sendCommandOrMessage(QStringLiteral("/reconnect"));
        QVERIFY(chatView->toPlainText().contains(
            QStringLiteral("No saved connection is available to reconnect.")));

        window.clearCurrentChat();
        window.sendCommandOrMessage(QStringLiteral("/disconnect"));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("! Not connected.")));

        window.clearCurrentChat();
        window.sendCommandOrMessage(QStringLiteral("/help"));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("! Core:")));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("/join")));
    }

    void serverCommandStartsLocalConnectionPlanWhileDisconnected() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        chatView->clear();
        window.sendCommandOrMessage(QStringLiteral("/server -nossl 127.0.0.1 1 server-secret"));

        QVERIFY(window.m_hasConnectionPlan);
        QCOMPARE(window.m_connectionPlan.networkName, QStringLiteral("127.0.0.1"));
        QCOMPARE(window.m_connectionPlan.serverPassword, QStringLiteral("server-secret"));
        QCOMPARE(window.m_connectionPlan.reconnect.servers.size(), 1);
        QCOMPARE(window.m_connectionPlan.reconnect.servers.first().host,
                 QStringLiteral("127.0.0.1"));
        QCOMPARE(window.m_connectionPlan.reconnect.servers.first().port, 1);
        QCOMPARE(window.m_connectionPlan.reconnect.servers.first().tls, false);
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("! Not connected.")));
    }

    void closeCommandRemovesCurrentTargetAndStoredHistory() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#chat"));
        window.rememberTarget(QStringLiteral("#chat"));
        window.rebuildNetworkTree();
        QVERIFY(window.visibleTreeTargets().contains(QStringLiteral("#chat")));

        window.appendSystemLine(QStringLiteral("<alice> hidden later"));
        QCOMPARE(window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#chat")))
                     .lines.size(),
                 1);

        window.sendCommandOrMessage(QStringLiteral("/close"));

        QCOMPARE(window.m_currentTarget, QString());
        QVERIFY(!window.visibleTreeTargets().contains(QStringLiteral("#chat")));
        QCOMPARE(window.m_chatBuffers.snapshot(window.bufferIdForTarget(QStringLiteral("#chat")))
                     .lines.size(),
                 0);
    }

    void replayedLogLinesUseInlineAlignment() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        MainWindow window;
        window.m_replayLogEnabled = false; // suppress auto-seed on activateBufferTarget
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_chatLogStore = maxchat::core::ChatLogStore(temp.path());
        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.m_timestampFormat = QStringLiteral("%I:%M %p");
        window.m_nickColumnWidth = 8;
        window.activateBufferTarget(QStringLiteral("#chat"));

        const QDate today = QDate::currentDate();
        QVERIFY(window.m_chatLogStore.appendLine(
            QStringLiteral("Libera.Chat"), QStringLiteral("#chat"),
            QStringLiteral("<bob> raw pipe | stays"), QDateTime(today, QTime(1, 2, 3))));
        QVERIFY(
            window.m_chatLogStore.appendLine(QStringLiteral("Libera.Chat"), QStringLiteral("#chat"),
                                             QStringLiteral("12:34 AM    <alice> | old formatted"),
                                             QDateTime(today, QTime(1, 2, 4))));

        // Seed replay while buffer is still empty (before live messages arrive).
        window.m_replayLogEnabled = true;
        window.seedReplayForBuffer(QStringLiteral("Libera.Chat"), QStringLiteral("#chat"));

        // Append a live message after the replay seed — verifies log format is correct.
        window.appendSystemLineToTarget(QStringLiteral("#chat"),
                                        QStringLiteral("<dana> newly saved"));
        const QStringList saved = window.m_chatLogStore.recentLines(QStringLiteral("Libera.Chat"),
                                                                    QStringLiteral("#chat"), 10);
        QVERIFY(!saved.isEmpty());
        QVERIFY(saved.last().endsWith(QStringLiteral(" <dana> newly saved")));
        QVERIFY(!saved.last().contains(QStringLiteral(" | newly saved")));

        window.renderActiveBuffer();
        const QString html = chatView->toHtml();

        QVERIFY(!html.contains(QStringLiteral("<table")));
        QVERIFY(html.contains(QStringLiteral("raw pipe | stays")));
        QVERIFY(html.contains(QStringLiteral("old formatted")));
        QVERIFY(html.contains(QStringLiteral("newly saved")));
        QVERIFY(!html.contains(today.toString(QStringLiteral("yyyy-MM-dd"))));
    }

    void replayHistorySurvivesBufferSwitch() {
        // AUDIT #29: resume history must be stored in the buffer model (not just
        // painted), so it shows on first open AND survives switching away/back.
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        MainWindow window;
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);
        window.m_chatLogStore = maxchat::core::ChatLogStore(temp.path());
        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.m_replayLogEnabled = true;

        const QDate today = QDate::currentDate();
        QVERIFY(window.m_chatLogStore.appendLine(
            QStringLiteral("Libera.Chat"), QStringLiteral("#chat"),
            QStringLiteral("<bob> hello there"), QDateTime(today, QTime(1, 2, 3))));

        // First open seeds dimmed history + a "Chat ended" divider into the model.
        window.activateBufferTarget(QStringLiteral("#chat"));
        const maxchat::core::ChatBufferId chatId = window.bufferIdForTarget(QStringLiteral("#chat"));
        const QList<maxchat::core::ChatBufferLine> seeded =
            window.m_chatBuffers.snapshot(chatId).lines;
        QVERIFY(std::any_of(seeded.cbegin(), seeded.cend(),
                            [](const maxchat::core::ChatBufferLine& l) {
                                return l.dimmed && l.sourceText.contains(QStringLiteral("hello there"));
                            }));
        QVERIFY(std::any_of(seeded.cbegin(), seeded.cend(),
                            [](const maxchat::core::ChatBufferLine& l) {
                                return l.systemLine && l.dimmed &&
                                       l.sourceText.contains(QStringLiteral("Chat ended"));
                            }));

        // Switching away and back must reproduce it (the old bug wiped it).
        window.activateBufferTarget(QStringLiteral("#other"));
        window.activateBufferTarget(QStringLiteral("#chat"));
        QVERIFY(chatView->toHtml().contains(QStringLiteral("hello there")));
        QVERIFY(chatView->toHtml().contains(QStringLiteral("Chat ended")));

        // Re-opening must not seed the history a second time.
        const int afterFirst = static_cast<int>(window.m_chatBuffers.snapshot(chatId).lines.size());
        window.activateBufferTarget(QStringLiteral("#other"));
        window.activateBufferTarget(QStringLiteral("#chat"));
        QCOMPARE(static_cast<int>(window.m_chatBuffers.snapshot(chatId).lines.size()), afterFirst);
    }

    void unreadMarkerPersistsAcrossRerender() {
        // The "new" marker must appear in a channel that got messages while you
        // were away, and survive a re-render (the old painted version vanished).
        MainWindow window;
        window.m_replayLogEnabled = false;
        window.m_markerLine = true;
        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        // Open #a and read a couple of messages (seen while active).
        window.activateBufferTarget(QStringLiteral("#a"));
        window.appendSystemLineToTarget(QStringLiteral("#a"), QStringLiteral("<bob> one"));
        window.appendSystemLineToTarget(QStringLiteral("#a"), QStringLiteral("<bob> two"));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("new"))); // nothing unread yet

        // Leave to #b; a message arrives in #a while we're away.
        window.activateBufferTarget(QStringLiteral("#b"));
        window.appendSystemLineToNetworkTarget(QStringLiteral("Libera.Chat"), QStringLiteral("#a"),
                                               QStringLiteral("<bob> while away"));

        // Returning shows the marker before the new message...
        window.activateBufferTarget(QStringLiteral("#a"));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("new")));

        // ...and a re-render (e.g. theme/metadata refresh) keeps it.
        window.renderActiveBuffer();
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("new")));
    }

    void chatLinesUseViewHangingIndent() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.m_showTimestamps = false;
        window.m_alignNicks = true;
        window.m_separatorLine = true;
        window.m_nickColumnWidth = 8;
        window.activateBufferTarget(QStringLiteral("#chat"));
        chatView->clear();

        window.appendSystemLine(
            QStringLiteral("<bob> a long line should wrap underneath the message column"));

        QTextBlock block = chatView->document()->firstBlock();
        QVERIFY(block.isValid());
        QTextBlockFormat format = block.blockFormat();
        QVERIFY(format.leftMargin() > 0.0);
        QCOMPARE(format.textIndent(), -format.leftMargin());
        QVERIFY(!chatView->toHtml().contains(QStringLiteral("<table")));

        chatView->clear();
        window.renderActiveBuffer();
        block = chatView->document()->firstBlock();
        QVERIFY(block.isValid());
        format = block.blockFormat();
        QVERIFY(format.leftMargin() > 0.0);
        QCOMPARE(format.textIndent(), -format.leftMargin());
    }

    void serverRepliesStayInServerBuffer() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#chat"));
        chatView->clear();

        QMetaObject::invokeMethod(&window.m_connection, "replyText", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("[motd] Please join #help")));

        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("[motd]")));

        window.activateBufferTarget(QString());
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("[motd]")));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("#help")));
    }

    void noisyChannelNumericsDoNotDuplicateChatLines() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#chat"));
        chatView->clear();

        QMetaObject::invokeMethod(&window.m_connection, "topicChanged", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("#chat")),
                                  Q_ARG(QString, QStringLiteral("Current topic")));

        QVERIFY(
            chatView->toPlainText().contains(QStringLiteral("! Topic for #chat: Current topic")));

        QMetaObject::invokeMethod(&window.m_connection, "topicChanged", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("#chat")),
                                  Q_ARG(QString, QStringLiteral("Current topic")));

        QCOMPARE(chatView->toPlainText().count(QStringLiteral("! Topic for #chat: Current topic")),
                 1);

        QMetaObject::invokeMethod(&window.m_connection, "replyText", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("[topic] #chat: Current topic")));
        QMetaObject::invokeMethod(
            &window.m_connection, "replyText", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("[channel] #chat created at 1234")));

        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("[topic]")));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("[channel]")));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("created at")));

        QMetaObject::invokeMethod(
            &window.m_connection, "replyText", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("[error] Cannot join #chat: invite only")));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("Cannot join #chat")));
    }

    void channelStateRepliesDoNotSpamChat() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#chat"));
        chatView->clear();

        QMetaObject::invokeMethod(&window.m_connection, "namesEnd", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("#chat")));
        QMetaObject::invokeMethod(&window.m_connection, "namesEnd", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("#chat")));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("End of names")));

        QMetaObject::invokeMethod(&window.m_connection, "channelModeIs", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("#chat")),
                                  Q_ARG(QString, QStringLiteral("+nt")));
        QMetaObject::invokeMethod(&window.m_connection, "channelModeIs", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("#chat")),
                                  Q_ARG(QString, QStringLiteral("+nt")));
        QCOMPARE(chatView->toPlainText().count(QStringLiteral("! Modes for #chat: +nt")), 1);

        QMetaObject::invokeMethod(&window.m_connection, "channelModeIs", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("#chat")),
                                  Q_ARG(QString, QStringLiteral("+ntk key")));
        QCOMPARE(chatView->toPlainText().count(QStringLiteral("! Modes for #chat")), 2);
    }

    void liveNickAndQuitEventsRouteToAffectedChannels() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#chat"));
        QVERIFY(window.m_chatBuffers.setMembers(window.bufferIdForTarget(QStringLiteral("#chat")),
                                                {QStringLiteral("@alice"), QStringLiteral("bob")}));
        window.activateBufferTarget(QStringLiteral("#other"));
        QVERIFY(
            window.m_chatBuffers.setMembers(window.bufferIdForTarget(QStringLiteral("#other")),
                                            {QStringLiteral("alice"), QStringLiteral("carol")}));
        window.activateBufferTarget(QStringLiteral("#chat"));
        chatView->clear();

        QMetaObject::invokeMethod(&window.m_connection, "nickChanged", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("alice")),
                                  Q_ARG(QString, QStringLiteral("alice_")));

        QVERIFY(chatView->toPlainText().contains(QStringLiteral("* alice is now known as alice_")));

        window.activateBufferTarget(QStringLiteral("#other"));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("* alice is now known as alice_")));

        window.activateBufferTarget(QStringLiteral("#chat"));
        chatView->clear();
        QMetaObject::invokeMethod(&window.m_connection, "userQuit", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("alice_")),
                                  Q_ARG(QString, QStringLiteral("gone")));

        QVERIFY(chatView->toPlainText().contains(QStringLiteral("* alice_ quit (gone)")));
        window.activateBufferTarget(QStringLiteral("#other"));
        QVERIFY(chatView->toPlainText().contains(QStringLiteral("* alice_ quit (gone)")));
    }

    void serverScopedRepliesStayOutOfChannelBuffers() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.activateBufferTarget(QStringLiteral("#chat"));
        chatView->clear();

        QMetaObject::invokeMethod(&window.m_connection, "listReply", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("#chat")), Q_ARG(int, 42),
                                  Q_ARG(QString, QStringLiteral("general chat")));
        QMetaObject::invokeMethod(&window.m_connection, "modeChanged", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("iw")),
                                  Q_ARG(QString, QStringLiteral("server.example.net")),
                                  Q_ARG(QString, QStringLiteral("+i")));
        QMetaObject::invokeMethod(&window.m_connection, "messageReceived", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("server.example.net")),
                                  Q_ARG(QString, QStringLiteral("iw")),
                                  Q_ARG(QString, QStringLiteral("maintenance soon")),
                                  Q_ARG(bool, true), Q_ARG(bool, false));

        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("[list]")));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("sets mode +i")));
        QVERIFY(!chatView->toPlainText().contains(QStringLiteral("maintenance soon")));

        window.activateBufferTarget(QString());
        const QString serverText = chatView->toPlainText();
        QVERIFY(serverText.contains(QStringLiteral("[list] #chat (42 users)")));
        QVERIFY(serverText.contains(QStringLiteral("* server.example.net sets mode +i on iw")));
        QVERIFY(serverText.contains(QStringLiteral("-server.example.net- maintenance soon")));
    }

    void applyingMessagePreferencesRerendersCurrentBuffer() {
        MainWindow window;
        window.m_replayLogEnabled = false; // isolate buffer mechanics from on-disk log replay
        auto* chatView = window.findChild<QTextBrowser*>(QStringLiteral("chatView"));
        QVERIFY(chatView != nullptr);

        window.m_hasConnectionPlan = true;
        window.m_connectionPlan.networkName = QStringLiteral("Libera.Chat");
        window.m_showTimestamps = true;
        window.m_alignNicks = true;
        window.m_separatorLine = true;
        window.activateBufferTarget(QStringLiteral("#chat"));
        chatView->clear();
        window.appendSystemLine(QStringLiteral("<alice> hello there"));
        QVERIFY(chatView->toPlainText().contains(
            QRegularExpression(QStringLiteral(R"(\d{1,2}:\d{2})"))));

        QVariantMap settings = window.m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("show_timestamps"), false);
        settings.insert(QStringLiteral("align_nicks"), false);
        settings.insert(QStringLiteral("separator_line"), false);
        QVERIFY(window.m_settings.saveRaw(settings));
        window.applyCurrentSettings();

        const QString rerendered = chatView->toPlainText();
        QVERIFY(!rerendered.contains(QRegularExpression(QStringLiteral(R"(\d{1,2}:\d{2})"))));
        QVERIFY(rerendered.contains(QStringLiteral("<alice> hello there")));
        QVERIFY(!rerendered.contains(QStringLiteral(" | ")));
    }
};

QTEST_MAIN(MainWindowLinkPreviewTest)

#include "main_window_link_preview_test.moc"
