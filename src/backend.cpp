// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.hpp"

#include "i18n.hpp"
#include "transport.hpp"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDesktopServices>
#include <QFont>
#include <QMetaObject>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QProcess>
#include <QRectF>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

namespace kalm {

namespace {

QString wearLabel(const QString& wear) {
    if (wear == u"worn") return trKey("wear_worn");
    if (wear == u"in_case") return trKey("wear_in_case");
    if (wear == u"removed") return trKey("wear_removed");
    return {};
}

QString formatBattValue(const BatteryReading& r) {
    if (!r.level) return QStringLiteral("—");
    return r.charging ? QStringLiteral("%1% ⚡").arg(*r.level) : QStringLiteral("%1%").arg(*r.level);
}

}  // namespace

QIcon makeKalmIcon(bool connected, std::optional<NoiseMode> mode) {
    QPixmap px(64, 64);
    px.fill(Qt::transparent);

    QPainter painter(&px);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor ringColor("#64748B");
    QColor fillColor(100, 116, 139, 42);
    QColor badgeColor("#475569");

    if (connected) {
        if (mode == NoiseMode::Anc) {
            ringColor = QColor("#38BDF8");
            fillColor = QColor(56, 189, 248, 55);
            badgeColor = QColor("#2563EB");
        } else if (mode == NoiseMode::Transparency) {
            ringColor = QColor("#34D399");
            fillColor = QColor(52, 211, 153, 55);
            badgeColor = QColor("#059669");
        } else {
            ringColor = QColor("#CBD5E1");
            fillColor = QColor(203, 213, 225, 40);
            badgeColor = QColor("#64748B");
        }
    }

    painter.setPen(QPen(ringColor, 3.5));
    painter.setBrush(fillColor);
    painter.drawEllipse(QRectF(5, 5, 54, 54));

    const QColor fg = connected ? QColor("#F8FAFC") : QColor("#94A3B8");
    QPen earPen(fg, 3.2);
    earPen.setCapStyle(Qt::RoundCap);
    painter.setPen(earPen);
    painter.setBrush(fg);

    painter.drawEllipse(QRectF(16, 18, 12, 11));
    painter.drawLine(20, 29, 20, 44);
    painter.drawEllipse(QRectF(36, 18, 12, 11));
    painter.drawLine(44, 29, 44, 44);

    painter.setPen(QPen(QColor("#0F172A"), 2.5));
    painter.setBrush(badgeColor);
    painter.drawEllipse(QRectF(42, 42, 16, 16));

    if (connected && mode) {
        painter.setPen(QColor("#FFFFFF"));
        painter.setFont(QFont(QStringLiteral("Sans"), 8, QFont::Bold));
        QString letter = QStringLiteral("O");
        if (*mode == NoiseMode::Anc) letter = QStringLiteral("A");
        else if (*mode == NoiseMode::Transparency) letter = QStringLiteral("T");
        painter.drawText(QRectF(42, 42, 16, 16), Qt::AlignCenter, letter);
    }

    painter.end();
    return QIcon(px);
}

// ---- KalmBackend ----

KalmBackend::KalmBackend(KalmSession& session, DeviceRegistry& registry, QObject* parent)
    : QObject(parent),
      m_session(session),
      m_registry(registry),
      m_state(session.snapshot()),
      m_caps(session.capabilities()),
      m_capsSignature(session.capabilities().signature()) {
    m_pool.setMaxThreadCount(3);
    rebuildLocalizedModels();
    refreshSavedDevicesAsync();
}

KalmBackend::~KalmBackend() {
    waitForWorkers();
}

void KalmBackend::waitForWorkers() {
    m_pool.clear();
    m_pool.waitForDone();
}

void KalmBackend::rebuildLocalizedModels() {
    m_languageModel.clear();
    for (const auto& l : availableLanguages()) {
        m_languageModel.append(QVariantMap{
            {QStringLiteral("text"), l.label},
            {QStringLiteral("value"), l.code},
        });
    }

    m_ancLevelModel.clear();
    for (const auto& a : m_caps.ancLevelList) {
        m_ancLevelModel.append(QVariantMap{
            {QStringLiteral("text"), trKey(a.trKeyName)},
            {QStringLiteral("value"), a.code},
        });
    }

    m_transEffectModel.clear();
    for (const auto& t : m_caps.transparencyList) {
        m_transEffectModel.append(QVariantMap{
            {QStringLiteral("text"), trKey(t.trKeyName)},
            {QStringLiteral("value"), t.code},
        });
    }

    m_eqModel.clear();
    for (const auto& e : m_caps.eqList) {
        m_eqModel.append(QVariantMap{
            {QStringLiteral("text"), trKey(e.trKeyName)},
            {QStringLiteral("value"), e.code},
        });
    }

    m_cycleModel.clear();
    for (const auto& c : m_caps.cycleList) {
        m_cycleModel.append(QVariantMap{
            {QStringLiteral("text"), trKey(c.trKeyName)},
            {QStringLiteral("value"), c.code},
        });
    }
}

#ifndef KALM_DISPLAY_VERSION
#define KALM_DISPLAY_VERSION "dev"
#endif

QString KalmBackend::appVersion() const {
    return QStringLiteral("Kalm · " KALM_DISPLAY_VERSION);
}

QVariantMap KalmBackend::i18n() const { return i18nVariantMap(); }

int KalmBackend::languageIndex() const {
    const QString pref = m_registry.language().trimmed().toLower();
    for (qsizetype i = 0; i < m_languageModel.size(); ++i) {
        if (m_languageModel[i].toMap().value(QStringLiteral("value")).toString() == pref) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void KalmBackend::setLanguageByIndex(int index) {
    if (index < 0 || index >= m_languageModel.size()) return;
    const QString code = m_languageModel[index].toMap().value(QStringLiteral("value")).toString();
    if (code.isEmpty()) return;
    m_registry.setLanguage(code);
    initI18n(code);
    rebuildLocalizedModels();
    Q_EMIT languageChanged();
    Q_EMIT stateChanged();
    Q_EMIT devicesChanged();
}

void KalmBackend::bindWindow(QWindow* win) {
    m_window = win;
    if (m_window) {
        m_window->setIcon(makeKalmIcon(m_state.connected, m_state.noiseMode));
    }
}

void KalmBackend::pushStateFromWorker(const EarbudState& st) {
    const SessionCapabilities caps = m_session.capabilities();
    const QString sig = caps.signature();
    QMetaObject::invokeMethod(
        this,
        [this, st, caps, sig]() {
            if (sig != m_capsSignature) {
                m_caps = caps;
                m_capsSignature = sig;
                rebuildLocalizedModels();
                Q_EMIT capabilitiesChanged();
                Q_EMIT languageChanged();
            }
            const bool becameConnected = (!m_state.connected && st.connected);
            m_state = st;
            if (becameConnected && m_bannerType >= 2) {
                clearBanner();
            }
            if (!st.connected && m_ringing) {
                m_ringing = false;
                Q_EMIT ringingChanged();
            }
            if (m_window) {
                m_window->setIcon(makeKalmIcon(st.connected, st.noiseMode));
            }
            Q_EMIT stateChanged();
        },
        Qt::QueuedConnection);
}

void KalmBackend::pushDevicesRefreshFromWorker() {
    QMetaObject::invokeMethod(this, &KalmBackend::refreshSavedDevicesAsync, Qt::QueuedConnection);
}

void KalmBackend::pushErrorFromWorker(const QString& msg) {
    QMetaObject::invokeMethod(this, [this, msg]() { setBannerMain(msg, 2); }, Qt::QueuedConnection);
}

void KalmBackend::pushShowFromWorker(const QString& activationToken) {
    QMetaObject::invokeMethod(
        this,
        [this, activationToken]() {
            if (!activationToken.isEmpty() && activationToken.size() <= 512) {
                bool safeToken = true;
                for (QChar ch : activationToken) {
                    if (ch < QChar(0x21) || ch > QChar(0x7E)) {
                        safeToken = false;
                        break;
                    }
                }
                if (safeToken) {
                    qputenv("XDG_ACTIVATION_TOKEN", activationToken.toLatin1());
                }
            }
            Q_EMIT windowShowRequested();
        },
        Qt::QueuedConnection);
}

void KalmBackend::setBannerMain(const QString& text, int type) {
    m_bannerText = text;
    m_bannerType = type;
    const int seq = ++m_bannerSeq;
    Q_EMIT bannerChanged();

    if (type == 1 && !text.isEmpty()) {
        QTimer::singleShot(4000, this, [this, seq]() {
            if (m_bannerSeq == seq && !m_bannerText.isEmpty()) {
                clearBanner();
            }
        });
    }
}

QString KalmBackend::deviceName() const {
    return m_state.deviceName.isEmpty() ? QStringLiteral("Kalm") : m_state.deviceName;
}

QString KalmBackend::statusSummary() const {
    if (m_state.connected) {
        QStringList parts{trKey("status_connected")};
        if (!m_state.firmware.isEmpty()) parts.append(QStringLiteral("FW %1").arg(m_state.firmware));
        if (!m_state.deviceMac.isEmpty()) parts.append(m_state.deviceMac);
        return parts.join(QStringLiteral(" · "));
    }
    if (!m_state.deviceMac.isEmpty()) {
        return QStringLiteral("%1 · %2").arg(trKey("status_disconnected"), m_state.deviceMac);
    }
    return trKey("status_add_or_select");
}

int KalmBackend::noiseModeIndex() const {
    if (!m_state.connected || !m_state.noiseMode) return -1;
    return static_cast<int>(static_cast<uint8_t>(*m_state.noiseMode));
}

int KalmBackend::ancLevelIndex() const {
    const int cur = m_state.noiseEffect.value_or(3);
    for (qsizetype i = 0; i < m_caps.ancLevelList.size(); ++i) {
        if (m_caps.ancLevelList[i].code == cur) return static_cast<int>(i);
    }
    return 0;
}

int KalmBackend::transEffectIndex() const {
    const int cur = m_state.transparencyEffect.value_or(1);
    for (qsizetype i = 0; i < m_caps.transparencyList.size(); ++i) {
        if (m_caps.transparencyList[i].code == cur) return static_cast<int>(i);
    }
    return 0;
}

QString KalmBackend::ancTextForCode(int code) const {
    for (const auto& v : m_ancLevelModel) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("value")).toInt() == code) {
            return m.value(QStringLiteral("text")).toString();
        }
    }
    return {};
}

