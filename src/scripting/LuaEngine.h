#pragma once

#include "scripting/ScriptPermissions.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
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

    bool load(const QString& path);     // load one .lua file (reloads if already loaded)
    int loadAll();                      // auto-load every non-"_" script; returns count
    bool unload(const QString& name);   // name = filename without .lua
    bool reload(const QString& name);
    [[nodiscard]] QStringList loaded() const;

    // Call `hook` on every loaded script, passing `args` after the api table.
    // `network` scopes api.echo to that network for the duration. Returns true
    // if any script's handler returned true (used by on_command to consume).
    bool dispatch(const QString& hook, const QString& network, const QVariantList& args = {});

    // Sandbox capabilities. Set before load()/loadAll(); changing them only
    // affects scripts (re)loaded afterwards.
    void setPermissions(const ScriptPermissions& perms);
    [[nodiscard]] const ScriptPermissions& permissions() const { return perms_; }

    // True if a script may open `path` for read/write given the permissions and
    // allowed dirs (the script's own data dir is always allowed). Used by the
    // guarded io.open.
    [[nodiscard]] bool fileAccessAllowed(const QString& path, bool write,
                                         const QString& dataDir) const;

    // Scopes api.echo (etc.) to a network while a hook runs.
    void setCurrentNetwork(const QString& network);

    // --- bridge helpers invoked by the C api closures (treat as internal) ---
    void hostEcho(const QString& text);
    void hostSay(const QString& target, const QString& text);
    void hostInsertInput(const QString& text);
    void hostNotify(const QString& title, const QString& text);
    void hostSendRaw(const QString& line);
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
    lua_State* createState(const QString& dataDir);
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
    QHash<int, ScriptTimer*> timers_;
    int nextTimerId_ = 1;
};

} // namespace maxchat::scripting
