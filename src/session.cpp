// SPDX-License-Identifier: GPL-3.0-or-later
#include "session.hpp"

#include "i18n.hpp"
#include "probe.hpp"
#include "transport.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QMutexLocker>
#include <QStandardPaths>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

#ifndef _WIN32
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <unistd.h>
#endif

namespace kalm {

QString ipcSocketPath() {
#ifdef _WIN32
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    const QString dir = base + QStringLiteral("/kalm");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/kalm.sock");
#else
    QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtimeDir.isEmpty()) {
        runtimeDir = QStringLiteral("/tmp/kalm-%1").arg(::getuid());
    }
    QDir().mkpath(runtimeDir);
    return runtimeDir + QStringLiteral("/kalm.sock");
#endif
}

std::optional<QJsonObject> ipcRequest(const QJsonObject& payload, int timeoutMs) {
    const QString path = ipcSocketPath();
    if (!QFile::exists(path)) return std::nullopt;

    UniqueSocket fd(openLocalIpcClient(path));
    if (!fd) return std::nullopt;

    const QByteArray reqBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if (!sendAll(fd.get(), reqBytes)) {
        return std::nullopt;
    }

    QByteArray respBytes;
    QElapsedTimer timer;
    timer.start();
    char buf[4096];
    while (timer.elapsed() < timeoutMs) {
        const int rem = std::max(50, static_cast<int>(timeoutMs - timer.elapsed()));
        const int pr = pollSocketRead(fd.get(), rem);
        if (pr <= 0) break;
        const qint64 n = recvSocket(fd.get(), buf, sizeof(buf));
        if (n <= 0) break;
        respBytes.append(buf, static_cast<qsizetype>(n));
    }

    if (respBytes.isEmpty()) return std::nullopt;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(respBytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    return doc.object();
}

namespace {

int wornBudCount(const EarbudState& state) {
    int count = 0;
    if (state.leftWear == u"worn") ++count;
    if (state.rightWear == u"worn") ++count;
    return count;
}

#ifndef _WIN32
QString findPlayingMprisPlayer() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return {};
    const QStringList services = bus.interface()->registeredServiceNames().value();
    for (const QString& name : services) {
        if (!name.startsWith(QStringLiteral("org.mpris.MediaPlayer2"))) continue;
        QDBusMessage msg = QDBusMessage::createMethodCall(
            name, QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        msg << QStringLiteral("org.mpris.MediaPlayer2.Player") << QStringLiteral("PlaybackStatus");
        const QDBusMessage reply = bus.call(msg, QDBus::Block, 350);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) continue;
        const QVariant value = reply.arguments().first();
        const QVariant inner =
            value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant() : value;
        if (inner.toString() == u"Playing") return name;
    }
    return {};
}

bool callMprisPlayer(const QString& service, const QString& method) {
    if (service.isEmpty()) return false;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    const QDBusMessage reply = bus.call(
        QDBusMessage::createMethodCall(service, QStringLiteral("/org/mpris/MediaPlayer2"),
                                       QStringLiteral("org.mpris.MediaPlayer2.Player"), method),
        QDBus::Block, 400);
    return reply.type() == QDBusMessage::ReplyMessage;
}
#else
QString findPlayingMprisPlayer() { return {}; }
bool callMprisPlayer(const QString&, const QString&) { return false; }
#endif

DeviceProtocol guessProtocolFromIdentity(const QString& rawName, const QStringList& uuids = {}) {
    if (huawei::matchesName(rawName)) return DeviceProtocol::HuaweiSpp;
    for (const QString& u : uuids) {
        const QString low = u.toLower();
        if (low == QLatin1String(kRealmeOppoTlUuid) ||
            low == QLatin1String(kOppoHeyMelodyUuid) ||
            low == QLatin1String(kOppoHeyMelodyAltUuid)) {
            return DeviceProtocol::RealmeOppoTl;
        }
    }
    const QString low = rawName.toLower();
    if (low.contains(u"realme") || low.contains(u"oppo") || low.contains(u"oneplus")) {
        return DeviceProtocol::RealmeOppoTl;
    }
    return DeviceProtocol::VivoGaia;
}

}  // namespace

// ---- KalmSession ----

KalmSession::KalmSession(DeviceRegistry& registry,
                         const QString& overrideMac,
                         StateCallback onStateChanged,
                         DevicesCallback onDevicesChanged,
                         ErrorCallback onError)
    : m_registry(registry),
      m_overrideMac(overrideMac.trimmed().toUpper()),
      m_onStateChanged(std::move(onStateChanged)),
      m_onDevicesChanged(std::move(onDevicesChanged)),
      m_onError(std::move(onError)),
      m_profile(selectProfile({})) {}

KalmSession::~KalmSession() { stop(); }

EarbudState KalmSession::snapshot() const {
    QMutexLocker lock(&m_lock);
    return m_state;
}

SessionCapabilities KalmSession::capabilities() const {
    SessionCapabilities caps;
    DeviceProtocol proto;
    QString name;
    {
        QMutexLocker lock(&m_lock);
        proto = m_protocol;
        name = m_modelName;
    }
    if (name.isEmpty()) {
        if (auto active = m_registry.getActive()) {
            name = active->name;
            if (!m_state.connected) {
                proto = guessProtocolFromIdentity(name);
            }
        }
    }

    if (proto == DeviceProtocol::HuaweiSpp) {
        const auto& profile = huawei::profileFor(name);
        caps.protocol = DeviceProtocol::HuaweiSpp;
        caps.profileLabel = profile.label;
        caps.ancLevelList = huawei::ancLevels(profile);
        caps.transparencyList = huawei::transparencyLevels(profile);
        caps.eqList = huawei::eqPresets(profile);
        caps.cycleList.clear();
        caps.ancLevels = profile.ancLevels && caps.ancLevelList.size() > 1;
        caps.transparency = caps.transparencyList.size() > 1;
        caps.equalizer = profile.equalizer;
        caps.gameMode = profile.lowLatency;
        caps.wearDetection = profile.autoPause;
        caps.noiseCycle = false;
        caps.multipoint = profile.dualConnect;
        caps.findDevice = false;
        caps.probe = true;
        return caps;
    }

    if (proto == DeviceProtocol::RealmeOppoTl) {
        caps.protocol = DeviceProtocol::RealmeOppoTl;
        caps.profileLabel = QStringLiteral("realme / OPPO Link");
        caps.ancLevels = false;
        caps.transparency = false;
        caps.equalizer = false;
        caps.gameMode = false;
        caps.wearDetection = false;
        caps.noiseCycle = false;
        caps.multipoint = false;
        caps.findDevice = false;
        caps.probe = true;
        return caps;
    }

    caps.protocol = DeviceProtocol::VivoGaia;
    caps.profileLabel = QStringLiteral("vivo / iQOO GAIA");
    for (const auto& a : ancLevelPresets()) caps.ancLevelList.append(a);
    for (const auto& t : transparencyPresets()) caps.transparencyList.append(t);
    for (const auto& e : audioEffectPresets()) caps.eqList.append(e);
    for (const auto& c : noiseCyclePresets()) caps.cycleList.append(c);
    return caps;
}

void KalmSession::updateProtocolHint(const QString& rawName, const QStringList& uuids) {
    QMutexLocker lock(&m_lock);
    m_modelName = rawName;
    const DeviceProtocol guessed = guessProtocolFromIdentity(rawName, uuids);
    m_protocol = guessed;
    m_state.protocol = guessed;
    if (guessed == DeviceProtocol::HuaweiSpp) {
        m_hwProfile = huawei::profileFor(rawName);
    }
}

void KalmSession::wakeWorker() {
    QMutexLocker lock(&m_wakeMtx);
    m_woken = true;
    m_wakeCv.wakeAll();
}

void KalmSession::start() {
    m_registry.ensureInitialImport();
    if (auto active = m_registry.getActive()) {
        QMutexLocker lock(&m_lock);
        m_state.deviceName = active->displayName();
        m_state.deviceMac = active->mac;
        updateProtocolHint(active->name);
    }
    if (m_worker.joinable()) return;
    m_stop.store(false);
    m_worker = std::thread(&KalmSession::runLoop, this);
}