QString KalmBackend::transTextForCode(int code) const {
    for (const auto& v : m_transEffectModel) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("value")).toInt() == code) {
            return m.value(QStringLiteral("text")).toString();
        }
    }
    return {};
}

QString KalmBackend::leftBatteryText() const { return formatBattValue(m_state.leftBattery); }

QString KalmBackend::leftBatterySub() const {
    const QString w = wearLabel(m_state.leftWear);
    return w.isEmpty() ? trKey("batt_left_full") : QStringLiteral("%1 · %2").arg(trKey("batt_left"), w);
}

QString KalmBackend::rightBatteryText() const { return formatBattValue(m_state.rightBattery); }

QString KalmBackend::rightBatterySub() const {
    const QString w = wearLabel(m_state.rightWear);
    return w.isEmpty() ? trKey("batt_right_full") : QStringLiteral("%1 · %2").arg(trKey("batt_right"), w);
}

QString KalmBackend::caseBatteryText() const { return formatBattValue(m_state.caseBattery); }

QString KalmBackend::caseBatterySub() const {
    if (m_state.caseBattery.level && m_state.caseBattery.stale) {
        return trKey("batt_case_cached");
    }
    return trKey("batt_case_full");
}

int KalmBackend::eqIndex() const {
    if (!m_state.audioEffect) return 0;
    for (qsizetype i = 0; i < m_caps.eqList.size(); ++i) {
        if (m_caps.eqList[i].code == *m_state.audioEffect) return static_cast<int>(i);
    }
    return 0;
}

