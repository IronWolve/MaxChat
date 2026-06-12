#include "scripting/LuaEngine.h"

#include "scripting/ScriptHost.h"

#include "irc/IrcFormat.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QProcess>
#if defined(Q_OS_WIN)
#include <windows.h>
#include <shellapi.h>
#endif
#include <QTimer>
#include <QVariant>

#include <algorithm>
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
    ScriptPermissions perms;
};

struct ScriptTimer {
    QTimer* timer = nullptr;
    ScriptState* state = nullptr; // the script that owns the callback
    int funcRef = LUA_NOREF;      // registry ref to the Lua callback
    int id = 0;
};

namespace {

// Every api.* closure carries two upvalues: (1) the LuaEngine, (2) this
// script's data-dir path (so file calls are jailed to it).
LuaEngine* engineOf(lua_State* L) {
    return static_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
}
QString dataDirOf(lua_State* L) {
    return QString::fromUtf8(lua_tostring(L, lua_upvalueindex(2)));
}

QString scriptNameOf(lua_State* L) {
    const QString name = QFileInfo(dataDirOf(L)).fileName();
    return name.isEmpty() ? QStringLiteral("script") : name;
}

// Resolve `name` to a path INSIDE dataDir, basename-only — a script can never
// escape its own folder (matches SoundPlayer/DCC path rules).
QString jailedPath(const QString& dataDir, const QString& name) {
    QString base = QFileInfo(QString(name).replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();
    base = base.trimmed();
    if (base.isEmpty()) {
        return {};
    }
    return QDir(dataDir).filePath(base);
}

// api.echo(text)
int l_echo(lua_State* L) {
    engineOf(L)->hostEcho(QString::fromUtf8(luaL_checkstring(L, 1)));
    return 0;
}

// api.say(target, text)
int l_say(lua_State* L) {
    const QString target = QString::fromUtf8(luaL_checkstring(L, 1));
    const QString text = QString::fromUtf8(luaL_checkstring(L, 2));
    engineOf(L)->hostSay(target, text);
    return 0;
}

// api.insert_input(text)
int l_insert_input(lua_State* L) {
    engineOf(L)->hostInsertInput(QString::fromUtf8(luaL_checkstring(L, 1)));
    return 0;
}

// api.notify(title[, text])
int l_notify(lua_State* L) {
    const QString title = QString::fromUtf8(luaL_checkstring(L, 1));
    const QString text = QString::fromUtf8(luaL_optstring(L, 2, ""));
    engineOf(L)->hostNotify(title, text);
    return 0;
}

// api.me() / api.target() / api.network()
int l_me(lua_State* L) {
    const QByteArray v = engineOf(L)->hostMe().toUtf8();
    lua_pushlstring(L, v.constData(), v.size());
    return 1;
}
int l_target(lua_State* L) {
    const QByteArray v = engineOf(L)->hostTarget().toUtf8();
    lua_pushlstring(L, v.constData(), v.size());
    return 1;
}
int l_network(lua_State* L) {
    const QByteArray v = engineOf(L)->hostNetwork().toUtf8();
    lua_pushlstring(L, v.constData(), v.size());
    return 1;
}

// api.timestamp([fmt]) — default "yyyy-MM-dd HH:mm:ss"; fmt is a Qt format.
int l_timestamp(lua_State* L) {
    const QString fmt = QString::fromUtf8(luaL_optstring(L, 1, ""));
    const QString out =
        QDateTime::currentDateTime().toString(fmt.isEmpty() ? QStringLiteral("yyyy-MM-dd HH:mm:ss")
                                                            : fmt);
    const QByteArray v = out.toUtf8();
    lua_pushlstring(L, v.constData(), v.size());
    return 1;
}

// api.data_dir()
int l_data_dir(lua_State* L) {
    const QString dir = dataDirOf(L);
    QDir().mkpath(dir); // ensure the directory exists before scripts write to it
    const QByteArray v = dir.toUtf8();
    lua_pushlstring(L, v.constData(), v.size());
    return 1;
}

// api.launch(cmdline) — start a detached process; does not block.
// Requires runPrograms permission. Returns true on success.
int l_launch(lua_State* L) {
    const QString cmd = QString::fromUtf8(luaL_checkstring(L, 1));
    const QStringList parts = QProcess::splitCommand(cmd);
    if (parts.isEmpty()) {
        lua_pushboolean(L, 0);
        return 1;
    }
#if defined(Q_OS_WIN)
    // ShellExecuteW is the correct Windows API for launching programs: it
    // handles UWP/Store apps, the shell PATH cache, and UAC elevation prompts.
    // QProcess::startDetached uses CreateProcess directly and can stall 5-10s
    // searching slow PATH entries or waiting on antivirus hooks.
    const QString prog = parts.at(0);
    // splitCommand stripped the user's quotes — re-quote anything with spaces
    // before re-joining, or `notepad "C:\My Files\a.txt"` arrives as two args.
    QStringList quoted;
    for (const QString& part : parts.mid(1)) {
        if (part.contains(QLatin1Char(' ')) || part.contains(QLatin1Char('\t')) ||
            part.contains(QLatin1Char('"')) || part.isEmpty()) {
            QString escaped = part;
            escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
            quoted.append(QLatin1Char('"') + escaped + QLatin1Char('"'));
        } else {
            quoted.append(part);
        }
    }
    const QString argsStr = quoted.join(QLatin1Char(' '));
    const HINSTANCE result = ShellExecuteW(
        nullptr, L"open",
        reinterpret_cast<LPCWSTR>(prog.utf16()),
        argsStr.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(argsStr.utf16()),
        nullptr, SW_SHOWNORMAL);
    const bool ok = (reinterpret_cast<quintptr>(result) > 32);
#else
    const bool ok = QProcess::startDetached(parts.at(0), parts.mid(1));
#endif
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// api.append_file(name, text) — append inside the script's data dir.
int l_append_file(lua_State* L) {
    const QString name = QString::fromUtf8(luaL_checkstring(L, 1));
    const QString text = QString::fromUtf8(luaL_checkstring(L, 2));
    const QString dataDir = dataDirOf(L);
    const QString path = jailedPath(dataDir, name);
    if (path.isEmpty()) {
        return luaL_error(L, "invalid file name");
    }
    QDir().mkpath(dataDir);
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) {
        return luaL_error(L, "cannot open file for append");
    }
    f.write(text.toUtf8());
    return 0;
}

// api.read_file(name) -> string | nil (inside the data dir only).
int l_read_file(lua_State* L) {
    const QString path = jailedPath(dataDirOf(L), QString::fromUtf8(luaL_checkstring(L, 1)));
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lua_pushnil(L);
        return 1;
    }
    const QByteArray data = f.readAll();
    lua_pushlstring(L, data.constData(), data.size());
    return 1;
}

void pushStringList(lua_State* L, const QStringList& items) {
    lua_createtable(L, static_cast<int>(items.size()), 0);
    int index = 1;
    for (const QString& item : items) {
        const QByteArray v = item.toUtf8();
        lua_pushlstring(L, v.constData(), v.size());
        lua_rawseti(L, -2, index++);
    }
}

// api.send_raw(line)
int l_send_raw(lua_State* L) {
    engineOf(L)->hostSendRaw(QString::fromUtf8(luaL_checkstring(L, 1)));
    return 0;
}

int l_mc_data(lua_State* L, const bool notice) {
    LuaEngine* engine = engineOf(L);
    const QString target = QString::fromUtf8(luaL_checkstring(L, 1));
    const QString service = QString::fromUtf8(luaL_checkstring(L, 2));
    const QString verb = QString::fromUtf8(luaL_checkstring(L, 3));
    const QString payload = QString::fromUtf8(luaL_optstring(L, 4, ""));
    lua_pushboolean(L, engine->hostMcData(target, service, verb, payload, notice) ? 1 : 0);
    return 1;
}

// api.mc_send(target, service, verb, payload)
int l_mc_send(lua_State* L) {
    return l_mc_data(L, false);
}

// api.mc_reply(target, service, verb, payload)
int l_mc_reply(lua_State* L) {
    return l_mc_data(L, true);
}

int l_terminal_open(lua_State* L) {
    QString profile = QStringLiteral("ibm-vga");
    int cols = 80;
    int rows = 25;
    if (lua_isnumber(L, 3)) {
        profile = QStringLiteral("free");
        cols = static_cast<int>(lua_tointeger(L, 3));
        rows = static_cast<int>(luaL_optinteger(L, 4, 25));
    } else if (!lua_isnoneornil(L, 3)) {
        profile = QString::fromUtf8(luaL_checkstring(L, 3));
        cols = static_cast<int>(luaL_optinteger(L, 4, 80));
        rows = static_cast<int>(luaL_optinteger(L, 5, 25));
    }
    lua_pushboolean(L,
                    engineOf(L)->hostTerminalOpen(
                        scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)),
                        QString::fromUtf8(luaL_optstring(L, 2, "")), profile, cols, rows)
                        ? 1
                        : 0);
    return 1;
}

