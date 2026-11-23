// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protocol.hpp"

#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>
#include <optional>

// Nothing Ear / CMF Buds RFCOMM SPP protocol ("0x55" framed protocol).
//
// Primary RFCOMM service UUID: aeac4a03-dff5-498f-843a-34487cf133eb
// Fallback RFCOMM channels: 15 (Nothing Ear), 16 (CMF Buds 2), 28 (CMF Headphone Pro).
//
// Packet layout (little-endian):
//   55 | ctrl (2, LE, 0x0160) | cmd (2, LE) | len (2, LE) | opId (1) | payload (len) | crc16 (2, LE)
//
// Protocol details cross-checked against GPL-3.0 SwiftNothingEar, MIT omarchy-nothing-ear,
// and Nothing X 3.10.3 interoperability analysis (CMF Buds 2 Plus = model B184).
namespace kalm::nothing {

inline constexpr uint8_t kSof = 0x55;
inline constexpr uint16_t kCtrlWithCrc = 0x0160;
inline constexpr uint16_t kMaskCrc = 0x0020;

// Request READ (0xC0xx)
inline constexpr uint16_t kCmdReadProtocolVersion = 0xC001;
inline constexpr uint16_t kCmdReadRingBuds = 0xC002;
inline constexpr uint16_t kCmdReadDeviceConfig = 0xC006;
inline constexpr uint16_t kCmdReadBattery = 0xC007;
inline constexpr uint16_t kCmdReadWearStatus = 0xC00A;
inline constexpr uint16_t kCmdReadInEarDetection = 0xC00E;
inline constexpr uint16_t kCmdReadAnc = 0xC01E;
inline constexpr uint16_t kCmdReadEq = 0xC01F;
inline constexpr uint16_t kCmdReadLowLatency = 0xC041;
inline constexpr uint16_t kCmdReadFirmware = 0xC042;
inline constexpr uint16_t kCmdReadListeningMode = 0xC050;

// Request WRITE (0xF0xx)
inline constexpr uint16_t kCmdWriteRingBuds = 0xF002;
inline constexpr uint16_t kCmdWriteInEarDetection = 0xF004;
inline constexpr uint16_t kCmdWriteAnc = 0xF00F;
inline constexpr uint16_t kCmdWriteEq = 0xF010;
inline constexpr uint16_t kCmdWriteListeningMode = 0xF01D;
inline constexpr uint16_t kCmdWriteLowLatency = 0xF040;

// Responses (0x40xx / 0x70xx) & Notifications (0xE0xx / 0x60xx)
inline constexpr uint16_t kRespProtocolVersion = 0x4001;
inline constexpr uint16_t kRespRingBuds = 0x4002;
inline constexpr uint16_t kRespDeviceConfig = 0x4006;
inline constexpr uint16_t kRespBattery = 0x4007;
inline constexpr uint16_t kRespWearStatus = 0x400A;
inline constexpr uint16_t kRespInEarDetection = 0x400E;
inline constexpr uint16_t kRespAnc = 0x401E;
inline constexpr uint16_t kRespEq = 0x401F;
inline constexpr uint16_t kRespLowLatency = 0x4041;
inline constexpr uint16_t kRespFirmware = 0x4042;
inline constexpr uint16_t kRespListeningMode = 0x4050;

inline constexpr uint16_t kAckRingBuds = 0x7002;
inline constexpr uint16_t kAckInEarDetection = 0x7004;
inline constexpr uint16_t kAckAnc = 0x700F;
inline constexpr uint16_t kAckEq = 0x7010;
inline constexpr uint16_t kAckListeningMode = 0x701D;
inline constexpr uint16_t kAckLowLatency = 0x7040;

inline constexpr uint16_t kNotifyBattery = 0xE001;
inline constexpr uint16_t kNotifyWearStatus = 0xE002;
inline constexpr uint16_t kNotifyAnc = 0xE003;
inline constexpr uint16_t kNotifyAncAlt = 0x6003;
inline constexpr uint16_t kNotifyLowLatency = 0xE005;

// Wire ANC / Noise mode values
inline constexpr uint8_t kWireAncHigh = 0x01;
inline constexpr uint8_t kWireAncMid = 0x02;
inline constexpr uint8_t kWireAncLow = 0x03;
inline constexpr uint8_t kWireAncAdaptive = 0x04;
inline constexpr uint8_t kWireOff = 0x05;
inline constexpr uint8_t kWireTransparency = 0x07;

struct Packet {
    uint16_t control = 0;
    uint16_t command = 0;
    uint8_t opId = 0;
    QByteArray payload;
    QByteArray raw;
};

uint16_t crc16Modbus(const QByteArray& data);
QByteArray encode(uint16_t command, const QByteArray& payload = {}, uint8_t opId = 1);

class Decoder {
public:
    QList<Packet> feed(const QByteArray& chunk);

private:
    QByteArray m_buf;
};

struct ModelProfile {
    QString label;
    bool anc = true;
    bool ancLevels = true;
    bool useListeningModeEq = false;  // Dirac Opteo / Listening mode (CMF Buds 2 / 2 Plus / 2a / Pro 2, Ear 3)
    bool isBuds2a = false;            // CMF Buds 2a uses 0x07 for default balanced preset
    bool isOverEar = false;           // Headphone (1) / CMF Headphone Pro single battery & ring byte 0x06
    bool isLegacyRing = false;        // Nothing Ear (1) uses 1-byte ring payload [0x01/0x00]
    bool equalizer = true;
    bool lowLatency = true;
    bool inEarDetection = true;
    bool findDevice = true;
};

bool matchesName(const QString& deviceName);
const ModelProfile& profileFor(const QString& deviceName);

const QList<PresetEntry>& ancLevels(const ModelProfile& profile);
const QList<PresetEntry>& eqPresets(const ModelProfile& profile);

QString ancLevelLabel(int code);
QString eqPresetLabel(int presetId, const ModelProfile& profile);

// ---- Packet builders -------------------------------------------------------

QByteArray buildProtocolActivate(uint8_t opId = 1);
QByteArray buildQueryBattery(uint8_t opId = 1);
QByteArray buildQueryFirmware(uint8_t opId = 1);
QByteArray buildQueryWearStatus(uint8_t opId = 1);
QByteArray buildQueryAnc(uint8_t opId = 1);
QByteArray buildSetAnc(NoiseMode mode, std::optional<int> ancLevel = std::nullopt, uint8_t opId = 1);
QByteArray buildQueryEq(const ModelProfile& profile, uint8_t opId = 1);
QByteArray buildSetEq(int presetId, const ModelProfile& profile, uint8_t opId = 1);
QByteArray buildQueryLowLatency(uint8_t opId = 1);
QByteArray buildSetLowLatency(bool enabled, uint8_t opId = 1);
QByteArray buildQueryInEarDetection(uint8_t opId = 1);
QByteArray buildSetInEarDetection(bool enabled, uint8_t opId = 1);
QByteArray buildSetRingBud(uint8_t budId, bool ring, uint8_t opId = 1);
QList<QByteArray> buildSetRingBuds(bool ring, const ModelProfile& profile, uint8_t opId = 1);

// ---- Incoming packets ------------------------------------------------------

bool applyPacket(const Packet& packet, EarbudState& state, const ModelProfile& profile);

}  // namespace kalm::nothing