int KalmBackend::cycleIndex() const {
    if (!m_state.noiseCycleLeft) return 0;
    for (qsizetype i = 0; i < m_caps.cycleList.size(); ++i) {
        if (m_caps.cycleList[i].code == *m_state.noiseCycleLeft) return static_cast<int>(i);
    }
    return 0;
}

QVariantList KalmBackend::multipointDevices() const {
    QVariantList out;
    for (const auto& d : m_state.pairedDevices) {
        out.append(QVariantMap{
            {QStringLiteral("mac"), d.macStr()},
            {QStringLiteral("name"), d.name.isEmpty() ? d.macStr() : d.name},
            {QStringLiteral("statusText"), d.statusLabel()},
            {QStringLiteral("connected"), d.connected()},
            {QStringLiteral("active"), d.active()},
        });
    }
    return out;
}

void KalmBackend::runBg(std::function<void()> fn) {
    m_pool.start([this, fn = std::move(fn)]() {
        try {
            fn();
        } catch (const std::exception& ex) {
            const QString err = QString::fromUtf8(ex.what());
            QMetaObject::invokeMethod(this, [this, err]() { setBannerMain(err, 3); }, Qt::QueuedConnection);
        }
    });
}

void KalmBackend::refreshSavedDevicesAsync() {
    m_pool.start([this]() {
        QMap<QString, BluezDevice> bmap;
        for (const auto& b : listBluezDevices(false, false)) {
            bmap.insert(b.mac, b);
        }
        const QString activeMac = m_registry.activeMac();
        const auto saved = m_registry.devices();
        QVariantList items;
        for (const auto& d : saved) {
            const bool hasB = bmap.contains(d.mac);
            const auto b = hasB ? bmap.value(d.mac) : BluezDevice{};
            items.append(QVariantMap{
                {QStringLiteral("mac"), d.mac},
                {QStringLiteral("displayName"), d.displayName()},
                {QStringLiteral("rawName"), d.name},
                {QStringLiteral("alias"), d.alias},
                {QStringLiteral("active"), d.mac == activeMac},
                {QStringLiteral("btConnected"), hasB && b.connected},
                {QStringLiteral("btPaired"), hasB && b.paired},
            });
        }
        QMetaObject::invokeMethod(
            this,
            [this, items]() {
                if (m_savedList == items) return;
                m_savedList = items;
                Q_EMIT devicesChanged();
            },
            Qt::QueuedConnection);
    });
}

