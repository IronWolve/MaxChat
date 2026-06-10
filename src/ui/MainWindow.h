#pragma once

#include "core/ChatBufferStore.h"
#include "core/ChatLineFormatter.h"
#include "core/ChatLogStore.h"
#include "core/ConnectionPlan.h"
#include "core/FloodGuard.h"
#include "core/SettingsStore.h"
#include "irc/IrcConnection.h"
#include "services/LinkPreviewPolicy.h"
#include "services/OpenGraphFetcher.h"
#ifdef MAXCHAT_WITH_HUNSPELL
#include "spell/HunspellSpellchecker.h"
#endif

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <memory>

class QEvent;
class QAction;
class QLabel;
class QListWidget;
class QMenu;
class QShortcut;
class QObject;
class QPushButton;
class QSplitter;
class QTabBar;
class QTextBrowser;
class QTextEdit;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;
class QSystemTrayIcon;
class QMenu;

namespace maxchat::ui {

class AudioPlayerBar;
class ComicView;
class Notifier;

class BanListDialog;
class ChannelListDialog;
class ChatFindDialog;
class RawLogDialog;
class SpellcheckHighlighter;
class UrlListDialog;

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool selfTest() const;

  private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

    void buildMenus();
    void buildLayout();
    void loadFonts();
    void applyCurrentSettings();
    void applyTheme(const QString& theme);
    void setTheme(const QString& theme, bool save);
    void syncThemeActions(const QString& theme);
    void setChatTheme(const QString& chatTheme, bool save);
    void syncChatThemeActions(const QString& chatTheme);
    void setWallpaper(const QString& wallpaper, bool save);
    void syncWallpaperActions(const QString& wallpaper);
    void configureSpellcheck(const QVariantMap& settings);
    void resizeMessageInput();
    void setServerListVisible(bool visible, bool save);
    void setMembersVisible(bool visible, bool save);
    void setButtonBarVisible(bool visible, bool save);
    void setBufferTabsVisible(bool visible, bool save);
    void setChatSeparatorVisible(bool visible, bool save);
    void setNickColumnWidth(int nickWidth, bool save);
    void setSplitterPanelVisible(int index, bool visible, bool save);
    void syncBufferTabs();
    void syncPanelActionsFromSplitter(bool save);
    void updateChatSeparatorGuide();
    void saveViewVisibilitySetting(const QString& key, bool visible);
    void saveSplitterSizes();
    void setupConnectionSignals();
    void setupConnectionSignals(const QString& network, maxchat::irc::IrcConnection* connection);
    void withNetworkContext(const QString& network, const std::function<void()>& body);
    void saveActiveNetworkState();
    [[nodiscard]] maxchat::irc::IrcConnection& connection();
    [[nodiscard]] const maxchat::irc::IrcConnection& connection() const;
    [[nodiscard]] maxchat::irc::IrcConnection* connectionForNetwork(const QString& network) const;
    [[nodiscard]] maxchat::irc::IrcConnection* ensureConnectionForNetwork(const QString& network);
    [[nodiscard]] bool anyNetworkConnectionIsConnected() const;
    void openServerList();
    void openQuickConnect();
    void openJoinDialog();
    void openPreferences();
    void openAliases();
    void openIgnoreList();
    void openFriendsNotify();
    void openChannelModes();
    void openBanList(const QString& channel = {});
    void openChannelList(bool reset = false);
    void openChatFind();
    void openRawLog();
    void openUrlList();
    void openCommandHelp();
    void openAbout();
    void leaveCurrentChannel();
    void replayCurrentLog();
    void markAllRead();
    void clearCurrentChat();
    void clearAllChats();
    void showUptime();
    void showNetInfo();
    void showFeaturePlanned(const QString& feature, const QString& detail = {});
    void closeTarget(const QString& target);
    void exportSettings();
    void importSettings();
    void resetServerList();
    void startConfiguredStartupConnection();
    void startConnection(const maxchat::core::NetworkConfig& network);
    void connectNextServer(const QString& network, bool forceNext = false);
    void connectNextServer(bool forceNext = false);
    void reconnectNetwork(const QString& network);
    void reconnectCurrentServer();
    void disconnectNetwork(const QString& network);
    void disconnectFromCurrentServer();
    void handleDisconnected(const QString& network, const QString& reason);
    void handleDisconnected(const QString& reason);
    void connectFromCommand(const QString& command, const QStringList& targets,
                            const QString& text);
    void handleInputSubmitted();
    void sendCommandOrMessage(const QString& text);
    void addInputHistory(const QString& text);
    bool showHistoryEntry(int delta);
    bool completeInput();
    void showNetworkTreeContextMenu(const QPoint& pos);
    void showMemberContextMenu(const QPoint& pos);
    void openQueryForNick(const QString& nick);
    void sendModeChange(const QString& channel, const QString& change);
    void kickNick(const QString& channel, const QString& nick, const QString& reason = {});
    void banNick(const QString& channel, const QString& nick);
    void kickBanNick(const QString& channel, const QString& nick);
    void addIgnoreMask(const QString& mask);
    void removeIgnoreMask(const QString& mask);
    void addMutedChannel(const QString& channel);
    void removeMutedChannel(const QString& channel);
    [[nodiscard]] bool isMutedChannel(const QString& channel) const;
    [[nodiscard]] bool textHighlightsMe(const QString& text, const QString& nick) const;
    void noteUserActivity();
    void triggerAutoAway();
    void applyCtcpVersion(const QVariantMap& settings);
    [[nodiscard]] QString mutedChannelKey(const QString& channel) const;
    void addFriendNick(const QString& nick);
    void removeFriendNick(const QString& nick);
    void showAliasList();
    void setAliasCommand(const QString& name, const QString& expansion);
    void removeAliasCommand(const QString& name);
    [[nodiscard]] QString systemInfoText() const;
    void showScriptsPlaceholder(const QString& command);
    void showCommandHelp(const QString& topic = {});
    void appendReplyLineForNetwork(const QString& network, const QString& line);
    void appendReplyLine(const QString& line);
    void pollFriends();
    void handleIsonReplyForNetwork(const QString& network, const QStringList& onlineNicks);
    void handleIsonReply(const QStringList& onlineNicks);
    [[nodiscard]] bool shouldDropForFlood(const QString& sender, const QString& ownNick);
    bool findInChat(const QString& text, bool backwards, bool caseSensitive, bool wrapSearch);
    [[nodiscard]] QStringList completionCandidates(bool commandCompletion) const;
    [[nodiscard]] maxchat::core::ChatLineFormatOptions chatLineFormatOptions() const;
    [[nodiscard]] QString timestampText() const;
    void appendPlainChatLine(const QString& line);
    void appendHtmlChatLine(const QString& html);
    void appendFormattedChatLine(const maxchat::core::FormattedChatLine& line);
    void appendPreviewHtmlLine(const QString& html);
    void appendPreviewHtmlToNetworkTarget(const QString& network, const QString& target,
                                          const QString& html);
    void appendPreviewHtml(const QString& html);
    void appendSystemLine(const QString& line, bool logLine = true);
    void appendSystemLineToNetworkTarget(const QString& network, const QString& target,
                                         const QString& line, bool logLine = true,
                                         bool localEcho = false, bool highlight = false,
                                         bool systemStyling = true);
    void appendSystemLineToTarget(const QString& target, const QString& line, bool logLine = true,
                                  bool localEcho = false, bool highlight = false,
                                  bool systemStyling = true);
    void appendRawLogLine(const QString& line);
    void appendChatLogLine(const QString& line);
    void appendChatLogLineForNetworkTarget(const QString& network, const QString& target,
                                           const QString& line);
    void appendChatLogLineForTarget(const QString& target, const QString& line);
    void rememberUrlsFromLine(const QString& line);
    void queueLinkPreviewsFromLine(const QString& line);
    void handlePreviewCardFetched(const QUrl& url, const maxchat::services::OpenGraphCard& card);
    void handlePreviewFetchFailed(const QUrl& url, const QString& reason);
    void setConnectionTopic(const QString& line);
    [[nodiscard]] maxchat::core::ChatBufferId bufferIdForTarget(const QString& target);
    [[nodiscard]] bool isActiveBufferTarget(const QString& target) const;
    [[nodiscard]] bool isActiveBufferTarget(const QString& network, const QString& target) const;
    void activateBufferTarget(const QString& target);
    void renderActiveBuffer(int unreadMarkerFromEnd = 0);
    void renderActiveBufferMetadata();
    void setComicMode(bool enabled);
    void refreshComic();
    void recolorMemberList();
    void appendUnreadMarkerLine();
    void rebuildLooksMenu();
    void saveCurrentLook();
    void applyLook(const QString& name);
    void deleteLook(const QString& name);
    [[nodiscard]] QTreeWidgetItem* treeItemForTabIndex(int index) const;
    void resetAllSettings();
    void memberListChanged();
    void setNickColorOverride(const QString& nick);
    void clearNickColorOverride(const QString& nick);
    void setupNavShortcuts();
    void applyNavShortcutOverrides(const QVariantMap& settings);
    void jumpToBufferIndex(int index);
    void jumpToNextActivity();
    void openShortcutEditor();
    void handleChatAnchorClicked(const QUrl& url);
    void removeMemberFromChannelBuffers(const QString& network, const QString& nick);
    void removeMemberFromChannelBuffers(const QString& nick);
    void renameMemberInChannelBuffers(const QString& network, const QString& oldNick,
                                      const QString& newNick);
    void renameMemberInChannelBuffers(const QString& oldNick, const QString& newNick);
    void setupTrayIcon();
    void updateTrayIcon();
    void toggleWindowVisibility();
    void updateMinimizeToTrayFromSettings();
    void notify(const QString& title, const QString& text,
                const QString& network = {}, const QString& target = {});
    [[nodiscard]] QStringList channelTargetsContainingMember(const QString& network,
                                                             const QString& nick) const;
    [[nodiscard]] QStringList channelTargetsContainingMember(const QString& nick) const;
    [[nodiscard]] QStringList joinedChannelTargets(const QString& network) const;
    [[nodiscard]] QStringList joinedChannelTargets() const;
    [[nodiscard]] QString treeDisplayLabelForNetwork(const QString& network);
    [[nodiscard]] QString treeDisplayLabelForTarget(const QString& network, const QString& target);
    [[nodiscard]] QString treeDisplayLabelForTarget(const QString& target);
    [[nodiscard]] QStringList visibleTreeTargets(const QString& network) const;
    [[nodiscard]] QStringList visibleTreeTargets() const;
    void updateNetworkTreeLabels();
    void rememberNetwork(const QString& network);
    void setActiveNetwork(const QString& network);
    [[nodiscard]] QString activeNetworkName() const;
    [[nodiscard]] QString currentTargetForNetwork(const QString& network) const;
    [[nodiscard]] QStringList openTargetsForNetwork(const QString& network) const;
    [[nodiscard]] bool networkRegistered(const QString& network) const;
    [[nodiscard]] maxchat::core::ChatBufferId bufferIdForNetworkTarget(const QString& network,
                                                                       const QString& target);
    void rememberTarget(const QString& target);
    void forgetTarget(const QString& target);
    void rebuildNetworkTree();
    void updateChannelModeButton();
    [[nodiscard]] QString currentLogNetwork() const;
    [[nodiscard]] QString currentLogTarget() const;
    [[nodiscard]] QString currentNickForNetwork(const QString& network) const;
    [[nodiscard]] maxchat::irc::IrcConnection::ConnectConfig
    connectConfigFor(const maxchat::irc::ServerEndpoint& server,
                     const maxchat::core::NetworkConnectionPlan& plan) const;
    [[nodiscard]] maxchat::irc::IrcConnection::ConnectConfig
    connectConfigFor(const maxchat::irc::ServerEndpoint& server) const;
    [[nodiscard]] int maxInitialConnectAttempts(const QString& network) const;
    [[nodiscard]] int maxInitialConnectAttempts() const;