int l_terminal_close(lua_State* L) {
    engineOf(L)->hostTerminalClose(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)));
    return 0;
}

int l_terminal_clear(lua_State* L) {
    engineOf(L)->hostTerminalClear(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)));
    return 0;
}

int l_terminal_write(lua_State* L) {
    engineOf(L)->hostTerminalWrite(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)),
                                   QString::fromUtf8(luaL_checkstring(L, 2)));
    return 0;
}

int l_terminal_status(lua_State* L) {
    engineOf(L)->hostTerminalStatus(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)),
                                    QString::fromUtf8(luaL_checkstring(L, 2)));
    return 0;
}

int l_terminal_prompt(lua_State* L) {
    engineOf(L)->hostTerminalPrompt(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)),
                                    QString::fromUtf8(luaL_checkstring(L, 2)));
    return 0;
}

int l_terminal_size(lua_State* L) {
    const QSize size =
        engineOf(L)->hostTerminalSize(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)));
    lua_pushinteger(L, size.width());
    lua_pushinteger(L, size.height());
    return 2;
}

int l_terminal_profile(lua_State* L) {
    engineOf(L)->hostTerminalProfile(
        scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)),
        QString::fromUtf8(luaL_checkstring(L, 2)),
        static_cast<int>(luaL_optinteger(L, 3, 80)),
        static_cast<int>(luaL_optinteger(L, 4, 25)));
    return 0;
}

