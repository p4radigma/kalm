// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <ws2bth.h>
#include <bluetoothapis.h>
#include <afunix.h>

#ifdef interface
#undef interface
#endif

#include "transport.hpp"

#include "i18n.hpp"
#include "protocol.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace kalm {

namespace {

constexpr GUID kVivoGaiaGuid = {
    0x00000837, 0xD102, 0x11E1, {0x9B, 0x23, 0x00, 0x02, 0x5B, 0x00, 0xA5, 0xA5}};

constexpr int kCandidateChannels[] = {15, 13, 14, 12, 16, 11};

void ensureWinsockInitialized() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
    });
}

bool parseMacToBthAddr(const QString& mac, BTH_ADDR& out) {
    const QStringList parts = mac.trimmed().split(u':');
    if (parts.size() != 6) return false;
    BTH_ADDR val = 0;
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        const int b = parts[i].toInt(&ok, 16);
        if (!ok || b < 0 || b > 255) return false;
        val = (val << 8) | static_cast<BTH_ADDR>(b);
    }
    out = val;
    return true;
}

QString formatBthBytes(const BYTE rgBytes[6]) {
    return QStringLiteral("%1:%2:%3:%4:%5:%6")
        .arg(rgBytes[5], 2, 16, QLatin1Char('0'))
        .arg(rgBytes[4], 2, 16, QLatin1Char('0'))
        .arg(rgBytes[3], 2, 16, QLatin1Char('0'))
        .arg(rgBytes[2], 2, 16, QLatin1Char('0'))
        .arg(rgBytes[1], 2, 16, QLatin1Char('0'))
        .arg(rgBytes[0], 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString guidToString(const GUID& g) {
    return QStringLiteral("%1-%2-%3-%4%5-%6%7%8%9%10%11")
        .arg(g.Data1, 8, 16, QLatin1Char('0'))
        .arg(g.Data2, 4, 16, QLatin1Char('0'))
        .arg(g.Data3, 4, 16, QLatin1Char('0'))
        .arg(g.Data4[0], 2, 16, QLatin1Char('0'))
        .arg(g.Data4[1], 2, 16, QLatin1Char('0'))
        .arg(g.Data4[2], 2, 16, QLatin1Char('0'))
        .arg(g.Data4[3], 2, 16, QLatin1Char('0'))
        .arg(g.Data4[4], 2, 16, QLatin1Char('0'))
        .arg(g.Data4[5], 2, 16, QLatin1Char('0'))
        .arg(g.Data4[6], 2, 16, QLatin1Char('0'))
        .arg(g.Data4[7], 2, 16, QLatin1Char('0'))
        .toLower();
}

QString channelCachePath() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    return base + QStringLiteral("/kalm/channels.json");
}

std::optional<int> loadCachedChannel(const QString& mac) {
    QFile f(channelCachePath());
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return std::nullopt;
    const int ch = doc.object().value(mac.toUpper()).toInt(-1);
    if (ch >= 1 && ch <= 30) return ch;
    return std::nullopt;
}

void saveCachedChannel(const QString& mac, int channel) {
    const QString path = channelCachePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonObject obj;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) obj = doc.object();
        }
    }
    obj.insert(mac.toUpper(), channel);
    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }
}

bool parseUuidToGuid(const QString& uuidStr, GUID& out) {
    const QUuid u(uuidStr.trimmed());
    if (u.isNull()) return false;
    out.Data1 = u.data1;
    out.Data2 = u.data2;
    out.Data3 = u.data3;
    std::memcpy(out.Data4, u.data4, 8);
    return true;
}

