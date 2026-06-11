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

    void scriptEcho(const QString&, const QString& text) override { echoes.append(text); }
    void scriptSay(const QString&, const QString&, const QString&) override {}
    void scriptSendRaw(const QString&, const QString&) override {}
    void scriptInsertInput(const QString&) override {}
    void scriptNotify(const QString&, const QString&) override {}
    QString scriptMe(const QString&) override { return QStringLiteral("me"); }
    QString scriptTarget() override { return QStringLiteral("#chan"); }
    QString scriptNetwork() override { return QStringLiteral("net"); }
    QStringList scriptChannels(const QString&) override { return {}; }
    QStringList scriptNicks(const QString&, const QString&) override { return {}; }
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

QTEST_APPLESS_MAIN(LuaEngineTest)

#include "lua_engine_test.moc"
