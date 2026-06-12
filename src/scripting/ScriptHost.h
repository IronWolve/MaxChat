#pragma once

#include <QString>
#include <QStringList>
#include <QSize>

namespace maxchat::scripting {

// The narrow callback surface the Lua `api` table calls into. MainWindow
// implements it; unit tests use a fake. Keeping this interface free of any Qt
// widget / Lua dependency is what lets LuaEngine be tested headless.
//
// `network` is the network a hook is firing for (so `api.echo` lands in the
// right buffer). An empty network means "the active one".
class ScriptHost {
  public:
    virtual ~ScriptHost() = default;

    virtual void scriptEcho(const QString& network, const QString& text) = 0;
    virtual void scriptSay(const QString& network, const QString& target,
                           const QString& text) = 0;
    virtual void scriptSendRaw(const QString& network, const QString& line) = 0;
    virtual void scriptInsertInput(const QString& text) = 0;
    virtual void scriptNotify(const QString& title, const QString& text) = 0;
    virtual bool scriptMcData(const QString& network, const QString& target,
                              const QString& service, const QString& verb,
                              const QString& payload, bool notice) = 0;
    virtual bool scriptTerminalOpen(const QString& scriptName, const QString& id,
                                    const QString& title, const QString& profile,
                                    int cols, int rows) = 0;
    virtual void scriptTerminalClose(const QString& scriptName, const QString& id) = 0;
    virtual void scriptTerminalClear(const QString& scriptName, const QString& id) = 0;
    virtual void scriptTerminalWrite(const QString& scriptName, const QString& id,
                                     const QString& text) = 0;
    virtual void scriptTerminalStatus(const QString& scriptName, const QString& id,
                                      const QString& text) = 0;
    virtual void scriptTerminalPrompt(const QString& scriptName, const QString& id,
                                      const QString& text) = 0;
    [[nodiscard]] virtual QSize scriptTerminalSize(const QString& scriptName,
                                                   const QString& id) = 0;
    virtual void scriptTerminalProfile(const QString& scriptName, const QString& id,
                                       const QString& profile, int cols, int rows) = 0;
    virtual void scriptTerminalFit(const QString& scriptName, const QString& id,
                                   const QString& mode) = 0;
    [[nodiscard]] virtual QString scriptTerminalHotspot(const QString& actionId,
                                                        const QString& label) = 0;

    [[nodiscard]] virtual QString scriptMe(const QString& network) = 0;
    [[nodiscard]] virtual QString scriptTarget() = 0;
    [[nodiscard]] virtual QString scriptNetwork() = 0;
    [[nodiscard]] virtual QStringList scriptChannels(const QString& network) = 0;
    [[nodiscard]] virtual QStringList scriptNicks(const QString& network,
                                                  const QString& target) = 0;
    // Synchronous HTTP GET for api.http_get (only reached when the network
    // permission is granted). Returns the body, or an empty string on failure.
    [[nodiscard]] virtual QString scriptHttpGet(const QString& url) = 0;
};

} // namespace maxchat::scripting
