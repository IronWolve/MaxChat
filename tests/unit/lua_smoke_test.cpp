// Proves the vendored Lua 5.4 sources compile, link, and run. Built only when
// -DMAXCHAT_LUA=ON.
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <QtTest/QtTest>

class LuaSmokeTest final : public QObject {
    Q_OBJECT

  private slots:
    void opensRunsCloses() {
        lua_State* L = luaL_newstate();
        QVERIFY(L != nullptr);
        luaL_openlibs(L);
        QCOMPARE(luaL_dostring(L, "return 2 + 3"), int(LUA_OK));
        QVERIFY(lua_isinteger(L, -1));
        QCOMPARE(static_cast<long long>(lua_tointeger(L, -1)), 5LL);
        lua_close(L);
    }

    void stdlibIsPresent() {
        lua_State* L = luaL_newstate();
        QVERIFY(L != nullptr);
        luaL_openlibs(L);
        // string + math libraries load and work.
        QCOMPARE(luaL_dostring(L, "return string.upper('hi')..tostring(math.max(1,2))"),
                 int(LUA_OK));
        QCOMPARE(QString::fromUtf8(lua_tostring(L, -1)), QStringLiteral("HI2"));
        lua_close(L);
    }

    void reportsVersion() {
        QVERIFY(QString::fromLatin1(LUA_VERSION).contains(QStringLiteral("5.4")));
    }
};

QTEST_APPLESS_MAIN(LuaSmokeTest)

#include "lua_smoke_test.moc"
