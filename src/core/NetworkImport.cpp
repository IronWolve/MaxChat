#include "core/NetworkImport.h"

#include <QHash>
#include <QSet>
#include <QString>

namespace maxchat::core {

namespace {

const QSet<QString>& catalogNetworkFields() {
    static const QSet<QString> fields = {
        QStringLiteral("name"), QStringLiteral("host"),    QStringLiteral("port"),
        QStringLiteral("tls"),  QStringLiteral("website"), QStringLiteral("servers"),
    };
    return fields;
}

} // namespace

QString networkKey(const NetworkConfig& network) {
    const QString name = network.value(QStringLiteral("name")).toString().trimmed().toCaseFolded();
    if (!name.isEmpty()) {
        return name;
    }
    return network.value(QStringLiteral("host")).toString().trimmed().toCaseFolded();
}

NetworkConfigList mergeImportedNetworks(const NetworkConfigList& importedNetworks,
                                        const NetworkConfigList& baseNetworks) {
    QHash<QString, NetworkConfig> baseByKey;
    for (const NetworkConfig& network : baseNetworks) {
        const QString key = networkKey(network);
        if (!key.isEmpty()) {
            baseByKey.insert(key, network);
        }
    }

    NetworkConfigList merged;
    QSet<QString> usedBaseKeys;

    for (const NetworkConfig& importedNetwork : importedNetworks) {
        const QString key = networkKey(importedNetwork);
        if (!key.isEmpty() && baseByKey.contains(key) && !usedBaseKeys.contains(key)) {
            NetworkConfig network = baseByKey.value(key);
            for (auto it = importedNetwork.cbegin(); it != importedNetwork.cend(); ++it) {
                if (!catalogNetworkFields().contains(it.key())) {
                    network.insert(it.key(), it.value());
                }
            }
            merged.append(network);
            usedBaseKeys.insert(key);
        } else {
            merged.append(importedNetwork);
        }
    }

    for (const NetworkConfig& baseNetwork : baseNetworks) {
        const QString key = networkKey(baseNetwork);
        if (key.isEmpty() || !usedBaseKeys.contains(key)) {
            merged.append(baseNetwork);
        }
    }

    return merged;
}

} // namespace maxchat::core
