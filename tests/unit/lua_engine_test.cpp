#include "scripting/LuaEngine.h"
#include "scripting/ScriptHost.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using maxchat::scripting::LuaEngine;
using maxchat::scripting::ScriptHost;

namespace {

// Records what scripts ask the host to do, so tests can assert on it.
class FakeHost final : public ScriptHost {
  public:
    QStringList echoes;
    QStringList says;     // "target|text"
    QStringList inserts;
    QStringList notifies; // "title|text"
    QStringList raws;

    void scriptEcho(const QString&, const QString& text) override { echoes.append(text); }
    void scriptSay(const QString&, const QString& target, const QString& text) override {
        says.append(target + QStringLiteral("|") + text);
    }
    void scriptSendRaw(const QString&, const QString& line) override { raws.append(line); }
    void scriptInsertInput(const QString& text) override { inserts.append(text); }
    void scriptNotify(const QString& title, const QString& text) override {
        notifies.append(title + QStringLiteral("|") + text);
    }
    QString scriptMe(const QString&) override { return QStringLiteral("me"); }
    QString scriptTarget() override { return QStringLiteral("#chan"); }
    QString scriptNetwork() override { return QStringLiteral("net"); }
    QStringList scriptChannels(const QString&) override {
        return {QStringLiteral("#a"), QStringLiteral("#b")};
    }
    QStringList scriptNicks(const QString&, const QString&) override {
        return {QStringLiteral("alice"), QStringLiteral("bob")};
    }
};

QString writeScript(const QDir& dir, const QString& name, const QString& body) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {};
    }
    f.write(body.toUtf8());
    f.close();
    return path;
}

} // namespace

class LuaEngineTest final : public QObject {
    Q_OBJECT

  private slots:
    void available() { QVERIFY(LuaEngine::available()); }

