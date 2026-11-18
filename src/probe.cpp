// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.hpp"

#include "huawei.hpp"
#include "protocol.hpp"
#include "transport.hpp"

#include <QElapsedTimer>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <vector>

namespace kalm {

namespace {

QString hexSpaced(const QByteArray& bytes) {
    if (bytes.isEmpty()) return QStringLiteral("(empty)");
    QStringList out;
    out.reserve(bytes.size());
    for (char c : bytes) {
        out.append(QStringLiteral("%1").arg(static_cast<uint8_t>(c), 2, 16, QLatin1Char('0')));
    }
    return out.join(u' ');
}

bool isNonRfcommStandardUuid(const QString& low) {
    // Standard Bluetooth SIG audio / BLE GATT / PnP / SDP UUIDs (...-0000-1000-8000-00805f9b34fb)
    // EXCEPT 00001101 (Serial Port Profile / SPP), which IS RFCOMM!
    if (!low.endsWith(u"-0000-1000-8000-00805f9b34fb")) return false;
    return low != QLatin1String(kStandardSppUuid);
}

QStringList buildCandidateControlUuids(const QStringList& devUuids) {
    QStringList prioritized;
    QStringList customOthers;
    bool hasSpp = false;

    for (const QString& raw : devUuids) {
        const QString low = raw.trimmed().toLower();
        if (low.isEmpty() || isNonRfcommStandardUuid(low)) continue;
        if (low == QLatin1String(kStandardSppUuid)) {
            hasSpp = true;
        } else if (low == QLatin1String(kVivoGaiaUuid) ||
                   low == QLatin1String(kRealmeOppoTlUuid) ||
                   low == QLatin1String(kOppoHeyMelodyUuid) ||
                   low == QLatin1String(kOppoHeyMelodyAltUuid)) {
            if (!prioritized.contains(low)) prioritized.append(low);
        } else if (low != QLatin1String(kGattServiceUuid)) {
            if (!customOthers.contains(low)) customOthers.append(low);
        }
    }

    QStringList result = prioritized;
    result.append(customOthers);
    if (hasSpp) result.append(QString::fromLatin1(kStandardSppUuid));
    return result;
}

QByteArray drainRawBytes(SocketHandle fd, int waitMs) {
    QByteArray out;
    QElapsedTimer t;
    t.start();
    char buf[2048];
    while (t.elapsed() < waitMs) {
        const int rem = std::max(20, static_cast<int>(waitMs - t.elapsed()));
        const int pr = pollSocketRead(fd, rem);
        if (pr <= 0) break;
        const qint64 n = recvSocket(fd, buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, static_cast<qsizetype>(n));
    }
    return out;
}

QString formatVivoFrameSummary(const Frame& f) {
    if (f.vendor == kVivoVendor && f.command == kCmdReportMultiConnect) {
        if (f.payload.size() >= 2) {
            return QStringLiteral("v=%1 cmd=0x8249 status=0x%2 count=%3 (hosts redacted)")
                .arg(f.version)
                .arg(static_cast<uint8_t>(f.payload[0]), 2, 16, QLatin1Char('0'))
                .arg(static_cast<uint8_t>(f.payload[1]));
        }
        return QStringLiteral("v=%1 cmd=0x8249 (redacted)").arg(f.version);
    }
    if (f.vendor == kVivoVendor && f.command == kCmdReportTelemetry) {
        EarbudState tmp;
        applyFrameToState(f, tmp);
        return QStringLiteral("v=%1 cmd=0x8224 telemetry fw=%2")
            .arg(f.version)
            .arg(tmp.firmware.isEmpty() ? QStringLiteral("n/a") : tmp.firmware);
    }
    return QStringLiteral("v=%1 vendor=0x%2 cmd=0x%3 payload=[%4]")
        .arg(f.version)
        .arg(f.vendor, 4, 16, QLatin1Char('0'))
        .arg(f.command, 4, 16, QLatin1Char('0'))
        .arg(hexSpaced(f.payload));
}

QString formatRealmeOppoFrameSummary(const RealmeOppoFrame& f) {
    // 0x8112 = MultiConnect host list response -> redact host MACs/names
    if (f.command == 0x8112) {
        return QStringLiteral("cmd=0x8112 seq=%1 (multipoint hosts redacted, len=%2)")
            .arg(f.seq)
            .arg(f.payload.size());
    }
    return QStringLiteral("cmd=0x%1 seq=%2 payload=[%3]")
        .arg(f.command, 4, 16, QLatin1Char('0'))
        .arg(f.seq)
        .arg(hexSpaced(f.payload));
}

std::optional<BluezDevice> pickProbeDevice(DeviceRegistry& reg, const QString& query) {
    const auto all = listBluezDevices(false, false);
    if (!query.trimmed().isEmpty()) {
        const QString q = query.trimmed();
        for (const auto& b : all) {
            if (b.mac.compare(q, Qt::CaseInsensitive) == 0 ||
                b.name.compare(q, Qt::CaseInsensitive) == 0) {
                return b;
            }
        }
        for (const auto& b : all) {
            if (b.name.contains(q, Qt::CaseInsensitive) || b.mac.contains(q, Qt::CaseInsensitive)) {
                return b;
            }
        }
        if (auto saved = reg.resolveQuery(q)) {
            if (auto b = findBluezByMac(saved->mac)) return b;
        }
        return std::nullopt;
    }

    if (auto active = reg.getActive()) {
        if (auto b = findBluezByMac(active->mac)) {
            if (b->connected) return b;
        }
    }
    for (const auto& b : all) {
        if (b.connected && b.isSupportedTws()) return b;
    }
    for (const auto& b : all) {
        if (b.connected && b.isAudioDevice()) return b;
    }
    if (auto active = reg.getActive()) {
        if (auto b = findBluezByMac(active->mac)) return b;
    }
    for (const auto& b : all) {
        if (b.isAudioDevice()) return b;
    }
    return std::nullopt;
}

bool runRealmeOppoProbe(SocketHandle fd, QTextStream& out) {
    struct TlSpec {
        const char* label;
        uint16_t cmd;
        QByteArray payload;
    };

    const std::vector<TlSpec> specs = {
        {"GetCapabilities", 0x0100, {}},
        {"GetFirmwareVersion", 0x0105, {}},
        {"GetBattery (0x0106)", 0x0106, {}},
        {"GetNotifyCaps (0x0200)", 0x0200, {}},
        {"QueryNoiseNotify (0x0202 [03])", 0x0202, QByteArray("\x03", 1)},
        {"GetNoiseReduction (0x010B)", 0x010B, {}},
        {"GetSwitchFeatures", 0x010D, QByteArray("\x04\x05\x06\x07", 4)},
        {"GetStatusFeature", 0x0129, QByteArray("\x01\x02\x03\x04", 4)},
    };

    // Quick test with 0x0100 or 0x0106 before running all queries
    sendAll(fd, buildRealmeOppoPacket(specs[0].cmd, 0, specs[0].payload));
    QByteArray firstRx = drainRawBytes(fd, 350);
    if (firstRx.isEmpty()) {
        sendAll(fd, buildRealmeOppoPacket(0x0106, 0, {}));
        firstRx = drainRawBytes(fd, 350);
    }
    if (firstRx.isEmpty()) {
        return false;
    }

    out << "\n### OPPO / OnePlus / realme Link (`0xAA` Protocol) Probe\n";
    uint8_t seq = 1;
    // Log initial reply
    const auto initParsed = parseRealmeOppoFrames(firstRx);
    if (!initParsed.isEmpty()) {
        for (const auto& f : initParsed) {
            out << "- Initial reply: `" << formatRealmeOppoFrameSummary(f) << "`\n";
        }
    } else {
        out << "- Initial raw reply: `[" << hexSpaced(firstRx) << "]`\n";
    }

    for (const auto& sp : specs) {
        sendAll(fd, buildRealmeOppoPacket(sp.cmd, seq++, sp.payload));
        const QByteArray rx = drainRawBytes(fd, 240);
        if (rx.isEmpty()) {
            out << "  - `" << QStringLiteral("0x%1").arg(sp.cmd, 4, 16, QLatin1Char('0'))
                << "` " << sp.label << ": *(no reply)*\n";
            continue;
        }
        const auto frames = parseRealmeOppoFrames(rx);
        if (!frames.isEmpty()) {
            QStringList summaries;
            for (const auto& f : frames) summaries.append(formatRealmeOppoFrameSummary(f));
            out << "  - `" << QStringLiteral("0x%1").arg(sp.cmd, 4, 16, QLatin1Char('0'))
                << "` " << sp.label << ": `" << summaries.join(QStringLiteral(" | ")) << "`\n";
        } else {
            out << "  - `" << QStringLiteral("0x%1").arg(sp.cmd, 4, 16, QLatin1Char('0'))
                << "` " << sp.label << ": raw `[" << hexSpaced(rx) << "]`\n";
        }
    }
    return true;
}

bool runVivoGaiaProbe(SocketHandle fd, QTextStream& out) {
    if (!sendAll(fd, buildHandshake())) return false;
    FrameDecoder dec;
    const QByteArray hsRaw = drainRawBytes(fd, 450);
    if (hsRaw.isEmpty()) return false;

    const auto hsFrames = dec.feed(hsRaw);
    bool gotGaiaHs = false;
    QString hsSummary;
    QString detectedFw;

    auto recordTelemetryFw = [&](const QList<Frame>& list) {
        for (const Frame& f : list) {
            if (f.vendor == kVivoVendor && f.command == kCmdReportTelemetry) {
                EarbudState tmp;
                applyFrameToState(f, tmp);
                if (!tmp.firmware.isEmpty()) detectedFw = tmp.firmware;
            }
        }
    };

    recordTelemetryFw(hsFrames);
    for (const Frame& f : hsFrames) {
        if (f.vendor == kGaiaVendor && f.command == kCmdHandshakeResp) {
            gotGaiaHs = true;
            hsSummary = formatVivoFrameSummary(f);
            break;
        }
    }
    if (!gotGaiaHs) {
        out << "- Non-GAIA reply to `0x0300`: `[" << hexSpaced(hsRaw) << "]`\n";
        return false;
    }

    out << "\n### Vivo / iQOO Compact GAIA Probe\n"
        << "- **Handshake (`0x0300`):** `" << hsSummary << "`\n";

    for (const QByteArray& pkt : buildRegisterNotifications()) {
        sendAll(fd, pkt);
    }
    recordTelemetryFw(dec.feed(drainRawBytes(fd, 350)));

    struct ProbeSpec {
        const char* label;
        uint8_t ver;
        uint16_t cmd;
        QByteArray payload;
        uint16_t expectedResp;
    };

    const std::vector<ProbeSpec> specs = {
        {"Battery (v4)", 4, 0x0207, {}, 0x8207},
        {"Noise Mode (v4 [00])", 4, 0x0230, QByteArray("\x00", 1), 0x8230},
        {"Noise Mode (v3 [])", 3, 0x0230, {}, 0x8230},
        {"Wear State (v4)", 4, 0x020D, {}, 0x820D},
        {"Wear Switch (v4)", 4, 0x0203, {}, 0x8203},
        {"Audio Effect / EQ (v4)", 4, 0x0218, {}, 0x8218},
        {"Audio Effect / EQ (v3)", 3, 0x0218, {}, 0x8218},
        {"Low Latency (v4)", 4, 0x0251, {}, 0x8251},
        {"Long-Press Cycle (v4 [05])", 4, 0x0231, QByteArray("\x05", 1), 0x8231},
        {"Long-Press Cycle (v3 [05])", 3, 0x0231, QByteArray("\x05", 1), 0x8231},
        {"Touch Button (v4)", 4, 0x0250, {}, 0x8250},
        {"Hearing Protection (v4)", 4, 0x0252, {}, 0x8252},
        {"Spatial Audio (v4)", 4, 0x0239, {}, 0x8239},
        {"Anti-Wind (v4)", 4, 0x025E, {}, 0x825E},
        {"Model ID (v4)", 4, 0x021B, {}, 0x821B},
        {"Firmware Raw (v4)", 4, 0x021C, {}, 0x821C},
        {"Multipoint (v4)", 4, 0x0249, {}, 0x8249},
    };

    out << "- **Command Probes:**\n";
    for (const auto& sp : specs) {
        sendAll(fd, encodeFrame(sp.ver, kVivoVendor, sp.cmd, sp.payload));
        const auto got = dec.feed(drainRawBytes(fd, 220));
        recordTelemetryFw(got);
        QString matchedStr;
        for (const Frame& f : got) {
            if (f.vendor == kVivoVendor && f.command == sp.expectedResp) {
                matchedStr = formatVivoFrameSummary(f);
                break;
            }
        }
        out << "  - `" << QStringLiteral("0x%1").arg(sp.cmd, 4, 16, QLatin1Char('0'))
            << "` " << sp.label << ": "
            << (matchedStr.isEmpty() ? QStringLiteral("*(no reply)*") : QStringLiteral("`%1`").arg(matchedStr))
            << "\n";
    }

    if (!detectedFw.isEmpty()) {
        out << "- **Telemetry Firmware (`0x8224`):** `" << detectedFw << "`\n";
    }
    return true;
}

bool runHuaweiSppProbe(SocketHandle fd, QTextStream& out) {
    auto formatHw = [](const huawei::Packet& p) -> QString {
        const QString cmdHex = QString::fromLatin1(p.command.toHex());
        if (cmdHex == u"0107") {
            EarbudState tmp;
            huawei::applyPacket(p, tmp, huawei::profileFor({}));
            return QStringLiteral("cmd=0x0107 fw=%1 (serial/SN redacted)")
                .arg(tmp.firmware.isEmpty() ? QStringLiteral("n/a") : tmp.firmware);
        }
        if (cmdHex == u"2b31") {
            return QStringLiteral("cmd=0x2b31 (dual-connect host redacted)");
        }
        QStringList tlvs;
        for (auto it = p.params.cbegin(); it != p.params.cend(); ++it) {
            tlvs.append(QStringLiteral("%1:[%2]").arg(it.key()).arg(hexSpaced(it.value())));
        }
        return QStringLiteral("cmd=0x%1 {%2}").arg(cmdHex, tlvs.join(QStringLiteral(", ")));
    };

    if (!sendAll(fd, huawei::buildQueryBattery())) return false;
    huawei::Decoder dec;
    const QByteArray firstRx = drainRawBytes(fd, 450);
    const auto firstPkts = dec.feed(firstRx);
    if (firstPkts.isEmpty()) return false;

    out << "\n### HUAWEI SPP (`0x5A` Protocol) Probe\n";
    for (const auto& p : firstPkts) {
        out << "- `0x0108` Battery: `" << formatHw(p) << "`\n";
    }

    struct HwSpec {
        const char* label;
        QByteArray pkt;
    };
    const std::vector<HwSpec> specs = {
        {"Device Info (`0x0107`)", huawei::buildQueryInfo()},
        {"Noise Control (`0x2b2a`)", huawei::buildQueryAnc()},
        {"Equalizer (`0x2b4a`)", huawei::buildQueryEqualizer()},
        {"Low Latency (`0x2b6c`)", huawei::buildQueryLowLatency()},
        {"Auto-Pause (`0x2b11`)", huawei::buildQueryAutoPause()},
        {"Dual-Connect Switch (`0x2b2f`)", huawei::buildQueryDualConnectEnabled()},
    };
    for (const auto& sp : specs) {
        sendAll(fd, sp.pkt);
        const auto pkts = dec.feed(drainRawBytes(fd, 260));
        if (pkts.isEmpty()) {
            out << "  - " << sp.label << ": *(no reply)*\n";
        } else {
            QStringList items;
            for (const auto& p : pkts) items.append(formatHw(p));
            out << "  - " << sp.label << ": `" << items.join(QStringLiteral(" | ")) << "`\n";
        }
    }
    return true;
}

bool runAirohaRaceProbe(SocketHandle fd, QTextStream& out) {
    // Airoha RACE read-only battery query: 05 5A 03 00 D6 0C 00
    const QByteArray raceBatt = QByteArray::fromHex("055a0300d60c00");
    sendAll(fd, raceBatt);
    const QByteArray rx = drainRawBytes(fd, 350);
    if (rx.isEmpty()) return false;
    out << "\n### Airoha RACE (`0x05 0x5A`) Probe\n"
        << "- `0x0CD6` TwsGetBattery reply: `[" << hexSpaced(rx) << "]`\n";
    return true;
}

}  // namespace

QString maskMacForReport(const QString& mac) {
    const QStringList parts = mac.trimmed().toUpper().split(u':');
    if (parts.size() == 6) {
        return QStringLiteral("%1:%2:%3:XX:XX:XX").arg(parts[0], parts[1], parts[2]);
    }
    return QStringLiteral("XX:XX:XX:XX:XX:XX");
}

QString generateDiagnosticReport(DeviceRegistry& registry,
                                 const QString& deviceQuery,
                                 int listenSeconds) {
    QString report;
    QTextStream out(&report);

    const auto devOpt = pickProbeDevice(registry, deviceQuery);
    if (!devOpt) {
        out << "### Kalm Diagnostic Report\n"
            << "- **Error:** No matching Bluetooth audio device found.\n";
        return report;
    }

    const BluezDevice& dev = *devOpt;
    const QString maskedMac = maskMacForReport(dev.mac);

    out << "### Bluetooth Device Info\n"
        << "- **Name:** `" << dev.name << "`\n"
        << "- **MAC (OUI):** `" << maskedMac << "`\n"
        << "- **Modalias:** `" << (dev.modalias.isEmpty() ? QStringLiteral("n/a") : dev.modalias) << "`\n"
        << "- **Icon:** `" << (dev.icon.isEmpty() ? QStringLiteral("n/a") : dev.icon) << "`\n"
        << "- **Connected / Paired:** `" << (dev.connected ? "true" : "false")
        << "` / `" << (dev.paired ? "true" : "false") << "`\n";
    if (dev.batteryPct) {
        out << "- **OS Battery:** `" << *dev.batteryPct << "%`\n";
    }
    out << "- **UUIDs (" << dev.uuids.size() << "):**\n";
    for (const QString& u : dev.uuids) {
        out << "  - `" << u << "`\n";
    }

    out << "\n### RFCOMM Transport Discovery\n";
    if (!dev.connected) {
        out << "- **Result:** Skipped (device is not currently connected via Bluetooth).\n";
        return report;
    }

    UniqueSocket sock;
    QString connectedVia;
    if (dev.isHuaweiTws()) {
        for (int ch : {1, 16}) {
            sock = openRfcommByChannel(dev.mac, ch, 1500);
            if (sock) {
                connectedVia = QStringLiteral("RFCOMM channel `%1` (HUAWEI SPP)").arg(ch);
                break;
            }
        }
    }

    const QStringList candUuids = buildCandidateControlUuids(dev.uuids);
    if (!sock) {
        for (const QString& u : candUuids) {
            sock = openRfcommByUuid(dev.mac, dev.path, u, 2500);
            if (sock) {
                const int ch = getRfcommPeerChannel(sock.get());
                connectedVia = (ch > 0)
                                   ? QStringLiteral("UUID `%1` (channel %2)").arg(u).arg(ch)
                                   : QStringLiteral("UUID `%1` (SDP auto)").arg(u);
                break;
            } else {
                out << "- UUID `" << u << "`: *(connection refused / not open)*\n";
            }
        }
    }

    if (!sock) {
        constexpr int kScanChannels[] = {15, 13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16};
        for (int ch : kScanChannels) {
            sock = openRfcommByChannel(dev.mac, ch, 900);
            if (sock) {
                connectedVia = QStringLiteral("RFCOMM channel `%1` (direct scan)").arg(ch);
                break;
            }
        }
    }

    if (!sock) {
        out << "- **Result:** Could not open any RFCOMM control channel (device may use BLE GATT only or require wake-up).\n";
        report.replace(dev.mac, maskedMac, Qt::CaseInsensitive);
        return report;
    }

    out << "- **Connected Control Channel:** " << connectedVia << "\n";

    // 1. Check for spontaneous greeting bytes upon connect (redacting known multipoint frames)
    const QByteArray greeting = drainRawBytes(sock.get(), 250);
    if (!greeting.isEmpty()) {
        FrameDecoder gDec;
        const auto gVivo = gDec.feed(greeting);
        const auto gTl = parseRealmeOppoFrames(greeting);
        if (!gVivo.isEmpty()) {
            QStringList items;
            for (const auto& f : gVivo) items.append(formatVivoFrameSummary(f));
            out << "- **Passive Greeting (Vivo):** `" << items.join(QStringLiteral(" | ")) << "`\n";
        } else if (!gTl.isEmpty()) {
            QStringList items;
            for (const auto& f : gTl) items.append(formatRealmeOppoFrameSummary(f));
            out << "- **Passive Greeting (OPPO/realme):** `" << items.join(QStringLiteral(" | ")) << "`\n";
        } else {
            out << "- **Passive Greeting (" << greeting.size() << "B):** `[" << hexSpaced(greeting) << "]`\n";
        }
    }

    // 2. Choose probe order based on advertised UUIDs / brand name
    const QString lowName = dev.name.toLower();
    const bool looksLikeHuawei = dev.isHuaweiTws();
    const bool looksLikeOppoRealme =
        candUuids.contains(QLatin1String(kRealmeOppoTlUuid)) ||
        candUuids.contains(QLatin1String(kOppoHeyMelodyUuid)) ||
        lowName.contains(u"realme") || lowName.contains(u"oppo") || lowName.contains(u"oneplus");

    bool matchedAny = false;
    if (looksLikeHuawei) {
        matchedAny = runHuaweiSppProbe(sock.get(), out);
        if (!matchedAny) matchedAny = runVivoGaiaProbe(sock.get(), out);
        if (!matchedAny) matchedAny = runRealmeOppoProbe(sock.get(), out);
        if (!matchedAny) matchedAny = runAirohaRaceProbe(sock.get(), out);
    } else if (looksLikeOppoRealme) {
        matchedAny = runRealmeOppoProbe(sock.get(), out);
        if (!matchedAny) matchedAny = runVivoGaiaProbe(sock.get(), out);
        if (!matchedAny) matchedAny = runHuaweiSppProbe(sock.get(), out);
        if (!matchedAny) matchedAny = runAirohaRaceProbe(sock.get(), out);
    } else {
        matchedAny = runVivoGaiaProbe(sock.get(), out);
        if (!matchedAny) matchedAny = runRealmeOppoProbe(sock.get(), out);
        if (!matchedAny) matchedAny = runHuaweiSppProbe(sock.get(), out);
        if (!matchedAny) matchedAny = runAirohaRaceProbe(sock.get(), out);
    }

    if (!matchedAny) {
        out << "- **Auto-Probe:** Channel is open, but did not respond to Vivo GAIA (`0xFF`), OPPO/realme (`0xAA`), HUAWEI SPP (`0x5A`), or Airoha RACE (`0x05 0x5A`). Run `kalm probe --listen 15` while toggling ANC on the earbuds to capture raw frames!\n";
    }

    // 3. Universal Live Event Listener (--listen <sec>)
    if (listenSeconds > 0) {
        out << "\n### Live Event Capture (" << listenSeconds << "s)\n";
        FrameDecoder vivoDec;
        huawei::Decoder hwDec;
        QElapsedTimer listenTimer;
        listenTimer.start();
        const qint64 totalMs = static_cast<qint64>(listenSeconds) * 1000;
        int capturedCount = 0;

        while (listenTimer.elapsed() < totalMs) {
            const QByteArray chunk = drainRawBytes(sock.get(), 300);
            if (chunk.isEmpty()) continue;

            const double sec = static_cast<double>(listenTimer.elapsed()) / 1000.0;
            const auto hwPackets = hwDec.feed(chunk);
            if (!hwPackets.isEmpty()) {
                for (const auto& p : hwPackets) {
                    ++capturedCount;
                    const QString cmdHex = QString::fromLatin1(p.command.toHex());
                    if (cmdHex == u"2b31" || cmdHex == u"0107") {
                        out << "- `[+" << QString::number(sec, 'f', 1) << "s]` HUAWEI SPP: `cmd=0x"
                            << cmdHex << " (redacted)`\n";
                    } else {
                        out << "- `[+" << QString::number(sec, 'f', 1) << "s]` HUAWEI SPP: `cmd=0x"
                            << cmdHex << " raw=[" << hexSpaced(p.raw) << "]`\n";
                    }
                }
                continue;
            }

            const auto tlFrames = parseRealmeOppoFrames(chunk);
            if (!tlFrames.isEmpty()) {
                for (const auto& f : tlFrames) {
                    ++capturedCount;
                    out << "- `[+" << QString::number(sec, 'f', 1) << "s]` OPPO/realme: `"
                        << formatRealmeOppoFrameSummary(f) << "`\n";
                }
                continue;
            }

            const auto vivoFrames = vivoDec.feed(chunk);
            if (!vivoFrames.isEmpty()) {
                for (const Frame& f : vivoFrames) {
                    if (f.vendor == kVivoVendor && f.command == kCmdReportTelemetry) continue;
                    ++capturedCount;
                    out << "- `[+" << QString::number(sec, 'f', 1) << "s]` Vivo GAIA: `"
                        << formatVivoFrameSummary(f) << "`\n";
                }
                continue;
            }

            ++capturedCount;
            out << "- `[+" << QString::number(sec, 'f', 1) << "s]` Raw (" << chunk.size()
                << "B): `[" << hexSpaced(chunk) << "]`\n";
        }

        if (capturedCount == 0) {
            out << "- *(No interactive events received during listen window)*\n";
        }
    }

    // Ensure full MAC is never leaked anywhere in the output
    report.replace(dev.mac, maskedMac, Qt::CaseInsensitive);
    return report;
}

}  // namespace kalm
