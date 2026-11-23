// SPDX-License-Identifier: GPL-3.0-or-later
#include "nothing.hpp"

#include "i18n.hpp"

#include <cstring>

namespace kalm::nothing {

namespace {

constexpr int kHeaderSize = 8;
constexpr int kMaxPacketBytes = 8192;

QString decodeAsciiString(const QByteArray& raw) {
    QByteArray data = raw;
    const qsizetype nul = data.indexOf('\0');
    if (nul >= 0) data.truncate(nul);
    return QString::fromUtf8(data).trimmed();
}

const ModelProfile& genericNothingProfile() {
    static const ModelProfile kProfile = [] {
        ModelProfile p;
        p.label = QStringLiteral("Nothing / CMF");
        return p;
    }();
    return kProfile;
}

const ModelProfile& cmfBuds2PlusProfile() {
    static const ModelProfile kProfile = [] {
        ModelProfile p;
        p.label = QStringLiteral("CMF Buds 2 Plus");
        p.useListeningModeEq = true;
        return p;
    }();
    return kProfile;
}

const ModelProfile& cmfListeningModeProfile() {
    static const ModelProfile kProfile = [] {
        ModelProfile p;
        p.label = QStringLiteral("CMF / Nothing (Dirac)");
        p.useListeningModeEq = true;
        return p;
    }();
    return kProfile;
}

const ModelProfile& cmfBuds2aProfile() {
    static const ModelProfile kProfile = [] {
        ModelProfile p;
        p.label = QStringLiteral("CMF Buds 2a");
        p.useListeningModeEq = true;
        p.isBuds2a = true;
        return p;
    }();
    return kProfile;
}

const ModelProfile& overEarProfile() {
    static const ModelProfile kProfile = [] {
        ModelProfile p;
        p.label = QStringLiteral("Nothing / CMF Headphone");
        p.useListeningModeEq = true;
        p.isOverEar = true;
        return p;
    }();
    return kProfile;
}

const ModelProfile& openEarNoAncProfile() {
    static const ModelProfile kProfile = [] {
        ModelProfile p;
        p.label = QStringLiteral("Nothing Ear (open/stick)");
        p.anc = false;
        p.ancLevels = false;
        return p;
    }();
    return kProfile;
}

const ModelProfile& ear1LegacyProfile() {
    static const ModelProfile kProfile = [] {
        ModelProfile p;
        p.label = QStringLiteral("Nothing Ear (1)");
        p.isLegacyRing = true;
        return p;
    }();
    return kProfile;
}

uint8_t wireAncByte(NoiseMode mode, std::optional<int> ancLevel) {
    if (mode == NoiseMode::Off) return kWireOff;
    if (mode == NoiseMode::Transparency) return kWireTransparency;
    const int lvl = ancLevel.value_or(kWireAncHigh);
    if (lvl == kWireAncHigh || lvl == kWireAncMid || lvl == kWireAncLow || lvl == kWireAncAdaptive) {
        return static_cast<uint8_t>(lvl);
    }
    return kWireAncHigh;
}

}  // namespace

uint16_t crc16Modbus(const QByteArray& data) {
    uint16_t crc = 0xFFFF;
    for (const char ch : data) {
        crc ^= static_cast<uint8_t>(ch);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x0001) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001)
                                 : static_cast<uint16_t>(crc >> 1);
        }
    }
    return crc;
}

QByteArray encode(uint16_t command, const QByteArray& payload, uint8_t opId) {
    const uint16_t len = static_cast<uint16_t>(payload.size());
    QByteArray out(kHeaderSize + payload.size() + 2, Qt::Uninitialized);
    out[0] = static_cast<char>(kSof);
    out[1] = static_cast<char>(kCtrlWithCrc & 0xFF);
    out[2] = static_cast<char>((kCtrlWithCrc >> 8) & 0xFF);
    out[3] = static_cast<char>(command & 0xFF);
    out[4] = static_cast<char>((command >> 8) & 0xFF);
    out[5] = static_cast<char>(len & 0xFF);
    out[6] = static_cast<char>((len >> 8) & 0xFF);
    out[7] = static_cast<char>(opId);
    if (!payload.isEmpty()) {
        std::memcpy(out.data() + kHeaderSize, payload.constData(), static_cast<size_t>(payload.size()));
    }
    const uint16_t crc = crc16Modbus(out.left(kHeaderSize + payload.size()));
    out[kHeaderSize + payload.size()] = static_cast<char>(crc & 0xFF);
    out[kHeaderSize + payload.size() + 1] = static_cast<char>((crc >> 8) & 0xFF);
    return out;
}

