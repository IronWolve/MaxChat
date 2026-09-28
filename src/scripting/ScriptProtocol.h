#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
class QLocalSocket;
namespace maxchat::scripting::protocol {
constexpr quint32 MaximumFrame = 4 * 1024 * 1024;
constexpr quint32 MaximumWorkerFrame = 1024 * 1024;
constexpr int ExecutionMilliseconds = 2000;
constexpr int HostWaitMilliseconds = 12000;
constexpr int MaximumWorkers = 16;
constexpr quint64 MaximumProcessBytes = 256ULL * 1024 * 1024;
constexpr quint64 MaximumAggregateBytes = 1024ULL * 1024 * 1024;
// Framed JSON has a bounded header, body and parser depth. No native pointers,
// QVariant metatype deserialization, script-selected identity or stdout framing.
bool validScriptName(const QString& name);
QByteArray frame(const QJsonObject& message);
bool send(QLocalSocket* socket, const QJsonObject& message);
// Returns one complete object; error reports malformed/oversized input.
bool receive(QLocalSocket* socket, QByteArray& buffer, QJsonObject& message, QString& error, quint32 maximum = MaximumFrame);
bool peerIs(QLocalSocket* socket, qint64 processId, bool serverSide);
quint64 residentBytes(qint64 processId);
double cpuSeconds(qint64 processId);
}
