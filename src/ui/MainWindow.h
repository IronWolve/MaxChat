#pragma once

#include "core/ChatBufferStore.h"
#include "core/ChatLineFormatter.h"
#include "core/ChatLogStore.h"
#include "comic/ComicCharacter.h"
#include "core/ConnectionPlan.h"
#include "core/FloodGuard.h"
#include "core/SettingsStore.h"
#include "irc/IrcConnection.h"
#include "services/LinkPreviewPolicy.h"
#include "services/LinkPreviewRenderer.h"
#include "services/ImageFetcher.h"
#include "services/OpenGraphFetcher.h"
#include "ui/ChatPane.h"
#include "ui/MainWindowHost.h"
#include "ui/PreviewFetcher.h"
#include "ui/SoundPlayer.h"
#include "spell/Speller.h" // backend-neutral; OS speller works without Hunspell
#include "ui/ChannelListDialog.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QSet>
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
// White-box unit test (tests/unit/main_window_link_preview_test.cpp) befriended
// below; forward-declared at global scope so the qualified friend resolves.
class MainWindowLinkPreviewTest;

namespace maxchat::ui {

class AudioPlayerBar;
class MediaController;
class AppearanceController;
class ComicView;
class DccManager;
class MediaPlayerDialog;
class Notifier;

class BanListDialog;
class ChatFindDialog;
class IrcRouter;
class RawLogDialog;
class ScriptBridge;
class SpellTextEdit;
class UrlListDialog;

class MainWindow final : public QMainWindow,
                        public MainWindowHost,
                        public ChatPaneDelegate,
                        public PreviewFetcherDelegate {
    // White-box test access without `#define private public` (which gives this
    // header a different layout in the test TU than in the app TU — an ODR
    // violation). A friend grants the same access cleanly.
    friend class ::MainWindowLinkPreviewTest;
    // IrcRouter (decomp Phase 7) drives the window directly from the IRC signal
    // handlers it owns — a behaviour-preserving relocation, so it needs the same
    // private access the handlers had when they lived here.
    friend class IrcRouter;

  public:
    explicit MainWindow(QWidget* parent = nullptr);

    // --- MainWindowHost — the seam decomposed controllers call back through ---
    [[nodiscard]] QString activeNetwork() const override { return activeNetworkName(); }
    [[nodiscard]] QString currentTarget() const override { return m_currentTarget; }
    [[nodiscard]] QString nickFor(const QString& network) override {
        return currentNickForNetwork(network);
    }
    [[nodiscard]] QStringList channelsFor(const QString& network) override;
    [[nodiscard]] QStringList nicksFor(const QString& network, const QString& target) override {
        return m_chatBuffers.snapshot(bufferIdForNetworkTarget(network, target)).members;
    }
    void appendActiveSystemLine(const QString& text) override { appendSystemLine(text); }
    void appendSystemLine(const QString& network, const QString& target,
                          const QString& text) override {
        appendSystemLineToNetworkTarget(network, target, text);
    }
    void echoOutbound(const QString& network, const QString& target,
                      const QString& text) override {
        appendSystemLineToNetworkTarget(network, target, text, true, true);
    }
    void insertInput(const QString& text) override; // m_input type incomplete here
    void notifyUser(const QString& title, const QString& text) override {
        notify(title, text, activeNetworkName(), m_currentTarget);
    }
    void appendInputUrl(const QString& url) override;  // m_input type incomplete here
    void showStatus(const QString& text, int timeoutMs = 0) override; // QStatusBar incomplete
    void clearStatus() override;
    [[nodiscard]] maxchat::irc::IrcConnection* connectionFor(const QString& network) override {
        return connectionForNetwork(network);
    }
    [[nodiscard]] QNetworkAccessManager& scriptNetworkManager() override {
        return m_updateNetworkManager;
    }
    [[nodiscard]] QNetworkAccessManager& previewNetworkManager() override {
        return m_previewNetworkManager;
    }
    [[nodiscard]] maxchat::core::SettingsStore& settings() override { return m_settings; }
    [[nodiscard]] QWidget* dialogParent() override { return this; }
    void rebuildTree() override { rebuildNetworkTree(); }
    // AppearanceController refresh hooks. renderActiveBuffer/recolorMemberList/
    // updateChatSeparatorGuide/updateTrayIcon are existing private methods that
    // auto-satisfy the pure virtuals; these two the window doesn't already have:
    void setMenuBarFont(const QFont& font) override; // QMenuBar incomplete in header
    void applyAllSettings() override { applyCurrentSettings(); }

    // --- ChatPaneDelegate — gestures from the chat view (render-pipeline R1/R2) ---
    void chatAnchorClicked(const QUrl& url) override; // MediaController incomplete here
    void chatSeparatorMoved(int nickWidth) override;
    void previewImageNeeded(const QUrl& url) override { m_imageFetcher.fetch(url); }
    [[nodiscard]] bool activeBufferReferencesImage(const QString& url) override;
    void rerenderActiveBuffer() override { renderActiveBuffer(); }

    // --- PreviewFetcherDelegate — where rendered link-preview cards land (2b) ---
    void appendPreviewHtmlToActiveBuffer(const QString& html) override { appendPreviewHtml(html); }
    void appendPreviewHtmlToBuffer(const QString& network, const QString& target,
                                   const QString& html) override {
        appendPreviewHtmlToNetworkTarget(network, target, html);
    }
    [[nodiscard]] QString previewOriginNetwork() override { return currentLogNetwork(); }
    [[nodiscard]] QString previewOriginTarget() override { return m_currentTarget; }
    [[nodiscard]] bool isReplayingLog() override { return m_replayingLog; }

    bool selfTest() const;

  private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

    // HexChat-style key redirect: typing anywhere in the main window jumps to
    // the message box. Returns true if the key was consumed (caller should too).
    // Never fires when a menu, popup, or modal dialog is active.
    bool redirectKeyToInput(QKeyEvent* e);

    void buildMenus();
    void buildLayout();
    void applyCurrentSettings();
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
    void closeBufferTab(int index); // close the buffer behind a tab (✕ / context menu)
    void showInputContextMenu(const QPoint& localPos, const QPoint& globalPos); // spell suggestions + edit
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
    void openComicHelp();
    void openAbout();
    // Quiet GitHub Releases check (manual=true also reports "you're up to date").
    void checkForUpdates(bool manual);
    // Play a received CTCP SOUND if enabled and the .wav is one the user owns.
    void handleCtcpSound(const QString& network, const QString& sender, const QString& target,
                         const QString& file, const QString& text);
    // /scripts (list) and /load /unload /reload <name>. The scripting subsystem
    // itself lives in ScriptBridge (m_scripts); these just forward.
    void handleScriptsCommand(const QString& command, const QString& arg);
    void openScriptsManager();
    void updateWindowTitle();      // "MaxChat <ver> — <network> / <channel>" (active context)
    void updateNickLabel();        // your-nick label by the input box
    void leaveCurrentChannel();
    void leaveAllChannels(const QString& network); // empty = active network
    [[nodiscard]] QColor resolvedChatBackground() const;
    // One source of nick colours for chat view + member list (contrast-guarded).
    [[nodiscard]] QStringList effectiveNickPalette(bool* monoOut) const;
    // Seed a buffer's stored line model with dimmed log history + a "Chat ended"
    // divider so resume survives buffer switches (rendered, not painted). Returns
    // true if any history was added.
    bool seedReplayForBuffer(const QString& network, const QString& target);
    QSet<QString> m_replayedBuffers; // network\x1ftarget already seeded
    void markAllRead();
    void clearCurrentChat();
    void clearAllChats();
    void showUptime();
    void showNetInfo();
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
    // Disconnect AND remove a network: frees its buffers, per-network state,
    // tree row, and (non-default) connection object.
    void closeNetwork(const QString& network);
    void disconnectFromCurrentServer();
    void handleDisconnected(const QString& network, const QString& reason);
    void handleDisconnected(const QString& reason);
    void connectFromCommand(const QString& command, const QStringList& targets,
                            const QString& text);
    void handleInputSubmitted();
    void sendCommandOrMessage(const QString& text);
    void addInputHistory(const QString& text);
    bool showHistoryEntry(int delta);
    bool completeInput(bool forward);
    void applyCompletionCandidate(); // write the current cycle candidate into the input
    struct CompletionCycle {
        bool active = false;
        bool commandCompletion = false;
        int tokenStart = 0;
        QString head;          // input text before the token being completed
        QString tail;          // input text after the original token
        QStringList matches;   // all candidates matching the typed prefix
        int index = -1;        // current position in matches
        QString lastText;      // input after our last insertion (to detect a continued cycle)
        int lastCursor = -1;
    } m_completion;

    // Autocorrect-on-space state. After a word is auto-replaced, an immediate
    // Backspace undoes it and suppresses re-correcting that exact word once
    // (so deliberately "wrong" spellings stick); typing past it resumes.
    struct AutocorrectState {
        bool active = false;   // a correction was just applied; next key decides
        int wordStart = 0;     // doc offset where the corrected word began
        QString original;      // what the user typed
        QString corrected;     // what we replaced it with
    } m_autocorrect;
    QString m_autocorrectSuppress; // word the user undid; don't re-correct it next
    int m_autocorrectMaxDistance = 2; // max edit distance for an auto-replacement
    bool tryAutocorrectAtSpace();  // returns true if it consumed the space
    bool undoAutocorrect();        // returns true if a pending correction was undone

    // Personal dictionary: words the user added ("Add to Dictionary"), one per
    // line in <config>/personal_dict.dic. Loaded into the live speller and folded
    // into settings export/import.
    [[nodiscard]] QString personalDictionaryPath() const;
    void loadPersonalDictionary();
    void addWordToPersonalDictionary(const QString& word);
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
    // ImageFetcher result handlers — forward into ChatPane's preview cache (R3).
    // activeBufferReferencesImage() is the ChatPaneDelegate override (declared above).
    void handlePreviewImageFetched(const QUrl& url, const QImage& image);
    void handlePreviewImageFailed(const QUrl& url, const QString& reason);
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
    void showConnectionStatus(const QString& line);
    [[nodiscard]] maxchat::core::ChatBufferId bufferIdForTarget(const QString& target);
    [[nodiscard]] bool isActiveBufferTarget(const QString& target) const;
    [[nodiscard]] bool isActiveBufferTarget(const QString& network, const QString& target) const;
    void activateBufferTarget(const QString& target);
    void renderActiveBuffer();
    void renderActiveBufferMetadata();
    // Per-buffer unread boundary: the line index of the first message that arrived
    // while the buffer was NOT active. Keyed network\x1ftarget. The `──── new ────`
    // marker is drawn before that line, so it persists across switches and shows
    // per channel; cleared once a message arrives while the buffer IS active (you
    // read it live). Absent = no unread boundary.
    QHash<QString, int> m_bufferMarkerCount;
    void noteUnreadBoundary(const maxchat::core::ChatBufferId& id, bool active,
                            int lineCountBeforeAppend);
    void setComicMode(bool enabled);
    void refreshComic();
    void ensureComicArt();
    [[nodiscard]] maxchat::comic::Character* comicCharacterForNick(const QString& nick);
    [[nodiscard]] QString comicEmotionForMessage(const QString& nick, const QString& text);
    [[nodiscard]] QImage comicBackground();
    void openComicSettings();
    void openCharacterGallery();
    void openEmotionPicker();
    void saveComic();
    void openDccTransfers();
    void handleDccCommand(const QStringList& args);
    void configureDcc();
    void recolorMemberList();
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
    QNetworkAccessManager m_updateNetworkManager;
    SoundPlayer m_soundPlayer;
    ScriptBridge* m_scripts = nullptr; // owns LuaEngine + ScriptTerminalManager
    IrcRouter* m_ircRouter = nullptr;  // IRC signal → handler wiring (Phase 7)
    PreviewFetcher* m_previewFetcher = nullptr; // link-preview card fetch (Phase 2b)
    maxchat::services::ImageFetcher m_imageFetcher;
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
    QVariantMap m_commandAliases;
    // Kept for the inline-image scaling in handlePreviewImageFetched; the link-
    // preview card fetch + its toggles/candidates live in PreviewFetcher (2b).
    maxchat::services::LinkPreviewRenderOptions m_ogRenderOptions;
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
    QTimer m_channelDrainTimer;
    QVector<ChannelListDialog::ChannelEntry> m_pendingChannels;
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
    QLabel* m_nickLabel = nullptr;
    QAction* m_doNotDisturbAction = nullptr;
    QAction* m_comicCaptionsAction = nullptr;
    QAction* m_comicModeAction = nullptr;
    ComicView* m_comicView = nullptr;
    QPointer<MediaPlayerDialog> m_mediaPlayerDialog; // single reusable video player
    QString m_lastNotifyNetwork; // where the last OS notification points
    QString m_lastNotifyTarget;
    bool m_comicMode = false;            // backend running — true iff any buffer opted in
    QSet<QString> m_comicEnabledBuffers; // per-buffer opt-in (key = network\x1ftarget)
    QString m_comicSelfEmotion = QStringLiteral("auto"); // override for your panels; "auto" = guess
    QString m_comicArtDirLoaded;
    QHash<QString, QString> m_comicCharacterPaths;  // stem -> .avb path
    QHash<QString, QString> m_comicBackgroundPaths; // filename -> .bgb path
    QHash<QString, QString> m_comicAutoChars;       // nick -> assigned stem
    QHash<QString, QImage> m_comicBgCache;
    QHash<QString, QPixmap> m_comicPanelCache; // rendered-panel cache, keyed by full visual input
    bool m_dccEnabled = false;
    DccManager* m_dccManager = nullptr;
    QToolBar* m_buttonBar = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QTreeWidget* m_networkTree = nullptr;
    QTabBar* m_bufferTabBar = nullptr;
    ChatPane* m_chatPane = nullptr;             // owns the chat view + append primitives
    QTextBrowser* m_chatView = nullptr;         // borrowed = m_chatPane->view() (narrowing)
    AudioPlayerBar* m_audioBar = nullptr;
    QWidget* m_memberPanel = nullptr;
    QListWidget* m_memberList = nullptr;
    QPushButton* m_channelModesButton = nullptr;
    QLabel* m_topicLabel = nullptr;
    QString m_topicFullText; // unelided topic; label shows an elided version
    void updateTopicElide();
    SpellTextEdit* m_input = nullptr;
    // Active speller (internal Hunspell or native OS engine) — declared
    // unconditionally so the OS backend works on builds without Hunspell.
    std::unique_ptr<maxchat::spell::Speller> m_spellchecker;
    MediaController* m_media = nullptr;           // inline media playback + image upload
    AppearanceController* m_appearance = nullptr; // fonts + (incrementally) theming
    bool m_autocorrectEnabled = false;
    bool m_focusedOnce = false;
    QPointer<ChatFindDialog> m_chatFindDialog;
    QPointer<BanListDialog> m_banListDialog;
    QPointer<ChannelListDialog> m_channelListDialog;
    QPointer<RawLogDialog> m_rawLogDialog;
    QPointer<UrlListDialog> m_urlListDialog;
};

} // namespace maxchat::ui