QList<Packet> Decoder::feed(const QByteArray& chunk) {
    if (!chunk.isEmpty()) m_buf.append(chunk);
    if (m_buf.size() > 131072) {
        m_buf.remove(0, m_buf.size() - 65536);
    }

    QList<Packet> out;
    while (m_buf.size() >= kHeaderSize) {
        const qsizetype sofIdx = m_buf.indexOf(static_cast<char>(kSof));
        if (sofIdx < 0) {
            m_buf.clear();
            break;
        }
        if (sofIdx > 0) {
            m_buf.remove(0, sofIdx);
            if (m_buf.size() < kHeaderSize) break;
        }

        const uint16_t ctrl = static_cast<uint8_t>(m_buf[1]) |
                              (static_cast<uint16_t>(static_cast<uint8_t>(m_buf[2])) << 8);
        const uint16_t cmd = static_cast<uint8_t>(m_buf[3]) |
                             (static_cast<uint16_t>(static_cast<uint8_t>(m_buf[4])) << 8);
        const uint16_t payloadLen = static_cast<uint8_t>(m_buf[5]) |
                                    (static_cast<uint16_t>(static_cast<uint8_t>(m_buf[6])) << 8);
        const bool hasCrc = (ctrl & kMaskCrc) != 0;
        const int totalLen = kHeaderSize + static_cast<int>(payloadLen) + (hasCrc ? 2 : 0);

        if (totalLen > kMaxPacketBytes) {
            m_buf.remove(0, 1);
            continue;
        }
        if (m_buf.size() < totalLen) break;

        const QByteArray raw = m_buf.left(totalLen);
        if (hasCrc) {
            const int crcOff = kHeaderSize + static_cast<int>(payloadLen);
            const uint16_t rxCrc = static_cast<uint8_t>(raw[crcOff]) |
                                   (static_cast<uint16_t>(static_cast<uint8_t>(raw[crcOff + 1])) << 8);
            const uint16_t fullCrc = crc16Modbus(raw.left(crcOff));
            if (rxCrc != fullCrc) {
                // Some newer CMF firmwares compute CRC16 over payload bytes only.
                const uint16_t payloadOnlyCrc = crc16Modbus(raw.mid(kHeaderSize, payloadLen));
                if (rxCrc != payloadOnlyCrc) {
                    m_buf.remove(0, 1);
                    continue;
                }
            }
        }

        Packet pkt;
        pkt.control = ctrl;
        pkt.command = cmd;
        pkt.opId = static_cast<uint8_t>(raw[7]);
        pkt.payload = raw.mid(kHeaderSize, payloadLen);
        pkt.raw = raw;
        out.append(std::move(pkt));
        m_buf.remove(0, totalLen);
    }

    return out;
}

bool matchesName(const QString& deviceName) {
    const QString low = deviceName.trimmed().toLower();
    if (low.isEmpty()) return false;
    if (low.contains(u"cmf buds") || low.contains(u"cmf neckband") ||
        low.contains(u"cmf headphone") || low.contains(u"cmf clip") ||
        low.contains(u"nothing ear") || low.contains(u"nothing headphone")) {
        return true;
    }
    if (low == u"ear (1)" || low == u"ear (2)" || low == u"ear (a)" ||
        low == u"ear (3)" || low == u"ear (3a)" || low == u"ear (open)" ||
        low == u"ear (stick)" || low == u"nothing ear" ||
        low == u"headphone (1)" || low == u"headphone (a)" ||
        low == u"neckband pro") {
        return true;
    }
    return false;
}

