// SPDX-License-Identifier: GPL-3.0-or-later
#include "transport.hpp"

#include "i18n.hpp"
#include "protocol.hpp"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace kalm {

namespace {

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#ifndef BTPROTO_RFCOMM
#define BTPROTO_RFCOMM 3
#endif

struct bdaddr_k {
    uint8_t b[6];
} __attribute__((packed));

struct sockaddr_rc_k {
    sa_family_t rc_family;
    bdaddr_k rc_bdaddr;
    uint8_t rc_channel;
};

constexpr int kCandidateChannels[] = {15, 13, 14, 12, 16, 11};

bool parseMacToBdaddr(const QString& mac, bdaddr_k& out) {
    const QStringList parts = mac.trimmed().split(u':');
    if (parts.size() != 6) return false;
    uint8_t be[6]{};
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        const int v = parts[i].toInt(&ok, 16);
        if (!ok || v < 0 || v > 255) return false;
        be[i] = static_cast<uint8_t>(v);
    }
    for (int i = 0; i < 6; ++i) {
        out.b[i] = be[5 - i];
    }
    return true;
}

QString channelCachePath() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.cache");
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

std::optional<RfcommConnection> tryConnectAndHandshake(const QString& mac, int channel, int timeoutMs) {
    UniqueSocket fd = openRfcommByChannel(mac, channel, timeoutMs);
    if (!fd) return std::nullopt;

    if (!sendAll(fd.get(), buildHandshake())) {
        return std::nullopt;
    }

    FrameDecoder decoder;
    QByteArray leftover;
    QElapsedTimer timer;
    timer.start();
    char buf[1024];
    while (timer.elapsed() < 1500) {
        const int rem = std::max(50, static_cast<int>(1500 - timer.elapsed()));
        const int pr = pollSocketRead(fd.get(), rem);
        if (pr <= 0) break;
        const qint64 n = recvSocket(fd.get(), buf, sizeof(buf));
        if (n <= 0) break;
        const QByteArray chunk(buf, static_cast<qsizetype>(n));
        leftover.append(chunk);
        const auto frames = decoder.feed(chunk);
        for (const auto& f : frames) {
            if (f.vendor == kGaiaVendor && f.command == kCmdHandshakeResp) {
                return RfcommConnection{std::move(fd), leftover};
            }
        }
    }

    return std::nullopt;
}

class SdpProfileReceiver : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Profile1")
public:
    int dupedFd = -1;
    int discoveredChannel = -1;
    QEventLoop* loop = nullptr;

public Q_SLOTS:
    void Release() {}
    void RequestDisconnection(const QDBusObjectPath&) {}
    void NewConnection(const QDBusObjectPath&, const QDBusUnixFileDescriptor& fd, const QVariantMap&) {
        if (fd.isValid()) {
            discoveredChannel = getRfcommPeerChannel(fd.fileDescriptor());
            dupedFd = ::fcntl(fd.fileDescriptor(), F_DUPFD_CLOEXEC, 0);
        }
        if (loop) loop->quit();
    }
};

