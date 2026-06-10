#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

namespace maxchat::core {

using NetworkConfig = QVariantMap;
using NetworkConfigList = QList<NetworkConfig>;

[[nodiscard]] QString networkKey(const NetworkConfig& network);
[[nodiscard]] NetworkConfigList mergeImportedNetworks(const NetworkConfigList& importedNetworks,
                                                      const NetworkConfigList& baseNetworks);

} // namespace maxchat::core
