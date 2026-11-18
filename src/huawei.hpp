// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protocol.hpp"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <optional>

// HUAWEI FreeBuds / FreeLace support.
//
// The earbuds use a proprietary "Z" framed SPP protocol (RFCOMM channel 1 on
// modern models). Packet layout:
//
//   5A | length (2, BE) | 00 | command (2) | TLV(params) | CRC16/XMODEM (2)
//
// `length` counts the command, all TLVs and one extra byte, i.e. the whole
// packet is `length + 5` bytes long.
//
// Protocol details were derived from the GPL-3 open source OpenFreebuds
// project (https://github.com/melianmiko/OpenFreebuds) and verified against a
// HUAWEI FreeBuds 6i.
namespace kalm::huawei {

inline constexpr uint8_t kMagic = 0x5A;

// ANC modes as sent by the device.
inline constexpr int kModeNormal = 0;
inline constexpr int kModeAnc = 1;
inline constexpr int kModeAwareness = 2;

struct Packet {
    QByteArray command;  // 2 bytes
    QMap<int, QByteArray> params;
    QByteArray raw;

    bool has(int type) const { return params.contains(type); }
    QByteArray param(int type) const { return params.value(type); }
    int paramByte(int type, int fallback = -1) const;
};

QByteArray crc16Xmodem(const QByteArray& data);
QByteArray encode(const QByteArray& command,
                  const QList<QPair<int, QByteArray>>& params = {});

class Decoder {
public:
    QList<Packet> feed(const QByteArray& chunk);

private:
    QByteArray m_buf;
};

struct ModelProfile {
    QString label;
    int sppChannel = 1;
    bool ancLevels = true;
    bool ancDynamic = false;
    bool transparencyVoiceBoost = false;
    bool equalizer = true;
    bool lowLatency = true;
    bool autoPause = true;
    bool dualConnect = true;
};

bool matchesName(const QString& deviceName);
const ModelProfile& profileFor(const QString& deviceName);
// Stable identifier of the selected profile (\<profile label\>), used by the
// UI to rebuild protocol-dependent models only when needed.
QString profileSignature(const QString& deviceName);

const QList<PresetEntry>& ancLevels(const ModelProfile& profile);
const QList<PresetEntry>& transparencyLevels(const ModelProfile& profile);
const QList<PresetEntry>& eqPresets(const ModelProfile& profile);

int toModeValue(NoiseMode mode);
std::optional<NoiseMode> modeFromValue(int value);

// ---- Packet builders -------------------------------------------------------

QByteArray buildQueryBattery();
QByteArray buildQueryAnc();
QByteArray buildSetAnc(int mode, std::optional<int> level = std::nullopt);
QByteArray buildQueryInfo();
QByteArray buildQueryEqualizer();
QByteArray buildSetEqualizerPreset(int presetId);
QByteArray buildQueryLowLatency();
QByteArray buildSetLowLatency(bool enabled);
QByteArray buildQueryAutoPause();
QByteArray buildSetAutoPause(bool enabled);
QByteArray buildQueryDualConnectEnabled();
QByteArray buildSetDualConnectEnabled(bool enabled);
QByteArray buildEnumerateDualConnect();
QByteArray buildDualConnectAction(const QString& mac, bool connect);

// ---- Incoming packets ------------------------------------------------------

struct PacketEffect {
    bool stateChanged = false;
    bool wearUpdate = false;
    bool dualConnectRow = false;
    int dcCount = 0;
    int dcIndex = 0;
    MultiConnectDevice dcDevice;
};

PacketEffect applyPacket(const Packet& packet, EarbudState& state,
                         const ModelProfile& profile);

// Equalizer response helpers (used by the session to follow device presets).
int eqCurrentPreset(const Packet& packet);
QList<int> eqAvailablePresets(const Packet& packet);

// Localized name of a HUAWEI equalizer preset id.
QString eqPresetLabel(int presetId);

}  // namespace kalm::huawei