std::optional<int> discoverChannelViaBluezSdp(const QString& devPath,
                                              const QString& uuidStr,
                                              UniqueSocket* outConnectedSock,
                                              int timeoutMs) {
    if (devPath.isEmpty() || uuidStr.isEmpty() || !QCoreApplication::instance()) return std::nullopt;

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) return std::nullopt;

    static int s_seq = 0;
    const QString profPath =
        QStringLiteral("/io/kalm/sdp_%1_%2").arg(QCoreApplication::applicationPid()).arg(++s_seq);
    SdpProfileReceiver receiver;
    QEventLoop loop;
    receiver.loop = &loop;

    if (!bus.registerObject(profPath, &receiver, QDBusConnection::ExportAllSlots)) {
        return std::nullopt;
    }

    QVariantMap opts{
        {QStringLiteral("Role"), QStringLiteral("client")},
        {QStringLiteral("RequireAuthentication"), false},
        {QStringLiteral("RequireAuthorization"), false},
    };
    QDBusMessage regMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"),
        QStringLiteral("/org/bluez"),
        QStringLiteral("org.bluez.ProfileManager1"),
        QStringLiteral("RegisterProfile"));
    regMsg << QVariant::fromValue(QDBusObjectPath(profPath))
           << uuidStr.toLower()
           << opts;
    const QDBusMessage regReply = bus.call(regMsg, QDBus::Block, 2000);
    if (regReply.type() == QDBusMessage::ErrorMessage) {
        bus.unregisterObject(profPath);
        return std::nullopt;
    }

    QDBusMessage connMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"),
        devPath,
        QStringLiteral("org.bluez.Device1"),
        QStringLiteral("ConnectProfile"));
    connMsg << uuidStr.toLower();
    bus.callWithCallback(connMsg, &loop, SLOT(quit()), SLOT(quit()), timeoutMs);

    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();

    if (outConnectedSock && receiver.dupedFd >= 0) {
        outConnectedSock->reset(receiver.dupedFd);
        receiver.dupedFd = -1;
    } else if (receiver.dupedFd >= 0) {
        ::close(receiver.dupedFd);
        receiver.dupedFd = -1;
    }

    // Note: UnregisterProfile closes BlueZ's side ONLY if not duped or if profile disconnects;
    // when outConnectedSock is null we only wanted channel discovery so unregister immediately.
    QDBusMessage unregMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"),
        QStringLiteral("/org/bluez"),
        QStringLiteral("org.bluez.ProfileManager1"),
        QStringLiteral("UnregisterProfile"));
    unregMsg << QVariant::fromValue(QDBusObjectPath(profPath));
    bus.call(unregMsg, QDBus::Block, 1000);
    bus.unregisterObject(profPath);

    if (receiver.discoveredChannel > 0) return receiver.discoveredChannel;
    return std::nullopt;
}

}  // namespace

void closeSocketHandle(SocketHandle s) noexcept {
    if (s >= 0) ::close(s);
}

void shutdownSocketHandle(SocketHandle s) noexcept {
    if (s >= 0) ::shutdown(s, SHUT_RDWR);
}

int pollSocketRead(SocketHandle s, int timeoutMs) noexcept {
    if (s < 0) return -1;
    pollfd pfd{s, POLLIN, 0};
    for (;;) {
        const int pr = ::poll(&pfd, 1, timeoutMs);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (pr == 0) return 0;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (pfd.revents & POLLIN) return 1;
            return -1;
        }
        return (pfd.revents & POLLIN) ? 1 : 0;
    }
}

qint64 recvSocket(SocketHandle s, char* buf, size_t maxLen) noexcept {
    if (s < 0) return -1;
    for (;;) {
        const ssize_t n = ::recv(s, buf, maxLen, 0);
        if (n < 0 && errno == EINTR) continue;
        return static_cast<qint64>(n);
    }
}

bool sendAll(SocketHandle s, const QByteArray& data) {
    if (s < 0) return false;
    qsizetype sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(s, data.constData() + sent,
                                 static_cast<size_t>(data.size() - sent), MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        sent += n;
    }
    return true;
}

SocketHandle openLocalIpcClient(const QString& path) {
    const QByteArray pathUtf8 = path.toUtf8();
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (static_cast<size_t>(pathUtf8.size()) >= sizeof(addr.sun_path)) return kInvalidSocket;
    std::memcpy(addr.sun_path, pathUtf8.constData(), static_cast<size_t>(pathUtf8.size()));

    UniqueSocket fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!fd) return kInvalidSocket;
    if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        return kInvalidSocket;
    }
    return fd.release();
}

