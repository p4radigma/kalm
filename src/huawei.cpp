// SPDX-License-Identifier: GPL-3.0-or-later
#include "huawei.hpp"

#include "i18n.hpp"

#include <QStringList>

namespace kalm::huawei {

namespace {

QByteArray byteValue(int value) {
    return QByteArray(1, static_cast<char>(value & 0xFF));
}

QByteArray macBytes(const QString& mac) {
    QByteArray out;
    const QStringList parts = mac.trimmed().split(u':');
    if (parts.size() != 6) return out;
    for (const QString& part : parts) {
        bool ok = false;
        const int v = part.toInt(&ok, 16);
        if (!ok) return {};
        out.append(static_cast<char>(v & 0xFF));
    }
    return out;
}

QString decodeText(const QByteArray& raw) {
    QByteArray data = raw;
    const qsizetype nul = data.indexOf('\0');
    if (nul >= 0) data.truncate(nul);
    return QString::fromUtf8(data).trimmed();
}

const ModelProfile& genericProfile() {
    static const ModelProfile kProfile = [] {
        ModelProfile p;
        p.label = QStringLiteral("HUAWEI FreeBuds");
        return p;
    }();
    return kProfile;
}

const ModelProfile& buds6iProfile() {
    static const ModelProfile kProfile = [] {
        ModelProfile p;
        p.label = QStringLiteral("HUAWEI FreeBuds 6i");
        p.ancDynamic = true;
        return p;
    }();
    return kProfile;
}

}  // namespace

int Packet::paramByte(int type, int fallback) const {
    const QByteArray v = params.value(type);
    if (v.isEmpty()) return fallback;
    return static_cast<uint8_t>(v.at(0));
}

QByteArray crc16Xmodem(const QByteArray& data) {
    uint16_t crc = 0;
    for (const char ch : data) {
        crc ^= static_cast<uint16_t>(static_cast<uint8_t>(ch)) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    QByteArray out;
    out.append(static_cast<char>((crc >> 8) & 0xFF));
    out.append(static_cast<char>(crc & 0xFF));
    return out;
}

QByteArray encode(const QByteArray& command, const QList<QPair<int, QByteArray>>& params) {
    QByteArray body = command;
    for (const auto& [type, value] : params) {
        body.append(static_cast<char>(type & 0xFF));
        body.append(static_cast<char>(value.size() & 0xFF));
        body.append(value);
    }

    const int length = static_cast<int>(body.size()) + 1;
    QByteArray out;
    out.reserve(length + 5);
    out.append(static_cast<char>(kMagic));
    out.append(static_cast<char>((length >> 8) & 0xFF));
    out.append(static_cast<char>(length & 0xFF));
    out.append('\0');
    out.append(body);
    out.append(crc16Xmodem(out));
    return out;
}

QList<Packet> Decoder::feed(const QByteArray& chunk) {
    m_buf.append(chunk);
    QList<Packet> out;

    while (m_buf.size() >= 4) {
        if (static_cast<uint8_t>(m_buf.at(0)) != kMagic) {
            m_buf.remove(0, 1);
            continue;
        }
        const int length = (static_cast<uint8_t>(m_buf.at(1)) << 8) |
                           static_cast<uint8_t>(m_buf.at(2));
        const int total = length + 5;
        if (length < 3 || total > 8192) {
            m_buf.remove(0, 1);
            continue;
        }
        if (m_buf.size() < total) break;

        const QByteArray raw = m_buf.left(total);
        if (crc16Xmodem(raw.left(total - 2)) != raw.right(2)) {
            m_buf.remove(0, 1);
            continue;
        }

        Packet packet;
        packet.raw = raw;
        packet.command = raw.mid(4, 2);

        const int bodyEnd = 4 + (length - 1);
        int pos = 6;
        while (pos + 2 <= bodyEnd) {
            const int type = static_cast<uint8_t>(raw.at(pos));
            const int valueLength = static_cast<uint8_t>(raw.at(pos + 1));
            if (pos + 2 + valueLength > bodyEnd) break;
            packet.params.insert(type, raw.mid(pos + 2, valueLength));
            pos += 2 + valueLength;
        }

        out.append(packet);
        m_buf.remove(0, total);
    }

    return out;
}

bool matchesName(const QString& deviceName) {
    const QString low = deviceName.toLower();
    if (low.contains(u"freebuds") || low.contains(u"freelace")) return true;
    if (!low.contains(u"huawei")) return false;
    return low.contains(u"eyewear") || low.contains(u"watch buds");
}

const ModelProfile& profileFor(const QString& deviceName) {
    const QString low = deviceName.toLower();
    if (low.contains(u"6i")) return buds6iProfile();
    if (low.contains(u"5i") || low.contains(u"pro")) {
        static const ModelProfile kDynamicProfile = [] {
            ModelProfile p;
            p.label = QStringLiteral("HUAWEI FreeBuds");
            p.ancDynamic = true;
            return p;
        }();
        return kDynamicProfile;
    }
    return genericProfile();
}

QString profileSignature(const QString& deviceName) {
    if (!matchesName(deviceName)) return QStringLiteral("gaia");
    return QStringLiteral("huawei:") + profileFor(deviceName).label;
}

const QList<PresetEntry>& ancLevels(const ModelProfile& profile) {
    static const QList<PresetEntry> kWithDynamic = {
        {3, "anc_adaptive", "adaptive"},
        {1, "anc_mild", "mild"},
        {0, "anc_standard", "standard"},
        {2, "anc_deep", "deep"},
    };
    static const QList<PresetEntry> kWithoutDynamic = {
        {1, "anc_mild", "mild"},
        {0, "anc_standard", "standard"},
        {2, "anc_deep", "deep"},
    };
    return profile.ancDynamic ? kWithDynamic : kWithoutDynamic;
}

const QList<PresetEntry>& transparencyLevels(const ModelProfile& profile) {
    static const QList<PresetEntry> kStandardOnly = {
        {1, "trans_standard", "standard"},
    };
    static const QList<PresetEntry> kWithVoice = {
        {1, "trans_standard", "standard"},
        {2, "trans_voice", "voice"},
    };
    return profile.transparencyVoiceBoost ? kWithVoice : kStandardOnly;
}

const QList<PresetEntry>& eqPresets(const ModelProfile&) {
    static const QList<PresetEntry> kList = {
        {1, "eq_standard", "standard"},
        {2, "eq_bass", "bass"},
        {3, "eq_treble", "treble"},
        {9, "eq_vocal", "vocal"},
    };
    return kList;
}

int toModeValue(NoiseMode mode) {
    switch (mode) {
        case NoiseMode::Anc: return kModeAnc;
        case NoiseMode::Transparency: return kModeAwareness;
        case NoiseMode::Off: return kModeNormal;
    }
    return kModeNormal;
}

std::optional<NoiseMode> modeFromValue(int value) {
    switch (value) {
        case kModeNormal: return NoiseMode::Off;
        case kModeAnc: return NoiseMode::Anc;
        case kModeAwareness: return NoiseMode::Transparency;
        default: return std::nullopt;
    }
}

// ---- Builders --------------------------------------------------------------

QByteArray buildQueryBattery() {
    return encode(QByteArray::fromHex("0108"), {{1, {}}, {2, {}}, {3, {}}});
}

QByteArray buildQueryAnc() {
    return encode(QByteArray::fromHex("2b2a"), {{1, {}}, {2, {}}});
}

QByteArray buildSetAnc(int mode, std::optional<int> level) {
    QByteArray payload;
    payload.append(static_cast<char>(mode & 0xFF));
    if (mode == kModeNormal) {
        payload.append('\0');
    } else if (level.has_value()) {
        payload.append(static_cast<char>(*level & 0xFF));
    } else {
        payload.append(static_cast<char>(0xFF));
    }
    return encode(QByteArray::fromHex("2b04"), {{1, payload}});
}

QByteArray buildQueryInfo() {
    QList<QPair<int, QByteArray>> params;
    for (int i = 0; i < 16; ++i) params.append(qMakePair(i, QByteArray()));
    return encode(QByteArray::fromHex("0107"), params);
}

QByteArray buildQueryEqualizer() {
    QList<QPair<int, QByteArray>> params;
    for (int i = 1; i <= 8; ++i) params.append(qMakePair(i, QByteArray()));
    return encode(QByteArray::fromHex("2b4a"), params);
}

QByteArray buildSetEqualizerPreset(int presetId) {
    return encode(QByteArray::fromHex("2b49"), {{1, byteValue(presetId)}});
}

QByteArray buildQueryLowLatency() {
    return encode(QByteArray::fromHex("2b6c"), {{2, {}}});
}

QByteArray buildSetLowLatency(bool enabled) {
    return encode(QByteArray::fromHex("2b6c"), {{1, byteValue(enabled ? 1 : 0)}});
}

QByteArray buildQueryAutoPause() {
    return encode(QByteArray::fromHex("2b11"), {{1, {}}});
}

QByteArray buildSetAutoPause(bool enabled) {
    return encode(QByteArray::fromHex("2b10"), {{1, byteValue(enabled ? 1 : 0)}});
}

QByteArray buildQueryDualConnectEnabled() {
    return encode(QByteArray::fromHex("2b2f"), {{1, {}}});
}

QByteArray buildSetDualConnectEnabled(bool enabled) {
    return encode(QByteArray::fromHex("2b2e"), {{1, byteValue(enabled ? 1 : 0)}});
}

QByteArray buildEnumerateDualConnect() {
    return encode(QByteArray::fromHex("2b31"), {{1, {}}});
}

QByteArray buildDualConnectAction(const QString& mac, bool connect) {
    const QByteArray target = macBytes(mac);
    if (target.size() != 6) return {};
    return encode(QByteArray::fromHex("2b33"), {{connect ? 1 : 2, target}});
}

// ---- Incoming --------------------------------------------------------------

PacketEffect applyPacket(const Packet& packet, EarbudState& state,
                         const ModelProfile& profile) {
    PacketEffect effect;

    if (packet.command == QByteArray::fromHex("0108") ||
        packet.command == QByteArray::fromHex("0127")) {
        const QByteArray levels = packet.param(2);
        if (levels.size() >= 3) {
            auto applyBatt = [](BatteryReading& br, uint8_t raw) {
                if (raw <= 100) {
                    br.level = static_cast<int>(raw);
                    br.stale = false;
                } else {
                    br.level = std::nullopt;
                    br.stale = true;
                }
            };
            applyBatt(state.leftBattery, static_cast<uint8_t>(levels.at(0)));
            applyBatt(state.rightBattery, static_cast<uint8_t>(levels.at(1)));
            applyBatt(state.caseBattery, static_cast<uint8_t>(levels.at(2)));

            const QByteArray charging = packet.param(3);
            auto chargeFlag = [&charging](int idx) {
                return idx < charging.size() && static_cast<uint8_t>(charging.at(idx)) != 0;
            };
            state.leftBattery.charging = chargeFlag(0);
            state.rightBattery.charging = chargeFlag(1);
            state.caseBattery.charging = chargeFlag(2);
            effect.stateChanged = true;
        }
        return effect;
    }

    if (packet.command == QByteArray::fromHex("2b2a")) {
        const QByteArray value = packet.param(1);
        if (value.size() >= 2) {
            const int level = static_cast<uint8_t>(value.at(0));
            const int mode = static_cast<uint8_t>(value.at(1));
            if (auto mapped = modeFromValue(mode)) {
                state.noiseMode = *mapped;
                if (*mapped == NoiseMode::Anc) {
                    state.noiseEffect = level;
                } else if (*mapped == NoiseMode::Transparency) {
                    // 6i only exposes plain awareness (device level 2 = normal).
                    state.transparencyEffect = 1;
                }
                effect.stateChanged = true;
            }
        }
        return effect;
    }

    if (packet.command == QByteArray::fromHex("2b4a")) {
        const int current = eqCurrentPreset(packet);
        if (current >= 0) {
            state.audioEffect = current;
            effect.stateChanged = true;
        }
        return effect;
    }

    if (packet.command == QByteArray::fromHex("2b6c")) {
        if (packet.has(2) && packet.param(2).size() == 1) {
            state.lowLatency = packet.paramByte(2) != 0;
            effect.stateChanged = true;
        }
        return effect;
    }

    // FreeBuds 6i reports the per-earbud wear state in 2b25 (param 1 = right,
    // param 2 = left, 1 = in ear) instead of the legacy 2b03 notification.
    if (packet.command == QByteArray::fromHex("2b25")) {
        const QByteArray right = packet.param(1);
        const QByteArray left = packet.param(2);
        auto wearText = [](const QByteArray& raw) -> QString {
            if (raw.size() != 1) return {};
            return static_cast<uint8_t>(raw.at(0)) != 0 ? QStringLiteral("worn")
                                                        : QStringLiteral("in_case");
        };
        bool changed = false;
        if (left.size() == 1) {
            state.leftWear = wearText(left);
            changed = true;
        }
        if (right.size() == 1) {
            state.rightWear = wearText(right);
            changed = true;
        }
        effect.wearUpdate = changed;
        effect.stateChanged = changed;
        return effect;
    }

    if (packet.command == QByteArray::fromHex("2b03")) {
        const QByteArray left = packet.param(8);
        const QByteArray right = packet.param(9);
        auto wearText = [](const QByteArray& raw) -> QString {
            if (raw.size() != 1) return {};
            return static_cast<uint8_t>(raw.at(0)) != 0 ? QStringLiteral("worn")
                                                        : QStringLiteral("in_case");
        };
        bool changed = false;
        if (left.size() == 1) {
            state.leftWear = wearText(left);
            changed = true;
        }
        if (right.size() == 1) {
            state.rightWear = wearText(right);
            changed = true;
        }
        if (!changed) {
            const QString single = wearText(packet.param(8).isEmpty() ? packet.param(9) : packet.param(8));
            if (!single.isEmpty()) {
                state.leftWear = single;
                state.rightWear = single;
                changed = true;
            }
        }
        effect.stateChanged = changed;
        return effect;
    }

    if (packet.command == QByteArray::fromHex("0107")) {
        const QString firmware = decodeText(packet.param(7));
        if (!firmware.isEmpty() && firmware != state.firmware) {
            state.firmware = firmware;
            effect.stateChanged = true;
        }
        return effect;
    }

    if (packet.command == QByteArray::fromHex("2b11")) {
        if (profile.autoPause && packet.has(1) && packet.param(1).size() == 1) {
            state.wearDetection = packet.paramByte(1) != 0;
            effect.stateChanged = true;
        }
        return effect;
    }

    if (packet.command == QByteArray::fromHex("2b31")) {
        const QByteArray mac = packet.param(4);
        if (mac.size() != 6) return effect;
        MultiConnectDevice device;
        device.mac = mac;
        device.name = decodeText(packet.param(9));
        const int connected = packet.paramByte(5, 0);
        device.status = connected > 0 ? 2 : 0;
        effect.dualConnectRow = true;
        effect.dcCount = packet.paramByte(2, 0);
        effect.dcIndex = packet.paramByte(3, 0);
        effect.dcDevice = device;
        return effect;
    }

    return effect;
}

int eqCurrentPreset(const Packet& packet) {
    if (!packet.has(2) || packet.param(2).size() != 1) return -1;
    return packet.paramByte(2, -1);
}

QList<int> eqAvailablePresets(const Packet& packet) {
    QList<int> out;
    for (const char ch : packet.param(3)) {
        out.append(static_cast<uint8_t>(ch));
    }
    return out;
}

QString eqPresetLabel(int presetId) {
    const char* key = nullptr;
    switch (presetId) {
        case 1: key = "eq_standard"; break;
        case 2: key = "eq_bass"; break;
        case 3: key = "eq_treble"; break;
        case 9: key = "eq_vocal"; break;
        default: break;
    }
    return key ? trKey(key) : QString();
}

}  // namespace kalm::huawei