int l_terminal_fit(lua_State* L) {
    engineOf(L)->hostTerminalFit(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)),
                                 QString::fromUtf8(luaL_checkstring(L, 2)));
    return 0;
}

int l_terminal_hotspot(lua_State* L) {
    const QString html = engineOf(L)->hostTerminalHotspot(
        QString::fromUtf8(luaL_checkstring(L, 1)), QString::fromUtf8(luaL_checkstring(L, 2)));
    const QByteArray bytes = html.toUtf8();
    lua_pushlstring(L, bytes.constData(), bytes.size());
    return 1;
}

// api.channels()
int l_channels(lua_State* L) {
    pushStringList(L, engineOf(L)->hostChannels());
    return 1;
}

// api.nicks([target])
int l_nicks(lua_State* L) {
    pushStringList(L, engineOf(L)->hostNicks(QString::fromUtf8(luaL_optstring(L, 1, ""))));
    return 1;
}

// api.strip(text) — remove IRC colour/format codes.
int l_strip(lua_State* L) {
    const QByteArray v =
        maxchat::irc::stripFormatting(QString::fromUtf8(luaL_checkstring(L, 1))).toUtf8();
    lua_pushlstring(L, v.constData(), v.size());
    return 1;
}

// Per-script persistent prefs live in <dataDir>/prefs.json.
QString prefsPath(const QString& dataDir) {
    return QDir(dataDir).filePath(QStringLiteral("prefs.json"));
}
QJsonObject loadPrefs(const QString& dataDir) {
    QFile f(prefsPath(dataDir));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(f.readAll()).object();
}

