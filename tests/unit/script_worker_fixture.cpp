// Authenticated-but-hostile helper fixture. Never included in application packages.
#include "scripting/ScriptProtocol.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QLocalSocket>
#include <QProcess>
#include <QtEndian>
#include <QThread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace maxchat::scripting;
namespace {
bool next(QLocalSocket& socket, QByteArray& buffer, QJsonObject& message) {
    QElapsedTimer elapsed; elapsed.start(); QString error;
    while (elapsed.elapsed() < 5000) {
        if (protocol::receive(&socket,buffer,message,error)) return true;
        if (!error.isEmpty() || socket.state()!=QLocalSocket::ConnectedState) return false;
        socket.waitForReadyRead(50);
    }
    return false;
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc,argv);
    if (argc<3) return 2;
    const QString server=QString::fromLocal8Bit(argv[1]);
    const QString mode=qEnvironmentVariable("MAXCHAT_TEST_WORKER_MODE");
    bool foreignRejected=false;
    if (mode==QLatin1String("wrong-peer") && argc==3) {
        QProcess foreign;
#ifdef Q_OS_WIN
        foreign.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
#endif
        foreign.start(QCoreApplication::applicationFilePath(),{server,QString::fromLocal8Bit(argv[2]),QStringLiteral("foreign")});
        if (!foreign.waitForFinished(3000) || foreign.exitCode()!=0) return 4;
        foreignRejected=true;
    }
    QLocalSocket socket;
#ifdef Q_OS_LINUX
    socket.setSocketOptions(QLocalSocket::AbstractNamespaceOption);
#endif
    socket.connectToServer(server);
    if (!socket.waitForConnected(3000)) return 5;
    if (argc==4) {
        socket.waitForReadyRead(1500);
        return socket.state()==QLocalSocket::UnconnectedState && socket.bytesAvailable()==0 ? 0 : 6;
    }
    QByteArray buffer; QJsonObject message;
    if (!next(socket,buffer,message) || message.value(QStringLiteral("type"))!=QJsonValue(QStringLiteral("init"))) return 7;
    protocol::send(&socket,{{QStringLiteral("type"),QStringLiteral("ready")}});
    int activity=0, hostId=0;
    while (next(socket,buffer,message)) {
        const int id=message.value(QStringLiteral("id")).toInt();
        const bool load=message.value(QStringLiteral("type"))==QJsonValue(QStringLiteral("load"));
        protocol::send(&socket,{{QStringLiteral("type"),QStringLiteral("begin")},{QStringLiteral("activity"),++activity},{QStringLiteral("command"),id}});
        if (load) {
            if (mode==QLatin1String("native-hang")) { for (;;) QThread::yieldCurrentThread(); }
            if (mode==QLatin1String("oversized-frame")) {
                QByteArray header(4,'\0');qToBigEndian(protocol::MaximumFrame+1,header.data());socket.write(header);socket.flush();
                socket.waitForDisconnected(3000);return 0;
            }
            QJsonObject call{{QStringLiteral("type"),QStringLiteral("host")},{QStringLiteral("id"),++hostId},
                {QStringLiteral("method"),QStringLiteral("echo")},{QStringLiteral("args"),QJsonArray{QStringLiteral("private channel")}}};
            if (mode==QLatin1String("forbidden-irc")) {call.insert(QStringLiteral("method"),QStringLiteral("raw"));call.insert(QStringLiteral("args"),QJsonArray{QStringLiteral("PRIVMSG #fixture :forged")});}
            if (mode==QLatin1String("bad-type")) call.insert(QStringLiteral("args"),QJsonArray{42});
            if (mode==QLatin1String("identity")) {
                call.insert(QStringLiteral("method"),QStringLiteral("terminal-open"));
                call.insert(QStringLiteral("args"),QJsonArray{QStringLiteral("panel"),QStringLiteral("fixture"),QStringLiteral("free"),80,25});
                call.insert(QStringLiteral("script"),QStringLiteral("victim"));
            }
            if (mode==QLatin1String("stdout-forgery")) {
                auto forged=call;forged.insert(QStringLiteral("method"),QStringLiteral("raw"));
                QFile output;output.open(stdout,QIODevice::WriteOnly);output.write(protocol::frame(forged));output.flush();
            }
            if (foreignRejected) call.insert(QStringLiteral("args"),QJsonArray{QStringLiteral("foreign peer rejected")});
            protocol::send(&socket,call);
            QJsonObject reply;if (!next(socket,buffer,reply)) return 8;
        }
        protocol::send(&socket,{{QStringLiteral("type"),QStringLiteral("end")},{QStringLiteral("activity"),activity},{QStringLiteral("command"),id}});
        protocol::send(&socket,{{QStringLiteral("type"),QStringLiteral("result")},{QStringLiteral("id"),id},{QStringLiteral("value"),true}});
    }
    return 0;
}
