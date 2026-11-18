// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "registry.hpp"

#include <QString>

namespace kalm {

// Mask last 3 octets of MAC for privacy in issue reports (e.g. "74:D7:13:XX:XX:XX")
QString maskMacForReport(const QString& mac);

// Connects to the target Bluetooth device (read-only), discovers active RFCOMM UUIDs/channels,
// auto-probes Vivo GAIA, realme/OPPO/OnePlus (0xAA), and Airoha RACE protocols, and returns a
// privacy-safe Markdown report ready for GitHub Issues.
QString generateDiagnosticReport(DeviceRegistry& registry,
                                 const QString& deviceQuery = {},
                                 int listenSeconds = 0);

}  // namespace kalm
