#include "scripting/LuaRuntime.h"

#include "scripting/ScriptHost.h"
#include "scripting/ScriptProtocol.h"

#include "irc/IrcFormat.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <limits>
#include <QFile>
#include <QSaveFile>
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
#include <cstdlib>
#include <cstdio>
#include <utility>

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
struct ExecutionScope {
    LuaRuntime* runtime;
    explicit ExecutionScope(LuaRuntime* value) : runtime(value) { runtime->enterExecution(); }
    ~ExecutionScope() { runtime->leaveExecution(); }
};

// Every api.* closure carries two upvalues: (1) the LuaRuntime, (2) this
// script's data-dir path (so file calls are jailed to it).
LuaRuntime* engineOf(lua_State* L) {
    return static_cast<LuaRuntime*>(lua_touserdata(L, lua_upvalueindex(1)));
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
    if (!protocol::validScriptName(base)) {
        return {};
    }
    const QString path = QDir(dataDir).filePath(base);
    // A pre-existing symlink must not turn the data-dir API into arbitrary I/O.
    if (QFileInfo(path).isSymbolicLink()) return {};
    return path;
}

// api.echo(text)
int l_echo(lua_State* L) {
    engineOf(L)->hostEcho(QString::fromUtf8(luaL_checkstring(L, 1)));
    return 0;
}

// Validate arguments before allocating C++ objects: Lua argument errors longjmp.
// api.say(target, text)
int l_say(lua_State* L) {
    luaL_checkstring(L, 1); luaL_checkstring(L, 2);
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
    luaL_checkstring(L, 1); luaL_optstring(L, 2, "");
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
    lua_pushboolean(L, engineOf(L)->hostLaunch(QString::fromUtf8(luaL_checkstring(L, 1))));
    return 1;
}

// Return errors to the Lua callback only after all C++ destructors have run.
const char* appendFile(lua_State* L, const char* name, const char* text) {
    const QString dataDir = dataDirOf(L);
    const QString path = jailedPath(dataDir, QString::fromUtf8(name));
    if (path.isEmpty()) return "invalid file name";
    const QByteArray bytes(text);
    constexpr qint64 fileLimit = 8 * 1024 * 1024;
    if (bytes.size() > fileLimit || QFileInfo(path).size() > fileLimit - bytes.size()) return "script data file size limit exceeded";
    qint64 remaining = 32 * 1024 * 1024;
    int count = 0;
    QDirIterator entries(dataDir,QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    while (entries.hasNext()) {
        entries.next();
        if (++count > 128) return "script data file count limit exceeded";
        const auto entry = entries.fileInfo();
        if (entry.isFile()) {
            if (entry.size() > remaining) return "script data directory size limit exceeded";
            remaining -= entry.size();
        }
    }
    if ((!QFileInfo::exists(path) && count >= 128) || bytes.size() > remaining) return "script data directory limit exceeded";
    QDir().mkpath(dataDir);
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return "cannot open file for append";
    if (f.write(bytes) != bytes.size()) return "cannot append file";
    return nullptr;
}

// api.append_file(name, text) — append inside the script's data dir.
int l_append_file(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const char* text = luaL_checkstring(L, 2);
    const char* error = appendFile(L, name, text);
    return error ? luaL_error(L, "%s", error) : 0;
}

// api.read_file(name) -> string | nil (inside the data dir only).
int l_read_file(lua_State* L) {
    luaL_checkstring(L, 1);
    const QString path = jailedPath(dataDirOf(L), QString::fromUtf8(luaL_checkstring(L, 1)));
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lua_pushnil(L);
        return 1;
    }
    if (f.size() > 4 * 1024 * 1024) { lua_pushnil(L); return 1; }
    const QByteArray data = f.read(4 * 1024 * 1024 + 1);
    if (data.size() > 4 * 1024 * 1024) { lua_pushnil(L); return 1; }
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
    luaL_checkstring(L, 1); luaL_checkstring(L, 2); luaL_checkstring(L, 3);
    luaL_optstring(L, 4, ""); luaL_optstring(L, 5, "");
    LuaRuntime* engine = engineOf(L);
    const QString target = QString::fromUtf8(luaL_checkstring(L, 1));
    const QString service = QString::fromUtf8(luaL_checkstring(L, 2));
    const QString verb = QString::fromUtf8(luaL_checkstring(L, 3));
    const QString payload = QString::fromUtf8(luaL_optstring(L, 4, ""));
    const QString network = QString::fromUtf8(luaL_optstring(L, 5, ""));
    lua_pushboolean(L,
                    engine->hostMcData(target, service, verb, payload, notice, network) ? 1 : 0);
    return 1;
}