SocketHandle openLocalIpcServer(const QString& path) {
    const QByteArray pathUtf8 = path.toUtf8();
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (static_cast<size_t>(pathUtf8.size()) >= sizeof(addr.sun_path)) return kInvalidSocket;
    std::memcpy(addr.sun_path, pathUtf8.constData(), static_cast<size_t>(pathUtf8.size()));

    UniqueSocket fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!fd) return kInvalidSocket;
    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        return kInvalidSocket;
    }
    ::chmod(pathUtf8.constData(), 0600);
    if (::listen(fd.get(), 8) < 0) {
        return kInvalidSocket;
    }
    return fd.release();
}

SocketHandle acceptLocalIpcClient(SocketHandle serverSock) {
    if (serverSock < 0) return kInvalidSocket;
    for (;;) {
        const int c = ::accept4(serverSock, nullptr, nullptr, SOCK_CLOEXEC);
        if (c < 0 && errno == EINTR) continue;
        return (c >= 0) ? c : kInvalidSocket;
    }
}

QList<BluezDevice> listBluezDevices(bool onlyVivo, bool onlyAudio) {
    QList<BluezDevice> result;
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) return result;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"),
        QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return result;
    }

    const auto rootArg = reply.arguments().at(0).value<QDBusArgument>();
    rootArg.beginMap();
    while (!rootArg.atEnd()) {
        rootArg.beginMapEntry();
        QDBusObjectPath objPath;
        rootArg >> objPath;

        QVariantMap devProps;
        QVariantMap battProps;
        bool hasDevice1 = false;

        rootArg.beginMap();
        while (!rootArg.atEnd()) {
            rootArg.beginMapEntry();
            QString ifaceName;
            QVariantMap props;
            rootArg >> ifaceName >> props;
            if (ifaceName == u"org.bluez.Device1") {
                hasDevice1 = true;
                devProps = props;
            } else if (ifaceName == u"org.bluez.Battery1") {
                battProps = props;
            }
            rootArg.endMapEntry();
        }
        rootArg.endMap();
        rootArg.endMapEntry();

        if (!hasDevice1) continue;
        const QString mac = devProps.value(QStringLiteral("Address")).toString().toUpper();
        if (mac.isEmpty()) continue;

        BluezDevice item;
        item.path = objPath.path();
        item.mac = mac;
        item.name = devProps.value(QStringLiteral("Alias")).toString();
        if (item.name.isEmpty()) item.name = devProps.value(QStringLiteral("Name")).toString();
        if (item.name.isEmpty()) item.name = mac;
        item.connected = devProps.value(QStringLiteral("Connected")).toBool();
        item.paired = devProps.value(QStringLiteral("Paired")).toBool();
        item.icon = devProps.value(QStringLiteral("Icon")).toString();
        item.modalias = devProps.value(QStringLiteral("Modalias")).toString();
        item.uuids = devProps.value(QStringLiteral("UUIDs")).toStringList();
        if (battProps.contains(QStringLiteral("Percentage"))) {
            item.batteryPct = battProps.value(QStringLiteral("Percentage")).toInt();
        }

        if (onlyVivo && !item.isSupportedTws()) continue;
        if (onlyAudio && !item.isAudioDevice()) continue;
        result.append(std::move(item));
    }
    rootArg.endMap();

    std::sort(result.begin(), result.end(), [](const BluezDevice& a, const BluezDevice& b) {
        if (a.isSupportedTws() != b.isSupportedTws()) return a.isSupportedTws() > b.isSupportedTws();
        if (a.connected != b.connected) return a.connected > b.connected;
        if (a.paired != b.paired) return a.paired > b.paired;
        return a.name.toLower() < b.name.toLower();
    });

    return result;
}

bool connectBluezDevice(const QString& mac) {
    const auto dev = findBluezByMac(mac);
    if (!dev) return false;
    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"),
        dev->path,
        QStringLiteral("org.bluez.Device1"),
        QStringLiteral("Connect"));
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 10000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

