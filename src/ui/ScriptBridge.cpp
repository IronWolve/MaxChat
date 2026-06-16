#include "ui/ScriptBridge.h"

#include "core/SettingsStore.h"
#include "irc/IrcConnection.h"
#include "scripting/LuaEngine.h"
#include "services/LinkPreviewClassifier.h"
#include "ui/MainWindowHost.h"
#if MAXCHAT_TERMINAL
#include "ui/AnsiRenderer.h"
#include "ui/TerminalProfile.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <optional>
#include <utility>

namespace maxchat::ui {

#if MAXCHAT_TERMINAL
namespace {

QString terminalScopedId(const QString& scriptName, const QString& id) {
    return QStringLiteral("%1/%2").arg(scriptName.trimmed(), id.trimmed());
}

std::optional<std::pair<QString, QString>> splitTerminalScopedId(const QString& scopedId) {
    const int slash = scopedId.indexOf(QLatin1Char('/'));
    if (slash <= 0 || slash == scopedId.size() - 1) {
        return std::nullopt;
    }
    return std::make_pair(scopedId.left(slash), scopedId.mid(slash + 1));
}

} // namespace
#endif // MAXCHAT_TERMINAL

ScriptBridge::ScriptBridge(MainWindowHost& host, QString scriptsDir, QObject* parent)
    : QObject(parent), host_(host), scriptsDir_(std::move(scriptsDir)) {
#if MAXCHAT_TERMINAL
    // The terminal manager parents real dialog widgets, so it needs a QWidget
    // parent (the window), not the bridge. Lifetime still tracks the window.
    terminals_ = new ScriptTerminalManager(host_.dialogParent());
    // Terminal hooks run in the network context the terminal was OPENED on,
    // not whatever network is active now — a BBS session must keep sending to
    // the network it dialed on even if the user switches buffers.
    const auto terminalContext = [this](const QString& scopedId) {
        const QString network =
            terminals_ != nullptr ? terminals_->terminalNetwork(scopedId) : QString();
        return network.isEmpty() ? host_.activeNetwork() : network;
    };
    connect(terminals_, &ScriptTerminalManager::inputSubmitted, this,
            [this, terminalContext](const QString& scopedId, const QString& text) {
                if (lua_ == nullptr) {
                    return;
                }
                const auto split = splitTerminalScopedId(scopedId);
                if (!split.has_value()) {
                    return;
                }
                lua_->dispatchToScript(split->first, QStringLiteral("on_terminal_input"),
                                       terminalContext(scopedId), {split->second, text});
            });
    connect(terminals_, &ScriptTerminalManager::linkActivated, this,
            [this, terminalContext](const QString& scopedId, const QString& actionId) {
                if (lua_ == nullptr) {
                    return;
                }
                const auto split = splitTerminalScopedId(scopedId);
                if (!split.has_value()) {
                    return;
                }
                lua_->dispatchToScript(split->first, QStringLiteral("on_terminal_link"),
                                       terminalContext(scopedId), {split->second, actionId});
            });
    connect(terminals_, &ScriptTerminalManager::terminalClosed, this,
            [this](const QString& scopedId, const QString& network) {
                if (lua_ == nullptr) {
                    return;
                }
                const auto split = splitTerminalScopedId(scopedId);
                if (!split.has_value()) {
                    return;
                }
                lua_->dispatchToScript(split->first, QStringLiteral("on_terminal_closed"),
                                       network.isEmpty() ? host_.activeNetwork() : network,
                                       {split->second});
            });
    connect(terminals_, &ScriptTerminalManager::terminalsChanged, this,
            [this]() { host_.rebuildTree(); });
    connect(terminals_, &ScriptTerminalManager::fontPreferenceChanged, this,
            [this](const QString& family, const int pointSize, const bool bold) {
                // A terminal's Settings menu changed the global terminal font.
                QVariantMap settings = host_.settings().loadRaw();
                settings.insert(QStringLiteral("terminal_font_family"), family);
                settings.insert(QStringLiteral("terminal_font_size"), pointSize);
                settings.insert(QStringLiteral("terminal_font_bold"), bold);
                (void)host_.settings().saveRaw(settings);
                terminals_->setTerminalFont(family, pointSize, bold);
            });
    connect(terminals_, &ScriptTerminalManager::gridSizeChanged, this,
            [this](const QString&, const int, const int rows) {
                // Remember the chosen grid as the default for new terminals.
                QVariantMap settings = host_.settings().loadRaw();
                settings.insert(QStringLiteral("terminal_rows"), rows);
                (void)host_.settings().saveRaw(settings);
            });
#endif // MAXCHAT_TERMINAL
    lua_ = new maxchat::scripting::LuaEngine(
        this, scriptsDir_, QDir(scriptsDir_).filePath(QStringLiteral("data")), this);
}

ScriptBridge::~ScriptBridge() = default;

bool ScriptBridge::scriptingAvailable() {
    return maxchat::scripting::LuaEngine::available();
}

void ScriptBridge::seedAndLoadAll() {
    if (!maxchat::scripting::LuaEngine::available()) {
        return;
    }
    QDir().mkpath(scriptsDir_);
    seedBundledScripts(scriptsDir_);
    lua_->loadAll(buildAllScriptPermsMap(), true);
}

QStringList ScriptBridge::loadedScripts() const {
    return maxchat::scripting::LuaEngine::available() ? lua_->loaded() : QStringList{};
}

bool ScriptBridge::loadByName(const QString& name) {
    const QString path = QDir(scriptsDir_).filePath(name + QStringLiteral(".lua"));
    return lua_->load(path, buildScriptPermissionsFor(name));
}

bool ScriptBridge::unloadByName(const QString& name) {
    return lua_->unload(name);
}

bool ScriptBridge::reloadByName(const QString& name) {
    return lua_->reload(name);
}

void ScriptBridge::reapplyPermissions(const QString& name) {
    if (maxchat::scripting::LuaEngine::available() && lua_->loaded().contains(name)) {
        const QString path = QDir(scriptsDir_).filePath(name + QStringLiteral(".lua"));
        lua_->load(path, buildScriptPermissionsFor(name));
    }
}

bool ScriptBridge::dispatch(const QString& hook, const QString& network,
                            const QVariantList& args) {
    return lua_ != nullptr && lua_->dispatch(hook, network, args);
}

#if MAXCHAT_TERMINAL
void ScriptBridge::showTerminal(const QString& id) {
    if (terminals_ != nullptr) {
        terminals_->showTerminal(id);
    }
}

void ScriptBridge::killTerminal(const QString& id) {
    if (terminals_ != nullptr) {
        terminals_->killTerminal(id);
    }
}

QList<TerminalInfo> ScriptBridge::terminals() const {
    return terminals_ != nullptr ? terminals_->terminals() : QList<TerminalInfo>{};
}

void ScriptBridge::setTerminalFont(const QString& family, const int pointSize, const bool bold) {
    if (terminals_ != nullptr) {
        terminals_->setTerminalFont(family, pointSize, bold);
    }
}
#else // !MAXCHAT_TERMINAL — built without the script terminal / BBS UI
void ScriptBridge::showTerminal(const QString&) {}
void ScriptBridge::killTerminal(const QString&) {}
QList<TerminalInfo> ScriptBridge::terminals() const { return {}; }
void ScriptBridge::setTerminalFont(const QString&, int, bool) {}
#endif // MAXCHAT_TERMINAL

// --- ScriptHost -------------------------------------------------------------

void ScriptBridge::scriptEcho(const QString& network, const QString& text) {
    if (network.isEmpty() || network.compare(host_.activeNetwork(), Qt::CaseInsensitive) == 0) {
        host_.appendActiveSystemLine(text);
    } else {
        host_.appendSystemLine(network, QStringLiteral("server"), text);
    }
}

void ScriptBridge::scriptSay(const QString& network, const QString& target, const QString& text) {
    const QString net = network.isEmpty() ? host_.activeNetwork() : network;
    maxchat::irc::IrcConnection* conn = host_.connectionFor(net);
    if (conn == nullptr || target.trimmed().isEmpty() || text.isEmpty()) {
        return;
    }
    if (conn->privmsg(target, text)) {
        host_.echoOutbound(net, target,
                           QStringLiteral("<%1> %2").arg(host_.nickFor(net), text));
    }
}

void ScriptBridge::scriptSendRaw(const QString& network, const QString& line) {
    const QString net = network.isEmpty() ? host_.activeNetwork() : network;
    if (maxchat::irc::IrcConnection* conn = host_.connectionFor(net); conn != nullptr) {
        conn->sendRaw(line); // sendRaw already strips CR/LF
    }
}

bool ScriptBridge::scriptMcData(const QString& network, const QString& target,
                                const QString& service, const QString& verb,
                                const QString& payload, const bool notice) {
    const QString net = network.isEmpty() ? host_.activeNetwork() : network;
    maxchat::irc::IrcConnection* conn = host_.connectionFor(net);
    if (conn == nullptr) {
        host_.appendSystemLine(
            net, QStringLiteral("server"),
            QStringLiteral("[scripts] Cannot send MC DATA: network is not connected."));
        return false;
    }
    const bool ok = conn->mcData(target, service, verb, payload, notice);
    if (!ok) {
        host_.appendSystemLine(net, QStringLiteral("server"),
                               QStringLiteral("[scripts] Cannot send MC DATA to %1.").arg(target));
    }
    return ok;
}

#if MAXCHAT_TERMINAL
bool ScriptBridge::scriptTerminalOpen(const QString& scriptName, const QString& id,
                                      const QString& title, const QString& profile,
                                      const int cols, const int rows) {
    if (terminals_ == nullptr || scriptName.trimmed().isEmpty() || id.trimmed().isEmpty()) {
        return false;
    }
    // Fixed-grid profiles default to the user's preferred rows (80x25 or 80x40).
    maxchat::ui::TerminalProfile prof = terminalProfile(profile, cols, rows);
    if (prof.fixedGrid && rows <= 0) {
        const int defRows =
            host_.settings().loadRaw().value(QStringLiteral("terminal_rows"), 25).toInt();
        if (defRows == 40 && prof.cols == 80) {
            prof.rows = 40;
        }
    }
    terminals_->openTerminal(terminalScopedId(scriptName, id), title, prof, host_.activeNetwork(),
                             scriptName.trimmed());
    return true;
}

void ScriptBridge::scriptTerminalClose(const QString& scriptName, const QString& id) {
    if (terminals_ != nullptr) {
        // A script closing its own terminal ends the session.
        terminals_->killTerminal(terminalScopedId(scriptName, id));
    }
}

void ScriptBridge::scriptTerminalClear(const QString& scriptName, const QString& id) {
    if (terminals_ != nullptr) {
        terminals_->clear(terminalScopedId(scriptName, id));
    }
}

void ScriptBridge::scriptTerminalWrite(const QString& scriptName, const QString& id,
                                       const QString& text) {
    if (terminals_ != nullptr) {
        terminals_->writeText(terminalScopedId(scriptName, id), text);
    }
}

bool ScriptBridge::scriptTerminalFrame(const QString& scriptName, const QString& id,
                                       const QString& ops) {
    if (terminals_ == nullptr) {
        return false;
    }
    QString error;
    const bool ok = terminals_->applyFrame(terminalScopedId(scriptName, id), ops, &error);
    if (!ok) {
        host_.appendActiveSystemLine(
            QStringLiteral("[scripts] Terminal frame rejected: %1").arg(error));
    }
    return ok;
}

void ScriptBridge::scriptTerminalStatus(const QString& scriptName, const QString& id,
                                        const QString& text) {
    if (terminals_ != nullptr) {
        terminals_->setStatusText(terminalScopedId(scriptName, id), text);
    }
}

void ScriptBridge::scriptTerminalPrompt(const QString& scriptName, const QString& id,
                                        const QString& text) {
    if (terminals_ != nullptr) {
        terminals_->setPromptText(terminalScopedId(scriptName, id), text);
    }
}

QSize ScriptBridge::scriptTerminalSize(const QString& scriptName, const QString& id) {
    return terminals_ != nullptr ? terminals_->terminalSize(terminalScopedId(scriptName, id))
                                 : QSize();
}

void ScriptBridge::scriptTerminalProfile(const QString& scriptName, const QString& id,
                                         const QString& profile, const int cols, const int rows) {
    if (terminals_ != nullptr) {
        terminals_->setProfile(terminalScopedId(scriptName, id),
                               terminalProfile(profile, cols, rows));
    }
}

void ScriptBridge::scriptTerminalFit(const QString& scriptName, const QString& id,
                                     const QString& mode) {
    if (terminals_ != nullptr) {
        terminals_->setFitMode(terminalScopedId(scriptName, id), mode);
    }
}

QString ScriptBridge::scriptTerminalHotspot(const QString& actionId, const QString& label) {
    return AnsiRenderer::hotspot(actionId, label);
}
#else // !MAXCHAT_TERMINAL — api.terminal_* are inert (BBS scripts degrade gracefully)
bool ScriptBridge::scriptTerminalOpen(const QString&, const QString&, const QString&,
                                      const QString&, int, int) {
    return false;
}
void ScriptBridge::scriptTerminalClose(const QString&, const QString&) {}
void ScriptBridge::scriptTerminalClear(const QString&, const QString&) {}
void ScriptBridge::scriptTerminalWrite(const QString&, const QString&, const QString&) {}
bool ScriptBridge::scriptTerminalFrame(const QString&, const QString&, const QString&) {
    return false;
}
void ScriptBridge::scriptTerminalStatus(const QString&, const QString&, const QString&) {}
void ScriptBridge::scriptTerminalPrompt(const QString&, const QString&, const QString&) {}
QSize ScriptBridge::scriptTerminalSize(const QString&, const QString&) { return {}; }
void ScriptBridge::scriptTerminalProfile(const QString&, const QString&, const QString&, int,
                                         int) {}
void ScriptBridge::scriptTerminalFit(const QString&, const QString&, const QString&) {}
QString ScriptBridge::scriptTerminalHotspot(const QString&, const QString& label) {
    return label; // no ANSI hotspot markup without the terminal renderer
}
#endif // MAXCHAT_TERMINAL

void ScriptBridge::scriptInsertInput(const QString& text) {
    host_.insertInput(text);
}

void ScriptBridge::scriptNotify(const QString& title, const QString& text) {
    host_.notifyUser(title, text);
}

QString ScriptBridge::scriptMe(const QString& network) {
    return host_.nickFor(network.isEmpty() ? host_.activeNetwork() : network);
}

QString ScriptBridge::scriptTarget() {
    const QString target = host_.currentTarget();
    return target.trimmed().isEmpty() ? QStringLiteral("(server)") : target;
}

QString ScriptBridge::scriptNetwork() {
    return host_.activeNetwork();
}

QStringList ScriptBridge::scriptChannels(const QString& network) {
    return host_.channelsFor(network.isEmpty() ? host_.activeNetwork() : network);
}

QStringList ScriptBridge::scriptNicks(const QString& network, const QString& target) {
    const QString net = network.isEmpty() ? host_.activeNetwork() : network;
    const QString tgt = target.trimmed().isEmpty() ? host_.currentTarget() : target;
    return host_.nicksFor(net, tgt);
}

QString ScriptBridge::scriptHttpGet(const QString& url) {
    const QUrl parsed(url);
    if (!parsed.isValid() || (parsed.scheme() != QLatin1String("http") &&
                              parsed.scheme() != QLatin1String("https"))) {
        return {};
    }

    // SSRF gate (same one the link-preview fetcher uses): a script must not be
    // able to reach loopback/link-local/private hosts — that's localhost
    // services and cloud metadata endpoints. Checked up front and on every
    // redirect hop below.
    bool allowed = false;
    {
        QEventLoop gateLoop;
        maxchat::services::resolvePreviewUrlPublicAsync(
            parsed, /*allowPrivateNetwork=*/false, this, [&](bool ok) {
                allowed = ok;
                gateLoop.quit();
            });
        gateLoop.exec();
    }
    if (!allowed) {
        return {};
    }

    QNetworkAccessManager& manager = host_.scriptNetworkManager();
    QNetworkRequest request(parsed);
    request.setRawHeader("User-Agent", "MaxChat-script");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = manager.get(request);

    constexpr qint64 kMaxBodyBytes = 2 * 1024 * 1024; // scripts get text, not blobs
    connect(reply, &QNetworkReply::redirected, this, [reply](const QUrl& target) {
        maxchat::services::resolvePreviewUrlPublicAsync(
            target, /*allowPrivateNetwork=*/false, reply, [reply](bool ok) {
                if (!ok) {
                    reply->abort();
                }
            });
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [reply](qint64 received, qint64 /*total*/) {
                if (received > kMaxBodyBytes) {
                    reply->abort(); // unbounded body = memory DoS
                }
            });

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
        body = QString::fromUtf8(reply->read(kMaxBodyBytes));
    } else if (!reply->isFinished()) {
        reply->abort();
    }
    reply->deleteLater();
    return body;
}

// --- Permissions / seeding --------------------------------------------------

QString ScriptBridge::scriptsDirectory() const {
    return scriptsDir_;
}

bool ScriptBridge::isBundledScript(const QString& name) const {
    // Shipped scripts leave a .bundled/<name>.lua snapshot when seeded.
    const QString snapshot =
        QDir(scriptsDir_).filePath(QStringLiteral(".bundled/%1.lua").arg(name));
    return QFile::exists(snapshot);
}

maxchat::scripting::ScriptPermissions
ScriptBridge::buildScriptPermissionsFor(const QString& name) const {
    const QVariantMap settings = host_.settings().loadWithDefaults();
    const QVariantMap perms =
        settings.value(QStringLiteral("scriptPerms")).toMap().value(name).toMap();
    maxchat::scripting::ScriptPermissions out =
        maxchat::scripting::ScriptPermissions::fromMap(perms);
    for (const QVariant& dir : settings.value(QStringLiteral("script_dirs")).toList()) {
        const QString path = dir.toString().trimmed();
        if (!path.isEmpty()) {
            out.allowedDirs << path;
        }
    }
    return out;
}

QHash<QString, maxchat::scripting::ScriptPermissions>
ScriptBridge::buildAllScriptPermsMap() const {
    const QVariantMap settings = host_.settings().loadWithDefaults();
    const QVariantMap allPerms = settings.value(QStringLiteral("scriptPerms")).toMap();
    QStringList allowedDirs;
    for (const QVariant& dir : settings.value(QStringLiteral("script_dirs")).toList()) {
        const QString path = dir.toString().trimmed();
        if (!path.isEmpty()) {
            allowedDirs << path;
        }
    }
    // Iterate the actual script files (not just saved-perms keys) so startup
    // loading can check every available script's saved load_start flag.
    QHash<QString, maxchat::scripting::ScriptPermissions> result;
    const QFileInfoList files =
        QDir(scriptsDir_).entryInfoList({QStringLiteral("*.lua")}, QDir::Files);
    for (const QFileInfo& fi : files) {
        const QString name = fi.completeBaseName();
        maxchat::scripting::ScriptPermissions p = maxchat::scripting::ScriptPermissions::fromMap(
            allPerms.value(name).toMap());
        p.allowedDirs = allowedDirs;
        result.insert(name, p);
    }
    return result;
}

void ScriptBridge::seedBundledScripts(const QString& destDir) {
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
    // `.bundled/` keeps a snapshot of each script as it was last seeded. If the
    // deployed copy still matches its snapshot the user never edited it, so a
    // newer bundled version may replace it. Without this, shipped script fixes
    // never reach existing installs (the 6-second `!run` stall: the os.execute
    // run.lua stayed deployed long after api.launch replaced it in assets).
    const QString recordDir = QDir(destDir).filePath(QStringLiteral(".bundled"));
    QDir().mkpath(recordDir);
    // A failed read must never feed an overwrite decision: an unreadable
    // (locked) file would compare equal to another unreadable file as "" == ""
    // and a user edit could be clobbered as "unmodified".
    const auto readAll = [](const QString& path, bool* ok) -> QByteArray {
        QFile f(path);
        *ok = f.open(QIODevice::ReadOnly);
        return *ok ? f.readAll() : QByteArray();
    };
    const auto copyOver = [](const QString& from, const QString& to) -> bool {
        QFile::remove(to);
        return QFile::copy(from, to);
    };
    const QFileInfoList examples =
        QDir(src).entryInfoList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : examples) {
        const QString dest = QDir(destDir).filePath(fi.fileName());
        const QString record = QDir(recordDir).filePath(fi.fileName());
        if (!QFile::exists(dest)) {
            // The record only updates when the deployed copy actually landed,
            // otherwise the file would look user-edited forever (deployed
            // matching neither bundled nor record) and never upgrade again.
            if (copyOver(fi.absoluteFilePath(), dest)) {
                copyOver(fi.absoluteFilePath(), record);
            }
            continue;
        }
        bool bundledOk = false;
        bool deployedOk = false;
        const QByteArray bundled = readAll(fi.absoluteFilePath(), &bundledOk);
        const QByteArray deployed = readAll(dest, &deployedOk);
        if (!bundledOk || !deployedOk) {
            continue; // can't tell what's deployed — try again next startup
        }
        if (deployed == bundled) {
            if (!QFile::exists(record)) {
                copyOver(fi.absoluteFilePath(), record); // adopt pre-record installs
            }
            continue;
        }
        bool recordOk = false;
        const QByteArray recorded = readAll(record, &recordOk);
        if (recordOk && deployed == recorded) {
            // unmodified — take the upgrade (record after dest, see above)
            if (copyOver(fi.absoluteFilePath(), dest)) {
                copyOver(fi.absoluteFilePath(), record);
            }
        }
        // deployed differs from both bundled and record: user edit — never touch.
    }
}

} // namespace maxchat::ui