const ModelProfile& profileFor(const QString& deviceName) {
    const QString low = deviceName.trimmed().toLower();
    if (low.contains(u"buds 2 plus")) return cmfBuds2PlusProfile();
    if (low.contains(u"buds 2a")) return cmfBuds2aProfile();
    if (low.contains(u"headphone")) return overEarProfile();
    if (low.contains(u"ear (1)")) return ear1LegacyProfile();
    if (low.contains(u"ear (open)") || low.contains(u"ear (stick)") || low.contains(u"clip")) {
        return openEarNoAncProfile();
    }
    if (low.contains(u"buds 2") || low.contains(u"buds pro 2") ||
        low.contains(u"ear (3)") || low.contains(u"ear (3a)") || low.contains(u"ear 3")) {
        return cmfListeningModeProfile();
    }
    return genericNothingProfile();
}

const QList<PresetEntry>& ancLevels(const ModelProfile& profile) {
    static const QList<PresetEntry> kEmpty;
    static const QList<PresetEntry> kLevels = {
        {kWireAncAdaptive, "anc_adaptive", "adaptive"},
        {kWireAncLow, "anc_mild", "mild"},
        {kWireAncMid, "anc_standard", "standard"},
        {kWireAncHigh, "anc_deep", "deep"},
    };
    return profile.ancLevels ? kLevels : kEmpty;
}

const QList<PresetEntry>& eqPresets(const ModelProfile& profile) {
    static const QList<PresetEntry> kListeningMode = {
        {0, "eq_dirac_opteo", "standard"},
        {3, "eq_pop", "pop"},
        {1, "eq_rock", "rock"},
        {2, "eq_electronic", "electronic"},
        {4, "eq_vocal", "vocal"},
        {5, "eq_classical", "classical"},
        {6, "eq_custom", "custom"},
    };
    static const QList<PresetEntry> kBuds2a = {
        {7, "eq_dirac_opteo", "standard"},
        {3, "eq_pop", "pop"},
        {1, "eq_rock", "rock"},
        {2, "eq_electronic", "electronic"},
        {4, "eq_vocal", "vocal"},
        {5, "eq_classical", "classical"},
        {6, "eq_custom", "custom"},
    };
    static const QList<PresetEntry> kClassic = {
        {0, "eq_standard", "standard"},
        {1, "eq_vocal", "vocal"},
        {2, "eq_treble", "treble"},
        {3, "eq_bass", "bass"},
        {5, "eq_custom", "custom"},
    };
    if (profile.isBuds2a) return kBuds2a;
    return profile.useListeningModeEq ? kListeningMode : kClassic;
}

QString ancLevelLabel(int code) {
    switch (code) {
        case kWireAncAdaptive: return trKey("anc_adaptive");
        case kWireAncLow: return trKey("anc_mild");
        case kWireAncMid: return trKey("anc_standard");
        case kWireAncHigh: return trKey("anc_deep");
        default: return {};
    }
}

QString eqPresetLabel(int presetId, const ModelProfile& profile) {
    for (const auto& e : eqPresets(profile)) {
        if (e.code == presetId) return trKey(e.trKeyName);
    }
    return {};
}

// ---- Packet builders -------------------------------------------------------

QByteArray buildProtocolActivate(uint8_t opId) {
    return encode(kCmdReadProtocolVersion, {}, opId);
}

QByteArray buildQueryBattery(uint8_t opId) {
    return encode(kCmdReadBattery, {}, opId);
}

QByteArray buildQueryFirmware(uint8_t opId) {
    return encode(kCmdReadFirmware, {}, opId);
}

QByteArray buildQueryWearStatus(uint8_t opId) {
    return encode(kCmdReadWearStatus, {}, opId);
}

QByteArray buildQueryAnc(uint8_t opId) {
    return encode(kCmdReadAnc, {}, opId);
}

QByteArray buildSetAnc(NoiseMode mode, std::optional<int> ancLevel, uint8_t opId) {
    QByteArray payload(3, '\x00');
    payload[0] = '\x01';
    payload[1] = static_cast<char>(wireAncByte(mode, ancLevel));
    payload[2] = '\x00';
    return encode(kCmdWriteAnc, payload, opId);
}

QByteArray buildQueryEq(const ModelProfile& profile, uint8_t opId) {
    return encode(profile.useListeningModeEq ? kCmdReadListeningMode : kCmdReadEq, {}, opId);
}