void KalmSession::stop() {
    m_stop.store(true);
    wakeWorker();
    closeSocket(false);
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

bool KalmSession::switchToDevice(const QString& mac) {
    if (!m_registry.setActive(mac)) return false;
    m_overrideMac.clear();
    const auto active = m_registry.getActive();
    closeSocket(true);
    if (active) {
        QMutexLocker lock(&m_lock);
        m_state.deviceName = active->displayName();
        m_state.deviceMac = active->mac;
        updateProtocolHint(active->name);
    }
    emitDevices();
    emitState();
    wakeWorker();
    return true;
}

void KalmSession::notifyRegistryUpdated() {
    const auto active = m_registry.getActive();
    bool needDrop = false;
    {
        QMutexLocker lock(&m_lock);
        if (!active) {
            needDrop = !m_state.deviceMac.isEmpty();
        } else if (!m_state.deviceMac.isEmpty() && active->mac != m_state.deviceMac) {
            needDrop = true;
        } else {
            m_state.deviceName = active->displayName();
        }
    }
    if (needDrop) {
        closeSocket(true);
        if (active) {
            QMutexLocker lock(&m_lock);
            m_state.deviceName = active->displayName();
            m_state.deviceMac = active->mac;
            updateProtocolHint(active->name);
        }
    }
    emitDevices();
    emitState();
    wakeWorker();
}

void KalmSession::forceReconnect() {
    closeSocket(false);
    wakeWorker();
}

void KalmSession::emitState() {
    if (m_onStateChanged) m_onStateChanged(snapshot());
}

void KalmSession::emitDevices() {
    if (m_onDevicesChanged) m_onDevicesChanged();
}

void KalmSession::emitError(const QString& msg) {
    if (m_onError) m_onError(msg);
}

void KalmSession::closeSocket(bool resetIdentity) {
    SocketHandle fd = kInvalidSocket;
    bool wasConnected = false;
    {
        QMutexLocker lock(&m_lock);
        fd = m_sockFd;
        m_sockFd = kInvalidSocket;
        wasConnected = m_state.connected;
        m_hwDcRows.clear();
        m_hwDcCount = 0;
        m_mprisPausedByWear = false;
        m_mprisService.clear();
        if (resetIdentity) {
            m_state = EarbudState{};
        } else {
            m_state.connected = false;
            m_state.leftBattery.stale = true;
            m_state.rightBattery.stale = true;
            m_state.caseBattery.stale = true;
            m_state.leftWear.clear();
            m_state.rightWear.clear();
        }
    }
    if (fd != kInvalidSocket) {
        shutdownSocketHandle(fd);
        closeSocketHandle(fd);
    }
    if (wasConnected || resetIdentity) {
        emitState();
    }
}

bool KalmSession::sendRaw(const QByteArray& data) {
    SocketHandle fd = kInvalidSocket;
    {
        QMutexLocker lock(&m_lock);
        fd = m_sockFd;
    }
    if (fd == kInvalidSocket) return false;
    if (!sendAll(fd, data)) {
        closeSocket(false);
        return false;
    }
    return true;
}

std::optional<Frame> KalmSession::sendAndWait(const QByteArray& packet,
                                              std::function<bool(const Frame&)> pred,
                                              int timeoutMs) {
    auto waiter = std::make_shared<Waiter>();
    waiter->pred = std::move(pred);
    {
        QMutexLocker lock(&m_waitersMtx);
        m_waiters.append(waiter);
    }

    auto cleanup = [&]() {
        QMutexLocker lock(&m_waitersMtx);
        m_waiters.removeOne(waiter);
    };

    if (!sendRaw(packet)) {
        cleanup();
        return std::nullopt;
    }

    std::optional<Frame> out;
    {
        QMutexLocker lock(&waiter->mtx);
        if (!waiter->done) {
            waiter->cv.wait(&waiter->mtx, timeoutMs);
        }
        if (waiter->done) out = waiter->result;
    }
    cleanup();
    return out;
}

std::optional<RealmeOppoFrame> KalmSession::sendAndWaitRealme(
    const QByteArray& packet,
    std::function<bool(const RealmeOppoFrame&)> pred,
    int timeoutMs) {
    auto waiter = std::make_shared<RealmeWaiter>();
    waiter->pred = std::move(pred);
    {
        QMutexLocker lock(&m_waitersMtx);
        m_realmeWaiters.append(waiter);
    }

    auto cleanup = [&]() {
        QMutexLocker lock(&m_waitersMtx);
        m_realmeWaiters.removeOne(waiter);
    };

    if (!sendRaw(packet)) {
        cleanup();
        return std::nullopt;
    }

    std::optional<RealmeOppoFrame> out;
    {
        QMutexLocker lock(&waiter->mtx);
        if (!waiter->done) {
            waiter->cv.wait(&waiter->mtx, timeoutMs);
        }
        if (waiter->done) out = waiter->result;
    }
    cleanup();
    return out;
}

std::optional<huawei::Packet> KalmSession::sendAndWaitHuawei(
    const QList<QByteArray>& packets,
    std::function<bool(const huawei::Packet&)> pred,
    int timeoutMs) {
    auto waiter = std::make_shared<HuaweiWaiter>();
    waiter->pred = std::move(pred);
    {
        QMutexLocker lock(&m_waitersMtx);
        m_hwWaiters.append(waiter);
    }

    auto cleanup = [&]() {
        QMutexLocker lock(&m_waitersMtx);
        m_hwWaiters.removeOne(waiter);
    };

    if (!packets.isEmpty()) {
        bool sent = false;
        for (const QByteArray& packet : packets) {
            sent = sendRaw(packet) || sent;
        }
        if (!sent) {
            cleanup();
            return std::nullopt;
        }
    }

    std::optional<huawei::Packet> out;
    {
        QMutexLocker lock(&waiter->mtx);
        if (!waiter->done) {
            waiter->cv.wait(&waiter->mtx, timeoutMs);
        }
        if (waiter->done) out = waiter->result;
    }
    cleanup();
    return out;
}

bool KalmSession::huaweiApply(const QByteArray& setPacket,
                              const QByteArray& ackCommand,
                              std::function<bool(const huawei::Packet&)> isTarget,
                              const QByteArray& queryPacket,
                              int timeoutMs) {
    if (!sendRaw(setPacket)) return false;

    // Wait for the write acknowledgement first. Sending the follow-up query
    // immediately makes some firmwares answer with the previous value, which
    // would overwrite the fresh state.
    sendAndWaitHuawei({}, [&ackCommand](const huawei::Packet& packet) {
        return packet.command == ackCommand;
    }, 800);

    if (sendAndWaitHuawei({}, isTarget, 450)) return true;
    return sendAndWaitHuawei({queryPacket}, isTarget, timeoutMs).has_value();
}

bool KalmSession::refresh() {
    DeviceProtocol proto;
    WireProfile p;
    {
        QMutexLocker lock(&m_lock);
        proto = m_protocol;
        p = m_profile;
    }

    if (proto == DeviceProtocol::HuaweiSpp) {
        bool ok = false;
        for (const QByteArray& pkt : {
                 huawei::buildQueryBattery(),
                 huawei::buildQueryAnc(),
                 huawei::buildQueryEqualizer(),
                 huawei::buildQueryLowLatency(),
                 huawei::buildQueryAutoPause(),
                 huawei::buildQueryDualConnectEnabled(),
                 huawei::buildEnumerateDualConnect(),
             }) {
            ok = sendRaw(pkt) || ok;
        }
        return ok;
    }

    if (proto == DeviceProtocol::RealmeOppoTl) {
        bool ok = false;
        for (const QByteArray& pkt : {
                 buildRealmeOppoPacket(kTlCmdGetFirmware, 0, {}),
                 buildRealmeOppoPacket(kTlCmdGetBattery, 0, {}),
                 buildRealmeOppoPacket(kTlCmdQueryNotify, 0, QByteArray("\x03", 1)),
                 buildRealmeOppoPacket(kTlCmdGetNoise, 0, {}),
                 buildRealmeOppoPacket(kTlCmdGetSwitches, 0, QByteArray("\x04\x05\x06\x07", 4)),
             }) {
            ok = sendRaw(pkt) || ok;
        }
        return ok;
    }

    bool ok = false;
    for (const QByteArray& pkt : {
             buildQueryBattery(),
             buildQueryNoiseMode(p),
             buildQueryWearState(p),
             buildQueryWearDetection(p),
             buildQueryAudioEffect(p),
             buildQueryLowLatency(p),
             buildQueryNoiseCycle(p),
             buildQueryMultiConnect(p),
         }) {
        ok = sendRaw(pkt) || ok;
    }
    return ok;
}

bool KalmSession::setNoiseMode(NoiseMode mode,
                               std::optional<int> ancLevel,
                               std::optional<int> transEffect) {
    DeviceProtocol proto;
    WireProfile p;
    std::optional<int> effAnc;
    std::optional<int> effTrans;
    {
        QMutexLocker lock(&m_lock);
        proto = m_protocol;
        p = m_profile;
        effAnc = ancLevel.has_value() ? ancLevel : m_state.noiseEffect;
        effTrans = transEffect.has_value() ? transEffect : m_state.transparencyEffect;
    }

    if (proto == DeviceProtocol::HuaweiSpp) {
        const int target = huawei::toModeValue(mode);
        const auto isTarget = [target](const huawei::Packet& pkt) {
            return pkt.command == QByteArray::fromHex("2b2a") && pkt.param(1).size() >= 2 &&
                   static_cast<uint8_t>(pkt.param(1).at(1)) == target;
        };

        const auto current = snapshot().noiseMode;
        const bool sameMode = current.has_value() && *current == mode;

        // Mode switches must be sent with the 0xFF "keep current level" marker;
        // otherwise FreeBuds treat the write as a level-only update and do not
        // engage ANC/transparency when coming from Off.
        std::optional<int> level;
        std::optional<int> requestedLevel;
        if (target == huawei::kModeAnc) {
            requestedLevel = ancLevel;
        } else if (target == huawei::kModeAwareness && transEffect.has_value()) {
            requestedLevel = 2;  // device level 2 = plain awareness
        }
        if (sameMode) level = requestedLevel;

        if (!huaweiApply(huawei::buildSetAnc(target, level),
                         QByteArray::fromHex("2b04"), isTarget, huawei::buildQueryAnc())) {
            return false;
        }
        if (!sameMode && requestedLevel.has_value()) {
            huaweiApply(huawei::buildSetAnc(target, requestedLevel),
                        QByteArray::fromHex("2b04"), isTarget, huawei::buildQueryAnc());
        }
        return true;
    }

    if (proto == DeviceProtocol::RealmeOppoTl) {
        const auto ack = sendAndWaitRealme(
            buildRealmeSetNoise(mode, 0),
            [](const RealmeOppoFrame& f) {
                return (f.command == kTlRespSetNoise && !f.payload.isEmpty() &&
                        static_cast<uint8_t>(f.payload[0]) == 0);
            },
            2200);
        if (ack) {
            {
                QMutexLocker lock(&m_lock);
                m_state.noiseMode = mode;
            }
            emitState();
            return true;
        }
        return false;
    }

    const auto resp = sendAndWait(
        buildSetNoiseMode(mode, p, effAnc, effTrans),
        [mode](const Frame& f) {
            return (f.command == kCmdAckNoiseMode || f.command == kCmdReportNoiseMode) &&
                   f.payload.size() >= 2 &&
                   static_cast<uint8_t>(f.payload[0]) == 0 &&
                   static_cast<uint8_t>(f.payload[1]) == static_cast<uint8_t>(mode);
        },
        2000);
    if (resp) return true;

    const auto q = sendAndWait(
        buildQueryNoiseMode(p),
        [](const Frame& f) { return f.command == kCmdReportNoiseMode; },
        1500);
    return q && q->payload.size() >= 2 &&
           static_cast<uint8_t>(q->payload[1]) == static_cast<uint8_t>(mode);
}

bool KalmSession::setAncLevel(int levelCode) {
    return setNoiseMode(NoiseMode::Anc, levelCode, std::nullopt);
}

bool KalmSession::setTransparencyEffect(int transCode) {
    return setNoiseMode(NoiseMode::Transparency, std::nullopt, transCode);
}

std::optional<NoiseMode> KalmSession::toggleNoiseMode(const QList<NoiseMode>& cycle) {
    const QList<NoiseMode> order =
        cycle.isEmpty() ? QList<NoiseMode>{NoiseMode::Anc, NoiseMode::Transparency, NoiseMode::Off}
                        : cycle;
    const auto cur = snapshot().noiseMode;
    NoiseMode nxt = order.first();
    if (cur) {
        const qsizetype idx = order.indexOf(*cur);
        if (idx >= 0) nxt = order[(idx + 1) % order.size()];
    }
    if (setNoiseMode(nxt)) return nxt;
    return std::nullopt;
}

bool KalmSession::setAudioEffect(int effectId) {
    DeviceProtocol proto;
    WireProfile p;
    {
        QMutexLocker lock(&m_lock);
        proto = m_protocol;
        p = m_profile;
    }
    if (proto == DeviceProtocol::HuaweiSpp) {
        return huaweiApply(
            huawei::buildSetEqualizerPreset(effectId),
            QByteArray::fromHex("2b49"),
            [effectId](const huawei::Packet& pkt) {
                return pkt.command == QByteArray::fromHex("2b4a") &&
                       huawei::eqCurrentPreset(pkt) == effectId;
            },
            huawei::buildQueryEqualizer());
    }
    if (proto != DeviceProtocol::VivoGaia) return false;
    return sendAndWait(
               buildSetAudioEffect(effectId, p),
               [](const Frame& f) {
                   return (f.command == kCmdAckAudioEffect || f.command == kCmdReportAudioEffect) &&
                          f.payload.size() >= 2 && static_cast<uint8_t>(f.payload[0]) == 0;
               },
               1800)
        .has_value();
}

bool KalmSession::setLowLatency(bool enabled) {
    DeviceProtocol proto;
    WireProfile p;
    {
        QMutexLocker lock(&m_lock);
        proto = m_protocol;
        p = m_profile;
    }
    if (proto == DeviceProtocol::HuaweiSpp) {
        return huaweiApply(
            huawei::buildSetLowLatency(enabled),
            QByteArray::fromHex("2b6c"),
            [enabled](const huawei::Packet& pkt) {
                return pkt.command == QByteArray::fromHex("2b6c") && pkt.param(2).size() == 1 &&
                       (pkt.paramByte(2) != 0) == enabled;
            },
            huawei::buildQueryLowLatency());
    }
    if (proto != DeviceProtocol::VivoGaia) return false;
    return sendAndWait(
               buildSetLowLatency(enabled, p),
               [](const Frame& f) {
                   return (f.command == kCmdAckLowLatency || f.command == kCmdReportLowLatency) &&
                          f.payload.size() >= 2 && static_cast<uint8_t>(f.payload[0]) == 0;
               },
               1800)
        .has_value();
}

bool KalmSession::setWearDetection(bool enabled) {
    DeviceProtocol proto;
    WireProfile p;
    {
        QMutexLocker lock(&m_lock);
        proto = m_protocol;
        p = m_profile;
    }
    if (proto == DeviceProtocol::HuaweiSpp) {
        return huaweiApply(
            huawei::buildSetAutoPause(enabled),
            QByteArray::fromHex("2b10"),
            [enabled](const huawei::Packet& pkt) {
                return pkt.command == QByteArray::fromHex("2b11") && pkt.param(1).size() == 1 &&
                       (pkt.paramByte(1) != 0) == enabled;
            },
            huawei::buildQueryAutoPause());
    }
    if (proto != DeviceProtocol::VivoGaia) return false;
    return sendAndWait(
               buildSetWearDetection(enabled, p),
               [](const Frame& f) {
                   return (f.command == kCmdAckWearDetection || f.command == kCmdReportWearDetection) &&
                          f.payload.size() >= 2 && static_cast<uint8_t>(f.payload[0]) == 0;
               },
               1800)
        .has_value();
}

bool KalmSession::setNoiseCycle(int cycleCode) {
    DeviceProtocol proto;
    WireProfile p;
    {
        QMutexLocker lock(&m_lock);
        proto = m_protocol;
        p = m_profile;
    }
    if (proto != DeviceProtocol::VivoGaia) return false;
    return sendAndWait(
               buildSetNoiseCycle(cycleCode, cycleCode, p),
               [](const Frame& f) {
                   return (f.command == kCmdAckNoiseCycle || f.command == kCmdReportNoiseCycle) &&
                          f.payload.size() >= 4 && static_cast<uint8_t>(f.payload[0]) == 0;
               },
               1800)
        .has_value();
}

bool KalmSession::setDeviceConnection(const QString& targetMac, bool connect) {
    DeviceProtocol proto;
    WireProfile p;
    {
        QMutexLocker lock(&m_lock);
        proto = m_protocol;
        p = m_profile;
    }
    if (proto == DeviceProtocol::HuaweiSpp) {
        const QByteArray action = huawei::buildDualConnectAction(targetMac, connect);
        if (action.isEmpty()) return false;
        if (!sendRaw(action)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        sendRaw(huawei::buildEnumerateDualConnect());
        return true;
    }
    if (proto != DeviceProtocol::VivoGaia) return false;
    const auto snap = snapshot();
    if (snap.pairedDevices.isEmpty()) return false;
    if (!sendRaw(buildSetMultiConnect(snap.pairedDevices, targetMac, connect, p))) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    sendRaw(buildQueryMultiConnect(p));
    return true;
}

bool KalmSession::findEarbuds(bool ring) {
    DeviceProtocol proto;
    WireProfile p;
    {
        QMutexLocker lock(&m_lock);
        proto = m_protocol;
        p = m_profile;
    }
    if (proto != DeviceProtocol::VivoGaia) return false;
    return sendAndWait(
               buildFindDevice(ring, p),
               [](const Frame& f) { return f.command == kCmdAckFindDevice; },
               1800)
        .has_value();
}

QString KalmSession::runProbe(const QString& deviceQuery, int listenSeconds) {
    DeviceProtocol proto;
    {
        QMutexLocker lock(&m_lock);
        proto = m_protocol;
    }
    m_paused.store(true);
    closeSocket(false);
    wakeWorker();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(proto == DeviceProtocol::HuaweiSpp ? 1200 : 300));

    QString report;
    try {
        report = generateDiagnosticReport(m_registry, deviceQuery, listenSeconds);
    } catch (...) {
        m_paused.store(false);
        wakeWorker();
        throw;
    }

    m_paused.store(false);
    wakeWorker();
    return report;
}

void KalmSession::runLoop() {
    auto sleepInterruptible = [this](int ms) {
        QMutexLocker lock(&m_wakeMtx);
        if (!m_woken && !m_stop.load()) {
            m_wakeCv.wait(&m_wakeMtx, ms);
        }
        m_woken = false;
    };

    while (!m_stop.load()) {
        if (m_paused.load()) {
            sleepInterruptible(300);
            continue;
        }

        QMap<QString, BluezDevice> bluezMap;
        for (const auto& d : listBluezDevices(false, false)) {
            bluezMap.insert(d.mac, d);
        }

        std::optional<SavedEarbud> chosen;
        std::optional<BluezDevice> chosenBluez;

        if (!m_overrideMac.isEmpty()) {
            if (bluezMap.contains(m_overrideMac)) chosenBluez = bluezMap.value(m_overrideMac);
            chosen = m_registry.get(m_overrideMac)
                         .value_or(SavedEarbud{m_overrideMac, chosenBluez ? chosenBluez->name : m_overrideMac, {}});
        } else if (auto active = m_registry.getActive()) {
            if (bluezMap.contains(active->mac)) chosenBluez = bluezMap.value(active->mac);
            if (chosenBluez && chosenBluez->connected) {
                chosen = active;
            } else if (m_registry.autoSwitch()) {
                for (const auto& cand : m_registry.devices()) {
                    if (bluezMap.contains(cand.mac) && bluezMap.value(cand.mac).connected) {
                        m_registry.setActive(cand.mac);
                        emitDevices();
                        chosen = cand;
                        chosenBluez = bluezMap.value(cand.mac);
                        break;
                    }
                }
                if (!chosen) chosen = active;
            } else {
                chosen = active;
            }
        } else {
            for (const auto& bdev : bluezMap) {
                if (bdev.isSupportedTws() && bdev.connected) {
                    chosen = m_registry.add(bdev.mac, bdev.name, {}, true);
                    chosenBluez = bdev;
                    emitDevices();
                    break;
                }
            }
        }

        if (!chosen) {
            sleepInterruptible(3000);
            continue;
        }

        const QString rawBtName = (chosenBluez && !chosenBluez->name.isEmpty())
                                      ? chosenBluez->name
                                      : (!chosen->name.isEmpty() ? chosen->name : chosen->displayName());
        {
            QMutexLocker lock(&m_lock);
            m_state.deviceName = chosen->displayName();
            m_state.deviceMac = chosen->mac;
            updateProtocolHint(rawBtName, chosenBluez ? chosenBluez->uuids : QStringList{});
        }

        if (!chosenBluez || !chosenBluez->connected) {
            sleepInterruptible(2500);
            continue;
        }

        if (!chosenBluez->name.isEmpty() && chosenBluez->name != chosen->name) {
            m_registry.add(chosen->mac, chosenBluez->name, {}, false);
        }

        RfcommConnection conn;
        try {
            conn = openEarbudRfcomm(chosen->mac, chosenBluez->path, rawBtName,
                                    chosenBluez->uuids, 2500);
        } catch (const std::exception& ex) {
            emitError(QString::fromUtf8(ex.what()));
            sleepInterruptible(3500);
            continue;
        }

        const WireProfile profile = selectProfile(rawBtName);
        const DeviceProtocol proto = conn.protocol;
        const SocketHandle rawConnFd = conn.fd.release();
        {
            QMutexLocker lock(&m_lock);
            m_sockFd = rawConnFd;
            m_protocol = proto;
            m_modelName = rawBtName;
            m_profile = profile;
            if (proto == DeviceProtocol::HuaweiSpp) {
                m_hwProfile = huawei::profileFor(rawBtName);
            }
            m_hwDcRows.clear();
            m_hwDcCount = 0;
            m_state.connected = true;
            m_state.protocol = proto;
            m_state.deviceName = chosen->displayName();
            m_state.deviceMac = chosen->mac;
        }

        FrameDecoder vivoDecoder;
        RealmeOppoDecoder realmeDecoder;
        huawei::Decoder hwDecoder;
        if (!conn.initialBytes.isEmpty()) {
            if (proto == DeviceProtocol::HuaweiSpp) {
                handleHuaweiPackets(hwDecoder.feed(conn.initialBytes));
            } else if (proto == DeviceProtocol::RealmeOppoTl) {
                handleRealmeFrames(realmeDecoder.feed(conn.initialBytes));
            } else {
                handleFrames(vivoDecoder.feed(conn.initialBytes));
            }
        }

        if (proto == DeviceProtocol::HuaweiSpp) {
            sendRaw(huawei::buildQueryInfo());
            refresh();
        } else if (proto == DeviceProtocol::VivoGaia) {
            for (const QByteArray& pkt : buildRegisterNotifications()) {
                sendRaw(pkt);
            }
            refresh();
        } else {
            // Subscribe to Battery (1), Wear (2), and Noise/ANC (3) live notifications
            sendRaw(buildRealmeOppoPacket(kTlCmdGetNotifyCaps, 0, {}));
            for (uint8_t evId : {uint8_t(1), uint8_t(2), uint8_t(3)}) {
                sendRaw(buildRealmeOppoPacket(kTlCmdRegNotify, 0, QByteArray(1, static_cast<char>(evId))));
            }
            refresh();
        }
        emitDevices();
        emitState();

        const qint64 pollIntervalMs = (proto == DeviceProtocol::RealmeOppoTl) ? 8000 : 30000;
        QElapsedTimer pollTimer;
        pollTimer.start();
        QElapsedTimer eventDebounceTimer;
        bool pendingEventRefresh = false;

        char buf[2048];
        while (!m_stop.load()) {
            SocketHandle curFd = kInvalidSocket;
            {
                QMutexLocker lock(&m_lock);
                curFd = m_sockFd;
            }
            if (curFd != rawConnFd) break;

            const int waitMs = pendingEventRefresh ? 200 : 1000;
            const int pr = pollSocketRead(rawConnFd, waitMs);
            if (pr < 0) break;
            if (pr > 0) {
                const qint64 n = recvSocket(rawConnFd, buf, sizeof(buf));
                if (n <= 0) break;
                const QByteArray chunk(buf, static_cast<qsizetype>(n));
                if (proto == DeviceProtocol::HuaweiSpp) {
                    const auto packets = hwDecoder.feed(chunk);
                    if (!packets.isEmpty()) handleHuaweiPackets(packets);
                } else if (proto == DeviceProtocol::RealmeOppoTl) {
                    const auto frames = realmeDecoder.feed(chunk);
                    if (!frames.isEmpty() && handleRealmeFrames(frames)) {
                        pendingEventRefresh = true;
                        eventDebounceTimer.restart();
                    }
                } else {
                    const auto frames = vivoDecoder.feed(chunk);
                    if (!frames.isEmpty()) handleFrames(frames);
                }
            }

            if (pendingEventRefresh && eventDebounceTimer.elapsed() >= 400) {
                pendingEventRefresh = false;
                sendRaw(buildRealmeOppoPacket(kTlCmdGetBattery, 0, {}));
                sendRaw(buildRealmeOppoPacket(kTlCmdQueryNotify, 0, QByteArray("\x03", 1)));
            }

            if (pollTimer.elapsed() >= pollIntervalMs) {
                pollTimer.restart();
                if (proto == DeviceProtocol::HuaweiSpp) {
                    refresh();
                } else if (proto == DeviceProtocol::RealmeOppoTl) {
                    sendRaw(buildRealmeOppoPacket(kTlCmdGetBattery, 0, {}));
                    sendRaw(buildRealmeOppoPacket(kTlCmdQueryNotify, 0, QByteArray("\x03", 1)));
                } else {
                    sendRaw(buildQueryBattery());
                    sendRaw(buildQueryWearState(profile));
                }
            }
        }

        closeSocket(false);
        emitDevices();
        if (!m_stop.load()) {
            sleepInterruptible(2000);
        }
    }
}

void KalmSession::handleFrames(const QList<Frame>& frames) {
    bool changed = false;
    bool replyTime = false;
    WireProfile prof;
    {
        QMutexLocker lock(&m_lock);
        prof = m_profile;
        for (const Frame& f : frames) {
            const QString tag = applyFrameToState(f, m_state);
            if (tag == u"time_request") {
                replyTime = true;
            } else if (tag != u"ignored" && tag != u"unknown" && tag != u"handshake") {
                changed = true;
            }
        }
    }

    QList<std::shared_ptr<Waiter>> activeWaiters;
    {
        QMutexLocker lock(&m_waitersMtx);
        activeWaiters = m_waiters;
    }
    for (const Frame& f : frames) {
        for (const auto& w : activeWaiters) {
            QMutexLocker wl(&w->mtx);
            if (!w->done && w->pred(f)) {
                w->result = f;
                w->done = true;
                w->cv.wakeAll();
            }
        }
    }

    if (replyTime) {
        sendRaw(buildHostTimeResponse(prof));
    }
    if (changed) {
        emitState();
    }
}

bool KalmSession::handleRealmeFrames(const QList<RealmeOppoFrame>& frames) {
    bool changed = false;
    bool needEventRefresh = false;
    {
        QMutexLocker lock(&m_lock);
        for (const RealmeOppoFrame& f : frames) {
            const QString tag = applyRealmeFrameToState(f, m_state);
            if (tag == u"event_refresh") {
                needEventRefresh = true;
                changed = true;
            } else if (tag != u"ignored" && tag != u"handshake") {
                changed = true;
            }
        }
    }

    QList<std::shared_ptr<RealmeWaiter>> activeWaiters;
    {
        QMutexLocker lock(&m_waitersMtx);
        activeWaiters = m_realmeWaiters;
    }
    for (const RealmeOppoFrame& f : frames) {
        for (const auto& w : activeWaiters) {
            QMutexLocker wl(&w->mtx);
            if (!w->done && w->pred(f)) {
                w->result = f;
                w->done = true;
                w->cv.wakeAll();
            }
        }
    }

    if (changed) {
        emitState();
    }
    return needEventRefresh;
}

void KalmSession::handleHuaweiPackets(const QList<huawei::Packet>& packets) {
    bool changed = false;
    bool sawWear = false;
    bool autoPause = false;
    int wornBefore = 0;
    int wornAfter = 0;
    {
        QMutexLocker lock(&m_lock);
        wornBefore = wornBudCount(m_state);
        autoPause = m_hwProfile.autoPause && m_state.wearDetection.value_or(false);
        for (const auto& packet : packets) {
            const auto effect = huawei::applyPacket(packet, m_state, m_hwProfile);
            if (effect.stateChanged) changed = true;
            if (effect.wearUpdate) sawWear = true;
            if (!effect.dualConnectRow) continue;

            if (effect.dcCount > 0) m_hwDcCount = effect.dcCount;
            m_hwDcRows.insert(effect.dcIndex, effect.dcDevice);
            if (m_hwDcCount > 0 && m_hwDcRows.size() >= m_hwDcCount) {
                QList<MultiConnectDevice> list;
                for (auto it = m_hwDcRows.cbegin(); it != m_hwDcRows.cend(); ++it) {
                    list.append(it.value());
                }
                m_state.pairedDevices = list;
                m_hwDcRows.clear();
                changed = true;
            }
        }
        wornAfter = wornBudCount(m_state);
    }

    bool shouldPause = false;
    bool shouldResume = false;
    QString resumeService;
    {
        QMutexLocker lock(&m_lock);
        if (sawWear && autoPause && wornAfter < wornBefore) {
            shouldPause = !m_mprisPausedByWear;
        } else if (sawWear && autoPause && wornAfter > wornBefore && m_mprisPausedByWear) {
            shouldResume = true;
            resumeService = m_mprisService;
        }
    }
    if (shouldPause) {
        const QString service = findPlayingMprisPlayer();
        if (!service.isEmpty() && callMprisPlayer(service, QStringLiteral("Pause"))) {
            QMutexLocker lock(&m_lock);
            m_mprisPausedByWear = true;
            m_mprisService = service;
        }
    } else if (shouldResume) {
        callMprisPlayer(resumeService, QStringLiteral("Play"));
        QMutexLocker lock(&m_lock);
        m_mprisPausedByWear = false;
        m_mprisService.clear();
    }

    QList<std::shared_ptr<HuaweiWaiter>> activeWaiters;
    {
        QMutexLocker lock(&m_waitersMtx);
        activeWaiters = m_hwWaiters;
    }
    for (const auto& packet : packets) {
        for (const auto& waiter : activeWaiters) {
            QMutexLocker wl(&waiter->mtx);
            if (!waiter->done && waiter->pred(packet)) {
                waiter->result = packet;
                waiter->done = true;
                waiter->cv.wakeAll();
            }
        }
    }

    if (changed) {
        emitState();
    }
}

// ---- IpcServer ----

IpcServer::IpcServer(KalmSession& session, ShowCallback onShowWindow, NotifyCallback onNotify)
    : m_session(session),
      m_onShowWindow(std::move(onShowWindow)),
      m_onNotify(std::move(onNotify)),
      m_path(ipcSocketPath()) {}

IpcServer::~IpcServer() { stop(); }

bool IpcServer::start() {
    if (QFile::exists(m_path)) {
        const auto ping = ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("ping")}}, 400);
        if (ping && ping->value(QStringLiteral("ok")).toBool()) {
            return false;
        }
        QFile::remove(m_path);
    }

    m_serverFd = openLocalIpcServer(m_path);
    if (m_serverFd == kInvalidSocket) return false;

    m_stop.store(false);
    m_thread = std::thread(&IpcServer::acceptLoop, this);
    return true;
}

