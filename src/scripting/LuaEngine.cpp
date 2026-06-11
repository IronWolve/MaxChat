#include "scripting/LuaEngine.h"

#include "scripting/ScriptHost.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

#ifdef MAXCHAT_LUA

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace maxchat::scripting {

// Per-script Lua state plus the registry ref to that script's `api` table.
struct ScriptState {
    lua_State* L = nullptr;
    int apiRef = LUA_NOREF;
    QString name;
};

namespace {

LuaEngine* engineOf(lua_State* L) {
    return static_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
}

// api.echo(text)
int l_echo(lua_State* L) {
    LuaEngine* engine = engineOf(L);
    const QString text = QString::fromUtf8(luaL_checkstring(L, 1));
    if (engine != nullptr) {
        engine->hostEcho(text);
    }
    return 0;
}

// Build a fresh, sandboxed state. Only known-safe libraries are opened, and the
// base-library escape hatches (load/dofile/loadfile/…) are removed. There is no
// `io`, `os`, `package`/`require`, or `debug`, so a script cannot reach the
// filesystem, run programs, or load C modules. (Step 3 adds the controlled
// api.* file/time calls back in.)
lua_State* makeSandboxedState() {
    lua_State* L = luaL_newstate();
    if (L == nullptr) {
        return nullptr;
    }
    static const luaL_Reg kSafeLibs[] = {
        {LUA_GNAME, luaopen_base},      {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8}, {nullptr, nullptr}};
    for (const luaL_Reg* lib = kSafeLibs; lib->func != nullptr; ++lib) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    static const char* kBanned[] = {"dofile",   "loadfile",       "load",
                                    "loadstring", "collectgarbage", nullptr};
    for (int i = 0; kBanned[i] != nullptr; ++i) {
        lua_pushnil(L);
        lua_setglobal(L, kBanned[i]);
    }
    return L;
}

} // namespace

LuaEngine::LuaEngine(ScriptHost* host, QString scriptsDir, QString dataRoot, QObject* parent)
    : QObject(parent), host_(host), scriptsDir_(std::move(scriptsDir)),
      dataRoot_(std::move(dataRoot)) {}

LuaEngine::~LuaEngine() {
    const QStringList names = scripts_.keys();
    for (const QString& name : names) {
        unload(name);
    }
}

bool LuaEngine::available() {
    return true;
}

void LuaEngine::setCurrentNetwork(const QString& network) {
    currentNetwork_ = network;
}

void LuaEngine::hostEcho(const QString& text) {
    if (host_ != nullptr) {
        host_->scriptEcho(currentNetwork_, text);
    }
}

QStringList LuaEngine::loaded() const {
    QStringList names = scripts_.keys();
    names.sort();
    return names;
}

void LuaEngine::reportError(const QString& script, const QString& where, const QString& message) {
    if (host_ != nullptr) {
        host_->scriptEcho(currentNetwork_,
                          QStringLiteral("[scripts] %1.%2: %3").arg(script, where, message));
    }
}

// Installs the `api` table into L (also as the global `api`) and returns a
// registry ref to it, used as the first argument to every hook.
static int installApi(lua_State* L, LuaEngine* engine) {
    lua_newtable(L);
    const auto reg = [&](const char* name, lua_CFunction fn) {
        lua_pushlightuserdata(L, engine);
        lua_pushcclosure(L, fn, 1);
        lua_setfield(L, -2, name);
    };
    reg("echo", l_echo); // more api.* land here in step 3
    lua_pushvalue(L, -1);
    lua_setglobal(L, "api");
    return luaL_ref(L, LUA_REGISTRYINDEX); // pops the table
}

bool LuaEngine::callHook(ScriptState* state, const char* hook) {
    lua_State* L = state->L;
    lua_getglobal(L, hook);
    if (lua_isfunction(L, -1) == 0) {
        lua_pop(L, 1); // not defined — nothing to do
        return false;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, state->apiRef); // arg1 = api
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        reportError(state->name, QString::fromLatin1(hook),
                    QString::fromUtf8(lua_tostring(L, -1)));
        lua_pop(L, 1);
        return false;
    }
    const bool consumed = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return consumed;
}

bool LuaEngine::load(const QString& path) {
    const QString name = QFileInfo(path).completeBaseName();
    if (scripts_.contains(name)) {
        unload(name);
    }
    lua_State* L = makeSandboxedState();
    if (L == nullptr) {
        return false;
    }
    const int apiRef = installApi(L, this);
    if (luaL_loadfile(L, path.toUtf8().constData()) != LUA_OK) {
        reportError(name, QStringLiteral("load"), QString::fromUtf8(lua_tostring(L, -1)));
        lua_close(L);
        return false;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) { // run the chunk (top-level code)
        reportError(name, QStringLiteral("init"), QString::fromUtf8(lua_tostring(L, -1)));
        lua_close(L);
        return false;
    }
    auto* state = new ScriptState{L, apiRef, name};
    scripts_.insert(name, state);
    callHook(state, "on_load");
    return true;
}

int LuaEngine::loadAll() {
    QDir dir(scriptsDir_);
    int count = 0;
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : files) {
        if (fi.fileName().startsWith(QLatin1Char('_'))) {
            continue; // leading underscore = manual-load only
        }
        if (load(fi.absoluteFilePath())) {
            ++count;
        }
    }
    return count;
}

bool LuaEngine::unload(const QString& name) {
    auto it = scripts_.find(name);
    if (it == scripts_.end()) {
        return false;
    }
    ScriptState* state = it.value();
    callHook(state, "on_unload");
    luaL_unref(state->L, LUA_REGISTRYINDEX, state->apiRef);
    lua_close(state->L);
    delete state;
    scripts_.erase(it);
    return true;
}

bool LuaEngine::reload(const QString& name) {
    return load(QDir(scriptsDir_).filePath(name + QStringLiteral(".lua")));
}

} // namespace maxchat::scripting

#else // !MAXCHAT_LUA — inert stub so the class exists without the interpreter.

namespace maxchat::scripting {

struct ScriptState {};

LuaEngine::LuaEngine(ScriptHost* host, QString scriptsDir, QString dataRoot, QObject* parent)
    : QObject(parent), host_(host), scriptsDir_(std::move(scriptsDir)),
      dataRoot_(std::move(dataRoot)) {}

LuaEngine::~LuaEngine() = default;

bool LuaEngine::available() {
    return false;
}

bool LuaEngine::load(const QString&) {
    return false;
}

int LuaEngine::loadAll() {
    return 0;
}

bool LuaEngine::unload(const QString&) {
    return false;
}

bool LuaEngine::reload(const QString&) {
    return false;
}

QStringList LuaEngine::loaded() const {
    return {};
}

void LuaEngine::setCurrentNetwork(const QString& network) {
    currentNetwork_ = network;
}

void LuaEngine::hostEcho(const QString&) {}

bool LuaEngine::callHook(ScriptState*, const char*) {
    return false;
}

void LuaEngine::reportError(const QString&, const QString&, const QString&) {}

} // namespace maxchat::scripting

#endif // MAXCHAT_LUA
