// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protocol.hpp"
#include "registry.hpp"
#include "session.hpp"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMap>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>
#include <optional>

namespace kalm {

QIcon makeKalmIcon(bool connected, std::optional<NoiseMode> mode);

class KalmBackend : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QVariantMap i18n READ i18n NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languageModel READ languageModel NOTIFY languageChanged)
    Q_PROPERTY(int languageIndex READ languageIndex NOTIFY languageChanged)

    Q_PROPERTY(bool quitting READ quitting NOTIFY quittingChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool vivoFeatures READ vivoFeatures NOTIFY stateChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged)
    Q_PROPERTY(QString deviceMac READ deviceMac NOTIFY stateChanged)
    Q_PROPERTY(QString firmware READ firmware NOTIFY stateChanged)
    Q_PROPERTY(QString statusSummary READ statusSummary NOTIFY stateChanged)
    Q_PROPERTY(int noiseModeIndex READ noiseModeIndex NOTIFY stateChanged)

    Q_PROPERTY(QVariantList ancLevelModel READ ancLevelModel NOTIFY languageChanged)
    Q_PROPERTY(int ancLevelIndex READ ancLevelIndex NOTIFY stateChanged)
    Q_PROPERTY(QVariantList transEffectModel READ transEffectModel NOTIFY languageChanged)
    Q_PROPERTY(int transEffectIndex READ transEffectIndex NOTIFY stateChanged)

    Q_PROPERTY(QString leftBatteryText READ leftBatteryText NOTIFY stateChanged)
    Q_PROPERTY(QString leftBatterySub READ leftBatterySub NOTIFY stateChanged)
    Q_PROPERTY(QString rightBatteryText READ rightBatteryText NOTIFY stateChanged)
    Q_PROPERTY(QString rightBatterySub READ rightBatterySub NOTIFY stateChanged)
    Q_PROPERTY(QString caseBatteryText READ caseBatteryText NOTIFY stateChanged)
    Q_PROPERTY(QString caseBatterySub READ caseBatterySub NOTIFY stateChanged)

    Q_PROPERTY(QVariantList eqModel READ eqModel NOTIFY languageChanged)
    Q_PROPERTY(int eqIndex READ eqIndex NOTIFY stateChanged)
    Q_PROPERTY(QVariantList cycleModel READ cycleModel NOTIFY languageChanged)
    Q_PROPERTY(int cycleIndex READ cycleIndex NOTIFY stateChanged)

    Q_PROPERTY(bool gameMode READ gameMode NOTIFY stateChanged)
    Q_PROPERTY(bool wearDetection READ wearDetection NOTIFY stateChanged)
    Q_PROPERTY(bool isRinging READ isRinging NOTIFY ringingChanged)

    Q_PROPERTY(bool supportsAncLevels READ supportsAncLevels NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsTransEffects READ supportsTransEffects NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsEq READ supportsEq NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsGameMode READ supportsGameMode NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsWearDetection READ supportsWearDetection NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsNoiseCycle READ supportsNoiseCycle NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsMultipoint READ supportsMultipoint NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsFindDevice READ supportsFindDevice NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsProbe READ supportsProbe NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString protocolLabel READ protocolLabel NOTIFY capabilitiesChanged)

    Q_PROPERTY(QVariantList multipointDevices READ multipointDevices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList savedDevices READ savedDevices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList discoveredDevices READ discoveredDevices NOTIFY discoveredChanged)
    Q_PROPERTY(bool autoSwitch READ autoSwitch NOTIFY devicesChanged)

    Q_PROPERTY(QString bannerText READ bannerText NOTIFY bannerChanged)
    Q_PROPERTY(int bannerType READ bannerType NOTIFY bannerChanged)
    Q_PROPERTY(bool probeBusy READ probeBusy NOTIFY probeChanged)
    Q_PROPERTY(QString probeReport READ probeReport NOTIFY probeChanged)