void KalmBackend::setNoiseByIndex(int index) {
    if (index < 0 || index > 2) return;
    const auto mode = static_cast<NoiseMode>(index);
    m_state.noiseMode = mode;
    Q_EMIT stateChanged();
    runBg([this, mode]() { m_session.setNoiseMode(mode); });
}

void KalmBackend::toggleNoiseAsync() {
    runBg([this]() { m_session.toggleNoiseMode(); });
}

void KalmBackend::setAncLevelByIndex(int index) {
    if (index < 0 || index >= m_caps.ancLevelList.size()) return;
    const int code = m_caps.ancLevelList[index].code;
    m_state.noiseMode = NoiseMode::Anc;
    m_state.noiseEffect = code;
    Q_EMIT stateChanged();
    runBg([this, code]() { m_session.setAncLevel(code); });
}

void KalmBackend::setTransEffectByIndex(int index) {
    if (index < 0 || index >= m_caps.transparencyList.size()) return;
    const int code = m_caps.transparencyList[index].code;
    m_state.noiseMode = NoiseMode::Transparency;
    m_state.transparencyEffect = code;
    Q_EMIT stateChanged();
    runBg([this, code]() { m_session.setTransparencyEffect(code); });
}

void KalmBackend::setEqByIndex(int index) {
    if (index < 0 || index >= m_caps.eqList.size()) return;
    const int code = m_caps.eqList[index].code;
    runBg([this, code]() { m_session.setAudioEffect(code); });
}

void KalmBackend::setCycleByIndex(int index) {
    if (index < 0 || index >= m_caps.cycleList.size()) return;
    const int code = m_caps.cycleList[index].code;
    runBg([this, code]() { m_session.setNoiseCycle(code); });
}

void KalmBackend::setGameMode(bool enabled) {
    runBg([this, enabled]() { m_session.setLowLatency(enabled); });
}

void KalmBackend::setWearDetection(bool enabled) {
    runBg([this, enabled]() { m_session.setWearDetection(enabled); });
}

void KalmBackend::toggleMultipointDevice(const QString& mac, bool connect) {
    runBg([this, mac, connect]() { m_session.setDeviceConnection(mac, connect); });
}

void KalmBackend::startFindEarbuds() {
    m_ringing = true;
    Q_EMIT ringingChanged();
    runBg([this]() { m_session.findEarbuds(true); });
}

void KalmBackend::stopFindEarbuds() {
    m_ringing = false;
    Q_EMIT ringingChanged();
    runBg([this]() { m_session.findEarbuds(false); });
}

void KalmBackend::refreshNow() {
    if (m_state.connected) {
        runBg([this]() { m_session.refresh(); });
    } else {
        m_session.forceReconnect();
    }
    refreshSavedDevicesAsync();
}