UniqueSocket connectRawSockaddrBth(const SOCKADDR_BTH& addr, int timeoutMs) {
    ensureWinsockInitialized();
    const SOCKET raw = ::socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (raw == INVALID_SOCKET) return {};
    UniqueSocket sock(static_cast<SocketHandle>(raw));

    u_long nonBlock = 1;
    ::ioctlsocket(raw, FIONBIO, &nonBlock);

    const int rc = ::connect(raw, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (rc == SOCKET_ERROR) {
        const int err = ::WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            return {};
        }
        WSAPOLLFD pfd{raw, POLLOUT, 0};
        if (::WSAPoll(&pfd, 1, timeoutMs) <= 0 || !(pfd.revents & POLLOUT)) {
            return {};
        }
        int soErr = 0;
        int len = sizeof(soErr);
        if (::getsockopt(raw, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len) == SOCKET_ERROR ||
            soErr != 0) {
            return {};
        }
    }

    nonBlock = 0;
    ::ioctlsocket(raw, FIONBIO, &nonBlock);
    return sock;
}

std::optional<RfcommConnection> tryConnectAddrAndHandshake(const SOCKADDR_BTH& addr, int timeoutMs) {
    UniqueSocket sock = connectRawSockaddrBth(addr, timeoutMs);
    if (!sock) return std::nullopt;

    if (!sendAll(sock.get(), buildHandshake())) {
        return std::nullopt;
    }

    FrameDecoder decoder;
    QByteArray leftover;
    QElapsedTimer timer;
    timer.start();
    char buf[1024];
    while (timer.elapsed() < 1500) {
        const int rem = std::max(50, static_cast<int>(1500 - timer.elapsed()));
        const int pr = pollSocketRead(sock.get(), rem);
        if (pr <= 0) break;
        const qint64 n = recvSocket(sock.get(), buf, sizeof(buf));
        if (n <= 0) break;
        const QByteArray chunk(buf, static_cast<qsizetype>(n));
        leftover.append(chunk);
        const auto frames = decoder.feed(chunk);
        for (const auto& f : frames) {
            if (f.vendor == kGaiaVendor && f.command == kCmdHandshakeResp) {
                return RfcommConnection{std::move(sock), leftover};
            }
        }
    }

    return std::nullopt;
}

}  // namespace

void closeSocketHandle(SocketHandle s) noexcept {
    if (s != kInvalidSocket) {
        ::closesocket(static_cast<SOCKET>(s));
    }
}

void shutdownSocketHandle(SocketHandle s) noexcept {
    if (s != kInvalidSocket) {
        ::shutdown(static_cast<SOCKET>(s), SD_BOTH);
    }
}

int pollSocketRead(SocketHandle s, int timeoutMs) noexcept {
    if (s == kInvalidSocket) return -1;
    ensureWinsockInitialized();
    WSAPOLLFD pfd{static_cast<SOCKET>(s), POLLRDNORM, 0};
    const int pr = ::WSAPoll(&pfd, 1, timeoutMs);
    if (pr < 0) return -1;
    if (pr == 0) return 0;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        if (pfd.revents & POLLRDNORM) return 1;
        return -1;
    }
    return (pfd.revents & POLLRDNORM) ? 1 : 0;
}

qint64 recvSocket(SocketHandle s, char* buf, size_t maxLen) noexcept {
    if (s == kInvalidSocket) return -1;
    const int n = ::recv(static_cast<SOCKET>(s), buf, static_cast<int>(maxLen), 0);
    return static_cast<qint64>(n);
}

