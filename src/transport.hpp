// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protocol.hpp"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <optional>
#include <utility>

namespace kalm {

#ifdef _WIN32
using SocketHandle = uintptr_t;
inline constexpr SocketHandle kInvalidSocket = ~static_cast<uintptr_t>(0);
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
#endif

void closeSocketHandle(SocketHandle s) noexcept;
void shutdownSocketHandle(SocketHandle s) noexcept;
// Returns >0 if readable, 0 on timeout, <0 on error/hangup
int pollSocketRead(SocketHandle s, int timeoutMs) noexcept;
qint64 recvSocket(SocketHandle s, char* buf, size_t maxLen) noexcept;
bool sendAll(SocketHandle s, const QByteArray& data);

SocketHandle openLocalIpcClient(const QString& path);
SocketHandle openLocalIpcServer(const QString& path);
SocketHandle acceptLocalIpcClient(SocketHandle serverSock);

// Move-only RAII socket wrapper preventing descriptor/HANDLE leaks on all OSes.
class UniqueSocket {
public:
    UniqueSocket() noexcept = default;
    explicit UniqueSocket(SocketHandle s) noexcept : m_sock(s) {}
    ~UniqueSocket() { reset(); }

    UniqueSocket(const UniqueSocket&) = delete;
    UniqueSocket& operator=(const UniqueSocket&) = delete;

    UniqueSocket(UniqueSocket&& other) noexcept
        : m_sock(std::exchange(other.m_sock, kInvalidSocket)) {}

    UniqueSocket& operator=(UniqueSocket&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.m_sock, kInvalidSocket));
        }
        return *this;
    }

    SocketHandle get() const noexcept { return m_sock; }
    explicit operator bool() const noexcept { return m_sock != kInvalidSocket; }

    SocketHandle release() noexcept { return std::exchange(m_sock, kInvalidSocket); }

    void reset(SocketHandle newSock = kInvalidSocket) noexcept {
        if (m_sock != kInvalidSocket) {
            closeSocketHandle(m_sock);
        }
        m_sock = newSock;
    }

private:
    SocketHandle m_sock = kInvalidSocket;
};

using UniqueFd = UniqueSocket;

struct BluezDevice {
    QString path;
    QString mac;
    QString name;
    bool connected = false;
    bool paired = false;
    QString icon;
    QString modalias;
    QStringList uuids;
    std::optional<int> batteryPct;

    bool isVivoTws() const;
    bool isRealmeOppoTws() const;
    bool isHuaweiTws() const;
    bool isSupportedTws() const { return isVivoTws() || isRealmeOppoTws() || isHuaweiTws(); }
    bool isAudioDevice() const;
};

QList<BluezDevice> listBluezDevices(bool onlySupported = false, bool onlyAudio = false);
std::optional<BluezDevice> findBluezByMac(const QString& mac);
bool connectBluezDevice(const QString& mac);

struct RfcommConnection {
    UniqueSocket fd;
    QByteArray initialBytes;
    DeviceProtocol protocol = DeviceProtocol::VivoGaia;
};

// Opens a raw RFCOMM socket via 128-bit Service UUID (SDP lookup) without sending any protocol bytes.
UniqueSocket openRfcommByUuid(const QString& mac,
                              const QString& devPath,
                              const QString& uuidStr,
                              int timeoutMs = 2500);

// Opens a raw RFCOMM socket directly to a numbered RFCOMM channel (1..30) without sending any bytes.
UniqueSocket openRfcommByChannel(const QString& mac, int channel, int timeoutMs = 1500);

// Opens a verified GAIA RFCOMM socket (handshake 0x0300 -> 0x8300 verified).
// Throws std::runtime_error if connection fails.
RfcommConnection openGaiaRfcomm(const QString& mac,
                                const QString& devPath = {},
                                const QString& name = {},
                                int timeoutMs = 2500);

// Opens a verified OPPO / OnePlus / realme Link "TL" (0xAA) RFCOMM socket (0x0100 -> 0x8100 verified).
std::optional<RfcommConnection> tryOpenRealmeOppoRfcomm(const QString& mac,
                                                        const QString& devPath = {},
                                                        const QStringList& uuids = {},
                                                        int timeoutMs = 2500);

// Opens an RFCOMM socket to a HUAWEI device (SPP channel 1 on modern models)
// and verifies it by sending a battery query (0x0108).
RfcommConnection openHuaweiRfcomm(const QString& mac,
                                  const QString& devPath = {},
                                  const QString& name = {},
                                  const QStringList& uuids = {},
                                  int timeoutMs = 2500);

// Auto-negotiates Vivo GAIA, realme/OPPO/OnePlus TL, or HUAWEI SPP protocol for the target earbuds.
RfcommConnection openEarbudRfcomm(const QString& mac,
                                  const QString& devPath = {},
                                  const QString& name = {},
                                  const QStringList& uuids = {},
                                  int timeoutMs = 2500);

int getRfcommPeerChannel(SocketHandle s) noexcept;

}  // namespace kalm