void KalmBackend::scanBluetoothDevices() {
    m_pool.start([this]() {
        const auto devs = listBluezDevices(false, true);
        QSet<QString> savedMacs;
        for (const auto& s : m_registry.devices()) savedMacs.insert(s.mac);
        QVariantList out;
        for (const auto& b : devs) {
            QStringList parts{b.mac};
            if (b.isNothingTws()) parts.append(QStringLiteral("Nothing / CMF"));
            else if (b.isHuaweiTws()) parts.append(QStringLiteral("HUAWEI FreeBuds"));
            else if (b.isVivoTws()) parts.append(QStringLiteral("vivo/iQOO GAIA"));
            else if (b.isRealmeOppoTws()) parts.append(QStringLiteral("realme/OPPO"));
            parts.append(b.connected ? trKey("status_connected") : trKey("status_paired"));
            const bool already = savedMacs.contains(b.mac);
            if (already) parts.append(trKey("status_in_list"));
            out.append(QVariantMap{
                {QStringLiteral("mac"), b.mac},
                {QStringLiteral("name"), b.name},
                {QStringLiteral("isSupported"), b.isSupportedTws()},
                {QStringLiteral("connected"), b.connected},
                {QStringLiteral("alreadySaved"), already},
                {QStringLiteral("subtitle"), parts.join(QStringLiteral(" · "))},
            });
        }
        QMetaObject::invokeMethod(
            this,
            [this, out]() {
                m_discoveredList = out;
                Q_EMIT discoveredChanged();
            },
            Qt::QueuedConnection);
    });
}

bool KalmBackend::addSavedDevice(const QString& mac, const QString& name, const QString& alias) {
    SavedEarbud entry;
    try {
        entry = m_registry.add(mac, name, alias, true);
    } catch (const std::exception& ex) {
        setBannerMain(QString::fromUtf8(ex.what()), 3);
        return false;
    }
    m_session.switchToDevice(entry.mac);
    refreshSavedDevicesAsync();
    setBannerMain(trKey("msg_selected_earbuds").arg(entry.displayName()), 1);
    return true;
}

void KalmBackend::removeSavedDevice(const QString& mac) {
    m_registry.remove(mac);
    m_session.notifyRegistryUpdated();
    refreshSavedDevicesAsync();
}

void KalmBackend::renameSavedDevice(const QString& mac, const QString& alias) {
    m_registry.rename(mac, alias);
    m_session.notifyRegistryUpdated();
    refreshSavedDevicesAsync();
}

void KalmBackend::selectSavedDevice(const QString& mac) {
    m_session.switchToDevice(mac);
    refreshSavedDevicesAsync();
}

void KalmBackend::connectBluetoothDevice(const QString& mac) {
    setBannerMain(trKey("msg_connecting_bt").arg(mac), 0);
    if (m_btConnecting) return;
    m_btConnecting = true;
    m_pool.start([this, mac]() {
        const bool ok = connectBluezDevice(mac);
        if (ok) {
            m_session.forceReconnect();
        }
        QMetaObject::invokeMethod(
            this,
            [this, ok]() {
                m_btConnecting = false;
                if (ok) {
                    setBannerMain(trKey("msg_bt_connected"), 1);
                } else {
                    setBannerMain(trKey("msg_bt_failed"), 2);
                }
            },
            Qt::QueuedConnection);
        refreshSavedDevicesAsync();
    });
}

void KalmBackend::setAutoSwitch(bool enabled) {
    m_registry.setAutoSwitch(enabled);
    Q_EMIT devicesChanged();
}

void KalmBackend::openSystemBluetoothSettings() {
#ifdef _WIN32
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:bluetooth")));
#else
    const QList<QStringList> cmds = {
        {QStringLiteral("systemsettings"), QStringLiteral("kcm_bluetooth")},
        {QStringLiteral("kcmshell6"), QStringLiteral("kcm_bluetooth")},
        {QStringLiteral("bluedevil-wizard")},
        {QStringLiteral("gnome-control-center"), QStringLiteral("bluetooth")},
        {QStringLiteral("blueman-manager")},
    };
    for (const QStringList& cmd : cmds) {
        if (!QStandardPaths::findExecutable(cmd.first()).isEmpty()) {
            QProcess::startDetached(cmd.first(), cmd.mid(1));
            return;
        }
    }
#endif
}