void IpcServer::stop() {
    m_stop.store(true);
    if (m_serverFd != kInvalidSocket) {
        shutdownSocketHandle(m_serverFd);
        closeSocketHandle(m_serverFd);
        m_serverFd = kInvalidSocket;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    QFile::remove(m_path);
}

void IpcServer::acceptLoop() {
    while (!m_stop.load()) {
        const SocketHandle srv = m_serverFd;
        if (srv == kInvalidSocket) break;
        const int pr = pollSocketRead(srv, 400);
        if (pr <= 0) {
            if (pr < 0) break;
            continue;
        }
        const SocketHandle clientFd = acceptLocalIpcClient(srv);
        if (clientFd == kInvalidSocket) break;
        handleClient(clientFd);
    }
}

void IpcServer::handleClient(SocketHandle rawClientFd) {
    UniqueSocket clientFd(rawClientFd);
    QByteArray raw;
    char buf[4096];
    if (pollSocketRead(clientFd.get(), 2000) > 0) {
        const qint64 n = recvSocket(clientFd.get(), buf, sizeof(buf));
        if (n > 0) raw.append(buf, static_cast<qsizetype>(n));
    }

    QJsonObject resp;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        resp = QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Invalid JSON")}};
    } else {
        try {
            resp = dispatch(doc.object());
        } catch (const std::exception& ex) {
            resp = QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QString::fromUtf8(ex.what())}};
        }
    }

    sendAll(clientFd.get(), QJsonDocument(resp).toJson(QJsonDocument::Compact));
}