    void onLoadCallsEcho() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeScript(QDir(dir.path()), QStringLiteral("greet.lua"),
                                         QStringLiteral("function on_load(api)\n"
                                                        "  api.echo('hello from lua')\n"
                                                        "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QCOMPARE(host.echoes.size(), 1);
        QCOMPARE(host.echoes.at(0), QStringLiteral("hello from lua"));
        QCOMPARE(engine.loaded(), QStringList{QStringLiteral("greet")});
    }

    void onUnloadFires() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeScript(QDir(dir.path()), QStringLiteral("bye.lua"),
                                         QStringLiteral("function on_unload(api)\n"
                                                        "  api.echo('unloaded')\n"
                                                        "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QVERIFY(engine.unload(QStringLiteral("bye")));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("unloaded")});
        QVERIFY(engine.loaded().isEmpty());
    }

    void loadAllSkipsUnderscore() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QDir d(dir.path());
        writeScript(d, QStringLiteral("auto.lua"), QStringLiteral("function on_load(a) end\n"));
        writeScript(d, QStringLiteral("_manual.lua"), QStringLiteral("function on_load(a) end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QCOMPARE(engine.loadAll(), 1);
        QCOMPARE(engine.loaded(), QStringList{QStringLiteral("auto")});
    }

    void sandboxBlocksIo() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // `io` must not exist in the sandbox, so this chunk errors at init and
        // the script fails to load (rather than reading the file).
        const QString path = writeScript(QDir(dir.path()), QStringLiteral("evil.lua"),
                                         QStringLiteral("io.open('/etc/passwd', 'r')\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(!engine.load(path));
        QVERIFY(engine.loaded().isEmpty());
        // The failure was reported via the host.
        QVERIFY(!host.echoes.isEmpty());
        QVERIFY(host.echoes.at(0).contains(QStringLiteral("[scripts]")));
    }

    void sandboxRemovesLoaders() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // load/dofile/require are all gone → indexing them is nil-call errors.
        const QString path = writeScript(
            QDir(dir.path()), QStringLiteral("escape.lua"),
            QStringLiteral("local ok = (load == nil) and (dofile == nil) and (require == nil)\n"
                           "function on_load(api) api.echo(ok and 'locked' or 'OPEN') end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("locked")});
    }

    void apiCoreCalls() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path =
            writeScript(QDir(dir.path()), QStringLiteral("core.lua"),
                        QStringLiteral("function on_load(api)\n"
                                       "  api.say('#c', 'hi')\n"
                                       "  api.insert_input('draft')\n"
                                       "  api.notify('T', 'B')\n"
                                       "  api.echo(api.me()..'/'..api.target()..'/'..api.network())\n"
                                       "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QCOMPARE(host.says, QStringList{QStringLiteral("#c|hi")});
        QCOMPARE(host.inserts, QStringList{QStringLiteral("draft")});
        QCOMPARE(host.notifies, QStringList{QStringLiteral("T|B")});
        QCOMPARE(host.echoes, QStringList{QStringLiteral("me/#chan/net")});
    }

    void apiFilesRoundTripInDataDir() {
        QTemporaryDir scripts;
        QTemporaryDir data;
        QVERIFY(scripts.isValid() && data.isValid());
        const QString path = writeScript(
            QDir(scripts.path()), QStringLiteral("notes.lua"),
            QStringLiteral("function on_load(api)\n"
                           "  api.append_file('log.txt', 'line1\\n')\n"
                           "  api.append_file('log.txt', 'line2\\n')\n"
                           "  api.echo(api.read_file('log.txt'))\n"
                           "  api.echo(api.read_file('missing.txt') == nil and 'NIL' or 'SET')\n"
                           "end\n"));
        FakeHost host;
        LuaEngine engine(&host, scripts.path(), data.path());
        QVERIFY(engine.load(path));
        QCOMPARE(host.echoes.size(), 2);
        QCOMPARE(host.echoes.at(0), QStringLiteral("line1\nline2\n"));
        QCOMPARE(host.echoes.at(1), QStringLiteral("NIL"));
        // The file landed in <data>/notes/, not anywhere else.
        QVERIFY(QFile::exists(QDir(data.path()).filePath(QStringLiteral("notes/log.txt"))));
    }

    void apiFileTraversalIsJailed() {
        QTemporaryDir scripts;
        QTemporaryDir data;
        QVERIFY(scripts.isValid() && data.isValid());
        // Try to write outside the data dir; only the basename is honoured, so
        // the byte lands in <data>/esc/evil.txt and nowhere up the tree.
        const QString path =
            writeScript(QDir(scripts.path()), QStringLiteral("esc.lua"),
                        QStringLiteral("function on_load(api)\n"
                                       "  api.append_file('../../evil.txt', 'x')\n"
                                       "end\n"));
        FakeHost host;
        LuaEngine engine(&host, scripts.path(), data.path());
        QVERIFY(engine.load(path));
        QVERIFY(QFile::exists(QDir(data.path()).filePath(QStringLiteral("esc/evil.txt"))));
        QVERIFY(!QFile::exists(QDir(data.path()).filePath(QStringLiteral("../evil.txt"))));
        QVERIFY(!QFile::exists(QDir(data.path()).filePath(QStringLiteral("evil.txt"))));
    }

    void dispatchOnCommandConsumes() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeScript(
            QDir(dir.path()), QStringLiteral("cmd.lua"),
            QStringLiteral("function on_command(api, cmd, args)\n"
                           "  if cmd == 'foo' then api.echo('got '..args) return true end\n"
                           "  return false\n"
                           "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QVERIFY(engine.dispatch(QStringLiteral("on_command"), QStringLiteral("net"),
                                {QStringLiteral("foo"), QStringLiteral("bar")}));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("got bar")});
        // A command the script doesn't handle is not consumed.
        QVERIFY(!engine.dispatch(QStringLiteral("on_command"), QStringLiteral("net"),
                                 {QStringLiteral("other"), QString()}));
    }

    void dispatchPassesHookArgs() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path =
            writeScript(QDir(dir.path()), QStringLiteral("msg.lua"),
                        QStringLiteral("function on_message(api, network, target, nick, text)\n"
                                       "  api.echo(network..'|'..target..'|'..nick..'|'..text)\n"
                                       "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        engine.dispatch(QStringLiteral("on_message"), QStringLiteral("libera"),
                        {QStringLiteral("libera"), QStringLiteral("#c"), QStringLiteral("bob"),
                         QStringLiteral("hi")});
        QCOMPARE(host.echoes, QStringList{QStringLiteral("libera|#c|bob|hi")});
    }

    void apiSendRawAndState() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeScript(
            QDir(dir.path()), QStringLiteral("state.lua"),
            QStringLiteral("function on_load(api)\n"
                           "  api.send_raw('PING xyz')\n"
                           "  api.echo(#api.channels()..'/'..api.nicks('#a')[1])\n"
                           "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QCOMPARE(host.raws, QStringList{QStringLiteral("PING xyz")});
        QCOMPARE(host.echoes, QStringList{QStringLiteral("2/alice")});
    }

    void apiStrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path =
            writeScript(QDir(dir.path()), QStringLiteral("strip.lua"),
                        QStringLiteral("function on_load(api)\n"
                                       "  api.echo(api.strip(string.char(2)..'bold'..string.char(15)))\n"
                                       "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("bold")});
    }

    void apiPrefsPersist() {
        QTemporaryDir scripts;
        QTemporaryDir data;
        QVERIFY(scripts.isValid() && data.isValid());
        const QString path = writeScript(
            QDir(scripts.path()), QStringLiteral("pref.lua"),
            QStringLiteral("function on_load(api)\n"
                           "  api.set('name', 'zoe')\n"
                           "  api.set('count', 7)\n"
                           "  local n = api.get('count')\n"
                           "  api.echo(api.get('name')..'/'..tostring(n == 7)..'/'\n"
                           "           ..tostring(api.get('missing') == nil))\n"
                           "end\n"));
        FakeHost host;
        LuaEngine engine(&host, scripts.path(), data.path());
        QVERIFY(engine.load(path));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("zoe/true/true")});
        QVERIFY(QFile::exists(QDir(data.path()).filePath(QStringLiteral("pref/prefs.json"))));
        // A fresh engine sees the persisted value.
        FakeHost host2;
        LuaEngine engine2(&host2, scripts.path(), data.path());
        const QString path2 =
            writeScript(QDir(scripts.path()), QStringLiteral("pref2.lua"),
                        QStringLiteral("function on_load(api) end\n"));
        Q_UNUSED(path2);
        // reload the same script; its prefs survive.
        QVERIFY(engine2.load(path));
        QCOMPARE(host2.echoes, QStringList{QStringLiteral("zoe/true/true")});
    }

    void apiTimerFires() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path =
            writeScript(QDir(dir.path()), QStringLiteral("tick.lua"),
                        QStringLiteral("function on_load(api)\n"
                                       "  api.timer(50, function() api.echo('tick') end)\n"
                                       "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QTRY_VERIFY(host.echoes.contains(QStringLiteral("tick")));
    }

    void apiTimerCancelStops() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path =
            writeScript(QDir(dir.path()), QStringLiteral("cancel.lua"),
                        QStringLiteral("function on_load(api)\n"
                                       "  local id = api.timer(50, function() api.echo('x') end)\n"
                                       "  api.cancel_timer(id)\n"
                                       "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QTest::qWait(200);
        QVERIFY(host.echoes.isEmpty());
    }

    void unloadCancelsTimers() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path =
            writeScript(QDir(dir.path()), QStringLiteral("ghost.lua"),
                        QStringLiteral("function on_load(api)\n"
                                       "  api.timer(50, function() api.echo('boom') end)\n"
                                       "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QVERIFY(engine.unload(QStringLiteral("ghost"))); // must stop the timer
        QTest::qWait(200);
        QVERIFY(host.echoes.isEmpty());
    }

    void scriptErrorDoesNotCrash() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeScript(QDir(dir.path()), QStringLiteral("boom.lua"),
                                         QStringLiteral("function on_load(api)\n"
                                                        "  error('kaboom')\n"
                                                        "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path)); // load succeeds; the hook error is caught
        QVERIFY(!host.echoes.isEmpty());
        QVERIFY(host.echoes.at(0).contains(QStringLiteral("boom.on_load")));
    }
};

QTEST_GUILESS_MAIN(LuaEngineTest)

#include "lua_engine_test.moc"
