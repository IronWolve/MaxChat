#pragma once

#include "scripting/ScriptPermissions.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QSize>
#include <QVariantList>

struct lua_State; // Lua's opaque state; full type stays in LuaEngine.cpp

namespace maxchat::scripting {

class ScriptHost;
struct ScriptState;  // defined in LuaEngine.cpp (holds the lua_State)
struct ScriptTimer;  // defined in LuaEngine.cpp (a QTimer + its Lua callback ref)

// Loads and runs user Lua scripts in a sandbox, dispatching hooks and exposing
// the `api` table. One isolated lua_State per script (unload == close state).
//
// The header is deliberately Lua-free so it can be included regardless of the
// MAXCHAT_LUA build flag; when the flag is off, the .cpp compiles to inert
// stubs and available() returns false.
class LuaEngine final : public QObject {
    Q_OBJECT

  public:
    LuaEngine(ScriptHost* host, QString scriptsDir, QString dataRoot,
              QObject* parent = nullptr);
    ~LuaEngine() override;

    // True when built with -DMAXCHAT_LUA=ON (i.e. scripting actually works).
    [[nodiscard]] static bool available();

    // Load one .lua file with explicit per-script permissions. If already loaded, reloads.
    bool load(const QString& path, const ScriptPermissions& perms = {});
    int loadAll(const QHash<QString, ScriptPermissions>& permsMap = {});
    bool unload(const QString& name);   // name = filename without .lua
    bool reload(const QString& name);   // reloads with the script's stored permissions
    [[nodiscard]] ScriptPermissions permsForScript(const QString& name) const;
    [[nodiscard]] QStringList loaded() const;

    // Call `hook` on every loaded script, passing `args` after the api table.
    // `network` scopes api.echo to that network for the duration. Returns true
    // if any script's handler returned true (used by on_command to consume).
    bool dispatch(const QString& hook, const QString& network, const QVariantList& args = {});
    bool dispatchToScript(const QString& script, const QString& hook,
                          const QString& network, const QVariantList& args = {});

    // Sandbox capabilities. Set before load()/loadAll(); changing them only
    // affects scripts (re)loaded afterwards.
    void setPermissions(const ScriptPermissions& perms);
    [[nodiscard]] const ScriptPermissions& permissions() const { return perms_; }

    // True if a script may open `path` for read/write. `L` identifies the calling
    // script so its per-script permissions are used. Used by the guarded io.open.
    [[nodiscard]] bool fileAccessAllowed(const QString& path, bool write,
                                         const QString& dataDir, lua_State* L) const;

    // Scopes api.echo (etc.) to a network while a hook runs.
    void setCurrentNetwork(const QString& network);

    // --- bridge helpers invoked by the C api closures (treat as internal) ---
    void hostEcho(const QString& text);
    void hostSay(const QString& target, const QString& text);
    void hostInsertInput(const QString& text);
    void hostNotify(const QString& title, const QString& text);
    void hostSendRaw(const QString& line);
    [[nodiscard]] bool hostMcData(const QString& target, const QString& service,
                                  const QString& verb, const QString& payload,
                                  bool notice);
    [[nodiscard]] bool hostTerminalOpen(const QString& scriptName, const QString& id,
                                        const QString& title, const QString& profile,
                                        int cols, int rows);
    void hostTerminalClose(const QString& scriptName, const QString& id);
    void hostTerminalClear(const QString& scriptName, const QString& id);
    void hostTerminalWrite(const QString& scriptName, const QString& id, const QString& text);
    [[nodiscard]] bool hostTerminalFrame(const QString& scriptName, const QString& id,
                                         const QString& ops);
    void hostTerminalStatus(const QString& scriptName, const QString& id, const QString& text);
    void hostTerminalPrompt(const QString& scriptName, const QString& id, const QString& text);
    [[nodiscard]] QSize hostTerminalSize(const QString& scriptName, const QString& id);
    void hostTerminalProfile(const QString& scriptName, const QString& id,
                             const QString& profile, int cols, int rows);
    void hostTerminalFit(const QString& scriptName, const QString& id, const QString& mode);
    [[nodiscard]] QString hostTerminalHotspot(const QString& actionId, const QString& label);
    [[nodiscard]] QString hostMe();
    [[nodiscard]] QString hostTarget();
    [[nodiscard]] QString hostNetwork();
    [[nodiscard]] QStringList hostChannels();
    [[nodiscard]] QStringList hostNicks(const QString& target);
    [[nodiscard]] QString hostHttpGet(const QString& url);
    // api.timer / api.cancel_timer (lua_State identifies the owning script).
    int createTimer(void* luaState, int intervalMs, int funcRef);
    void cancelTimer(int id);

  private:
    lua_State* createState(const QString& dataDir, const ScriptPermissions& perms);
    bool callHook(ScriptState* state, const char* hook, const QVariantList& args = {});
    void reportError(const QString& script, const QString& where, const QString& message);
    void fireTimer(int id);
    void cancelTimersFor(ScriptState* state);

    ScriptHost* host_ = nullptr;
    QString scriptsDir_;
    QString dataRoot_;
    QString currentNetwork_;
    ScriptPermissions perms_;
    QHash<QString, ScriptState*> scripts_;
    QHash<lua_State*, ScriptState*> stateByLua_;
    QHash<int, ScriptTimer*> timers_;
    int nextTimerId_ = 1;
};

} // namespace maxchat::scripting