void KalmBackend::runDiagnosticProbe() {
    if (m_probeBusy) return;
    m_probeBusy = true;
    m_probeReport = trKey("dlg_probe_running");
    Q_EMIT probeChanged();

    m_pool.start([this]() {
        QString rep;
        try {
            rep = m_session.runProbe();
        } catch (const std::exception& ex) {
            rep = QString::fromUtf8(ex.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, rep]() {
                m_probeReport = rep;
                m_probeBusy = false;
                Q_EMIT probeChanged();
            },
            Qt::QueuedConnection);
    });
}

void KalmBackend::copyReportToClipboard() {
    if (m_probeReport.isEmpty()) return;
    if (auto* cb = QApplication::clipboard()) {
        cb->setText(m_probeReport);
        setBannerMain(trKey("msg_copied"), 1);
    }
}

void KalmBackend::clearBanner() {
    if (m_bannerText.isEmpty()) return;
    m_bannerText.clear();
    ++m_bannerSeq;
    Q_EMIT bannerChanged();
}

void KalmBackend::quitApp() {
    m_quitting = true;
    Q_EMIT quittingChanged();
    if (auto* app = QApplication::instance()) {
        app->quit();
    }
}

// ---- KalmTrayController ----

KalmTrayController::KalmTrayController(KalmSession& session, KalmBackend& backend, QObject* parent)
    : QObject(parent),
      m_session(session),
      m_backend(backend),
      m_tray(makeKalmIcon(false, std::nullopt), this) {
    m_headerAct = m_menu.addAction(QStringLiteral("Kalm"));
    m_headerAct->setEnabled(false);
    m_battAct = m_menu.addAction(QStringLiteral("—"));
    m_battAct->setEnabled(false);
    m_menu.addSeparator();

    auto* modeGroup = new QActionGroup(&m_menu);
    modeGroup->setExclusive(true);

    m_actAnc = m_menu.addAction(QString{});
    m_actAnc->setCheckable(true);
    modeGroup->addAction(m_actAnc);
    connect(m_actAnc, &QAction::triggered, this, [this]() { m_backend.setNoiseByIndex(0); });

    m_actTrans = m_menu.addAction(QString{});
    m_actTrans->setCheckable(true);
    modeGroup->addAction(m_actTrans);
    connect(m_actTrans, &QAction::triggered, this, [this]() { m_backend.setNoiseByIndex(2); });

    m_actOff = m_menu.addAction(QString{});
    m_actOff->setCheckable(true);
    modeGroup->addAction(m_actOff);
    connect(m_actOff, &QAction::triggered, this, [this]() { m_backend.setNoiseByIndex(1); });

    m_ancLevelMenu = m_menu.addMenu(QString{});

    m_menu.addSeparator();

    m_devsMenu = m_menu.addMenu(QString{});
    m_devsGroup = new QActionGroup(m_devsMenu);
    m_devsGroup->setExclusive(true);

    m_eqMenu = m_menu.addMenu(QString{});

    m_gameAct = m_menu.addAction(QString{});
    m_gameAct->setCheckable(true);
    connect(m_gameAct, &QAction::triggered, this, [this](bool checked) { m_backend.setGameMode(checked); });

    m_menu.addSeparator();
    m_openAct = m_menu.addAction(QString{});
    connect(m_openAct, &QAction::triggered, &m_backend, &KalmBackend::windowShowRequested);

    m_quitAct = m_menu.addAction(QString{});
    connect(m_quitAct, &QAction::triggered, &m_backend, &KalmBackend::quitApp);

    m_tray.setContextMenu(&m_menu);
    connect(&m_tray, &QSystemTrayIcon::activated, this, &KalmTrayController::onActivated);
    connect(&m_backend, &KalmBackend::languageChanged, this, &KalmTrayController::retranslateMenu);
    connect(&m_backend, &KalmBackend::capabilitiesChanged, this, &KalmTrayController::rebuildProtocolMenus);
    connect(&m_backend, &KalmBackend::stateChanged, this, &KalmTrayController::syncState);
    connect(&m_backend, &KalmBackend::devicesChanged, this, &KalmTrayController::syncDevicesMenu);

    rebuildProtocolMenus();
    retranslateMenu();
    m_tray.show();
}

