#include "scripting/LuaEngine.h"
#include "scripting/ScriptProtocol.h"
#include "scripting/ScriptHost.h"
#include "scripting/ScriptPermissions.h"
#include "ui/TerminalFrame.h"

#include <QDir>
#include <QHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QCoreApplication>
#include <QScopeGuard>
#include <functional>
#include <QSize>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using maxchat::scripting::LuaEngine;
using maxchat::scripting::ScriptHost;

namespace {

// Records what scripts ask the host to do, so tests can assert on it.
class FakeHost final : public ScriptHost {
  public:
    QStringList echoes;
    QStringList echoNetworks;
    std::function<QString()> httpReply;
    std::function<void()> afterEcho;
    QStringList says;     // "target|text"
    QStringList inserts;
    QStringList notifies; // "title|text"
    QStringList raws;
    QStringList mcData;   // "network|target|service|verb|payload|notice"
    QStringList terminals;
    QHash<QString, QSize> terminalSizes;

    void scriptEcho(const QString& network, const QString& text) override { echoNetworks.append(network); echoes.append(text); const auto callback = afterEcho; if (callback) callback(); }
    void scriptSay(const QString&, const QString& target, const QString& text) override {
        says.append(target + QStringLiteral("|") + text);
    }
    void scriptSendRaw(const QString&, const QString& line) override { raws.append(line); }
    void scriptInsertInput(const QString& text) override { inserts.append(text); }
    void scriptNotify(const QString& title, const QString& text) override {
        notifies.append(title + QStringLiteral("|") + text);
    }
    bool scriptMcData(const QString& network, const QString& target, const QString& service,
                      const QString& verb, const QString& payload, bool notice) override {
        mcData.append(QStringLiteral("%1|%2|%3|%4|%5|%6")
                          .arg(network, target, service, verb, payload,
                               notice ? QStringLiteral("notice") : QStringLiteral("privmsg")));
        return !target.isEmpty();
    }
    bool scriptTerminalOpen(const QString& scriptName, const QString& id, const QString& title,
                            const QString& profile, int cols, int rows) override {
        terminals.append(QStringLiteral("open|%1|%2|%3|%4|%5x%6")
                             .arg(scriptName, id, title, profile)
                             .arg(cols)
                             .arg(rows));
        terminalSizes.insert(scriptName + QStringLiteral("/") + id, QSize(cols, rows));
        return true;
    }
    void scriptTerminalClose(const QString& scriptName, const QString& id) override {
        terminals.append(QStringLiteral("close|%1|%2").arg(scriptName, id));
    }
    void scriptTerminalClear(const QString& scriptName, const QString& id) override {
        terminals.append(QStringLiteral("clear|%1|%2").arg(scriptName, id));
    }
    void scriptTerminalWrite(const QString& scriptName, const QString& id,
                             const QString& text) override {
        terminals.append(QStringLiteral("write|%1|%2|%3").arg(scriptName, id, text));
    }
    bool scriptTerminalFrame(const QString& scriptName, const QString& id,
                             const QString& ops) override {
        terminals.append(QStringLiteral("frame|%1|%2|%3").arg(scriptName, id, ops));
        return !ops.isEmpty();
    }
    void scriptTerminalStatus(const QString& scriptName, const QString& id,
                              const QString& text) override {
        terminals.append(QStringLiteral("status|%1|%2|%3").arg(scriptName, id, text));
    }
    void scriptTerminalPrompt(const QString& scriptName, const QString& id,
                              const QString& text) override {
        terminals.append(QStringLiteral("prompt|%1|%2|%3").arg(scriptName, id, text));
    }
    QSize scriptTerminalSize(const QString& scriptName, const QString& id) override {
        return terminalSizes.value(scriptName + QStringLiteral("/") + id, QSize(0, 0));
    }
    void scriptTerminalProfile(const QString& scriptName, const QString& id,
                               const QString& profile, int cols, int rows) override {
        terminals.append(QStringLiteral("profile|%1|%2|%3|%4x%5")
                             .arg(scriptName, id, profile)
                             .arg(cols)
                             .arg(rows));
    }
    void scriptTerminalFit(const QString& scriptName, const QString& id,
                           const QString& mode) override {
        terminals.append(QStringLiteral("fit|%1|%2|%3").arg(scriptName, id, mode));
    }
    QString scriptTerminalHotspot(const QString& actionId, const QString& label) override {
        return QStringLiteral("<hotspot action=\"%1\">%2</hotspot>").arg(actionId, label);
    }
    QString scriptMe(const QString&) override { return QStringLiteral("me"); }
    QString scriptTarget() override { return QStringLiteral("#chan"); }
    QString scriptNetwork() override { return QStringLiteral("synIRC"); }
    QStringList scriptChannels(const QString&) override {
        return {QStringLiteral("#a"), QStringLiteral("#b")};
    }
    QStringList scriptNicks(const QString&, const QString&) override {
        return {QStringLiteral("alice"), QStringLiteral("bob")};
    }
    QString scriptHttpGet(const QString&) override { return httpReply ? httpReply() : QStringLiteral("BODY"); }
};

// Permissions granting IRC send (say/send_raw/mc_send/mc_reply).
maxchat::scripting::ScriptPermissions ircPerms() {
    maxchat::scripting::ScriptPermissions p;
    p.ircSend = true;
    return p;
}

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
    void supervisorRejectsHostilePeerMessages_data() {
        QTest::addColumn<QString>("mode"); QTest::addColumn<bool>("accepted");
        QTest::newRow("forbidden-irc") << QStringLiteral("forbidden-irc") << false;
        QTest::newRow("bad-type") << QStringLiteral("bad-type") << false;
        QTest::newRow("oversized-frame") << QStringLiteral("oversized-frame") << false;
        QTest::newRow("native-hang") << QStringLiteral("native-hang") << false;
        QTest::newRow("script-identity") << QStringLiteral("identity") << true;
        QTest::newRow("stdout-not-protocol") << QStringLiteral("stdout-forgery") << true;
        QTest::newRow("wrong-process") << QStringLiteral("wrong-peer") << true;
    }
    void supervisorRejectsHostilePeerMessages() {
        QFETCH(QString, mode); QFETCH(bool, accepted);
        const auto previous=qgetenv("MAXCHAT_TEST_WORKER_MODE");
        const auto restore=qScopeGuard([previous]{ if(previous.isNull()) qunsetenv("MAXCHAT_TEST_WORKER_MODE"); else qputenv("MAXCHAT_TEST_WORKER_MODE",previous); });
        qputenv("MAXCHAT_TEST_WORKER_MODE",mode.toUtf8());
        QTemporaryDir directory; FakeHost host;
        QString helper=QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("maxchat-worker-fixture"));
#ifdef Q_OS_WIN
        helper+=QStringLiteral(".exe");
#endif
        LuaEngine engine(&host,directory.path(),directory.path(),nullptr,helper);
        const auto source=writeScript(QDir(directory.path()),QStringLiteral("owner.lua"),QStringLiteral("-- fixture"));
        QElapsedTimer elapsed;elapsed.start();
        QCOMPARE(engine.load(source),accepted);
        QVERIFY(elapsed.elapsed()<5000);
        QVERIFY(host.raws.isEmpty());
        if (mode==QLatin1String("identity")) {
            QCOMPARE(host.terminals,QStringList{QStringLiteral("open|owner|panel|fixture|free|80x25")});
        } else if (mode==QLatin1String("stdout-forgery")) {
            QCOMPARE(host.echoes,QStringList{QStringLiteral("private channel")});
        } else if (mode==QLatin1String("wrong-peer")) {
            QCOMPARE(host.echoes,QStringList{QStringLiteral("foreign peer rejected")});
        }
    }

    void hostileScriptsAreContained_data() {
        QTest::addColumn<QString>("source");
        QTest::newRow("infinite-loop") << QStringLiteral("while true do end");
        QTest::newRow("caught-errors") << QStringLiteral("while true do pcall(function() error('caught') end) end");
        QTest::newRow("allocation") << QStringLiteral("local x = string.rep('x', 40 * 1024 * 1024)");
        QTest::newRow("caught-allocation") << QStringLiteral("while true do pcall(string.rep, 'x', 40 * 1024 * 1024) end");
        QTest::newRow("native-pattern-loop") << QStringLiteral("string.match(string.rep('a',30000), 'a*a*a*a*a*a*a*a*b')");
        QTest::newRow("stdout-flood") << QStringLiteral("while true do print(string.rep('x',65536)) end");
        QTest::newRow("ipc-flood") << QStringLiteral("function on_load(api) while true do api.echo(string.rep('x',100000)) end end");
        QTest::newRow("oversized-ipc") << QStringLiteral("function on_load(api) api.echo(string.rep('x',3*1024*1024)) end");
    }
    void hostileScriptsAreContained() {
        QFETCH(QString, source);
        QTemporaryDir directory; FakeHost host;
        LuaEngine engine(&host,directory.path(),directory.path());
        const QString good = writeScript(QDir(directory.path()),QStringLiteral("good.lua"),
            QStringLiteral("function on_ping(api) api.echo('healthy'); return true end"));
        QVERIFY(engine.load(good));
        int heartbeats = 0; QTimer timer; timer.setInterval(10);
        connect(&timer,&QTimer::timeout,this,[&] { ++heartbeats; }); timer.start();
        QElapsedTimer elapsed; elapsed.start();
        const QString bad = writeScript(QDir(directory.path()),QStringLiteral("bad.lua"),source);
        engine.load(bad);
        QTRY_VERIFY_WITH_TIMEOUT(!engine.loaded().contains(QStringLiteral("bad")),5000);
        QVERIFY2(elapsed.elapsed()<5000,"Runaway worker blocked its caller");
        QVERIFY(elapsed.elapsed()<50 || heartbeats>0);
        QVERIFY(engine.dispatch(QStringLiteral("on_ping"),QStringLiteral("net")));
        QVERIFY(host.echoes.contains(QStringLiteral("healthy")));
        QCOMPARE(engine.loaded(),QStringList{QStringLiteral("good")});
    }
    void runawayTimerIsContained() {
        QTemporaryDir directory; FakeHost host;
        LuaEngine engine(&host,directory.path(),directory.path());
        const auto file=writeScript(QDir(directory.path()),QStringLiteral("timer-loop.lua"),
            QStringLiteral("function on_load(api) api.timer(50,function() while true do end end) end"));
        QVERIFY(engine.load(file));
        QTRY_VERIFY_WITH_TIMEOUT(engine.loaded().isEmpty(),5000);
        QVERIFY(!host.echoes.isEmpty());
    }
    void unloadDuringCallbackCancelsOnlyThatWorker() {
        QTemporaryDir directory; FakeHost host;
        LuaEngine engine(&host,directory.path(),directory.path());
        const auto file=writeScript(QDir(directory.path()),QStringLiteral("cancel.lua"),
            QStringLiteral("function on_load(api) api.echo('cancel me') end"));
        host.afterEcho=[&] { host.afterEcho={}; engine.unload(QStringLiteral("cancel")); };
        QVERIFY(!engine.load(file));
        QVERIFY(engine.loaded().isEmpty());
    }
    void missingWorkerFailsClosed() {
        QTemporaryDir directory; FakeHost host;
        LuaEngine engine(&host,directory.path(),directory.path(),nullptr,directory.filePath(QStringLiteral("missing-worker")));
        const auto file=writeScript(QDir(directory.path()),QStringLiteral("probe.lua"),QStringLiteral("function on_load(api) api.echo('unsafe') end"));
        QVERIFY(!engine.load(file)); QVERIFY(engine.loaded().isEmpty());
        QVERIFY(!host.echoes.contains(QStringLiteral("unsafe")));
        QVERIFY(host.echoes.join(QLatin1Char(' ')).contains(QStringLiteral("worker is missing")));
    }
    void cancelledTimerMayReportAnErrorWithoutDanglingState() {
        QTemporaryDir directory; FakeHost host;
        LuaEngine engine(&host,directory.path(),directory.path());
        const auto file=writeScript(QDir(directory.path()),QStringLiteral("cancel-timer.lua"),
            QStringLiteral("function on_load(api) local id; id=api.timer(50,function() api.cancel_timer(id); error('expected') end) end"));
        QVERIFY(engine.load(file));
        QTRY_VERIFY(host.echoes.join(QLatin1Char(' ')).contains(QStringLiteral("expected")));
        QCOMPARE(engine.loaded(),QStringList{QStringLiteral("cancel-timer")});
    }

    void destructionDuringHostCallbackIsSafe() {
        QTemporaryDir directory; FakeHost host;
        auto* engine=new LuaEngine(&host,directory.path(),directory.path());
        const auto file=writeScript(QDir(directory.path()),QStringLiteral("destroy.lua"),
            QStringLiteral("function on_load(api) api.echo('destroy') end"));
        host.afterEcho=[&] { host.afterEcho={}; delete engine; engine=nullptr; };
        QVERIFY(!engine->load(file)); QVERIFY(engine==nullptr);
    }
    void queuedEventsRetainNetworkScopeDuringHostWait() {
        QTemporaryDir directory; FakeHost host;
        LuaEngine engine(&host,directory.path(),directory.path()); engine.setCurrentNetwork(QStringLiteral("initial"));
        const auto file=writeScript(QDir(directory.path()),QStringLiteral("queued.lua"),
            QStringLiteral("function on_load(api) api.http_get('fixture'); api.echo('loaded') end\n"
                           "function on_message(api,text) api.echo(text) end"));
        host.httpReply=[&] {
            QEventLoop loop;
            QTimer::singleShot(0,&loop,[&]{engine.dispatch(QStringLiteral("on_message"),QStringLiteral("other"),{QStringLiteral("queued")});});
            QTimer::singleShot(50,&loop,&QEventLoop::quit); loop.exec(); return QStringLiteral("reply");
        };
        auto permissions=ircPerms(); permissions.network=true;
        QVERIFY(engine.load(file,permissions));
        QTRY_VERIFY(host.echoes.contains(QStringLiteral("queued")));
        QCOMPARE(host.echoes,(QStringList{QStringLiteral("loaded"),QStringLiteral("queued")}));
        QCOMPARE(host.echoNetworks,(QStringList{QStringLiteral("initial"),QStringLiteral("other")}));
    }
    void shortTimerCpuFloodHasAggregateLimit() {
        QTemporaryDir directory; FakeHost host;
        LuaEngine engine(&host,directory.path(),directory.path());
        const auto file=writeScript(QDir(directory.path()),QStringLiteral("timer-flood.lua"),
            QStringLiteral("function on_load(api) api.timer(50,function() local start=os.clock(); while os.clock()-start<0.12 do end end) end"));
        QVERIFY(engine.load(file));
        QTRY_VERIFY_WITH_TIMEOUT(engine.loaded().isEmpty(),6000);
    }
    void unloadFinalizersAreBounded() {
        QTemporaryDir directory; FakeHost host;
        LuaEngine engine(&host,directory.path(),directory.path());
        const auto file=writeScript(QDir(directory.path()),QStringLiteral("finalizer.lua"),
            QStringLiteral("held=setmetatable({}, {__gc=function() while true do end end})"));
        QVERIFY(engine.load(file)); QElapsedTimer elapsed;elapsed.start();
        QVERIFY(engine.unload(QStringLiteral("finalizer")));
        QVERIFY(elapsed.elapsed()<4000); QVERIFY(engine.loaded().isEmpty());
    }
    void unloadKeepsApprovedFileAccessUntilStateCloses() {
        QTemporaryDir directory; FakeHost host;
        LuaEngine engine(&host,directory.path(),directory.path());
        const auto file=writeScript(QDir(directory.path()),QStringLiteral("flush.lua"),
            QStringLiteral("function on_unload(api) local f=assert(io.open(api.data_dir()..'/final.txt','w')); f:write('done'); f:close() end"));
        auto permissions=ircPerms();permissions.writeFiles=true;
        QVERIFY(engine.load(file,permissions)); QVERIFY(engine.unload(QStringLiteral("flush")));
        QFile saved(directory.filePath(QStringLiteral("flush/final.txt"))); QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(),QByteArray("done"));
    }
    void loadedScriptCountIsBounded() {
        QTemporaryDir directory; FakeHost host;
        LuaEngine engine(&host,directory.path(),directory.path());
        for (int i=0;i<17;++i) {
            const auto path=writeScript(QDir(directory.path()),QStringLiteral("script%1.lua").arg(i),QStringLiteral("-- bounded fixture"));
            QCOMPARE(engine.load(path),i<16);
        }
        QCOMPARE(engine.loaded().size(),16);
    }

    void builtInDataWritesAreBoundedAndPreserveExistingFiles() {
        QTemporaryDir directory; FakeHost host;
        const QString data=directory.filePath(QStringLiteral("quota")); QVERIFY(QDir().mkpath(data));
        QFile content(QDir(data).filePath(QStringLiteral("full.txt")));
        QVERIFY(content.open(QIODevice::WriteOnly)); QVERIFY(content.resize(8*1024*1024)); content.close();
        QFile preferences(QDir(data).filePath(QStringLiteral("prefs.json")));
        QVERIFY(preferences.open(QIODevice::WriteOnly)); QVERIFY(preferences.resize(1024*1024+1)); preferences.close();
        const auto source=writeScript(QDir(directory.path()),QStringLiteral("quota.lua"),
            QStringLiteral("function on_load(api) assert(not pcall(api.append_file,'full.txt','x')); assert(not pcall(api.set,'key','value')); api.echo('bounded') end"));
        LuaEngine engine(&host,directory.path(),directory.path()); QVERIFY(engine.load(source));
        QCOMPARE(host.echoes,QStringList{QStringLiteral("bounded")});
        QCOMPARE(QFileInfo(content).size(),qint64(8*1024*1024));
        QCOMPARE(QFileInfo(preferences).size(),qint64(1024*1024+1));
    }

    void dataScopeNamesArePortableAndCannotNormalizeToParentOrDevice() {
        using maxchat::scripting::protocol::validScriptName;
        for (const QString& name : {QStringLiteral(".. "),QStringLiteral(".."),QStringLiteral("."),
             QStringLiteral("name."),QStringLiteral("con"),QStringLiteral("NUL.txt"),QStringLiteral("COM1"),
             QStringLiteral("LPT\u00b2"),QStringLiteral("CONOUT$"),QStringLiteral("../other")}) QVERIFY(!validScriptName(name));
        QVERIFY(validScriptName(QStringLiteral("memo"))); QVERIFY(validScriptName(QStringLiteral("\u00e9cho script")));
    }

    void approvedIoUsesTheSameUnicodePathAsThePermissionCheck() {
        QTemporaryDir directory; FakeHost host;
        const auto file=writeScript(QDir(directory.path()),QStringLiteral("\u00e9cho.lua"),
            QStringLiteral("function on_load(api) local path=api.data_dir()..'/\u6f22.txt'; local f=assert(io.open(path,'w')); f:write('unicode'); f:close(); local r=assert(io.open(path,'r')); api.echo(r:read('a')); r:close() end"));
        LuaEngine engine(&host,directory.path(),directory.path()); auto permissions=ircPerms();
        permissions.readFiles=true;permissions.writeFiles=true;
        QVERIFY(engine.load(file,permissions)); QCOMPARE(host.echoes,QStringList{QStringLiteral("unicode")});
        QFile saved(directory.filePath(QStringLiteral("\u00e9cho/\u6f22.txt"))); QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(),QByteArray("unicode"));
    }

    void available() { QVERIFY(LuaEngine::available()); }

    void scriptBasenameCannotEscapeItsDataDirectory() {
        QTemporaryDir scripts, data;
        const QString path = writeScript(QDir(scripts.path()), QStringLiteral("...lua"),
            QStringLiteral("function on_load(api) api.append_file('escaped.txt', 'bad') end"));
        QCOMPARE(QFileInfo(path).completeBaseName(), QStringLiteral(".."));
        FakeHost host;
        LuaEngine engine(&host, scripts.path(), data.path());
        QVERIFY(!engine.load(path));
        QVERIFY(engine.loaded().isEmpty());
        QVERIFY(!QFile::exists(QDir(data.path()).filePath(QStringLiteral("../escaped.txt"))));
    }

    void unicodeEntryPathPreservesBomAndShebang() {
        QTemporaryDir root;
        const QString scripts = root.filePath(QStringLiteral("scripts with spaces \u03a9\u6f22"));
        QVERIFY(QDir().mkpath(scripts));
        const QString path = writeScript(QDir(scripts), QStringLiteral("\u00e9cho.lua"),
            QString(QChar(0xfeff)) + QStringLiteral("#!/usr/bin/env lua\nfunction on_load(api) api.echo('unicode loaded') end"));
        FakeHost host;
        LuaEngine engine(&host, scripts, root.filePath(QStringLiteral("data")));
        QVERIFY(engine.load(path));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("unicode loaded")});
    }

    void rejectsBinaryEntryScriptsBeforeDecoding() {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("binary.lua"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromHex("1b4c7561"));
        file.close();
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(!engine.load(path));
        QVERIFY(host.echoes.join(QLatin1Char('\n')).contains(QStringLiteral("mode is 't'")));
    }

    void requireRejectsBinaryModules() {
        QTemporaryDir dir;
        const QString module = QDir(dir.path()).filePath(QStringLiteral("bytecode.lua"));
        QFile file(module);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromHex("1b4c7561"));
        file.close();
        const QString path = writeScript(QDir(dir.path()), QStringLiteral("loader.lua"),
            QStringLiteral("package.path = [[%1/?.lua]]\n"
                           "function on_load(api)\n"
                           " local ok, err = pcall(require, 'bytecode')\n"
                           " api.echo(tostring(ok)); api.echo(tostring(err))\nend\n").arg(dir.path()));
        maxchat::scripting::ScriptPermissions perms;
        perms.loadModules = true;
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path, perms));
        QCOMPARE(host.echoes.value(0), QStringLiteral("false"));
        QVERIFY(host.echoes.value(1).contains(QStringLiteral("mode is 't'")));
    }

    void dataApiRejectsSymlinkEscapes() {
#ifdef Q_OS_WIN
        QSKIP("QFile::link creates Windows shortcuts, not POSIX symlinks");
#endif
        QTemporaryDir dir;
        const QString outside = QDir(dir.path()).filePath(QStringLiteral("outside.txt"));
        QFile file(outside);
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("unchanged"); file.close();
        const QString data = QDir(dir.path()).filePath(QStringLiteral("data"));
        QVERIFY(QDir().mkpath(data + QStringLiteral("/escape")));
        QVERIFY(QFile::link(outside, data + QStringLiteral("/escape/link.txt")));
        const QString path = writeScript(QDir(dir.path()), QStringLiteral("escape.lua"),
            QStringLiteral("function on_load(api)\n"
                           " api.echo(tostring(api.read_file('link.txt') == nil))\n"
                           " api.echo(tostring(pcall(api.append_file, 'link.txt', 'bad')))\nend\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), data);
        QVERIFY(engine.load(path));
        QCOMPARE(host.echoes, (QStringList{QStringLiteral("true"), QStringLiteral("false")}));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("unchanged"));
    }

    void writePermissionDoesNotGrantReadWriteModes() {
        QTemporaryDir dir;
        const QString path = writeScript(QDir(dir.path()), QStringLiteral("writer.lua"),
            QStringLiteral("function on_load(api)\n"
                           " local f = io.open([[%1/readwrite.txt]], 'w+')\n"
                           " api.echo(tostring(f == nil))\nend\n").arg(dir.path()));
        maxchat::scripting::ScriptPermissions perms;
        perms.writeFiles = true; perms.allowedDirs = {dir.path()};
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path, perms));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("true")});
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QStringLiteral("readwrite.txt"))));
    }

    void allowedDirectoryRejectsSymlinkEscapes() {
#ifdef Q_OS_WIN
        QSKIP("POSIX symlink regression");
#endif
        QTemporaryDir dir;
        const QString allowed = QDir(dir.path()).filePath(QStringLiteral("allowed"));
        const QString outside = QDir(dir.path()).filePath(QStringLiteral("outside"));
        QVERIFY(QDir().mkpath(allowed)); QVERIFY(QDir().mkpath(outside));
        QVERIFY(QFile::link(outside, allowed + QStringLiteral("/link")));
        const QString path = writeScript(QDir(dir.path()), QStringLiteral("guard.lua"),
            QStringLiteral("function on_load(api)\n"
                           " local f = io.open([[%1/link/escape.txt]], 'w')\n"
                           " api.echo(tostring(f == nil))\nend\n").arg(allowed));
        maxchat::scripting::ScriptPermissions perms;
        perms.writeFiles = true; perms.allowedDirs = {allowed};
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path, perms));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("true")});
        QVERIFY(!QFile::exists(outside + QStringLiteral("/escape.txt")));
    }


    void moduleLoadingCannotReadOutsideApprovedDirectories() {
        QTemporaryDir scripts, outside;
        const QString secret = writeScript(QDir(outside.path()), QStringLiteral("private.lua"),
                                           QStringLiteral("return 'private-fixture'"));
        const QString script = writeScript(QDir(scripts.path()), QStringLiteral("loader.lua"),
            QStringLiteral("function on_load(api)\n"
                           " local f = loadfile([[%1]])\n"
                           " api.echo(tostring(f == nil))\nend\n").arg(secret));
        FakeHost host;
        LuaEngine engine(&host, scripts.path(), scripts.path());
        maxchat::scripting::ScriptPermissions permissions; permissions.loadModules = true;
        QVERIFY(engine.load(script, permissions));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("true")});
    }

    void textLoadersPreserveTheirDefaultEnvironment() {
        QTemporaryDir scripts;
        writeScript(QDir(scripts.path()), QStringLiteral("module.lua"), QStringLiteral("return math.floor(3.7)"));
        const QString script = writeScript(QDir(scripts.path()), QStringLiteral("loader.lua"),
            QStringLiteral("function on_load(api)\n"
                           " api.echo(tostring(assert(load('return math.floor(4.7)'))()))\n"
                           " api.echo(tostring(assert(loadfile('module.lua'))()))\nend\n"));
        FakeHost host;
        LuaEngine engine(&host, scripts.path(), scripts.path());
        maxchat::scripting::ScriptPermissions permissions; permissions.loadModules = true;
        QVERIFY(engine.load(script, permissions));
        QCOMPARE(host.echoes, (QStringList{QStringLiteral("4"), QStringLiteral("3")}));
    }

    void filePermissionDoesNotExposeProcessStdio() {
        QTemporaryDir scripts;
        const QString script = writeScript(QDir(scripts.path()), QStringLiteral("stdio.lua"),
            QStringLiteral("function on_load(api) api.echo(tostring(io.stdin == nil and io.read == nil and io.stdout == nil)) end"));
        FakeHost host;
        LuaEngine engine(&host, scripts.path(), scripts.path());
        maxchat::scripting::ScriptPermissions permissions; permissions.writeFiles = true;
        QVERIFY(engine.load(script, permissions));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("true")});
    }

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

    void startupLoadAllRequiresLoadAtStart() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QDir d(dir.path());
        writeScript(d, QStringLiteral("auto.lua"), QStringLiteral("function on_load(a) end\n"));
        writeScript(d, QStringLiteral("manual.lua"), QStringLiteral("function on_load(a) end\n"));

        QHash<QString, maxchat::scripting::ScriptPermissions> perms;
        maxchat::scripting::ScriptPermissions autoPerms;
        autoPerms.loadAtStart = true;
        perms.insert(QStringLiteral("auto"), autoPerms);
        perms.insert(QStringLiteral("manual"), maxchat::scripting::ScriptPermissions{});

        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QCOMPARE(engine.loadAll(perms, true), 1);
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
        QVERIFY(engine.load(path, ircPerms()));
        QCOMPARE(host.says, QStringList{QStringLiteral("#c|hi")});
        QCOMPARE(host.inserts, QStringList{QStringLiteral("draft")});
        QCOMPARE(host.notifies, QStringList{QStringLiteral("T|B")});
        QCOMPARE(host.echoes, QStringList{QStringLiteral("me/#chan/synIRC")});
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
        QVERIFY(engine.load(path, ircPerms()));
        QCOMPARE(host.raws, QStringList{QStringLiteral("PING xyz")});
        QCOMPARE(host.echoes, QStringList{QStringLiteral("2/alice")});
    }

    void ircSendGatedWithoutPermission() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Without the ircSend permission, api.say/send_raw/mc_send must be nil
        // (not registered) so a script cannot emit onto the network.
        const QString path =
            writeScript(QDir(dir.path()), QStringLiteral("gated.lua"),
                        QStringLiteral("function on_load(api)\n"
                                       "  api.echo(tostring(api.say)..'/'..tostring(api.send_raw)\n"
                                       "           ..'/'..tostring(api.mc_send)..'/'\n"
                                       "           ..tostring(api.mc_reply))\n"
                                       "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path)); // default perms: ircSend off
        QCOMPARE(host.echoes, QStringList{QStringLiteral("nil/nil/nil/nil")});
        QVERIFY(host.raws.isEmpty());
    }

    void apiMcDataCalls() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeScript(
            QDir(dir.path()), QStringLiteral("mc.lua"),
            QStringLiteral("function on_load(api)\n"
                           "  api.echo(tostring(api.mc_send('alice', 'BBS', 'hello', 'hi')))\n"
                           "  api.echo(tostring(api.mc_reply('alice', 'bbs', 'STATUS', 'ok')))\n"
                           "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path, ircPerms()));
        QCOMPARE(host.mcData,
                 QStringList({
                     QStringLiteral("|alice|BBS|hello|hi|privmsg"),
                     QStringLiteral("|alice|bbs|STATUS|ok|notice"),
                 }));
        QCOMPARE(host.echoes, QStringList({QStringLiteral("true"), QStringLiteral("true")}));
    }

    void dispatchPassesMcDataHookArgs() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path =
            writeScript(QDir(dir.path()), QStringLiteral("mc_hook.lua"),
                        QStringLiteral("function on_mc_data(api, network, target, nick, service, verb, payload, notice)\n"
                                       "  api.echo(network..'|'..target..'|'..nick..'|'..service..'|'..verb..'|'..payload..'|'..tostring(notice))\n"
                                       "  return true\n"
                                       "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("STATUS"), QStringLiteral("ok"), true}));
        QCOMPARE(host.echoes,
                 QStringList{QStringLiteral("synIRC|bob|alice|bbs|STATUS|ok|true")});
    }

    void apiTerminalCallsAreScriptScoped() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeScript(
            QDir(dir.path()), QStringLiteral("term.lua"),
            QStringLiteral("function on_load(api)\n"
                           "  api.echo(tostring(api.terminal_open('main', 'Retro-BBS', 'free', 100, 30)))\n"
                           "  api.terminal_status('main', 'CONNECT: Retro-BBS')\n"
                           "  api.terminal_prompt('main', 'bbs> ')\n"
                           "  api.terminal_write('main', 'hello')\n"
                           "  api.echo(tostring(api.terminal_frame('main', 'CP0101W02Hi')))\n"
                           "  local cols, rows = api.terminal_size('main')\n"
                           "  api.echo(cols..'x'..rows)\n"
                           "  api.terminal_profile('main', 'c64')\n"
                           "  api.terminal_fit('main', 'integer')\n"
                           "  api.echo(api.terminal_hotspot('menu', 'Menu'))\n"
                           "  api.terminal_clear('main')\n"
                           "  api.terminal_close('main')\n"
                           "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(path));
        QCOMPARE(host.terminals,
                 QStringList({
                     QStringLiteral("open|term|main|Retro-BBS|free|100x30"),
                     QStringLiteral("status|term|main|CONNECT: Retro-BBS"),
                     QStringLiteral("prompt|term|main|bbs> "),
                     QStringLiteral("write|term|main|hello"),
                     QStringLiteral("frame|term|main|CP0101W02Hi"),
                     QStringLiteral("profile|term|main|c64|80x25"),
                     QStringLiteral("fit|term|main|integer"),
                     QStringLiteral("clear|term|main"),
                     QStringLiteral("close|term|main"),
                 }));
        QCOMPARE(host.echoes,
                 QStringList({
                     QStringLiteral("true"),
                     QStringLiteral("true"),
                     QStringLiteral("100x30"),
                     QStringLiteral("<hotspot action=\"menu\">Menu</hotspot>"),
                 }));
    }

    void dispatchToScriptRoutesTerminalHooks() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString first =
            writeScript(QDir(dir.path()), QStringLiteral("first.lua"),
                        QStringLiteral("function on_terminal_input(api, id, text)\n"
                                       "  api.echo('first:'..id..':'..text)\n"
                                       "  return true\n"
                                       "end\n"));
        const QString second =
            writeScript(QDir(dir.path()), QStringLiteral("second.lua"),
                        QStringLiteral("function on_terminal_input(api, id, text)\n"
                                       "  api.echo('second:'..id..':'..text)\n"
                                       "  return true\n"
                                       "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(first));
        QVERIFY(engine.load(second));

        QVERIFY(engine.dispatchToScript(QStringLiteral("second"),
                                        QStringLiteral("on_terminal_input"),
                                        QStringLiteral("synIRC"),
                                        {QStringLiteral("main"), QStringLiteral("help")}));

        QCOMPARE(host.echoes, QStringList{QStringLiteral("second:main:help")});
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

    void bundledExamplesAllLoad() {
#ifdef MAXCHAT_EXAMPLE_SCRIPTS_DIR
        const QString dir = QStringLiteral(MAXCHAT_EXAMPLE_SCRIPTS_DIR);
        QTemporaryDir data;
        QVERIFY(data.isValid());
        FakeHost host;
        LuaEngine engine(&host, dir, data.path());
        const QFileInfoList files =
            QDir(dir).entryInfoList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
        QVERIFY(!files.isEmpty());
        for (const QFileInfo& fi : files) {
            QVERIFY2(engine.load(fi.absoluteFilePath()), qPrintable(fi.fileName()));
        }
#else
        QSKIP("example scripts dir not defined");
#endif
    }

    void bundledBbsScriptStartsAndDials() {
#ifdef MAXCHAT_EXAMPLE_SCRIPTS_DIR
        const QString dir = QStringLiteral(MAXCHAT_EXAMPLE_SCRIPTS_DIR);
        const QString path = QDir(dir).filePath(QStringLiteral("bbs.lua"));
        QVERIFY2(QFile::exists(path), qPrintable(path));

        QTemporaryDir data;
        QVERIFY(data.isValid());
        FakeHost host;
        LuaEngine engine(&host, dir, data.path());
        QVERIFY(engine.load(path, ircPerms()));

        QVERIFY(engine.dispatch(QStringLiteral("on_command"), QStringLiteral("synIRC"),
                                {QStringLiteral("bbsserve"), QString()}));
        QVERIFY(engine.dispatch(QStringLiteral("on_command"), QStringLiteral("synIRC"),
                                {QStringLiteral("bbs"), QStringLiteral("alice retro-bbs")}));

        QVERIFY(host.terminals.contains(
            QStringLiteral("open|bbs|server|Retro-BBS Server|free|80x25")));
        QVERIFY(host.terminals.contains(
            QStringLiteral("open|bbs|client:alice:retro-bbs|Retro-BBS - alice|ibm-vga|80x25")));
        QVERIFY(host.mcData.contains(
            QStringLiteral("synIRC|alice|bbs|HELLO|bbs_id=retro-bbs;cols=80;rows=25;profile=ibm-vga;caps=T,S,B1|privmsg")));

        const auto containsMcDataPrefix = [&host](const QString& prefix) {
            for (const QString& line : host.mcData) {
                if (line.startsWith(prefix)) {
                    return true;
                }
            }
            return false;
        };
        const auto containsTerminalPrefix = [&host](const QString& prefix) {
            for (const QString& line : host.terminals) {
                if (line.startsWith(prefix)) {
                    return true;
                }
            }
            return false;
        };

        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("HELLO"),
                                 QStringLiteral("bbs_id=retro-bbs;cols=80;rows=25;profile=ibm-vga;caps=T,S,B1"),
                                 false}));
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("INPUT"), QStringLiteral("sir_iw"), false}));
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("INPUT"), QStringLiteral("bbsiscool"), false}));
        // Framed pages span multiple chunks; static parts are "main" or "main#N".
        QVERIFY(containsMcDataPrefix(QStringLiteral("synIRC|alice|bbs|S|main")));

        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("INPUT"), QStringLiteral("1"), false}));
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("INPUT"), QStringLiteral("B"), false}));
        QVERIFY(containsMcDataPrefix(QStringLiteral("synIRC|alice|bbs|RP|main")));

        // Pic gallery: 6 opens it, 1 sends bitmap chunks (B) + insert (I),
        // any key returns to the gallery list.
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("INPUT"), QStringLiteral("6"), false}));
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("INPUT"), QStringLiteral("1"), false}));
        QVERIFY(containsMcDataPrefix(QStringLiteral("synIRC|alice|bbs|B|earth 160 50 ")));
        QVERIFY(containsMcDataPrefix(QStringLiteral("synIRC|alice|bbs|I|earth ")));
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("INPUT"), QStringLiteral("x"), false}));
        QVERIFY(containsMcDataPrefix(QStringLiteral("synIRC|alice|bbs|S|pics")));
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("INPUT"), QStringLiteral("B"), false}));

        // Client side: receiving B then I decodes and renders a local frame.
        // frame_hash("F0F0") per the script's djb-33 mod 2^32 = 002737EC.
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("B"),
                                 QStringLiteral("t 4 4 002737EC raw1 1/1 F0F0"), false}));
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("I"), QStringLiteral("t 002737EC P0101"),
                                 false}));
        QVERIFY(containsTerminalPrefix(
            QStringLiteral("frame|bbs|client:alice:retro-bbs|CP0101AF0W")));

        // Z85 armor: "[BczT" decodes to F0 F0 00 00 (same 4x4 image, padded).
        // frame_hash("[BczT") = 06949C0E.
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("B"),
                                 QStringLiteral("tz 4 4 06949C0E raw1z 1/1 [BczT"), false}));
        const int framesBefore = host.terminals.size();
        QVERIFY(engine.dispatch(QStringLiteral("on_mc_data"), QStringLiteral("synIRC"),
                                {QStringLiteral("synIRC"), QStringLiteral("bob"),
                                 QStringLiteral("alice"), QStringLiteral("bbs"),
                                 QStringLiteral("I"), QStringLiteral("tz 06949C0E P0101"),
                                 false}));
        QVERIFY(host.terminals.size() > framesBefore);
        QVERIFY(host.terminals.last().startsWith(
            QStringLiteral("frame|bbs|client:alice:retro-bbs|CP0101AF0W")));

        // Every frame chunk the script ever SENT must parse with the real C++
        // grid parser — the fake host only records them, so without this a
        // malformed op stream ships and the client shows "bad static frame".
        for (const QString& line : host.mcData) {
            // record format: net|target|service|verb|payload|type — the payload
            // itself may contain '|', so take everything between the 4th and
            // the LAST separator.
            const QString verb = line.section(QLatin1Char('|'), 3, 3);
            QString ops = line.section(QLatin1Char('|'), 4, -2);
            if (verb == QStringLiteral("S")) {
                // "S <part> <hash> <ops>"
                const qsizetype second =
                    ops.indexOf(QLatin1Char(' '), ops.indexOf(QLatin1Char(' ')) + 1);
                ops = second >= 0 ? ops.mid(second + 1) : QString();
            } else if (verb != QStringLiteral("T") && verb != QStringLiteral("D")) {
                continue;
            }
            QVector<maxchat::ui::TerminalFrame::Op> parsed;
            QString error;
            QVERIFY2(maxchat::ui::TerminalFrame::parse(ops, &parsed, &error),
                     qPrintable(error + QStringLiteral(" len=%1 ops=[").arg(ops.size()) + ops +
                                QStringLiteral("]")));
        }

        QVERIFY(engine.dispatch(QStringLiteral("on_command"), QStringLiteral("synIRC"),
                                {QStringLiteral("bbscache"), QString()}));
        QVERIFY(!host.echoes.isEmpty());
        QVERIFY(host.echoes.last().contains(QStringLiteral("fallback T=")));
        QVERIFY(engine.dispatch(QStringLiteral("on_command"), QStringLiteral("synIRC"),
                                {QStringLiteral("bbscache"), QStringLiteral("clear")}));
        QVERIFY(host.echoes.contains(QStringLiteral("[bbscache] cleared local static frame cache")));
        QVERIFY(engine.dispatch(QStringLiteral("on_command"), QStringLiteral("synIRC"),
                                {QStringLiteral("bbscache"), QString()}));
        QVERIFY(host.echoes.contains(QStringLiteral("[bbscache] local static frames=0")));
        QVERIFY(containsTerminalPrefix(QStringLiteral("write|bbs|server|Cache: static sent=")));