// api.mc_send(target, service, verb, payload [, network])
// network defaults to the dispatch-context network (the one the triggering
// event arrived on); pass it explicitly when replying outside that context,
// e.g. sysop console actions aimed at a session on another network.
int l_mc_send(lua_State* L) {
    return l_mc_data(L, false);
}

// api.mc_reply(target, service, verb, payload [, network])
int l_mc_reply(lua_State* L) {
    return l_mc_data(L, true);
}

int l_terminal_open(lua_State* L) {
    luaL_checkstring(L, 1); luaL_optstring(L, 2, "");
    if (lua_isnumber(L, 3)) { luaL_optinteger(L, 4, 25); }
    else if (!lua_isnoneornil(L, 3)) {
        luaL_checkstring(L, 3); luaL_optinteger(L, 4, 80); luaL_optinteger(L, 5, 25);
    }
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
    luaL_checkstring(L, 1);
    engineOf(L)->hostTerminalClose(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)));
    return 0;
}

int l_terminal_clear(lua_State* L) {
    luaL_checkstring(L, 1);
    engineOf(L)->hostTerminalClear(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)));
    return 0;
}

int l_terminal_write(lua_State* L) {
    luaL_checkstring(L, 1); luaL_checkstring(L, 2);
    engineOf(L)->hostTerminalWrite(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)),
                                   QString::fromUtf8(luaL_checkstring(L, 2)));
    return 0;
}

int l_terminal_frame(lua_State* L) {
    luaL_checkstring(L, 1); luaL_checkstring(L, 2);
    lua_pushboolean(L,
                    engineOf(L)->hostTerminalFrame(scriptNameOf(L),
                                                   QString::fromUtf8(luaL_checkstring(L, 1)),
                                                   QString::fromUtf8(luaL_checkstring(L, 2)))
                        ? 1
                        : 0);
    return 1;
}

int l_terminal_status(lua_State* L) {
    luaL_checkstring(L, 1); luaL_checkstring(L, 2);
    engineOf(L)->hostTerminalStatus(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)),
                                    QString::fromUtf8(luaL_checkstring(L, 2)));
    return 0;
}

int l_terminal_prompt(lua_State* L) {
    luaL_checkstring(L, 1); luaL_checkstring(L, 2);
    engineOf(L)->hostTerminalPrompt(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)),
                                    QString::fromUtf8(luaL_checkstring(L, 2)));
    return 0;
}

int l_terminal_size(lua_State* L) {
    luaL_checkstring(L, 1);
    const QSize size =
        engineOf(L)->hostTerminalSize(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)));
    lua_pushinteger(L, size.width());
    lua_pushinteger(L, size.height());
    return 2;
}

int l_terminal_profile(lua_State* L) {
    luaL_checkstring(L, 1); luaL_checkstring(L, 2);
    luaL_optinteger(L, 3, 80); luaL_optinteger(L, 4, 25);
    engineOf(L)->hostTerminalProfile(
        scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)),
        QString::fromUtf8(luaL_checkstring(L, 2)),
        static_cast<int>(luaL_optinteger(L, 3, 80)),
        static_cast<int>(luaL_optinteger(L, 4, 25)));
    return 0;
}

int l_terminal_fit(lua_State* L) {
    luaL_checkstring(L, 1); luaL_checkstring(L, 2);
    engineOf(L)->hostTerminalFit(scriptNameOf(L), QString::fromUtf8(luaL_checkstring(L, 1)),
                                 QString::fromUtf8(luaL_checkstring(L, 2)));
    return 0;
}