bool sendAll(SocketHandle s, const QByteArray& data) {
    if (s == kInvalidSocket) return false;
    ensureWinsockInitialized();
    qsizetype sent = 0;
    while (sent < data.size()) {
        const int n = ::send(static_cast<SOCKET>(s),
                             data.constData() + sent,
                             static_cast<int>(data.size() - sent),
                             0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

SocketHandle openLocalIpcClient(const QString& path) {
    ensureWinsockInitialized();
    const QByteArray pathUtf8 = QDir::toNativeSeparators(path).toUtf8();
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (static_cast<size_t>(pathUtf8.size()) >= sizeof(addr.sun_path)) return kInvalidSocket;
    std::memcpy(addr.sun_path, pathUtf8.constData(), static_cast<size_t>(pathUtf8.size()));

    const SOCKET raw = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (raw == INVALID_SOCKET) return kInvalidSocket;
    UniqueSocket s(static_cast<SocketHandle>(raw));
    if (::connect(raw, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        return kInvalidSocket;
    }
    return s.release();
}

SocketHandle openLocalIpcServer(const QString& path) {
    ensureWinsockInitialized();
    const QByteArray pathUtf8 = QDir::toNativeSeparators(path).toUtf8();
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (static_cast<size_t>(pathUtf8.size()) >= sizeof(addr.sun_path)) return kInvalidSocket;
    std::memcpy(addr.sun_path, pathUtf8.constData(), static_cast<size_t>(pathUtf8.size()));

    const SOCKET raw = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (raw == INVALID_SOCKET) return kInvalidSocket;
    UniqueSocket s(static_cast<SocketHandle>(raw));
    if (::bind(raw, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        return kInvalidSocket;
    }
    if (::listen(raw, 8) == SOCKET_ERROR) {
        return kInvalidSocket;
    }
    return s.release();
}

SocketHandle acceptLocalIpcClient(SocketHandle serverSock) {
    if (serverSock == kInvalidSocket) return kInvalidSocket;
    const SOCKET c = ::accept(static_cast<SOCKET>(serverSock), nullptr, nullptr);
    return (c != INVALID_SOCKET) ? static_cast<SocketHandle>(c) : kInvalidSocket;
}

QList<BluezDevice> listBluezDevices(bool onlyVivo, bool onlyAudio) {
    QList<BluezDevice> result;

    BLUETOOTH_DEVICE_SEARCH_PARAMS sp{};
    sp.dwSize = sizeof(sp);
    sp.fReturnAuthenticated = TRUE;
    sp.fReturnRemembered = TRUE;
    sp.fReturnUnknown = FALSE;
    sp.fReturnConnected = TRUE;
    sp.fIssueInquiry = FALSE;
    sp.cTimeoutMultiplier = 0;
    sp.hRadio = nullptr;

    BLUETOOTH_DEVICE_INFO bdi{};
    bdi.dwSize = sizeof(bdi);

    HBLUETOOTH_DEVICE_FIND hFind = ::BluetoothFindFirstDevice(&sp, &bdi);
    if (!hFind) return result;

    do {
        BluezDevice item;
        item.mac = formatBthBytes(bdi.Address.rgBytes);
        item.path = item.mac;
        item.name = QString::fromWCharArray(bdi.szName).trimmed();
        if (item.name.isEmpty()) item.name = item.mac;
        item.connected = (bdi.fConnected != FALSE);
        item.paired = (bdi.fAuthenticated != FALSE) || (bdi.fRemembered != FALSE);

        // Bluetooth Major Device Class 0x04 = Audio/Video
        const ULONG majorClass = (bdi.ulClassofDevice >> 8) & 0x1F;
        if (majorClass == 0x04) {
            item.icon = QStringLiteral("audio-headset");
        }
        item.modalias = QStringLiteral("cod:0x%1").arg(bdi.ulClassofDevice, 6, 16, QLatin1Char('0'));

        DWORD numServices = 0;
        ::BluetoothEnumerateInstalledServices(nullptr, &bdi, &numServices, nullptr);
        if (numServices > 0 && numServices <= 64) {
            std::vector<GUID> guids(numServices);
            if (::BluetoothEnumerateInstalledServices(nullptr, &bdi, &numServices, guids.data()) == ERROR_SUCCESS) {
                for (DWORD i = 0; i < numServices; ++i) {
                    item.uuids.append(guidToString(guids[i]));
                }
            }
        }

        if (onlyVivo && !item.isSupportedTws()) continue;
        if (onlyAudio && !item.isAudioDevice()) continue;
        result.append(std::move(item));

        bdi = BLUETOOTH_DEVICE_INFO{};
        bdi.dwSize = sizeof(bdi);
    } while (::BluetoothFindNextDevice(hFind, &bdi));

    ::BluetoothFindDeviceClose(hFind);

    std::sort(result.begin(), result.end(), [](const BluezDevice& a, const BluezDevice& b) {
        if (a.isSupportedTws() != b.isSupportedTws()) return a.isSupportedTws() > b.isSupportedTws();
        if (a.connected != b.connected) return a.connected > b.connected;
        if (a.paired != b.paired) return a.paired > b.paired;
        return a.name.toLower() < b.name.toLower();
    });

    return result;
}

bool connectBluezDevice(const QString& mac) {
    // On Windows, opening an RFCOMM socket to a paired device pages and connects it at the OS level.
    try {
        auto conn = openGaiaRfcomm(mac, {}, {}, 5000);
        return static_cast<bool>(conn.fd);
    } catch (...) {
        return false;
    }
}

int getRfcommPeerChannel(SocketHandle s) noexcept {
    if (s == kInvalidSocket) return -1;
    SOCKADDR_BTH peer{};
    int len = sizeof(peer);
    if (::getpeername(static_cast<SOCKET>(s), reinterpret_cast<sockaddr*>(&peer), &len) == 0) {
        return static_cast<int>(peer.port);
    }
    return -1;
}

UniqueSocket openRfcommByChannel(const QString& mac, int channel, int timeoutMs) {
    if (channel < 1 || channel > 30) return {};
    BTH_ADDR btAddr = 0;
    if (!parseMacToBthAddr(mac, btAddr)) return {};

    SOCKADDR_BTH sa{};
    sa.addressFamily = AF_BTH;
    sa.btAddr = btAddr;
    sa.port = static_cast<ULONG>(channel);
    return connectRawSockaddrBth(sa, timeoutMs);
}

UniqueSocket openRfcommByUuid(const QString& mac,
                              const QString& /*devPath*/,
                              const QString& uuidStr,
                              int timeoutMs) {
    BTH_ADDR btAddr = 0;
    GUID svcGuid{};
    if (!parseMacToBthAddr(mac, btAddr) || !parseUuidToGuid(uuidStr, svcGuid)) return {};

    SOCKADDR_BTH sa{};
    sa.addressFamily = AF_BTH;
    sa.btAddr = btAddr;
    sa.serviceClassId = svcGuid;
    sa.port = BT_PORT_ANY;
    return connectRawSockaddrBth(sa, timeoutMs);
}

RfcommConnection openGaiaRfcomm(const QString& mac, const QString& /*devPath*/,
                                const QString& name, int timeoutMs) {
    const QString normMac = mac.trimmed().toUpper();
    BTH_ADDR btAddr = 0;
    if (!parseMacToBthAddr(normMac, btAddr)) {
        throw std::runtime_error(trKey("err_invalid_mac").arg(mac).toStdString());
    }

    // 1. Try cached channel first if known (fastest ~60ms path)
    if (auto cached = loadCachedChannel(normMac)) {
        SOCKADDR_BTH sa{};
        sa.addressFamily = AF_BTH;
        sa.btAddr = btAddr;
        sa.port = static_cast<ULONG>(*cached);
        if (auto res = tryConnectAddrAndHandshake(sa, timeoutMs)) {
            return std::move(*res);
        }
    }

    // 2. Try Winsock SDP resolution by Vivo GAIA Service UUID
    {
        SOCKADDR_BTH sa{};
        sa.addressFamily = AF_BTH;
        sa.btAddr = btAddr;
        sa.serviceClassId = kVivoGaiaGuid;
        sa.port = BT_PORT_ANY;
        if (auto res = tryConnectAddrAndHandshake(sa, timeoutMs + 1000)) {
            const int ch = getRfcommPeerChannel(res->fd.get());
            if (ch > 0) saveCachedChannel(normMac, ch);
            return std::move(*res);
        }
    }

    // 3. Fallback scan of known vivo RFCOMM channels (15 for TWS 5, 13 for TWS 3e/Air3 Pro, etc.)
    for (int ch : kCandidateChannels) {
        SOCKADDR_BTH sa{};
        sa.addressFamily = AF_BTH;
        sa.btAddr = btAddr;
        sa.port = static_cast<ULONG>(ch);
        if (auto res = tryConnectAddrAndHandshake(sa, 1600)) {
            saveCachedChannel(normMac, ch);
            return std::move(*res);
        }
    }

    const QString title = name.isEmpty() ? normMac : QStringLiteral("%1 (%2)").arg(name, normMac);
    throw std::runtime_error(trKey("err_gaia_open").arg(title).toStdString());
}

}  // namespace kalm
