#pragma once

#include "scripting/ScriptHost.h"
#include "scripting/ScriptPermissions.h"
#include "ui/ScriptTerminalManager.h" // TerminalInfo

#include <QHash>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace maxchat::scripting {
class LuaEngine;
}

namespace maxchat::ui {

class MainWindowHost;

// Owns the Lua scripting subsystem and the script-owned terminal manager, and
// implements ScriptHost (the callback surface the Lua `api` table reaches). It
// was lifted wholesale out of MainWindow (decomp phase 1): MainWindow now holds
// a ScriptBridge and forwards script commands to it, instead of being the
// ScriptHost itself. The bridge calls back into the window through a narrow
// MainWindowHost& so its routing/permission logic is unit-testable against a
// fake host.
class ScriptBridge final : public QObject, public maxchat::scripting::ScriptHost {
    Q_OBJECT

  public:
    // `host` must outlive the bridge. `scriptsDir` is the user scripts folder.
    ScriptBridge(MainWindowHost& host, QString scriptsDir, QObject* parent = nullptr);
    ~ScriptBridge() override;

    // --- Lifecycle / commands (forwarded by MainWindow) ---------------------
    [[nodiscard]] static bool scriptingAvailable();
    // Deferred first-run seeding of bundled scripts + load-all (call once the
    // window has painted).
    void seedAndLoadAll();
    [[nodiscard]] QStringList loadedScripts() const;
    bool loadByName(const QString& name);
    bool unloadByName(const QString& name);
    bool reloadByName(const QString& name);
    // Re-apply a script's permissions (after the user edits them in prefs); a
    // no-op unless the script is currently loaded.
    void reapplyPermissions(const QString& name);
    // Fire a global hook (on_join, on_notice, ...) across all loaded scripts.
    bool dispatch(const QString& hook, const QString& network, const QVariantList& args = {});

    // --- Terminal forwarders (manager is owned here now) --------------------
    void showTerminal(const QString& id);
    void killTerminal(const QString& id);
    [[nodiscard]] QList<TerminalInfo> terminals() const;
    void setTerminalFont(const QString& family, int pointSize, bool bold);

    [[nodiscard]] QString scriptsDirectory() const;
    [[nodiscard]] bool isBundledScript(const QString& name) const;
    [[nodiscard]] maxchat::scripting::ScriptPermissions buildScriptPermissionsFor(
        const QString& name) const;

    // --- ScriptHost ---------------------------------------------------------
    void scriptEcho(const QString& network, const QString& text) override;
    void scriptSay(const QString& network, const QString& target, const QString& text) override;
    void scriptSendRaw(const QString& network, const QString& line) override;
    void scriptInsertInput(const QString& text) override;
    void scriptNotify(const QString& title, const QString& text) override;
    bool scriptMcData(const QString& network, const QString& target, const QString& service,
                      const QString& verb, const QString& payload, bool notice) override;
    bool scriptTerminalOpen(const QString& scriptName, const QString& id, const QString& title,
                            const QString& profile, int cols, int rows) override;
    void scriptTerminalClose(const QString& scriptName, const QString& id) override;
    void scriptTerminalClear(const QString& scriptName, const QString& id) override;
    void scriptTerminalWrite(const QString& scriptName, const QString& id,
                             const QString& text) override;
    bool scriptTerminalFrame(const QString& scriptName, const QString& id,
                             const QString& ops) override;
    void scriptTerminalStatus(const QString& scriptName, const QString& id,
                              const QString& text) override;
    void scriptTerminalPrompt(const QString& scriptName, const QString& id,
                              const QString& text) override;
    QSize scriptTerminalSize(const QString& scriptName, const QString& id) override;
    void scriptTerminalProfile(const QString& scriptName, const QString& id,
                               const QString& profile, int cols, int rows) override;
    void scriptTerminalFit(const QString& scriptName, const QString& id,
                           const QString& mode) override;
    QString scriptTerminalHotspot(const QString& actionId, const QString& label) override;
    QString scriptMe(const QString& network) override;
    QString scriptTarget() override;
    QString scriptNetwork() override;
    QStringList scriptChannels(const QString& network) override;
    QStringList scriptNicks(const QString& network, const QString& target) override;
    QString scriptHttpGet(const QString& url) override;

  private:
    [[nodiscard]] QHash<QString, maxchat::scripting::ScriptPermissions> buildAllScriptPermsMap()
        const;
    void seedBundledScripts(const QString& destDir);

    MainWindowHost& host_;
    QString scriptsDir_;
    maxchat::scripting::LuaEngine* lua_ = nullptr;
    ScriptTerminalManager* terminals_ = nullptr;
};

} // namespace maxchat::ui