// api.get(key) -> string|number|boolean|nil
int l_get(lua_State* L) {
    const QString key = QString::fromUtf8(luaL_checkstring(L, 1));
    const QJsonValue v = loadPrefs(dataDirOf(L)).value(key);
    if (v.isString()) {
        const QByteArray s = v.toString().toUtf8();
        lua_pushlstring(L, s.constData(), s.size());
    } else if (v.isDouble()) {
        lua_pushnumber(L, v.toDouble());
    } else if (v.isBool()) {
        lua_pushboolean(L, v.toBool() ? 1 : 0);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

// api.set(key, value) — value is string, number, or boolean.
int l_set(lua_State* L) {
    const QString key = QString::fromUtf8(luaL_checkstring(L, 1));
    const QString dataDir = dataDirOf(L);
    QJsonObject obj = loadPrefs(dataDir);
    if (lua_isboolean(L, 2)) {
        obj.insert(key, lua_toboolean(L, 2) != 0);
    } else if (lua_isnumber(L, 2)) {
        obj.insert(key, lua_tonumber(L, 2));
    } else {
        obj.insert(key, QString::fromUtf8(luaL_checkstring(L, 2)));
    }
    QDir().mkpath(dataDir);
    QFile f(prefsPath(dataDir));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return luaL_error(L, "cannot write prefs");
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    return 0;
}

// api.timer(ms, fn) -> id
int l_timer(lua_State* L) {
    LuaEngine* engine = engineOf(L);
    const int ms = static_cast<int>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    const int funcRef = luaL_ref(L, LUA_REGISTRYINDEX); // pops the function copy
    lua_pushinteger(L, engine->createTimer(L, ms, funcRef));
    return 1;
}

// api.cancel_timer(id)
int l_cancel_timer(lua_State* L) {
    engineOf(L)->cancelTimer(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

// api.http_get(url) -> string|nil  (only registered when the network permission
// is granted; the actual fetch is the host's, synchronous).
int l_http_get(lua_State* L) {
    LuaEngine* engine = engineOf(L);
    const QString url = QString::fromUtf8(luaL_checkstring(L, 1));
    const QString body = engine != nullptr ? engine->hostHttpGet(url) : QString();
    if (body.isEmpty()) {
        lua_pushnil(L);
        return 1;
    }
    const QByteArray b = body.toUtf8();
    lua_pushlstring(L, b.constData(), b.size());
    return 1;
}

// Guarded io.open: enforces the read/write permission + the allowed-dir list
// before delegating to the real io.open (saved in the registry). Upvalues:
// (1) engine, (2) this script's data dir.
int l_guarded_open(lua_State* L) {
    LuaEngine* engine = engineOf(L);
    const QString dataDir = dataDirOf(L);
    const QString path = QString::fromUtf8(luaL_checkstring(L, 1));
    const QString mode = QString::fromUtf8(luaL_optstring(L, 2, "r"));
    const bool write =
        mode.contains(QLatin1Char('w')) || mode.contains(QLatin1Char('a')) ||
        mode.contains(QLatin1Char('+'));
    if (engine == nullptr || !engine->fileAccessAllowed(path, write, dataDir, L)) {
        lua_pushnil(L);
        lua_pushstring(L, "permission denied (enable file access in Preferences > Scripts)");
        return 2;
    }
    const int base = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "maxchat_io_open"); // the real io.open
    lua_pushvalue(L, 1);                                   // path
    const QByteArray m = mode.toUtf8();
    lua_pushlstring(L, m.constData(), m.size()); // mode
    lua_call(L, 2, LUA_MULTRET);
    return lua_gettop(L) - base;
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

void LuaEngine::setPermissions(const ScriptPermissions& perms) {
    perms_ = perms;
}

bool LuaEngine::fileAccessAllowed(const QString& path, bool write, const QString& dataDir,
                                   lua_State* L) const {
    const ScriptState* state = stateByLua_.value(L, nullptr);
    const ScriptPermissions& perms = state ? state->perms : ScriptPermissions{};
    if (write ? !perms.writeFiles : !perms.readFiles) {
        return false;
    }
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    const QString abs = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const auto within = [&](const QString& root) {
        if (root.trimmed().isEmpty()) {
            return false;
        }
        const QString r = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
        if (abs.compare(r, cs) == 0) {
            return true;
        }
        return abs.startsWith(r.endsWith(QLatin1Char('/')) ? r : r + QLatin1Char('/'), cs);
    };
    if (within(dataDir)) {
        return true; // a script's own data dir is always in-bounds
    }
    for (const QString& dir : perms.allowedDirs) {
        if (within(dir)) {
            return true;
        }
    }
    return false;
}

// Open a fresh state with exactly the libraries the given permissions allow.
// The safe core (base/table/string/math/utf8 + a read-only os subset) is always
// present; io/package/os-danger/load are gated by per-script permissions.
lua_State* LuaEngine::createState(const QString& dataDir, const ScriptPermissions& perms) {
    lua_State* L = luaL_newstate();
    if (L == nullptr) {
        return nullptr;
    }
    static const luaL_Reg kSafeLibs[] = {
        {LUA_GNAME, luaopen_base},        {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},  {nullptr, nullptr}};
    for (const luaL_Reg* lib = kSafeLibs; lib->func != nullptr; ++lib) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }

    // os: open it, then always strip os.exit (a script must never kill the
    // client) and — unless "run programs" is granted — the process/file verbs.
    luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 1);
    lua_pushnil(L);
    lua_setfield(L, -2, "exit");
    if (!perms.runPrograms) {
        for (const char* fn : {"execute", "getenv", "remove", "rename", "tmpname", "setlocale"}) {
            lua_pushnil(L);
            lua_setfield(L, -2, fn);
        }
    }
    lua_pop(L, 1); // os table

    // load/dofile/loadfile + package/require: only with "load modules".
    if (perms.loadModules) {
        luaL_requiref(L, LUA_LOADLIBNAME, luaopen_package, 1);
        // "Load modules" means LUA modules, never native code. Out of the box,
        // package.loadlib and require's C-library searchers (slots 3 and 4)
        // would dlopen/LoadLibrary any .so/.dll on disk — arbitrary native
        // code execution that bypasses the separate "run programs" permission.
        lua_pushnil(L);
        lua_setfield(L, -2, "loadlib");
        lua_getfield(L, -1, "searchers");
        if (lua_istable(L, -1) != 0) {
            lua_pushnil(L);
            lua_rawseti(L, -2, 4); // all-in-one C loader
            lua_pushnil(L);
            lua_rawseti(L, -2, 3); // C library loader
        }
        lua_pop(L, 2); // searchers + package
        // Precompiled Lua bytecode can corrupt the interpreter (it is not
        // verified) — force text-only chunks for load/loadfile/dofile.
        luaL_dostring(L,
                      "local rawload, rawloadfile = load, loadfile\n"
                      "load = function(c, n, _, e) return rawload(c, n, 't', e) end\n"
                      "loadfile = function(f, _, e) return rawloadfile(f, 't', e) end\n"
                      "dofile = function(f)\n"
                      "  local fn, err = loadfile(f)\n"
                      "  if not fn then error(err, 2) end\n"
                      "  return fn()\n"
                      "end\n");
    } else {
        for (const char* fn : {"dofile", "loadfile", "load", "loadstring"}) {
            lua_pushnil(L);
            lua_setglobal(L, fn);
        }
    }

    // io: only when read or write is granted. io.open is replaced with a guarded
    // version; the unguarded openers are removed, and io.popen needs exec.
    if (perms.anyFileAccess()) {
        luaL_requiref(L, LUA_IOLIBNAME, luaopen_io, 1);
        lua_getfield(L, -1, "open"); // save the real io.open in the registry
        lua_setfield(L, LUA_REGISTRYINDEX, "maxchat_io_open");
        lua_pushlightuserdata(L, this);
        const QByteArray dd = dataDir.toUtf8();
        lua_pushlstring(L, dd.constData(), dd.size());
        lua_pushcclosure(L, l_guarded_open, 2);
        lua_setfield(L, -2, "open");
        for (const char* fn : {"lines", "input", "output", "tmpfile"}) {
            lua_pushnil(L);
            lua_setfield(L, -2, fn);
        }
        if (!perms.runPrograms) {
            lua_pushnil(L);
            lua_setfield(L, -2, "popen");
        }
        lua_pop(L, 1); // io table
    }
    return L;
}

void LuaEngine::setCurrentNetwork(const QString& network) {
    currentNetwork_ = network;
}

void LuaEngine::hostEcho(const QString& text) {
    if (host_ != nullptr) {
        host_->scriptEcho(currentNetwork_, text);
    }
}

void LuaEngine::hostSay(const QString& target, const QString& text) {
    if (host_ != nullptr) {
        host_->scriptSay(currentNetwork_, target, text);
    }
}

void LuaEngine::hostInsertInput(const QString& text) {
    if (host_ != nullptr) {
        host_->scriptInsertInput(text);
    }
}

void LuaEngine::hostNotify(const QString& title, const QString& text) {
    if (host_ != nullptr) {
        host_->scriptNotify(title, text);
    }
}

void LuaEngine::hostSendRaw(const QString& line) {
    if (host_ != nullptr) {
        host_->scriptSendRaw(currentNetwork_, line);
    }
}

bool LuaEngine::hostMcData(const QString& target, const QString& service,
                           const QString& verb, const QString& payload,
                           const bool notice) {
    return host_ != nullptr &&
           host_->scriptMcData(currentNetwork_, target, service, verb, payload, notice);
}

bool LuaEngine::hostTerminalOpen(const QString& scriptName, const QString& id,
                                 const QString& title, const QString& profile,
                                 const int cols, const int rows) {
    return host_ != nullptr &&
           host_->scriptTerminalOpen(scriptName, id, title, profile, cols, rows);
}

void LuaEngine::hostTerminalClose(const QString& scriptName, const QString& id) {
    if (host_ != nullptr) {
        host_->scriptTerminalClose(scriptName, id);
    }
}

void LuaEngine::hostTerminalClear(const QString& scriptName, const QString& id) {
    if (host_ != nullptr) {
        host_->scriptTerminalClear(scriptName, id);
    }
}

void LuaEngine::hostTerminalWrite(const QString& scriptName, const QString& id,
                                  const QString& text) {
    if (host_ != nullptr) {
        host_->scriptTerminalWrite(scriptName, id, text);
    }
}

void LuaEngine::hostTerminalStatus(const QString& scriptName, const QString& id,
                                   const QString& text) {
    if (host_ != nullptr) {
        host_->scriptTerminalStatus(scriptName, id, text);
    }
}

void LuaEngine::hostTerminalPrompt(const QString& scriptName, const QString& id,
                                   const QString& text) {
    if (host_ != nullptr) {
        host_->scriptTerminalPrompt(scriptName, id, text);
    }
}

QSize LuaEngine::hostTerminalSize(const QString& scriptName, const QString& id) {
    return host_ != nullptr ? host_->scriptTerminalSize(scriptName, id) : QSize();
}

void LuaEngine::hostTerminalProfile(const QString& scriptName, const QString& id,
                                    const QString& profile, const int cols, const int rows) {
    if (host_ != nullptr) {
        host_->scriptTerminalProfile(scriptName, id, profile, cols, rows);
    }
}

void LuaEngine::hostTerminalFit(const QString& scriptName, const QString& id,
                                const QString& mode) {
    if (host_ != nullptr) {
        host_->scriptTerminalFit(scriptName, id, mode);
    }
}

QString LuaEngine::hostTerminalHotspot(const QString& actionId, const QString& label) {
    return host_ != nullptr ? host_->scriptTerminalHotspot(actionId, label) : label;
}

QString LuaEngine::hostMe() {
    return host_ != nullptr ? host_->scriptMe(currentNetwork_) : QString();
}

QString LuaEngine::hostTarget() {
    return host_ != nullptr ? host_->scriptTarget() : QString();
}

QString LuaEngine::hostNetwork() {
    return host_ != nullptr ? host_->scriptNetwork() : QString();
}

QStringList LuaEngine::hostChannels() {
    return host_ != nullptr ? host_->scriptChannels(currentNetwork_) : QStringList();
}

QStringList LuaEngine::hostNicks(const QString& target) {
    return host_ != nullptr ? host_->scriptNicks(currentNetwork_, target) : QStringList();
}

QString LuaEngine::hostHttpGet(const QString& url) {
    return host_ != nullptr ? host_->scriptHttpGet(url) : QString();
}

int LuaEngine::createTimer(void* luaState, int intervalMs, int funcRef) {
    auto* L = static_cast<lua_State*>(luaState);
    ScriptState* owner = nullptr;
    for (ScriptState* state : scripts_) {
        if (state->L == L) {
            owner = state;
            break;
        }
    }
    if (owner == nullptr) {
        luaL_unref(L, LUA_REGISTRYINDEX, funcRef);
        return 0;
    }
    auto* entry = new ScriptTimer{};
    entry->timer = new QTimer(this);
    entry->state = owner;
    entry->funcRef = funcRef;
    entry->id = nextTimerId_++;
    entry->timer->setInterval(std::max(50, intervalMs)); // floor runaway 0ms timers
    const int id = entry->id;
    connect(entry->timer, &QTimer::timeout, this, [this, id]() { fireTimer(id); });
    timers_.insert(id, entry);
    entry->timer->start();
    return id;
}

void LuaEngine::fireTimer(int id) {
    ScriptTimer* entry = timers_.value(id, nullptr);
    if (entry == nullptr || entry->state == nullptr) {
        return;
    }
    lua_State* L = entry->state->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, entry->funcRef);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        reportError(entry->state->name, QStringLiteral("timer"),
                    QString::fromUtf8(lua_tostring(L, -1)));
        lua_pop(L, 1);
    }
}