QJsonObject IpcServer::dispatch(const QJsonObject& req) {
    const QString cmd = req.value(QStringLiteral("cmd")).toString();
    if (cmd == u"ping") {
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (cmd == u"show") {
        if (m_onShowWindow) m_onShowWindow(req.value(QStringLiteral("token")).toString());
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (cmd == u"notify") {
        if (m_onNotify) {
            m_onNotify(req.value(QStringLiteral("title")).toString(),
                       req.value(QStringLiteral("body")).toString());
        }
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (cmd == u"status") {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("state"), m_session.snapshot().toJson()}};
    }
    if (cmd == u"select_device") {
        const bool ok = m_session.switchToDevice(req.value(QStringLiteral("mac")).toString());
        return QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("state"), m_session.snapshot().toJson()}};
    }
    if (cmd == u"reload_registry") {
        m_session.registry().load();
        m_session.notifyRegistryUpdated();
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (cmd == u"set_noise") {
        const auto m = parseNoiseModeStr(req.value(QStringLiteral("mode")).toString());
        if (!m) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Invalid mode")}};
        std::optional<int> ancLvl;
        std::optional<int> transEff;
        if (req.contains(QStringLiteral("anc_level")) && !req.value(QStringLiteral("anc_level")).isNull()) {
            ancLvl = req.value(QStringLiteral("anc_level")).toInt();
        }
        if (req.contains(QStringLiteral("trans_effect")) && !req.value(QStringLiteral("trans_effect")).isNull()) {
            transEff = req.value(QStringLiteral("trans_effect")).toInt();
        }
        const bool ok = m_session.setNoiseMode(*m, ancLvl, transEff);
        return QJsonObject{
            {QStringLiteral("ok"), ok},
            {QStringLiteral("error"), ok ? QJsonValue() : QJsonValue(trKey("err_mode_unconfirmed"))},
            {QStringLiteral("state"), m_session.snapshot().toJson()},
        };
    }
    if (cmd == u"toggle_noise") {
        QList<NoiseMode> cycle;
        for (const QJsonValue& v : req.value(QStringLiteral("modes")).toArray()) {
            if (auto m = parseNoiseModeStr(v.toString())) cycle.append(*m);
        }
        const auto nxt = m_session.toggleNoiseMode(cycle);
        return QJsonObject{
            {QStringLiteral("ok"), nxt.has_value()},
            {QStringLiteral("error"), nxt ? QJsonValue() : QJsonValue(trKey("err_mode_unconfirmed"))},
            {QStringLiteral("state"), m_session.snapshot().toJson()},
        };
    }
    if (cmd == u"set_eq") {
        const bool ok = m_session.setAudioEffect(req.value(QStringLiteral("effect")).toInt());
        return QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("state"), m_session.snapshot().toJson()}};
    }
    if (cmd == u"set_game") {
        const bool ok = m_session.setLowLatency(req.value(QStringLiteral("enabled")).toBool());
        return QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("state"), m_session.snapshot().toJson()}};
    }
    if (cmd == u"probe") {
        const QString q = req.value(QStringLiteral("device")).toString();
        const int listenSec = req.value(QStringLiteral("listen")).toInt(0);
        const QString report = m_session.runProbe(q, listenSec);
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("report"), report}};
    }
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Unknown IPC command")}};
}