int l_terminal_hotspot(lua_State* L) {
    luaL_checkstring(L, 1); luaL_checkstring(L, 2);
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
    if (f.size() > 1024 * 1024) return {};
    const QByteArray data = f.read(1024 * 1024 + 1);
    return data.size() <= 1024 * 1024 ? QJsonDocument::fromJson(data).object() : QJsonObject();
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
const char* savePrefs(lua_State* L) {
    const QString key = QString::fromUtf8(luaL_checkstring(L, 1));
    const QString dataDir = dataDirOf(L);
    if (QFileInfo(prefsPath(dataDir)).size() > 1024 * 1024) return "existing script preferences exceed the size limit";
    QJsonObject obj = loadPrefs(dataDir);
    if (lua_isboolean(L, 2)) {
        obj.insert(key, lua_toboolean(L, 2) != 0);
    } else if (lua_isnumber(L, 2)) {
        obj.insert(key, lua_tonumber(L, 2));
    } else {
        obj.insert(key, QString::fromUtf8(luaL_checkstring(L, 2)));
    }
    QDir().mkpath(dataDir);
    // QSaveFile: write to a temp + atomic rename so a crash mid-write can't
    // truncate/corrupt the script's prefs (plain QFile::Truncate did).
    QSaveFile f(prefsPath(dataDir));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return "cannot write prefs";
    }
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    if (bytes.size() > 1024 * 1024) return "script preferences size limit exceeded";
    if (f.write(bytes) != bytes.size() || !f.commit()) return "cannot write prefs";
    return nullptr;
}

int l_set(lua_State* L) {
    luaL_checkstring(L, 1);
    if (!lua_isboolean(L, 2) && !lua_isnumber(L, 2)) luaL_checkstring(L, 2);
    const char* error = savePrefs(L);
    return error ? luaL_error(L, "%s", error) : 0;
}