int getRfcommPeerChannel(SocketHandle s) noexcept {
    if (s < 0) return -1;
    sockaddr_rc_k peer{};
    socklen_t len = sizeof(peer);
    if (::getpeername(s, reinterpret_cast<sockaddr*>(&peer), &len) == 0) {
        return static_cast<int>(peer.rc_channel);
    }
    return -1;
}

UniqueSocket openRfcommByChannel(const QString& mac, int channel, int timeoutMs) {
    if (channel < 1 || channel > 30) return {};
    bdaddr_k bd{};
    if (!parseMacToBdaddr(mac, bd)) return {};

    UniqueSocket fd(::socket(AF_BLUETOOTH, SOCK_STREAM | SOCK_CLOEXEC, BTPROTO_RFCOMM));
    if (!fd) return {};

    const int origFlags = ::fcntl(fd.get(), F_GETFL, 0);
    ::fcntl(fd.get(), F_SETFL, origFlags | O_NONBLOCK);

    sockaddr_rc_k addr{};
    addr.rc_family = AF_BLUETOOTH;
    addr.rc_bdaddr = bd;
    addr.rc_channel = static_cast<uint8_t>(channel);

    const int rc = ::connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS && errno != EAGAIN) {
        return {};
    }
    if (rc < 0) {
        pollfd pfd{fd.get(), POLLOUT, 0};
        if (::poll(&pfd, 1, timeoutMs) <= 0 || !(pfd.revents & POLLOUT)) {
            return {};
        }
        int soErr = 0;
        socklen_t len = sizeof(soErr);
        if (::getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &soErr, &len) < 0 || soErr != 0) {
            return {};
        }
    }

    ::fcntl(fd.get(), F_SETFL, origFlags & ~O_NONBLOCK);
    return fd;
}

UniqueSocket openRfcommByUuid(const QString& mac,
                              const QString& devPath,
                              const QString& uuidStr,
                              int timeoutMs) {
    const auto sdpCh = discoverChannelViaBluezSdp(devPath, uuidStr, nullptr, timeoutMs);
    if (sdpCh && *sdpCh > 0) {
        // Give the remote Bluetooth stack a brief moment after BlueZ Profile1 teardown
        ::usleep(250 * 1000);
        return openRfcommByChannel(mac, *sdpCh, timeoutMs);
    }
    return {};
}

RfcommConnection openGaiaRfcomm(const QString& mac, const QString& devPath,
                                const QString& name, int timeoutMs) {
    const QString normMac = mac.trimmed().toUpper();
    QList<int> candidates;
    if (auto cached = loadCachedChannel(normMac)) {
        candidates.append(*cached);
    }
    for (int ch : kCandidateChannels) {
        if (!candidates.contains(ch)) candidates.append(ch);
    }

    const int primaryCount = std::min<int>(3, candidates.size());
    for (int i = 0; i < primaryCount; ++i) {
        if (auto res = tryConnectAndHandshake(normMac, candidates[i], timeoutMs)) {
            saveCachedChannel(normMac, candidates[i]);
            return std::move(*res);
        }
    }

    std::optional<int> sdpCh = discoverChannelViaBluezSdp(devPath, QString::fromLatin1(kVivoGaiaUuid), nullptr, 3500);
    if (sdpCh) {
        if (auto res = tryConnectAndHandshake(normMac, *sdpCh, timeoutMs)) {
            saveCachedChannel(normMac, *sdpCh);
            return std::move(*res);
        }
    }

    for (int i = primaryCount; i < candidates.size(); ++i) {
        if (sdpCh && candidates[i] == *sdpCh) continue;
        if (auto res = tryConnectAndHandshake(normMac, candidates[i], 1500)) {
            saveCachedChannel(normMac, candidates[i]);
            return std::move(*res);
        }
    }

    const QString title = name.isEmpty() ? normMac : QStringLiteral("%1 (%2)").arg(name, normMac);
    throw std::runtime_error(trKey("err_gaia_open").arg(title).toStdString());
}

}  // namespace kalm

#include "transport_linux.moc"