void KalmTrayController::rebuildProtocolMenus() {
    if (m_ancLevelGroup) {
        m_ancLevelGroup->deleteLater();
        m_ancLevelGroup = nullptr;
    }
    m_ancLevelMenu->clear();
    m_ancLevelActions.clear();
    m_ancLevelGroup = new QActionGroup(m_ancLevelMenu);
    m_ancLevelGroup->setExclusive(true);

    const QVariantList ancList = m_backend.ancLevelModel();
    for (qsizetype i = 0; i < ancList.size(); ++i) {
        const QVariantMap item = ancList[i].toMap();
        QAction* act = m_ancLevelMenu->addAction(item.value(QStringLiteral("text")).toString());
        act->setCheckable(true);
        m_ancLevelGroup->addAction(act);
        m_ancLevelActions.insert(item.value(QStringLiteral("value")).toInt(), act);
        const int idx = static_cast<int>(i);
        connect(act, &QAction::triggered, this, [this, idx]() { m_backend.setAncLevelByIndex(idx); });
    }

    if (m_eqGroup) {
        m_eqGroup->deleteLater();
        m_eqGroup = nullptr;
    }
    m_eqMenu->clear();
    m_eqActions.clear();
    m_eqGroup = new QActionGroup(m_eqMenu);
    m_eqGroup->setExclusive(true);

    const QVariantList eqList = m_backend.eqModel();
    for (qsizetype i = 0; i < eqList.size(); ++i) {
        const QVariantMap item = eqList[i].toMap();
        QAction* act = m_eqMenu->addAction(item.value(QStringLiteral("text")).toString());
        act->setCheckable(true);
        m_eqGroup->addAction(act);
        m_eqActions.insert(item.value(QStringLiteral("value")).toInt(), act);
        const int idx = static_cast<int>(i);
        connect(act, &QAction::triggered, this, [this, idx]() { m_backend.setEqByIndex(idx); });
    }

    syncState();
}

void KalmTrayController::retranslateMenu() {
    m_actAnc->setText(trKey("mode_anc_short"));
    m_actTrans->setText(trKey("mode_trans"));
    m_actOff->setText(trKey("mode_off"));

    m_ancLevelMenu->setTitle(trKey("tray_anc_level_menu"));
    for (const auto& v : m_backend.ancLevelModel()) {
        const QVariantMap item = v.toMap();
        const int code = item.value(QStringLiteral("value")).toInt();
        if (m_ancLevelActions.contains(code)) {
            m_ancLevelActions.value(code)->setText(item.value(QStringLiteral("text")).toString());
        }
    }

    m_devsMenu->setTitle(trKey("window_my_earbuds"));
    m_eqMenu->setTitle(trKey("tray_eq_menu"));
    for (const auto& v : m_backend.eqModel()) {
        const QVariantMap item = v.toMap();
        const int code = item.value(QStringLiteral("value")).toInt();
        if (m_eqActions.contains(code)) {
            m_eqActions.value(code)->setText(item.value(QStringLiteral("text")).toString());
        }
    }

    m_gameAct->setText(trKey("game_title"));
    m_openAct->setText(trKey("action_open_app"));
    m_quitAct->setText(trKey("action_quit"));

    syncState();
    syncDevicesMenu();
}

void KalmTrayController::onActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        if (m_window && m_window->isVisible()) {
            m_window->setVisible(false);
        } else {
            Q_EMIT m_backend.windowShowRequested();
        }
    } else if (reason == QSystemTrayIcon::MiddleClick) {
        m_backend.toggleNoiseAsync();
    }
}

void KalmTrayController::showToast(const QString& title, const QString& body) {
    QMetaObject::invokeMethod(
        this,
        [this, title, body]() {
            m_tray.showMessage(title, body, m_tray.icon(), 2500);
        },
        Qt::QueuedConnection);
}

void KalmTrayController::syncDevicesMenu() {
    for (QAction* a : m_devsGroup->actions()) {
        m_devsGroup->removeAction(a);
    }
    m_devsMenu->clear();

    const QVariantList items = m_backend.savedDevices();
    if (items.isEmpty()) {
        QAction* empty = m_devsMenu->addAction(trKey("tray_no_saved"));
        empty->setEnabled(false);
        return;
    }

    for (const QVariant& v : items) {
        const QVariantMap d = v.toMap();
        const QString suffix = d.value(QStringLiteral("btConnected")).toBool()
                                   ? QStringLiteral(" (%1)").arg(trKey("status_online_short"))
                                   : QString{};
        QAction* act = m_devsMenu->addAction(d.value(QStringLiteral("displayName")).toString() + suffix);
        act->setCheckable(true);
        act->setChecked(d.value(QStringLiteral("active")).toBool());
        const QString mac = d.value(QStringLiteral("mac")).toString();
        connect(act, &QAction::triggered, this, [this, mac]() { m_backend.selectSavedDevice(mac); });
        m_devsGroup->addAction(act);
    }
}