#else
        QSKIP("example scripts dir not defined");
#endif
    }

    void permsGateFileExecModules() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // With all permissions off (default), io/os.execute/require are absent.
        const QString locked =
            writeScript(QDir(dir.path()), QStringLiteral("probe.lua"),
                        QStringLiteral("function on_load(api)\n"
                                       "  api.echo(tostring(io)..'/'..tostring(os.execute)..'/'\n"
                                       "           ..tostring(require))\n"
                                       "end\n"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(locked));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("nil/nil/nil")});
    }

    void readPermJailsToAllowedDirs() {
        QTemporaryDir scripts;
        QTemporaryDir allowed;
        QTemporaryDir off; // NOT in the allow-list
        QVERIFY(scripts.isValid() && allowed.isValid() && off.isValid());
        // A readable file in each location.
        QFile a(QDir(allowed.path()).filePath(QStringLiteral("ok.txt")));
        QVERIFY(a.open(QIODevice::WriteOnly));
        a.write("yes");
        a.close();
        QFile b(QDir(off.path()).filePath(QStringLiteral("secret.txt")));
        QVERIFY(b.open(QIODevice::WriteOnly));
        b.write("no");
        b.close();

        const QString okPath = QDir(allowed.path()).filePath(QStringLiteral("ok.txt"));
        const QString offPath = QDir(off.path()).filePath(QStringLiteral("secret.txt"));
        const QString script = writeScript(
            QDir(scripts.path()), QStringLiteral("reader.lua"),
            QStringLiteral("function on_load(api)\n"
                           "  local f = io.open([[%1]], 'r')\n"
                           "  api.echo(f and (f:read('a')) or 'DENIED')\n"
                           "  if f then f:close() end\n"
                           "  local g = io.open([[%2]], 'r')\n"
                           "  api.echo(g and 'LEAK' or 'DENIED')\n"
                           "  if g then g:close() end\n"
                           "end\n")
                .arg(okPath, offPath));

        maxchat::scripting::ScriptPermissions perms;
        perms.readFiles = true;
        perms.allowedDirs = {allowed.path()};
        FakeHost host;
        LuaEngine engine(&host, scripts.path(), scripts.path());
        QVERIFY(engine.load(script, perms));
        QCOMPARE(host.echoes, (QStringList{QStringLiteral("yes"), QStringLiteral("DENIED")}));
    }

    void writeDeniedWithoutWritePerm() {
        QTemporaryDir scripts;
        QTemporaryDir allowed;
        QVERIFY(scripts.isValid() && allowed.isValid());
        const QString target = QDir(allowed.path()).filePath(QStringLiteral("out.txt"));
        const QString script =
            writeScript(QDir(scripts.path()), QStringLiteral("writer.lua"),
                        QStringLiteral("function on_load(api)\n"
                                       "  local f = io.open([[%1]], 'w')\n"
                                       "  api.echo(f and 'WROTE' or 'DENIED')\n"
                                       "  if f then f:close() end\n"
                                       "end\n")
                            .arg(target));
        // read granted (so io exists) but NOT write.
        maxchat::scripting::ScriptPermissions perms;
        perms.readFiles = true;
        perms.allowedDirs = {allowed.path()};
        FakeHost host;
        LuaEngine engine(&host, scripts.path(), scripts.path());
        QVERIFY(engine.load(script, perms));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("DENIED")});
        QVERIFY(!QFile::exists(target));
    }

    void networkPermGatesHttpGet() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString script =
            writeScript(QDir(dir.path()), QStringLiteral("net.lua"),
                        QStringLiteral("function on_load(api)\n"
                                       "  api.echo(api.http_get and api.http_get('x') or 'NOFUNC')\n"
                                       "end\n"));
        // Off by default → api.http_get absent.
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        QVERIFY(engine.load(script));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("NOFUNC")});
        // On → present, returns the host body.
        maxchat::scripting::ScriptPermissions perms;
        perms.network = true;
        FakeHost host2;
        LuaEngine engine2(&host2, dir.path(), dir.path());
        QVERIFY(engine2.load(script, perms));
        QCOMPARE(host2.echoes, QStringList{QStringLiteral("BODY")});
    }

    void bridgeErrorsReleaseCppResources() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // A regular file prevents creation of the per-script data directory.
        QFile blocker(QDir(dir.path()).filePath(QStringLiteral("errors")));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        const QString path = writeScript(QDir(dir.path()), QStringLiteral("errors.lua"),
            QStringLiteral(R"lua(
function on_load(api)
  local calls = {
    function() api.say('target', {}) end,
    function() api.notify('title', {}) end,
    function() api.mc_send('target', 'service', {}) end,
    function() api.terminal_open('id', 'title', 'free', {}) end,
    function() api.terminal_write('id', {}) end,
    function() api.terminal_profile('id', 'free', {}) end,
    function() api.append_file('file.txt', {}) end,
    function() api.append_file('file.txt', 'content') end,
    function() api.append_file('..', 'content') end,
    function() api.set('key', {}) end,
    function() api.set('key', 'value') end,
    function() io.open(api.data_dir(), 'r?') end,
  }
  for i = 1, 20 do
    for index, call in ipairs(calls) do assert(not pcall(call), tostring(index)) end
  end
  api.echo('errors caught')
end
)lua"));
        FakeHost host;
        LuaEngine engine(&host, dir.path(), dir.path());
        auto perms = ircPerms();
        perms.readFiles = true;
        QVERIFY(engine.load(path, perms));
        QCOMPARE(host.echoes, QStringList{QStringLiteral("errors caught")});
        QVERIFY(host.says.isEmpty());
        QVERIFY(host.notifies.isEmpty());
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