    maxchat::core::SettingsStore m_settings;
    maxchat::core::ChatLogStore m_chatLogStore;
    maxchat::core::ChatBufferStore m_chatBuffers;
    maxchat::core::FloodGuard m_floodGuard;
    maxchat::irc::IrcConnection m_connection;
    QHash<QString, maxchat::irc::IrcConnection*> m_connectionsByNetwork;
    QNetworkAccessManager m_previewNetworkManager;
    maxchat::services::OpenGraphFetcher m_openGraphFetcher;
    maxchat::core::NetworkConnectionPlan m_connectionPlan;
    QString m_currentTarget;
    int m_initialConnectAttempts = 0;
    QHash<QString, int> m_initialConnectAttemptsByNetwork;
    bool m_hasConnectionPlan = false;
    bool m_registered = false;
    QElapsedTimer m_appUptime;
    QElapsedTimer m_connectionUptime;

    // Tray icon (optional, only if system tray is available)
    ::QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    bool m_minimizeToTray = false;
    bool m_osNotifyAvailable = false;
    QHash<QString, qint64> m_connectionUptimeStartMsByNetwork;
    bool m_connectionUptimeRunning = false;
    bool m_manualDisconnect = false;
    QHash<QString, bool> m_manualDisconnectByNetwork;
    bool m_reconnectRequested = false;
    QHash<QString, bool> m_reconnectRequestedByNetwork;
    bool m_backgroundNetworkContext = false;
    bool m_autoReconnect = true;
    bool m_loggingEnabled = true;
    bool m_replayLogEnabled = true;
    bool m_replayingLog = false;
    bool m_showTimestamps = true;
    bool m_alignNicks = true;
    bool m_separatorLine = true;
    bool m_hideJoinPart = false;
    bool m_showFormatting = true;
    bool m_coloredNicks = true;
    bool m_showMode = true;
    bool m_pmEcho = true;
    bool m_indentWrap = true;
    bool m_markerLine = true;
    int m_replayLines = 0;
    int m_nickColumnWidth = 16;
    QString m_timestampFormat = QStringLiteral("%I:%M %p");
    QString m_currentTheme = QStringLiteral("synthwave");
    QString m_currentChatTheme = QStringLiteral("follow");
    QString m_currentWallpaper;
    QVariantMap m_commandAliases;
    maxchat::services::LinkPreviewToggles m_linkPreviewToggles;
    QHash<QString, maxchat::services::LinkPreviewCandidate> m_pendingPreviewCandidates;
    QHash<QString, QStringList> m_pendingNamesByChannel;
    QHash<QString, QString> m_channelModeLines;
    QHash<QString, maxchat::core::NetworkConnectionPlan> m_connectionPlansByNetwork;
    QHash<QString, QString> m_currentTargetByNetwork;
    QHash<QString, QStringList> m_openTargetsByNetwork;
    QHash<QString, bool> m_registeredByNetwork;
    QHash<QString, QSet<QString>> m_onlineFriendsByNetwork;
    QHash<QString, bool> m_haveFriendSnapshotByNetwork;
    QStringList m_knownNetworks;
    QStringList m_openTargets;
    QStringList m_rawLogLines;
    QStringList m_urlList;
    QStringList m_inputHistory;
    QStringList m_ignoreMasks;
    QStringList m_mutedChannelKeys;
    QStringList m_friendNicks;
    QSet<QString> m_onlineFriends;
    int m_inputHistoryIndex = 0;
    bool m_haveFriendSnapshot = false;
    QTimer m_friendPollTimer;
    Notifier* m_notifier = nullptr;
    QStringList m_highlightWords;
    bool m_notifyPm = true;
    bool m_notifyHighlight = true;
    bool m_beepHighlight = false;
    bool m_notifyFlash = true;
    bool m_notifySound = false;
    QString m_notifyStyle = QStringLiteral("custom");
    QString m_notifyCorner = QStringLiteral("br");
    int m_notifyDuration = 6;
    QString m_notifyTheme = QStringLiteral("follow");
    QAction* m_buttonBarAction = nullptr;
    QAction* m_serverListVisibleAction = nullptr;
    QAction* m_membersVisibleAction = nullptr;
    QAction* m_buttonsAsTabsAction = nullptr;
    QAction* m_chatSeparatorAction = nullptr;
    QMenu* m_looksMenu = nullptr;
    QHash<QString, QShortcut*> m_navShortcuts;
    QHash<QString, QSet<QString>> m_awayNicksByNetwork; // lowercase nicks
    QVariantMap m_nickColorOverrides;                   // lowercase nick -> hex
    QString m_eventColor;                               // "" = chat theme / default
    bool m_sortByStatus = true;
    bool m_pasteGuard = true;
    int m_pasteLines = 4;
    bool m_autoRejoin = false;
    int m_rejoinDelay = 2;
    bool m_ignoreInvites = false;
    bool m_inviteProtect = true;
    bool m_confirmQuit = true;
    int m_scrollback = 2000;
    int m_autoAwayMins = 0;
    bool m_autoAwayActive = false;
    QTimer m_autoAwayTimer;
    QLabel* m_membersHeader = nullptr;
    QAction* m_doNotDisturbAction = nullptr;
    QAction* m_comicCaptionsAction = nullptr;
    QAction* m_comicModeAction = nullptr;
    ComicView* m_comicView = nullptr;
    bool m_comicMode = false;
    QList<QAction*> m_themeActions;
    QList<QAction*> m_chatThemeActions;
    QList<QAction*> m_wallpaperActions;
    QToolBar* m_buttonBar = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QTreeWidget* m_networkTree = nullptr;
    QTabBar* m_bufferTabBar = nullptr;
    QTextBrowser* m_chatView = nullptr;
    AudioPlayerBar* m_audioBar = nullptr;
    QWidget* m_memberPanel = nullptr;
    QListWidget* m_memberList = nullptr;
    QPushButton* m_channelModesButton = nullptr;
    QLabel* m_topicLabel = nullptr;
    QTextEdit* m_input = nullptr;
    SpellcheckHighlighter* m_spellcheckHighlighter = nullptr;
#ifdef MAXCHAT_WITH_HUNSPELL
    std::unique_ptr<maxchat::spell::HunspellSpellchecker> m_spellchecker;
#endif
    QPointer<ChatFindDialog> m_chatFindDialog;
    QPointer<BanListDialog> m_banListDialog;
    QPointer<ChannelListDialog> m_channelListDialog;
    QPointer<RawLogDialog> m_rawLogDialog;
    QPointer<UrlListDialog> m_urlListDialog;
};

} // namespace maxchat::ui
