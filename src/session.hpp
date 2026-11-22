// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "huawei.hpp"
#include "nothing.hpp"
#include "protocol.hpp"
#include "registry.hpp"
#include "transport.hpp"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QString>
#include <QWaitCondition>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace kalm {

QString ipcSocketPath();
std::optional<QJsonObject> ipcRequest(const QJsonObject& payload, int timeoutMs = 2500);

// Protocol-dependent capabilities and preset lists. The GUI and system tray
// rebuild their selectors and section visibility from here so each brand only
// exposes controls the connected earbuds actually support.
struct SessionCapabilities {
    DeviceProtocol protocol = DeviceProtocol::VivoGaia;
    QString profileLabel;

    bool ancLevels = true;
    bool transparency = true;
    bool equalizer = true;
    bool gameMode = true;
    bool wearDetection = true;
    bool noiseCycle = true;
    bool multipoint = true;
    bool findDevice = true;
    bool probe = true;

    QList<PresetEntry> ancLevelList;
    QList<PresetEntry> transparencyList;
    QList<PresetEntry> eqList;
    QList<PresetEntry> cycleList;

    QString signature() const {
        return QStringLiteral("%1:%2").arg(static_cast<int>(protocol)).arg(profileLabel);
    }
};

class KalmSession {
public:
    using StateCallback = std::function<void(const EarbudState&)>;
    using DevicesCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const QString&)>;

    KalmSession(DeviceRegistry& registry,
                const QString& overrideMac = {},
                StateCallback onStateChanged = {},
                DevicesCallback onDevicesChanged = {},
                ErrorCallback onError = {});
    ~KalmSession();

    DeviceRegistry& registry() { return m_registry; }
    EarbudState snapshot() const;
    SessionCapabilities capabilities() const;

    void start();
    void stop();
    bool switchToDevice(const QString& mac);
    void notifyRegistryUpdated();
    void forceReconnect();

    bool refresh();
    bool setNoiseMode(NoiseMode mode,
                      std::optional<int> ancLevel = std::nullopt,
                      std::optional<int> transEffect = std::nullopt);
    bool setAncLevel(int levelCode);
    bool setTransparencyEffect(int transCode);
    std::optional<NoiseMode> toggleNoiseMode(const QList<NoiseMode>& cycle = {});
    bool setAudioEffect(int effectId);
    bool setLowLatency(bool enabled);
    bool setWearDetection(bool enabled);
    bool setNoiseCycle(int cycleCode);
    bool setDeviceConnection(const QString& targetMac, bool connect);
    bool findEarbuds(bool ring);
    QString runProbe(const QString& deviceQuery = {}, int listenSeconds = 0);