void LuaEngine::cancelTimer(int id) {
    ScriptTimer* entry = timers_.take(id);
    if (entry == nullptr) {
        return;
    }
    entry->timer->stop();
    entry->timer->deleteLater();
    if (entry->state != nullptr) {
        luaL_unref(entry->state->L, LUA_REGISTRYINDEX, entry->funcRef);
    }
    delete entry;
}

void LuaEngine::cancelTimersFor(ScriptState* state) {
    const QList<int> ids = timers_.keys();
    for (int id : ids) {
        ScriptTimer* entry = timers_.value(id, nullptr);
        if (entry != nullptr && entry->state == state) {
            entry->timer->stop();
            entry->timer->deleteLater();
            luaL_unref(state->L, LUA_REGISTRYINDEX, entry->funcRef); // before lua_close
            timers_.remove(id);
            delete entry;
        }
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
// registry ref to it, used as the first argument to every hook. Each closure
// gets two upvalues: the engine and this script's data-dir path.
static int installApi(lua_State* L, LuaEngine* engine, const QString& dataDir,
                      const ScriptPermissions& perms) {
    lua_newtable(L);
    const QByteArray dd = dataDir.toUtf8();
    const auto reg = [&](const char* name, lua_CFunction fn) {
        lua_pushlightuserdata(L, engine);
        lua_pushlstring(L, dd.constData(), dd.size());
        lua_pushcclosure(L, fn, 2);
        lua_setfield(L, -2, name);
    };
    reg("echo", l_echo);
    reg("say", l_say);
    reg("insert_input", l_insert_input);
    reg("notify", l_notify);
    reg("me", l_me);
    reg("target", l_target);
    reg("network", l_network);
    reg("timestamp", l_timestamp);
    reg("data_dir", l_data_dir);
    reg("append_file", l_append_file);
    reg("read_file", l_read_file);
    reg("send_raw", l_send_raw);
    reg("mc_send", l_mc_send);
    reg("mc_reply", l_mc_reply);
    reg("terminal_open", l_terminal_open);
    reg("terminal_close", l_terminal_close);
    reg("terminal_clear", l_terminal_clear);
    reg("terminal_write", l_terminal_write);
    reg("terminal_status", l_terminal_status);
    reg("terminal_prompt", l_terminal_prompt);
    reg("terminal_size", l_terminal_size);
    reg("terminal_profile", l_terminal_profile);
    reg("terminal_fit", l_terminal_fit);
    reg("terminal_hotspot", l_terminal_hotspot);
    reg("channels", l_channels);
    reg("nicks", l_nicks);
    reg("strip", l_strip);
    reg("get", l_get);
    reg("set", l_set);
    reg("timer", l_timer);
    reg("cancel_timer", l_cancel_timer);
    if (perms.runPrograms) {
        reg("launch", l_launch);
    }
    if (perms.network) {
        reg("http_get", l_http_get);
    }
    lua_pushvalue(L, -1);
    lua_setglobal(L, "api");
    return luaL_ref(L, LUA_REGISTRYINDEX); // pops the table
}

namespace {
// Push a QVariant onto the Lua stack as the matching primitive (strings, numbers
// and bools — the only types hooks pass). Anything else becomes nil.
void pushVariant(lua_State* L, const QVariant& value) {
    switch (value.typeId()) {
    case QMetaType::Bool:
        lua_pushboolean(L, value.toBool() ? 1 : 0);
        break;
    case QMetaType::Int:
    case QMetaType::LongLong:
        lua_pushinteger(L, static_cast<lua_Integer>(value.toLongLong()));
        break;
    case QMetaType::Double:
    case QMetaType::Float:
        lua_pushnumber(L, value.toDouble());
        break;
    default: {
        if (value.isNull() || !value.isValid()) {
            lua_pushnil(L);
        } else {
            const QByteArray s = value.toString().toUtf8();
            lua_pushlstring(L, s.constData(), s.size());
        }
    }
    }
}
} // namespace

bool LuaEngine::callHook(ScriptState* state, const char* hook, const QVariantList& args) {
    lua_State* L = state->L;
    lua_getglobal(L, hook);
    if (lua_isfunction(L, -1) == 0) {
        lua_pop(L, 1); // not defined — nothing to do
        return false;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, state->apiRef); // arg1 = api
    for (const QVariant& arg : args) {
        pushVariant(L, arg);
    }
    if (lua_pcall(L, 1 + static_cast<int>(args.size()), 1, 0) != LUA_OK) {
        reportError(state->name, QString::fromLatin1(hook),
                    QString::fromUtf8(lua_tostring(L, -1)));
        lua_pop(L, 1);
        return false;
    }
    const bool consumed = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return consumed;
}

bool LuaEngine::dispatch(const QString& hook, const QString& network, const QVariantList& args) {
    setCurrentNetwork(network);
    bool consumed = false;
    const QByteArray hookName = hook.toUtf8();
    // Copy the values: a hook could load/unload scripts and mutate the map.
    const QList<ScriptState*> states = scripts_.values();
    for (ScriptState* state : states) {
        if (callHook(state, hookName.constData(), args)) {
            consumed = true;
        }
    }
    return consumed;
}

bool LuaEngine::dispatchToScript(const QString& script, const QString& hook,
                                 const QString& network, const QVariantList& args) {
    setCurrentNetwork(network);
    ScriptState* state = scripts_.value(script);
    if (state == nullptr) {
        return false;
    }
    const QByteArray hookName = hook.toUtf8();
    return callHook(state, hookName.constData(), args);
}

bool LuaEngine::load(const QString& path, const ScriptPermissions& perms) {
    const QString name = QFileInfo(path).completeBaseName();
    if (scripts_.contains(name)) {
        unload(name);
    }
    const QString dataDir = QDir(dataRoot_).filePath(name);
    lua_State* L = createState(dataDir, perms);
    if (L == nullptr) {
        return false;
    }
    const int apiRef = installApi(L, this, dataDir, perms);
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
    auto* state = new ScriptState{L, apiRef, name, perms};
    scripts_.insert(name, state);
    stateByLua_.insert(L, state);
    callHook(state, "on_load");
    return true;
}

int LuaEngine::loadAll(const QHash<QString, ScriptPermissions>& permsMap) {
    QDir dir(scriptsDir_);
    int count = 0;
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : files) {
        if (fi.fileName().startsWith(QLatin1Char('_'))) {
            continue; // leading underscore = manual-load only
        }
        const QString name = fi.completeBaseName();
        if (load(fi.absoluteFilePath(), permsMap.value(name))) {
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
    cancelTimersFor(state); // stop + unref this script's timers before its state dies
    stateByLua_.remove(state->L);
    luaL_unref(state->L, LUA_REGISTRYINDEX, state->apiRef);
    lua_close(state->L);
    delete state;
    scripts_.erase(it);
    return true;
}

bool LuaEngine::reload(const QString& name) {
    // Save perms before unload() destroys the state.
    ScriptPermissions savedPerms;
    const auto it = scripts_.constFind(name);
    if (it != scripts_.constEnd()) {
        savedPerms = it.value()->perms;
    }
    return load(QDir(scriptsDir_).filePath(name + QStringLiteral(".lua")), savedPerms);
}

ScriptPermissions LuaEngine::permsForScript(const QString& name) const {
    const ScriptState* state = scripts_.value(name, nullptr);
    return state ? state->perms : ScriptPermissions{};
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

bool LuaEngine::load(const QString&, const ScriptPermissions&) {
    return false;
}

int LuaEngine::loadAll(const QHash<QString, ScriptPermissions>&) {
    return 0;
}

ScriptPermissions LuaEngine::permsForScript(const QString&) const {
    return {};
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
void LuaEngine::hostSay(const QString&, const QString&) {}
void LuaEngine::hostInsertInput(const QString&) {}
void LuaEngine::hostNotify(const QString&, const QString&) {}
void LuaEngine::hostSendRaw(const QString&) {}
bool LuaEngine::hostMcData(const QString&, const QString&, const QString&, const QString&, bool) {
    return false;
}
bool LuaEngine::hostTerminalOpen(const QString&, const QString&, const QString&, const QString&,
                                 int, int) {
    return false;
}
void LuaEngine::hostTerminalClose(const QString&, const QString&) {}
void LuaEngine::hostTerminalClear(const QString&, const QString&) {}
void LuaEngine::hostTerminalWrite(const QString&, const QString&, const QString&) {}
void LuaEngine::hostTerminalStatus(const QString&, const QString&, const QString&) {}
void LuaEngine::hostTerminalPrompt(const QString&, const QString&, const QString&) {}
QSize LuaEngine::hostTerminalSize(const QString&, const QString&) {
    return {};
}
void LuaEngine::hostTerminalProfile(const QString&, const QString&, const QString&, int, int) {}
void LuaEngine::hostTerminalFit(const QString&, const QString&, const QString&) {}
QString LuaEngine::hostTerminalHotspot(const QString&, const QString& label) {
    return label;
}
QString LuaEngine::hostMe() {
    return {};
}
QString LuaEngine::hostTarget() {
    return {};
}
QString LuaEngine::hostNetwork() {
    return {};
}
QStringList LuaEngine::hostChannels() {
    return {};
}
QStringList LuaEngine::hostNicks(const QString&) {
    return {};
}
QString LuaEngine::hostHttpGet(const QString&) {
    return {};
}
void LuaEngine::setPermissions(const ScriptPermissions& perms) {
    perms_ = perms;
}
bool LuaEngine::fileAccessAllowed(const QString&, bool, const QString&, lua_State*) const {
    return false;
}
int LuaEngine::createTimer(void*, int, int) {
    return 0;
}
void LuaEngine::cancelTimer(int) {}
void LuaEngine::fireTimer(int) {}
void LuaEngine::cancelTimersFor(ScriptState*) {}

bool LuaEngine::callHook(ScriptState*, const char*, const QVariantList&) {
    return false;
}

bool LuaEngine::dispatch(const QString&, const QString&, const QVariantList&) {
    return false;
}

bool LuaEngine::dispatchToScript(const QString&, const QString&, const QString&,
                                 const QVariantList&) {
    return false;
}

void LuaEngine::reportError(const QString&, const QString&, const QString&) {}

} // namespace maxchat::scripting

#endif // MAXCHAT_LUA