void KalmTrayController::syncState() {
    const EarbudState st = m_session.snapshot();
    m_tray.setIcon(makeKalmIcon(st.connected, st.noiseMode));

    const QString name = st.deviceName.isEmpty() ? QStringLiteral("Kalm") : st.deviceName;
    if (!st.connected) {
        const QString dis = QStringLiteral("%1 — %2").arg(name, trKey("status_disconnected"));
        m_tray.setToolTip(dis);
        m_headerAct->setText(dis);
        m_battAct->setText(QStringLiteral("%1: —").arg(trKey("batt_label")));
        for (QAction* a : {m_actAnc, m_actTrans, m_actOff, m_gameAct}) a->setEnabled(false);
        m_ancLevelMenu->setEnabled(false);
        return;
    }

    const bool hasAncLevels = m_backend.supportsAncLevels();
    const bool hasTransEffects = m_backend.supportsTransEffects();
    const bool hasEq = m_backend.supportsEq() && !m_backend.eqModel().isEmpty();
    const bool hasGame = m_backend.supportsGameMode();

    QString modeTxt = st.noiseMode ? noiseModeLabel(*st.noiseMode) : QStringLiteral("—");
    if (hasAncLevels && st.noiseMode == NoiseMode::Anc && st.noiseEffect) {
        const QString sub = m_backend.ancTextForCode(*st.noiseEffect);
        if (!sub.isEmpty()) modeTxt += QStringLiteral(": %1").arg(sub);
    } else if (hasTransEffects && st.noiseMode == NoiseMode::Transparency && st.transparencyEffect) {
        const QString sub = m_backend.transTextForCode(*st.transparencyEffect);
        if (!sub.isEmpty()) modeTxt += QStringLiteral(": %1").arg(sub);
    }

    QStringList bParts;
    if (st.leftBattery.level) bParts.append(QStringLiteral("%1: %2%").arg(trKey("batt_left").left(1), QString::number(*st.leftBattery.level)));
    if (st.rightBattery.level) bParts.append(QStringLiteral("%1: %2%").arg(trKey("batt_right").left(1), QString::number(*st.rightBattery.level)));
    if (st.caseBattery.level && !st.caseBattery.stale) {
        bParts.append(QStringLiteral("%1: %2%").arg(trKey("batt_case"), QString::number(*st.caseBattery.level)));
    }
    const QString battStr = bParts.isEmpty() ? QStringLiteral("—") : bParts.join(QStringLiteral("  "));

    m_tray.setToolTip(QStringLiteral("%1\n%2\n%3").arg(name, modeTxt, battStr));
    m_headerAct->setText(QStringLiteral("%1 (%2)").arg(name, modeTxt));
    m_battAct->setText(QStringLiteral("%1: %2").arg(trKey("batt_label"), battStr));

    for (QAction* a : {m_actAnc, m_actTrans, m_actOff}) a->setEnabled(true);
    m_ancLevelMenu->menuAction()->setVisible(hasAncLevels);
    m_ancLevelMenu->setEnabled(hasAncLevels);
    m_eqMenu->menuAction()->setVisible(hasEq);
    m_eqMenu->setEnabled(hasEq);
    m_gameAct->setVisible(hasGame);
    m_gameAct->setEnabled(hasGame);

    if (st.noiseMode == NoiseMode::Anc) m_actAnc->setChecked(true);
    else if (st.noiseMode == NoiseMode::Off) m_actOff->setChecked(true);
    else if (st.noiseMode == NoiseMode::Transparency) m_actTrans->setChecked(true);

    const int ancCode = st.noiseEffect.value_or(3);
    if (m_ancLevelActions.contains(ancCode)) {
        m_ancLevelActions.value(ancCode)->setChecked(true);
    }
    if (st.audioEffect && m_eqActions.contains(*st.audioEffect)) {
        m_eqActions.value(*st.audioEffect)->setChecked(true);
    }
    if (st.lowLatency) {
        m_gameAct->setChecked(*st.lowLatency);
    }
}

}  // namespace kalm