QByteArray buildSetEq(int presetId, const ModelProfile& profile, uint8_t opId) {
    QByteArray payload(2, '\x00');
    payload[0] = static_cast<char>(presetId & 0xFF);
    payload[1] = '\x00';
    return encode(profile.useListeningModeEq ? kCmdWriteListeningMode : kCmdWriteEq, payload, opId);
}

QByteArray buildQueryLowLatency(uint8_t opId) {
    return encode(kCmdReadLowLatency, {}, opId);
}

QByteArray buildSetLowLatency(bool enabled, uint8_t opId) {
    QByteArray payload(2, '\x00');
    payload[0] = static_cast<char>(enabled ? 0x01 : 0x02);
    payload[1] = '\x00';
    return encode(kCmdWriteLowLatency, payload, opId);
}

QByteArray buildQueryInEarDetection(uint8_t opId) {
    return encode(kCmdReadInEarDetection, {}, opId);
}

QByteArray buildSetInEarDetection(bool enabled, uint8_t opId) {
    QByteArray payload(3, '\x00');
    payload[0] = '\x01';
    payload[1] = '\x01';
    payload[2] = static_cast<char>(enabled ? 0x01 : 0x00);
    return encode(kCmdWriteInEarDetection, payload, opId);
}

QByteArray buildSetRingBud(uint8_t budId, bool ring, uint8_t opId) {
    const char onByte = ring ? '\x01' : '\x00';
    if (budId == 0x05) {
        // Legacy Ear (1) / TWS-all 1-byte format: [0x01] / [0x00]
        return encode(kCmdWriteRingBuds, QByteArray(1, onByte), opId);
    }
    QByteArray p(2, '\x00');
    p[0] = static_cast<char>(budId);
    p[1] = onByte;
    return encode(kCmdWriteRingBuds, p, opId);
}

QList<QByteArray> buildSetRingBuds(bool ring, const ModelProfile& profile, uint8_t opId) {
    if (profile.isLegacyRing) {
        return {buildSetRingBud(0x05, ring, opId)};
    }
    if (profile.isOverEar) {
        return {buildSetRingBud(0x06, ring, opId)};
    }
    // Standard Nothing / CMF TWS bud IDs: 0x02 = Left, 0x03 = Right
    return {
        buildSetRingBud(0x02, ring, opId),
        buildSetRingBud(0x03, ring, static_cast<uint8_t>(opId + 1)),
    };
}

// ---- Incoming packets ------------------------------------------------------