public:
    KalmBackend(KalmSession& session, DeviceRegistry& registry, QObject* parent = nullptr);
    ~KalmBackend() override;

    void bindWindow(QWindow* win);

    void pushStateFromWorker(const EarbudState& st);
    void pushDevicesRefreshFromWorker();
    void pushErrorFromWorker(const QString& msg);
    void pushShowFromWorker(const QString& activationToken = {});

    QString appVersion() const;
    QVariantMap i18n() const;
    QVariantList languageModel() const { return m_languageModel; }
    int languageIndex() const;

    bool quitting() const { return m_quitting; }
    bool connected() const { return m_state.connected; }
    bool vivoFeatures() const { return !m_state.connected || m_state.protocol == DeviceProtocol::VivoGaia; }
    QString deviceName() const;
    QString deviceMac() const { return m_state.deviceMac; }
    QString firmware() const { return m_state.firmware; }
    QString statusSummary() const;
    int noiseModeIndex() const;

    QVariantList ancLevelModel() const { return m_ancLevelModel; }
    int ancLevelIndex() const;
    QVariantList transEffectModel() const { return m_transEffectModel; }
    int transEffectIndex() const;

    QString leftBatteryText() const;
    QString leftBatterySub() const;
    QString rightBatteryText() const;
    QString rightBatterySub() const;
    QString caseBatteryText() const;
    QString caseBatterySub() const;

    QVariantList eqModel() const { return m_eqModel; }
    int eqIndex() const;
    QVariantList cycleModel() const { return m_cycleModel; }
    int cycleIndex() const;

    bool gameMode() const { return m_state.lowLatency.value_or(false); }
    bool wearDetection() const { return m_state.wearDetection.value_or(false); }
    bool isRinging() const { return m_ringing; }

    bool supportsAncLevels() const { return m_caps.ancLevels; }
    bool supportsTransEffects() const { return m_caps.transparency; }
    bool supportsEq() const { return m_caps.equalizer; }
    bool supportsGameMode() const { return m_caps.gameMode; }
    bool supportsWearDetection() const { return m_caps.wearDetection; }
    bool supportsNoiseCycle() const { return m_caps.noiseCycle; }
    bool supportsMultipoint() const { return m_caps.multipoint; }
    bool supportsFindDevice() const { return m_caps.findDevice; }
    bool supportsProbe() const { return m_caps.probe; }
    QString protocolLabel() const { return m_caps.profileLabel; }

    QString ancTextForCode(int code) const;
    QString transTextForCode(int code) const;
    QString eqTextForCode(int code) const;

    QVariantList multipointDevices() const;
    QVariantList savedDevices() const { return m_savedList; }
    QVariantList discoveredDevices() const { return m_discoveredList; }
    bool autoSwitch() const { return m_registry.autoSwitch(); }

    QString bannerText() const { return m_bannerText; }
    int bannerType() const { return m_bannerType; }
    bool probeBusy() const { return m_probeBusy; }
    QString probeReport() const { return m_probeReport; }

public Q_SLOTS:
    void refreshSavedDevicesAsync();
    void setLanguageByIndex(int index);
    void setNoiseByIndex(int index);
    void toggleNoiseAsync();
    void setAncLevelByIndex(int index);
    void setTransEffectByIndex(int index);
    void setEqByIndex(int index);
    void setCycleByIndex(int index);
    void setGameMode(bool enabled);
    void setWearDetection(bool enabled);
    void toggleMultipointDevice(const QString& mac, bool connect);
    void startFindEarbuds();
    void stopFindEarbuds();
    void refreshNow();
    void scanBluetoothDevices();
    bool addSavedDevice(const QString& mac, const QString& name, const QString& alias);
    void removeSavedDevice(const QString& mac);
    void renameSavedDevice(const QString& mac, const QString& alias);
    void selectSavedDevice(const QString& mac);
    void connectBluetoothDevice(const QString& mac);
    void setAutoSwitch(bool enabled);
    void openSystemBluetoothSettings();
    void runDiagnosticProbe();
    void copyReportToClipboard();
    void clearBanner();
    void quitApp();

Q_SIGNALS:
    void languageChanged();
    void stateChanged();
    void devicesChanged();
    void discoveredChanged();
    void bannerChanged();
    void probeChanged();
    void ringingChanged();
    void quittingChanged();
    void capabilitiesChanged();
    void windowShowRequested();

private:
    void rebuildLocalizedModels();
    void runBg(std::function<void()> fn);
    void setBannerMain(const QString& text, int type);

    KalmSession& m_session;
    DeviceRegistry& m_registry;
    QThreadPool m_pool;
    EarbudState m_state;
    SessionCapabilities m_caps;
    QString m_capsSignature;
    QVariantList m_languageModel;
    QVariantList m_ancLevelModel;
    QVariantList m_transEffectModel;
    QVariantList m_eqModel;
    QVariantList m_cycleModel;
    QVariantList m_savedList;
    QVariantList m_discoveredList;
    QString m_bannerText;
    int m_bannerType = 0;
    bool m_probeBusy = false;
    QString m_probeReport;
    bool m_ringing = false;
    bool m_quitting = false;
    QPointer<QWindow> m_window;
};

class KalmTrayController : public QObject {
    Q_OBJECT
public:
    KalmTrayController(KalmSession& session, KalmBackend& backend, QObject* parent = nullptr);

    void bindWindow(QWindow* win) { m_window = win; }
    void hide() { m_tray.hide(); }
    void showToast(const QString& title, const QString& body);

private Q_SLOTS:
    void onActivated(QSystemTrayIcon::ActivationReason reason);
    void retranslateMenu();
    void rebuildProtocolMenus();
    void syncState();
    void syncDevicesMenu();

private:
    KalmSession& m_session;
    KalmBackend& m_backend;
    QPointer<QWindow> m_window;

    QSystemTrayIcon m_tray;
    QMenu m_menu;
    QAction* m_headerAct = nullptr;
    QAction* m_battAct = nullptr;
    QAction* m_actAnc = nullptr;
    QAction* m_actTrans = nullptr;
    QAction* m_actOff = nullptr;
    QMenu* m_ancLevelMenu = nullptr;
    QMap<int, QAction*> m_ancLevelActions;
    QActionGroup* m_ancLevelGroup = nullptr;
    QMenu* m_devsMenu = nullptr;
    QActionGroup* m_devsGroup = nullptr;
    QMenu* m_eqMenu = nullptr;
    QMap<int, QAction*> m_eqActions;
    QActionGroup* m_eqGroup = nullptr;
    QAction* m_gameAct = nullptr;
    QAction* m_openAct = nullptr;
    QAction* m_quitAct = nullptr;
};

}  // namespace kalm