private:
    struct Waiter {
        std::function<bool(const Frame&)> pred;
        QMutex mtx;
        QWaitCondition cv;
        bool done = false;
        bool aborted = false;
        Frame result;
    };

    struct RealmeWaiter {
        std::function<bool(const RealmeOppoFrame&)> pred;
        QMutex mtx;
        QWaitCondition cv;
        bool done = false;
        bool aborted = false;
        RealmeOppoFrame result;
    };

    struct HuaweiWaiter {
        std::function<bool(const huawei::Packet&)> pred;
        QMutex mtx;
        QWaitCondition cv;
        bool done = false;
        bool aborted = false;
        huawei::Packet result;
    };

    struct NothingWaiter {
        std::function<bool(const nothing::Packet&)> pred;
        QMutex mtx;
        QWaitCondition cv;
        bool done = false;
        bool aborted = false;
        nothing::Packet result;
    };

    void runLoop();
    void handleFrames(const QList<Frame>& frames);
    bool handleRealmeFrames(const QList<RealmeOppoFrame>& frames);
    void handleHuaweiPackets(const QList<huawei::Packet>& packets);
    void handleNothingPackets(const QList<nothing::Packet>& packets);
    void updateProtocolHint(const QString& rawName, const QStringList& uuids = {});
    void closeSocket(bool resetIdentity = false);
    bool sendRaw(const QByteArray& data);
    std::optional<Frame> sendAndWait(const QByteArray& packet,
                                     std::function<bool(const Frame&)> pred,
                                     int timeoutMs = 1800);
    std::optional<RealmeOppoFrame> sendAndWaitRealme(const QByteArray& packet,
                                                     std::function<bool(const RealmeOppoFrame&)> pred,
                                                     int timeoutMs = 2000);
    std::optional<huawei::Packet> sendAndWaitHuawei(const QList<QByteArray>& packets,
                                                    std::function<bool(const huawei::Packet&)> pred,
                                                    int timeoutMs = 1800);
    std::optional<nothing::Packet> sendAndWaitNothing(const QByteArray& packet,
                                                      std::function<bool(const nothing::Packet&)> pred,
                                                      int timeoutMs = 1800);
    uint8_t nextNothingOpId();
    bool huaweiApply(const QByteArray& setPacket,
                     const QByteArray& ackCommand,
                     std::function<bool(const huawei::Packet&)> isTarget,
                     const QByteArray& queryPacket,
                     int timeoutMs = 1600);
    void wakeWorker();

    void emitState();
    void emitDevices();
    void emitError(const QString& msg);

    DeviceRegistry& m_registry;
    QString m_overrideMac;
    StateCallback m_onStateChanged;
    DevicesCallback m_onDevicesChanged;
    ErrorCallback m_onError;

    mutable QRecursiveMutex m_lock;
    SocketHandle m_sockFd = kInvalidSocket;
    DeviceProtocol m_protocol = DeviceProtocol::VivoGaia;
    QString m_modelName;
    WireProfile m_profile;
    huawei::ModelProfile m_hwProfile;
    nothing::ModelProfile m_nothingProfile;
    std::atomic<uint8_t> m_nothingOpId{1};
    QMap<int, MultiConnectDevice> m_hwDcRows;
    int m_hwDcCount = 0;
    bool m_mprisPausedByWear = false;
    QString m_mprisService;
    EarbudState m_state;

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_paused{false};
    QMutex m_wakeMtx;
    QWaitCondition m_wakeCv;
    bool m_woken = false;
    std::thread m_worker;

    QMutex m_waitersMtx;
    QList<std::shared_ptr<Waiter>> m_waiters;
    QList<std::shared_ptr<RealmeWaiter>> m_realmeWaiters;
    QList<std::shared_ptr<HuaweiWaiter>> m_hwWaiters;
    QList<std::shared_ptr<NothingWaiter>> m_nothingWaiters;
};

class IpcServer {
public:
    using ShowCallback = std::function<void(const QString&)>;
    using NotifyCallback = std::function<void(const QString&, const QString&)>;

    IpcServer(KalmSession& session, ShowCallback onShowWindow = {}, NotifyCallback onNotify = {});
    ~IpcServer();

    bool start();
    void stop();

private:
    void acceptLoop();
    void handleClient(SocketHandle clientFd);
    QJsonObject dispatch(const QJsonObject& req);

    KalmSession& m_session;
    ShowCallback m_onShowWindow;
    NotifyCallback m_onNotify;
    QString m_path;
    std::atomic<SocketHandle> m_serverFd{kInvalidSocket};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

// Standalone oneshot CLI operations when no GUI/daemon is running
QJsonObject oneshotStatus(DeviceRegistry& reg, const QString& deviceQuery = {});
QJsonObject oneshotSetNoise(DeviceRegistry& reg, NoiseMode mode,
                            std::optional<int> ancLevel = std::nullopt,
                            std::optional<int> transEffect = std::nullopt,
                            const QString& deviceQuery = {});
QJsonObject oneshotToggleNoise(DeviceRegistry& reg, const QList<NoiseMode>& cycle, const QString& deviceQuery = {});
QJsonObject oneshotSetEq(DeviceRegistry& reg, int effectId, const QString& deviceQuery = {});
QJsonObject oneshotSetGame(DeviceRegistry& reg, bool enabled, const QString& deviceQuery = {});

}  // namespace kalm