bool applyPacket(const Packet& packet, EarbudState& state, const ModelProfile& profile) {
    const QByteArray& p = packet.payload;
    const uint16_t cmd = packet.command;

    // Battery: 0x4007 (response) or 0xE001 (push notification)
    if (cmd == kRespBattery || cmd == kNotifyBattery) {
        if (p.isEmpty()) return false;
        if (profile.isOverEar && p.size() == 3) {
            const uint8_t raw = static_cast<uint8_t>(p[2]);
            const int lvl = raw & 0x7F;
            const bool chg = (raw & 0x80) != 0;
            if (lvl <= 100) {
                state.leftBattery = BatteryReading{lvl, chg, false};
                state.rightBattery = BatteryReading{lvl, chg, false};
                state.caseBattery = BatteryReading{std::nullopt, false, true};
                return true;
            }
            return false;
        }

        const int count = static_cast<uint8_t>(p[0]);
        if (p.size() < 1 + count * 2) return false;

        bool seenL = false, seenR = false, seenC = false;
        for (int i = 0; i < count; ++i) {
            const int off = 1 + i * 2;
            const uint8_t compId = static_cast<uint8_t>(p[off]);
            const uint8_t raw = static_cast<uint8_t>(p[off + 1]);
            const int lvl = raw & 0x7F;
            const bool chg = (raw & 0x80) != 0;
            if (lvl > 100) continue;

            if (compId == 0x02) {
                state.leftBattery = BatteryReading{lvl, chg, false};
                seenL = true;
            } else if (compId == 0x03) {
                state.rightBattery = BatteryReading{lvl, chg, false};
                seenR = true;
            } else if (compId == 0x04) {
                state.caseBattery = BatteryReading{lvl, chg, false};
                seenC = true;
            } else if (compId == 0x06 || compId == 0x07) {
                state.leftBattery = BatteryReading{lvl, chg, false};
                state.rightBattery = BatteryReading{lvl, chg, false};
                seenL = seenR = true;
            }
        }
        if (!seenL) state.leftBattery.stale = true;
        if (!seenR) state.rightBattery.stale = true;
        if (!seenC) state.caseBattery.stale = true;
        return seenL || seenR || seenC;
    }

    // Firmware version: 0x4042
    if (cmd == kRespFirmware) {
        const QString fw = decodeAsciiString(p);
        if (!fw.isEmpty() && fw != state.firmware) {
            state.firmware = fw;
            return true;
        }
        return false;
    }

    // Wear / In-Ear status: 0x400A (response) or 0xE002 (push notification)
    if (cmd == kRespWearStatus || cmd == kNotifyWearStatus) {
        if (p.isEmpty()) return false;
        const int count = static_cast<uint8_t>(p[0]);
        if (p.size() < 1 + count * 2) return false;
        bool changed = false;
        for (int i = 0; i < count; ++i) {
            const int off = 1 + i * 2;
            const uint8_t compId = static_cast<uint8_t>(p[off]);
            const uint8_t flags = static_cast<uint8_t>(p[off + 1]);
            const QString wear = (flags & 0x04) ? QStringLiteral("worn")
                                                : ((flags & 0x01) ? QStringLiteral("in_case")
                                                                  : QStringLiteral("removed"));
            if (compId == 0x02) {
                state.leftWear = wear;
                changed = true;
            } else if (compId == 0x03) {
                state.rightWear = wear;
                changed = true;
            }
        }
        return changed;
    }

    // ANC / Noise Control: 0x401E, 0xE003, 0x6003
    if (cmd == kRespAnc || cmd == kNotifyAnc || cmd == kNotifyAncAlt) {
        if (p.size() < 2) return false;
        const uint8_t modeByte = static_cast<uint8_t>(p[1]);
        if (modeByte == kWireOff) {
            state.noiseMode = NoiseMode::Off;
            return true;
        }
        if (modeByte == kWireTransparency) {
            state.noiseMode = NoiseMode::Transparency;
            return true;
        }
        if (modeByte == kWireAncHigh || modeByte == kWireAncMid ||
            modeByte == kWireAncLow || modeByte == kWireAncAdaptive) {
            state.noiseMode = NoiseMode::Anc;
            state.noiseEffect = static_cast<int>(modeByte);
            return true;
        }
        return false;
    }

    // EQ / Listening Mode: 0x4050 (Dirac/Listening Mode) or 0x401F (Classic EQ)
    if (cmd == kRespListeningMode || cmd == kRespEq) {
        if (p.isEmpty()) return false;
        // 2-byte LE BasicInt puts preset id in byte[0] ([preset, 0x00]), while
        // older firmwares return [0x00, preset].
        int preset = static_cast<int>(static_cast<uint8_t>(p[0]));
        if (p.size() >= 2 && preset == 0 && static_cast<uint8_t>(p[1]) != 0) {
            preset = static_cast<int>(static_cast<uint8_t>(p[1]));
        }
        state.audioEffect = preset;
        return true;
    }

    // Low Latency / Game Mode: 0x4041 or 0xE005
    if (cmd == kRespLowLatency || cmd == kNotifyLowLatency) {
        if (p.isEmpty()) return false;
        state.lowLatency = (static_cast<uint8_t>(p[0]) == 0x01);
        return true;
    }

    // In-Ear Detection switch: 0x400E -> [count, (featType, enabled) * count], featType 1 = in-ear
    if (cmd == kRespInEarDetection) {
        if (p.size() < 3) return false;
        const int count = static_cast<uint8_t>(p[0]);
        for (int i = 0; i < count; ++i) {
            const int off = 1 + i * 2;
            if (off + 2 > p.size()) break;
            if (static_cast<uint8_t>(p[off]) == 0x01) {
                state.wearDetection = (static_cast<uint8_t>(p[off + 1]) != 0);
                return true;
            }
        }
        return false;
    }

    return false;
}

}  // namespace kalm::nothing