// ---- Oneshot CLI Operations ----

namespace {

struct TargetInfo {
    QString mac;
    QString displayName;
    QString rawName;
    QString devPath;
    QStringList uuids;
};

TargetInfo resolveOneshotTarget(DeviceRegistry& reg, const QString& query) {
    reg.ensureInitialImport();
    if (!query.trimmed().isEmpty()) {
        const auto resolved = reg.resolveQuery(query);
        if (!resolved) {
            throw std::runtime_error(trKey("err_device_not_saved").arg(query).toStdString());
        }
        const auto bdev = findBluezByMac(resolved->mac);
        if (bdev && !bdev->connected) {
            throw std::runtime_error(
                trKey("err_not_connected_bt").arg(resolved->displayName(), resolved->mac).toStdString());
        }
        return {resolved->mac, resolved->displayName(), bdev ? bdev->name : resolved->name,
                bdev ? bdev->path : QString{}, bdev ? bdev->uuids : QStringList{}};
    }

    if (auto active = reg.getActive()) {
        const auto bdev = findBluezByMac(active->mac);
        if (bdev && bdev->connected) {
            return {active->mac, active->displayName(), bdev->name, bdev->path, bdev->uuids};
        }
        if (reg.autoSwitch()) {
            for (const auto& cand : reg.devices()) {
                const auto cb = findBluezByMac(cand.mac);
                if (cb && cb->connected) {
                    return {cand.mac, cand.displayName(), cb->name, cb->path, cb->uuids};
                }
            }
        }
        throw std::runtime_error(
            trKey("err_not_connected_bt").arg(active->displayName(), active->mac).toStdString());
    }

    for (const auto& bdev : listBluezDevices(true, false)) {
        if (bdev.connected) {
            return {bdev.mac, bdev.name, bdev.name, bdev.path, bdev.uuids};
        }
    }
    throw std::runtime_error(trKey("err_no_vivo_found").toStdString());
}

bool recvUntil(SocketHandle fd, FrameDecoder& dec, EarbudState& state, const WireProfile& profile,
               std::function<bool(const Frame&)> stopWhen, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    bool matched = false;
    char buf[2048];
    while (timer.elapsed() < timeoutMs) {
        const int rem = std::min(400, std::max(50, static_cast<int>(timeoutMs - timer.elapsed())));
        const int pr = pollSocketRead(fd, rem);
        if (pr == 0) {
            if (matched) return true;
            continue;
        }
        if (pr < 0) break;
        const qint64 n = recvSocket(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (const Frame& f : dec.feed(QByteArray(buf, static_cast<qsizetype>(n)))) {
            const QString tag = applyFrameToState(f, state);
            if (tag == u"time_request") {
                sendAll(fd, buildHostTimeResponse(profile));
            }
            if (stopWhen(f)) matched = true;
        }
        if (matched) return true;
    }
    return matched;
}

bool recvUntilRealme(SocketHandle fd, RealmeOppoDecoder& dec, EarbudState& state,
                     std::function<bool(const RealmeOppoFrame&)> stopWhen, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    bool matched = false;
    char buf[2048];
    while (timer.elapsed() < timeoutMs) {
        const int rem = std::min(400, std::max(50, static_cast<int>(timeoutMs - timer.elapsed())));
        const int pr = pollSocketRead(fd, rem);
        if (pr == 0) {
            if (matched) return true;
            continue;
        }
        if (pr < 0) break;
        const qint64 n = recvSocket(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (const RealmeOppoFrame& f : dec.feed(QByteArray(buf, static_cast<qsizetype>(n)))) {
            applyRealmeFrameToState(f, state);
            if (stopWhen(f)) matched = true;
        }
        if (matched) return true;
    }
    return matched;
}

bool recvUntilHuawei(SocketHandle fd, huawei::Decoder& dec, EarbudState& state,
                     const huawei::ModelProfile& profile,
                     std::function<bool(const huawei::Packet&)> stopWhen, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    bool matched = false;
    char buf[2048];
    QMap<int, MultiConnectDevice> dcRows;
    int dcCount = 0;
    while (timer.elapsed() < timeoutMs) {
        const int rem = std::min(400, std::max(50, static_cast<int>(timeoutMs - timer.elapsed())));
        const int pr = pollSocketRead(fd, rem);
        if (pr == 0) {
            if (matched) return true;
            continue;
        }
        if (pr < 0) break;
        const qint64 n = recvSocket(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (const auto& packet : dec.feed(QByteArray(buf, static_cast<qsizetype>(n)))) {
            const auto effect = huawei::applyPacket(packet, state, profile);
            if (effect.dualConnectRow) {
                if (effect.dcCount > 0) dcCount = effect.dcCount;
                dcRows.insert(effect.dcIndex, effect.dcDevice);
                if (dcCount > 0 && dcRows.size() >= dcCount) {
                    QList<MultiConnectDevice> list;
                    for (auto it = dcRows.cbegin(); it != dcRows.cend(); ++it) list.append(it.value());
                    state.pairedDevices = list;
                }
            }
            if (stopWhen(packet)) matched = true;
        }
        if (matched) return true;
    }
    return matched;
}

bool huaweiOneshotApply(SocketHandle fd, huawei::Decoder& dec, EarbudState& state,
                        const huawei::ModelProfile& profile,
                        const QByteArray& setPacket,
                        const QByteArray& ackCommand,
                        std::function<bool(const huawei::Packet&)> isTarget,
                        const QByteArray& queryPacket) {
    if (!sendAll(fd, setPacket)) return false;
    recvUntilHuawei(fd, dec, state, profile,
                    [&ackCommand](const huawei::Packet& p) { return p.command == ackCommand; },
                    800);
    if (recvUntilHuawei(fd, dec, state, profile, isTarget, 450)) return true;
    sendAll(fd, queryPacket);
    return recvUntilHuawei(fd, dec, state, profile, isTarget, 1600);
}

struct OneshotContext {
    RfcommConnection conn;
    WireProfile vivoProfile;
    huawei::ModelProfile hwProfile;
    EarbudState state;
    FrameDecoder vivoDec;
    RealmeOppoDecoder realmeDec;
    huawei::Decoder hwDec;
};

template <typename Fn>
QJsonObject runOneshot(DeviceRegistry& reg, const QString& query, Fn&& action) {
    const TargetInfo t = resolveOneshotTarget(reg, query);
    const QString btName = !t.rawName.isEmpty() ? t.rawName : t.displayName;
    OneshotContext ctx{
        openEarbudRfcomm(t.mac, t.devPath, btName, t.uuids, 3000),
        selectProfile(btName),
        huawei::profileFor(btName),
        EarbudState{},
        FrameDecoder{},
        RealmeOppoDecoder{},
        huawei::Decoder{},
    };
    ctx.state.connected = true;
    ctx.state.protocol = ctx.conn.protocol;
    ctx.state.deviceName = t.displayName;
    ctx.state.deviceMac = t.mac;
    if (!ctx.conn.initialBytes.isEmpty()) {
        if (ctx.conn.protocol == DeviceProtocol::HuaweiSpp) {
            for (const auto& p : ctx.hwDec.feed(ctx.conn.initialBytes)) {
                huawei::applyPacket(p, ctx.state, ctx.hwProfile);
            }
        } else if (ctx.conn.protocol == DeviceProtocol::RealmeOppoTl) {
            for (const auto& f : ctx.realmeDec.feed(ctx.conn.initialBytes)) {
                applyRealmeFrameToState(f, ctx.state);
            }
        } else {
            for (const Frame& f : ctx.vivoDec.feed(ctx.conn.initialBytes)) {
                applyFrameToState(f, ctx.state);
            }
        }
    }
    action(ctx);
    return ctx.state.toJson();
}

}  // namespace

QJsonObject oneshotStatus(DeviceRegistry& reg, const QString& deviceQuery) {
    return runOneshot(reg, deviceQuery, [](OneshotContext& ctx) {
        const SocketHandle fd = ctx.conn.fd.get();
        if (ctx.conn.protocol == DeviceProtocol::HuaweiSpp) {
            for (const QByteArray& pkt : {
                     huawei::buildQueryBattery(),
                     huawei::buildQueryAnc(),
                     huawei::buildQueryEqualizer(),
                     huawei::buildQueryLowLatency(),
                     huawei::buildQueryAutoPause(),
                     huawei::buildQueryInfo(),
                     huawei::buildQueryDualConnectEnabled(),
                     huawei::buildEnumerateDualConnect(),
                 }) {
                sendAll(fd, pkt);
            }
            recvUntilHuawei(fd, ctx.hwDec, ctx.state, ctx.hwProfile,
                            [](const huawei::Packet&) { return false; }, 1400);
            return;
        }

        if (ctx.conn.protocol == DeviceProtocol::RealmeOppoTl) {
            sendAll(fd, buildRealmeOppoPacket(kTlCmdGetFirmware, 0, {}));
            sendAll(fd, buildRealmeOppoPacket(kTlCmdGetBattery, 0, {}));
            sendAll(fd, buildRealmeOppoPacket(kTlCmdQueryNotify, 0, QByteArray("\x03", 1)));
            sendAll(fd, buildRealmeOppoPacket(kTlCmdGetNoise, 0, {}));
            sendAll(fd, buildRealmeOppoPacket(kTlCmdGetSwitches, 0, QByteArray("\x04\x05\x06\x07", 4)));
            QList<uint16_t> seen;
            recvUntilRealme(
                fd, ctx.realmeDec, ctx.state,
                [&](const RealmeOppoFrame& f) {
                    if (!seen.contains(f.command)) seen.append(f.command);
                    return seen.contains(kTlRespGetBattery) &&
                           (seen.contains(kTlRespQueryNotify) || seen.contains(kTlRespGetNoise)) &&
                           seen.contains(kTlRespGetFirmware);
                },
                1200);
            return;
        }

        for (const QByteArray& pkt : buildRegisterNotifications()) sendAll(fd, pkt);
        for (const QByteArray& pkt : {
                 buildQueryBattery(),
                 buildQueryNoiseMode(ctx.vivoProfile),
                 buildQueryWearState(ctx.vivoProfile),
                 buildQueryAudioEffect(ctx.vivoProfile),
                 buildQueryLowLatency(ctx.vivoProfile),
                 buildQueryNoiseCycle(ctx.vivoProfile),
                 buildQueryMultiConnect(ctx.vivoProfile),
             }) {
            sendAll(fd, pkt);
        }
        QList<uint16_t> seen;
        recvUntil(
            fd, ctx.vivoDec, ctx.state, ctx.vivoProfile,
            [&](const Frame& f) {
                if ((f.command == kCmdReportBattery || f.command == kCmdReportNoiseMode ||
                     f.command == kCmdReportWearState || f.command == kCmdReportAudioEffect ||
                     f.command == kCmdReportLowLatency || f.command == kCmdReportMultiConnect) &&
                    !seen.contains(f.command)) {
                    seen.append(f.command);
                }
                return seen.size() >= 5 && !ctx.state.firmware.isEmpty();
            },
            1200);
    });
}

QJsonObject oneshotSetNoise(DeviceRegistry& reg, NoiseMode mode,
                            std::optional<int> ancLevel,
                            std::optional<int> transEffect,
                            const QString& deviceQuery) {
    return runOneshot(reg, deviceQuery, [=](OneshotContext& ctx) {
        const SocketHandle fd = ctx.conn.fd.get();
        if (ctx.conn.protocol == DeviceProtocol::HuaweiSpp) {
            const int target = huawei::toModeValue(mode);
            const auto isTarget = [target](const huawei::Packet& pkt) {
                return pkt.command == QByteArray::fromHex("2b2a") && pkt.param(1).size() >= 2 &&
                       static_cast<uint8_t>(pkt.param(1).at(1)) == target;
            };
            // Step 1: switch mode using 0xFF "keep level" marker so FreeBuds DSP engages from Off
            if (!huaweiOneshotApply(fd, ctx.hwDec, ctx.state, ctx.hwProfile,
                                    huawei::buildSetAnc(target, std::nullopt),
                                    QByteArray::fromHex("2b04"), isTarget, huawei::buildQueryAnc())) {
                throw std::runtime_error(trKey("err_mode_unconfirmed").toStdString());
            }
            // Step 2: apply explicit level if requested
            std::optional<int> requestedLevel;
            if (target == huawei::kModeAnc && ancLevel.has_value()) {
                requestedLevel = ancLevel;
            } else if (target == huawei::kModeAwareness && transEffect.has_value()) {
                requestedLevel = 2;
            }
            if (requestedLevel.has_value()) {
                huaweiOneshotApply(fd, ctx.hwDec, ctx.state, ctx.hwProfile,
                                   huawei::buildSetAnc(target, requestedLevel),
                                   QByteArray::fromHex("2b04"), isTarget, huawei::buildQueryAnc());
            }
            return;
        }

        if (ctx.conn.protocol == DeviceProtocol::RealmeOppoTl) {
            sendAll(fd, buildRealmeOppoPacket(kTlCmdGetBattery, 0, {}));
            sendAll(fd, buildRealmeSetNoise(mode, 0));
            const bool ok = recvUntilRealme(
                fd, ctx.realmeDec, ctx.state,
                [](const RealmeOppoFrame& f) {
                    return f.command == kTlRespSetNoise && !f.payload.isEmpty() &&
                           static_cast<uint8_t>(f.payload[0]) == 0;
                },
                2200);
            if (!ok) throw std::runtime_error(trKey("err_mode_unconfirmed").toStdString());
            ctx.state.noiseMode = mode;
            return;
        }

        sendAll(fd, buildQueryBattery());
        if (!ancLevel.has_value() || !transEffect.has_value()) {
            sendAll(fd, buildQueryNoiseMode(ctx.vivoProfile));
            recvUntil(fd, ctx.vivoDec, ctx.state, ctx.vivoProfile,
                      [](const Frame& f) { return f.command == kCmdReportNoiseMode; }, 800);
        }
        const auto effAnc = ancLevel.has_value() ? ancLevel : ctx.state.noiseEffect;
        const auto effTrans = transEffect.has_value() ? transEffect : ctx.state.transparencyEffect;
        sendAll(fd, buildSetNoiseMode(mode, ctx.vivoProfile, effAnc, effTrans));
        const bool ok = recvUntil(
            fd, ctx.vivoDec, ctx.state, ctx.vivoProfile,
            [mode](const Frame& f) {
                return (f.command == kCmdAckNoiseMode || f.command == kCmdReportNoiseMode) &&
                       f.payload.size() >= 2 && static_cast<uint8_t>(f.payload[0]) == 0 &&
                       static_cast<uint8_t>(f.payload[1]) == static_cast<uint8_t>(mode);
            },
            2000);
        if (!ok) throw std::runtime_error(trKey("err_mode_unconfirmed").toStdString());
    });
}

QJsonObject oneshotToggleNoise(DeviceRegistry& reg, const QList<NoiseMode>& cycle, const QString& deviceQuery) {
    const QList<NoiseMode> order =
        cycle.isEmpty() ? QList<NoiseMode>{NoiseMode::Anc, NoiseMode::Transparency, NoiseMode::Off} : cycle;
    return runOneshot(reg, deviceQuery, [&order](OneshotContext& ctx) {
        const SocketHandle fd = ctx.conn.fd.get();
        if (ctx.conn.protocol == DeviceProtocol::HuaweiSpp) {
            sendAll(fd, huawei::buildQueryAnc());
            recvUntilHuawei(
                fd, ctx.hwDec, ctx.state, ctx.hwProfile,
                [](const huawei::Packet& pkt) {
                    return pkt.command == QByteArray::fromHex("2b2a") && pkt.param(1).size() >= 2;
                },
                1500);

            NoiseMode nxt = order.first();
            if (ctx.state.noiseMode) {
                const qsizetype idx = order.indexOf(*ctx.state.noiseMode);
                if (idx >= 0) nxt = order[(idx + 1) % order.size()];
            }

            const int target = huawei::toModeValue(nxt);
            const auto isTarget = [target](const huawei::Packet& pkt) {
                return pkt.command == QByteArray::fromHex("2b2a") && pkt.param(1).size() >= 2 &&
                       static_cast<uint8_t>(pkt.param(1).at(1)) == target;
            };
            if (!huaweiOneshotApply(fd, ctx.hwDec, ctx.state, ctx.hwProfile,
                                    huawei::buildSetAnc(target, std::nullopt),
                                    QByteArray::fromHex("2b04"), isTarget, huawei::buildQueryAnc())) {
                throw std::runtime_error(trKey("err_mode_unconfirmed").toStdString());
            }
            return;
        }

        if (ctx.conn.protocol == DeviceProtocol::RealmeOppoTl) {
            sendAll(fd, buildRealmeOppoPacket(kTlCmdGetBattery, 0, {}));
            sendAll(fd, buildRealmeOppoPacket(kTlCmdQueryNotify, 0, QByteArray("\x03", 1)));
            sendAll(fd, buildRealmeOppoPacket(kTlCmdGetNoise, 0, {}));
            recvUntilRealme(
                fd, ctx.realmeDec, ctx.state,
                [&](const RealmeOppoFrame&) { return ctx.state.noiseMode.has_value(); },
                1200);
            NoiseMode nxt = order.first();
            if (ctx.state.noiseMode) {
                const qsizetype idx = order.indexOf(*ctx.state.noiseMode);
                if (idx >= 0) nxt = order[(idx + 1) % order.size()];
            }
            sendAll(fd, buildRealmeSetNoise(nxt, 0));
            const bool ok = recvUntilRealme(
                fd, ctx.realmeDec, ctx.state,
                [](const RealmeOppoFrame& f) {
                    return f.command == kTlRespSetNoise && !f.payload.isEmpty() &&
                           static_cast<uint8_t>(f.payload[0]) == 0;
                },
                2200);
            if (!ok) throw std::runtime_error(trKey("err_mode_unconfirmed").toStdString());
            ctx.state.noiseMode = nxt;
            return;
        }

        sendAll(fd, buildQueryBattery());
        sendAll(fd, buildQueryNoiseMode(ctx.vivoProfile));
        recvUntil(fd, ctx.vivoDec, ctx.state, ctx.vivoProfile,
                  [](const Frame& f) { return f.command == kCmdReportNoiseMode; }, 1500);
        NoiseMode nxt = order.first();
        if (ctx.state.noiseMode) {
            const qsizetype idx = order.indexOf(*ctx.state.noiseMode);
            if (idx >= 0) nxt = order[(idx + 1) % order.size()];
        }
        sendAll(fd, buildSetNoiseMode(nxt, ctx.vivoProfile, ctx.state.noiseEffect, ctx.state.transparencyEffect));
        const bool ok = recvUntil(
            fd, ctx.vivoDec, ctx.state, ctx.vivoProfile,
            [nxt](const Frame& f) {
                return (f.command == kCmdAckNoiseMode || f.command == kCmdReportNoiseMode) &&
                       f.payload.size() >= 2 && static_cast<uint8_t>(f.payload[0]) == 0 &&
                       static_cast<uint8_t>(f.payload[1]) == static_cast<uint8_t>(nxt);
            },
            2000);
        if (!ok) throw std::runtime_error(trKey("err_mode_unconfirmed").toStdString());
    });
}

QJsonObject oneshotSetEq(DeviceRegistry& reg, int effectId, const QString& deviceQuery) {
    return runOneshot(reg, deviceQuery, [effectId](OneshotContext& ctx) {
        const SocketHandle fd = ctx.conn.fd.get();
        if (ctx.conn.protocol == DeviceProtocol::HuaweiSpp) {
            const bool ok = huaweiOneshotApply(
                fd, ctx.hwDec, ctx.state, ctx.hwProfile,
                huawei::buildSetEqualizerPreset(effectId),
                QByteArray::fromHex("2b49"),
                [effectId](const huawei::Packet& pkt) {
                    return pkt.command == QByteArray::fromHex("2b4a") &&
                           huawei::eqCurrentPreset(pkt) == effectId;
                },
                huawei::buildQueryEqualizer());
            if (!ok) throw std::runtime_error("EQ change unconfirmed.");
            return;
        }
        if (ctx.conn.protocol != DeviceProtocol::VivoGaia) {
            throw std::runtime_error("EQ presets are not supported on this device.");
        }
        sendAll(fd, buildSetAudioEffect(effectId, ctx.vivoProfile));
        const bool ok = recvUntil(
            fd, ctx.vivoDec, ctx.state, ctx.vivoProfile,
            [](const Frame& f) { return f.command == kCmdAckAudioEffect || f.command == kCmdReportAudioEffect; },
            1800);
        if (!ok) throw std::runtime_error("EQ change unconfirmed.");
    });
}

QJsonObject oneshotSetGame(DeviceRegistry& reg, bool enabled, const QString& deviceQuery) {
    return runOneshot(reg, deviceQuery, [enabled](OneshotContext& ctx) {
        const SocketHandle fd = ctx.conn.fd.get();
        if (ctx.conn.protocol == DeviceProtocol::HuaweiSpp) {
            const bool ok = huaweiOneshotApply(
                fd, ctx.hwDec, ctx.state, ctx.hwProfile,
                huawei::buildSetLowLatency(enabled),
                QByteArray::fromHex("2b6c"),
                [enabled](const huawei::Packet& pkt) {
                    return pkt.command == QByteArray::fromHex("2b6c") && pkt.param(2).size() == 1 &&
                           (pkt.paramByte(2) != 0) == enabled;
                },
                huawei::buildQueryLowLatency());
            if (!ok) throw std::runtime_error("Game mode change unconfirmed.");
            return;
        }
        if (ctx.conn.protocol != DeviceProtocol::VivoGaia) {
            throw std::runtime_error("Game mode is not supported on this device.");
        }
        sendAll(fd, buildSetLowLatency(enabled, ctx.vivoProfile));
        const bool ok = recvUntil(
            fd, ctx.vivoDec, ctx.state, ctx.vivoProfile,
            [](const Frame& f) { return f.command == kCmdAckLowLatency || f.command == kCmdReportLowLatency; },
            1800);
        if (!ok) throw std::runtime_error("Game mode change unconfirmed.");
    });
}

}  // namespace kalm