// api.timer(ms, fn) -> id
int l_timer(lua_State* L) {
    LuaRuntime* engine = engineOf(L);
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
    LuaRuntime* engine = engineOf(L);
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

// Module permission authorizes reviewed Lua code in selected directories, not
// an arbitrary filesystem read via loadfile()/string.dump().
int l_guarded_loadfile(lua_State* L) {
    size_t length = 0;
    const char* raw = luaL_checklstring(L, 1, &length);
    const QString name = QString::fromUtf8(raw, qsizetype(length));
    const auto reject = [&]() {
        lua_pushnil(L); lua_pushliteral(L, "module path is not allowed"); return 2;
    };
    if (name.contains(QChar(0)) || QFileInfo(name).suffix().compare(QLatin1String("lua"), Qt::CaseInsensitive) != 0)
        return reject();
    const QString base = QString::fromUtf8(lua_tostring(L, lua_upvalueindex(2)));
    const QFileInfo fileInfo(QDir::isAbsolutePath(name) ? name : QDir(base).filePath(name));
    const QString actual = fileInfo.canonicalFilePath();
    if (actual.isEmpty() || !fileInfo.isFile() || fileInfo.size() > 1024 * 1024 ||
        QFileInfo(actual).suffix().compare(QLatin1String("lua"), Qt::CaseInsensitive) != 0) return reject();
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    bool allowed = false;
    const size_t roots = lua_rawlen(L, lua_upvalueindex(1));
    for (size_t i = 1; i <= roots; ++i) {
        lua_rawgeti(L, lua_upvalueindex(1), lua_Integer(i));
        const QString root = QFileInfo(QString::fromUtf8(lua_tostring(L, -1))).canonicalFilePath();
        lua_pop(L, 1);
        if (!root.isEmpty() && actual.startsWith(root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/'), sensitivity)) allowed = true;
    }
    if (!allowed) return reject();
    QFile file(actual);
    if (!file.open(QIODevice::ReadOnly)) return reject();
    QByteArray source = file.read(1024 * 1024 + 1);
    if (source.size() > 1024 * 1024) return reject();
    if (source.startsWith(QByteArray::fromHex("efbbbf"))) source.remove(0, 3);
    if (source.startsWith('#')) { const qsizetype newline = source.indexOf('\n'); source = newline < 0 ? QByteArray() : source.mid(newline); }
    const int arguments = lua_gettop(L);
    const QByteArray label = (QLatin1Char('@') + actual).toUtf8();
    const int result = luaL_loadbufferx(L, source.constData(), size_t(source.size()), label.constData(), "t");
    if (result != LUA_OK) { lua_pushnil(L); lua_insert(L, -2); return 2; }
    if (arguments >= 3) { lua_pushvalue(L, 3); lua_setupvalue(L, -2, 1); }
    return 1;
}

// Guarded io.open: enforces the read/write permission + the allowed-dir list
// before delegating to the real io.open (saved in the registry). Upvalues:
// (1) engine, (2) this script's data dir.
#ifdef Q_OS_WIN
int closeWideFile(lua_State* L) {
    auto* stream = static_cast<luaL_Stream*>(luaL_checkudata(L,1,LUA_FILEHANDLE));
    const int result = std::fclose(stream->f); stream->f = nullptr;
    return luaL_fileresult(L,result == 0,nullptr);
}
#endif
int guardedOpen(lua_State* L) {
    LuaRuntime* engine = engineOf(L);
    const QString dataDir = dataDirOf(L);
    const QString path = QString::fromUtf8(luaL_checkstring(L, 1));
    const QString mode = QString::fromUtf8(luaL_optstring(L, 2, "r"));
    const bool write =
        mode.contains(QLatin1Char('w')) || mode.contains(QLatin1Char('a')) ||
        mode.contains(QLatin1Char('+'));
    if (engine == nullptr || !engine->fileAccessAllowed(path, write, dataDir, L) ||
        (mode.contains(QLatin1Char('+')) && !engine->fileAccessAllowed(path, false, dataDir, L))) {
        lua_pushnil(L);
        lua_pushstring(L, "permission denied (enable file access in Preferences > Scripts)");
        return 2;
    }
#ifdef Q_OS_WIN
    // Open exactly the Unicode path checked above, independently of Windows'
    // active ANSI code page. A UTF-8-to-ANSI mismatch must not select a sibling.
    const QByteArray asciiMode = mode.toLatin1();
    int index = 1;
    bool valid = !asciiMode.isEmpty() && (asciiMode[0] == 'r' || asciiMode[0] == 'w' || asciiMode[0] == 'a');
    if (index < asciiMode.size() && asciiMode[index] == '+') ++index;
    while (index < asciiMode.size() && asciiMode[index] == 'b') ++index;
    if (!valid || index != asciiMode.size()) { lua_pushliteral(L,"invalid file mode"); return -1; }
    auto* stream = static_cast<luaL_Stream*>(lua_newuserdatauv(L,sizeof(luaL_Stream),0));
    stream->f = nullptr; stream->closef = nullptr;
    luaL_setmetatable(L,LUA_FILEHANDLE);
    stream->f = _wfopen(reinterpret_cast<const wchar_t*>(path.utf16()), reinterpret_cast<const wchar_t*>(mode.utf16()));
    if (!stream->f) return luaL_fileresult(L,0,nullptr);
    stream->closef = closeWideFile;
    return 1;
#else
    const int base = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "maxchat_io_open"); // the real io.open
    lua_pushvalue(L, 1);                                   // path
    const QByteArray m = mode.toUtf8();
    lua_pushlstring(L, m.constData(), m.size()); // mode
    if (lua_pcall(L, 2, LUA_MULTRET, 0) != LUA_OK) return -1;
    return lua_gettop(L) - base;
#endif
}

int l_guarded_open(lua_State* L) {
    luaL_checkstring(L, 1);
    luaL_optstring(L, 2, "r");
    const int result = guardedOpen(L);
    return result < 0 ? lua_error(L) : result;
}

} // namespace

LuaRuntime::LuaRuntime(ScriptHost* host, QString scriptsDir, QString dataRoot, QObject* parent)
    : QObject(parent), host_(host), scriptsDir_(std::move(scriptsDir)),
      dataRoot_(std::move(dataRoot)) {}

LuaRuntime::~LuaRuntime() {
    const QStringList names = scripts_.keys();
    for (const QString& name : names) {
        unload(name);
    }
}

void* LuaRuntime::allocateLua(void* context, void* pointer, size_t oldSize, size_t newSize) {
    auto* runtime = static_cast<LuaRuntime*>(context);
    if (!pointer) oldSize = 0; // Lua passes a type tag for a new allocation.
    if (!newSize) { runtime->heapBytes_ -= oldSize; std::free(pointer); return nullptr; }
    constexpr size_t limit = 32 * 1024 * 1024;
    const size_t retained = runtime->heapBytes_ - oldSize;
    if (newSize > limit || retained > limit - newSize) {
        runtime->memoryLimitExceeded_.store(true);
        return nullptr;
    }
    void* result = std::realloc(pointer, newSize);
    if (result) runtime->heapBytes_ = retained + newSize;
    return result;
}
void LuaRuntime::enterExecution() { if (executionDepth_++ == 0 && activity_) activity_(true); }
void LuaRuntime::leaveExecution() { if (--executionDepth_ == 0 && activity_) activity_(false); }
bool LuaRuntime::hostLaunch(const QString& command) { return host_ && host_->scriptLaunch(command); }

bool LuaRuntime::available() {
    return true;
}

void LuaRuntime::setPermissions(const ScriptPermissions& perms) {
    perms_ = perms;
}

bool LuaRuntime::fileAccessAllowed(const QString& path, bool write, const QString& dataDir,
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
    // Resolve symlinks through the nearest existing ancestor, including when
    // a write creates a new file below a symlinked directory.
    const auto resolved = [](const QString& value) {
        QFileInfo info(QFileInfo(value).absoluteFilePath());
        QStringList missing;
        while (!info.exists()) {
            if (info.isSymbolicLink()) return QString(); // dangling link
            missing.prepend(info.fileName());
            const QString parent = info.absolutePath();
            if (parent == info.absoluteFilePath()) return QString();
            info.setFile(parent);
        }
        const QString canonical = info.canonicalFilePath();
        if (canonical.isEmpty()) return QString();
        return QDir::cleanPath(missing.isEmpty() ? canonical
            : QDir(canonical).filePath(missing.join(QLatin1Char('/'))));
    };
    const QString abs = resolved(path);
    if (abs.isEmpty()) return false;
    const auto within = [&](const QString& root) {
        if (root.trimmed().isEmpty()) {
            return false;
        }
        const QString r = resolved(root);
        if (r.isEmpty()) return false;
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
lua_State* LuaRuntime::createState(const QString& dataDir, const ScriptPermissions& perms) {
    lua_State* L = lua_newstate(&LuaRuntime::allocateLua, this);
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
        lua_newtable(L);
        QStringList moduleRoots = perms.allowedDirs;
        moduleRoots.prepend(dataDir);
        moduleRoots.prepend(scriptsDir_);
        int rootIndex = 1;
        for (const auto& root : moduleRoots) {
            const QByteArray path = QFileInfo(root).absoluteFilePath().toUtf8();
            lua_pushlstring(L, path.constData(), size_t(path.size()));
            lua_rawseti(L, -2, rootIndex++);
        }
        const QByteArray moduleBase = QFileInfo(scriptsDir_).absoluteFilePath().toUtf8();
        lua_pushlstring(L, moduleBase.constData(), size_t(moduleBase.size()));
        lua_pushcclosure(L, l_guarded_loadfile, 2);
        lua_setglobal(L, "loadfile");
        // Precompiled Lua bytecode can corrupt the interpreter (it is not
        // verified) — force text-only chunks for load/loadfile/dofile.
        luaL_dostring(L,
                      "local rawload, rawloadfile = load, loadfile\n"
                      "load = function(c, n, _, ...)\n"
                      "  if select('#', ...) == 0 then return rawload(c, n, 't') end\n"
                      "  return rawload(c, n, 't', ...)\nend\n"
                      "loadfile = function(f, _, ...)\n"
                      "  if select('#', ...) == 0 then return rawloadfile(f, 't') end\n"
                      "  return rawloadfile(f, 't', ...)\nend\n"
                      "dofile = function(f)\n"
                      "  local fn, err = loadfile(f)\n"
                      "  if not fn then error(err, 2) end\n"
                      "  return fn()\n"
                      "end\n"
                      "local searchpath = package.searchpath\n"
                      "package.searchers[2] = function(name)\n"
                      "  local file, err = searchpath(name, package.path)\n"
                      "  if not file then return err end\n"
                      "  local fn, why = rawloadfile(file, 't')\n"
                      "  if not fn then error(why, 2) end\n"
                      "  return fn, file\n"
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
        for (const char* fn : {"lines", "input", "output", "tmpfile", "stdin", "stdout", "stderr", "read", "write", "flush", "close"}) {
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

void LuaRuntime::setCurrentNetwork(const QString& network) {
    currentNetwork_ = network;
}

void LuaRuntime::hostEcho(const QString& text) {
    if (host_ != nullptr) {
        host_->scriptEcho(currentNetwork_, text);
    }
}

void LuaRuntime::hostSay(const QString& target, const QString& text) {
    if (host_ != nullptr) {
        host_->scriptSay(currentNetwork_, target, text);
    }
}

void LuaRuntime::hostInsertInput(const QString& text) {
    if (host_ != nullptr) {
        host_->scriptInsertInput(text);
    }
}

void LuaRuntime::hostNotify(const QString& title, const QString& text) {
    if (host_ != nullptr) {
        host_->scriptNotify(title, text);
    }
}

void LuaRuntime::hostSendRaw(const QString& line) {
    if (host_ != nullptr) {
        host_->scriptSendRaw(currentNetwork_, line);
    }
}

bool LuaRuntime::hostMcData(const QString& target, const QString& service,
                           const QString& verb, const QString& payload,
                           const bool notice, const QString& network) {
    return host_ != nullptr &&
           host_->scriptMcData(network.isEmpty() ? currentNetwork_ : network, target, service,
                               verb, payload, notice);
}

bool LuaRuntime::hostTerminalOpen(const QString& scriptName, const QString& id,
                                 const QString& title, const QString& profile,
                                 const int cols, const int rows) {
    return host_ != nullptr &&
           host_->scriptTerminalOpen(scriptName, id, title, profile, cols, rows);
}

void LuaRuntime::hostTerminalClose(const QString& scriptName, const QString& id) {
    if (host_ != nullptr) {
        host_->scriptTerminalClose(scriptName, id);
    }
}

void LuaRuntime::hostTerminalClear(const QString& scriptName, const QString& id) {
    if (host_ != nullptr) {
        host_->scriptTerminalClear(scriptName, id);
    }
}

void LuaRuntime::hostTerminalWrite(const QString& scriptName, const QString& id,
                                  const QString& text) {
    if (host_ != nullptr) {
        host_->scriptTerminalWrite(scriptName, id, text);
    }
}

bool LuaRuntime::hostTerminalFrame(const QString& scriptName, const QString& id,
                                  const QString& ops) {
    return host_ != nullptr && host_->scriptTerminalFrame(scriptName, id, ops);
}

void LuaRuntime::hostTerminalStatus(const QString& scriptName, const QString& id,
                                   const QString& text) {
    if (host_ != nullptr) {
        host_->scriptTerminalStatus(scriptName, id, text);
    }
}

void LuaRuntime::hostTerminalPrompt(const QString& scriptName, const QString& id,
                                   const QString& text) {
    if (host_ != nullptr) {
        host_->scriptTerminalPrompt(scriptName, id, text);
    }
}

QSize LuaRuntime::hostTerminalSize(const QString& scriptName, const QString& id) {
    return host_ != nullptr ? host_->scriptTerminalSize(scriptName, id) : QSize();
}

void LuaRuntime::hostTerminalProfile(const QString& scriptName, const QString& id,
                                    const QString& profile, const int cols, const int rows) {
    if (host_ != nullptr) {
        host_->scriptTerminalProfile(scriptName, id, profile, cols, rows);
    }
}

void LuaRuntime::hostTerminalFit(const QString& scriptName, const QString& id,
                                const QString& mode) {
    if (host_ != nullptr) {
        host_->scriptTerminalFit(scriptName, id, mode);
    }
}

QString LuaRuntime::hostTerminalHotspot(const QString& actionId, const QString& label) {
    return host_ != nullptr ? host_->scriptTerminalHotspot(actionId, label) : label;
}

QString LuaRuntime::hostMe() {
    return host_ != nullptr ? host_->scriptMe(currentNetwork_) : QString();
}

QString LuaRuntime::hostTarget() {
    return host_ != nullptr ? host_->scriptTarget() : QString();
}

QString LuaRuntime::hostNetwork() {
    return host_ != nullptr ? host_->scriptNetwork() : QString();
}

QStringList LuaRuntime::hostChannels() {
    return host_ != nullptr ? host_->scriptChannels(currentNetwork_) : QStringList();
}

QStringList LuaRuntime::hostNicks(const QString& target) {
    return host_ != nullptr ? host_->scriptNicks(currentNetwork_, target) : QStringList();
}

QString LuaRuntime::hostHttpGet(const QString& url) {
    return host_ != nullptr ? host_->scriptHttpGet(url) : QString();
}

int LuaRuntime::createTimer(void* luaState, int intervalMs, int funcRef) {
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
    // Cap timers per script so a buggy/hostile script can't exhaust QTimer/handle
    // resources by spawning thousands.
    constexpr int kMaxTimersPerScript = 64;
    int owned = 0;
    for (const ScriptTimer* t : timers_) {
        if (t->state == owner && ++owned >= kMaxTimersPerScript) {
            luaL_unref(L, LUA_REGISTRYINDEX, funcRef);
            return 0;
        }
    }
    if (nextTimerId_ == std::numeric_limits<int>::max()) { luaL_unref(L,LUA_REGISTRYINDEX,funcRef); return 0; }
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

void LuaRuntime::fireTimer(int id) {
    ExecutionScope scope(this);
    ScriptTimer* entry = timers_.value(id, nullptr);
    if (entry == nullptr || entry->state == nullptr) {
        return;
    }
    lua_State* L = entry->state->L;
    const QString scriptName = entry->state->name;
    lua_rawgeti(L, LUA_REGISTRYINDEX, entry->funcRef);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        reportError(scriptName, QStringLiteral("timer"),
                    QString::fromUtf8(lua_tostring(L, -1)));
        lua_pop(L, 1);
    }
}

void LuaRuntime::cancelTimer(int id) {
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

void LuaRuntime::cancelTimersFor(ScriptState* state) {
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

QStringList LuaRuntime::loaded() const {
    QStringList names = scripts_.keys();
    names.sort();
    return names;
}

void LuaRuntime::reportError(const QString& script, const QString& where, const QString& message) {
    if (host_ != nullptr) {
        host_->scriptEcho(currentNetwork_,
                          QStringLiteral("[scripts] %1.%2: %3").arg(script, where, message));
    }
}

// Installs the `api` table into L (also as the global `api`) and returns a
// registry ref to it, used as the first argument to every hook. Each closure
// gets two upvalues: the engine and this script's data-dir path.
static int installApi(lua_State* L, LuaRuntime* engine, const QString& dataDir,
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
    reg("insert_input", l_insert_input);
    reg("notify", l_notify);
    reg("me", l_me);
    reg("target", l_target);
    reg("network", l_network);
    reg("timestamp", l_timestamp);
    reg("data_dir", l_data_dir);
    reg("append_file", l_append_file);
    reg("read_file", l_read_file);
    // IRC-send APIs are gated: a script must hold the ircSend permission to emit
    // anything onto the network.
    if (perms.ircSend) {
        reg("say", l_say);
        reg("send_raw", l_send_raw);
        reg("mc_send", l_mc_send);
        reg("mc_reply", l_mc_reply);
    }
    reg("terminal_open", l_terminal_open);
    reg("terminal_close", l_terminal_close);
    reg("terminal_clear", l_terminal_clear);
    reg("terminal_write", l_terminal_write);
    reg("terminal_frame", l_terminal_frame);
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

bool LuaRuntime::callHook(ScriptState* state, const char* hook, const QVariantList& args) {
    ExecutionScope scope(this);
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

bool LuaRuntime::dispatch(const QString& hook, const QString& network, const QVariantList& args) {
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

bool LuaRuntime::dispatchToScript(const QString& script, const QString& hook,
                                 const QString& network, const QVariantList& args) {
    setCurrentNetwork(network);
    ScriptState* state = scripts_.value(script);
    if (state == nullptr) {
        return false;
    }
    const QByteArray hookName = hook.toUtf8();
    return callHook(state, hookName.constData(), args);
}

bool LuaRuntime::load(const QString& path, const ScriptPermissions& perms) {
    ExecutionScope scope(this);
    const QString name = QFileInfo(path).completeBaseName();
    if (!protocol::validScriptName(name) || !QFileInfo(path).isFile() || QFileInfo(path).size() > 1024 * 1024) {
        reportError(QStringLiteral("script"), QStringLiteral("load"), QStringLiteral("invalid script name or source exceeds 1 MiB"));
        return false;
    }
    // Qt opens Unicode paths correctly on Windows; Lua's narrow fopen does not.
    // Read a bounded snapshot so a file growing after the size check stays bounded.
    QFile sourceFile(path);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        reportError(name, QStringLiteral("load"), QStringLiteral("cannot open script"));
        return false;
    }
    QByteArray source = sourceFile.read(1024 * 1024 + 1);
    if (source.size() > 1024 * 1024 || sourceFile.error() != QFileDevice::NoError) {
        reportError(name, QStringLiteral("load"), QStringLiteral("cannot read script or source exceeds 1 MiB"));
        return false;
    }
    if (source.startsWith(QByteArray::fromHex("efbbbf"))) source.remove(0, 3);
    if (source.startsWith('#')) {
        const qsizetype newline = source.indexOf('\n');
        source = newline < 0 ? QByteArray() : source.mid(newline);
    }
    if (scripts_.contains(name)) {
        unload(name);
    }
    const QString dataDir = QDir(dataRoot_).filePath(name);
    lua_State* L = createState(dataDir, perms);
    if (L == nullptr) {
        return false;
    }
    const int apiRef = installApi(L, this, dataDir, perms);
    const QByteArray chunkName = (QLatin1Char('@') + path).toUtf8();
    if (luaL_loadbufferx(L, source.constData(), size_t(source.size()), chunkName.constData(), "t") != LUA_OK) {
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

int LuaRuntime::loadAll(const QHash<QString, ScriptPermissions>& permsMap, const bool startupOnly) {
    QDir dir(scriptsDir_);
    int count = 0;
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : files) {
        if (fi.fileName().startsWith(QLatin1Char('_'))) {
            continue; // leading underscore = manual-load only
        }
        const QString name = fi.completeBaseName();
        const ScriptPermissions perms = permsMap.value(name);
        if (startupOnly && !perms.loadAtStart) {
            continue;
        }
        if (load(fi.absoluteFilePath(), perms)) {
            ++count;
        }
    }
    return count;
}

bool LuaRuntime::unload(const QString& name) {
    ExecutionScope scope(this);
    ScriptState* state = scripts_.take(name);
    if (!state) return false;
    // Remove ownership first: unload/finalizer hooks may not create new timers
    // whose state would already be closed when the timer next fires.
    callHook(state, "on_unload");
    cancelTimersFor(state);
    luaL_unref(state->L, LUA_REGISTRYINDEX, state->apiRef);
    lua_close(state->L);
    stateByLua_.remove(state->L);
    delete state;
    return true;
}

bool LuaRuntime::reload(const QString& name) {
    // Save perms before unload() destroys the state.
    ScriptPermissions savedPerms;
    const auto it = scripts_.constFind(name);
    if (it != scripts_.constEnd()) {
        savedPerms = it.value()->perms;
    }
    return load(QDir(scriptsDir_).filePath(name + QStringLiteral(".lua")), savedPerms);
}

ScriptPermissions LuaRuntime::permsForScript(const QString& name) const {
    const ScriptState* state = scripts_.value(name, nullptr);
    return state ? state->perms : ScriptPermissions{};
}

} // namespace maxchat::scripting
