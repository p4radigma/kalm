// SPDX-License-Identifier: GPL-3.0-or-later
#include "protocol.hpp"

#include "huawei.hpp"
#include "i18n.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStandardPaths>

namespace kalm {

namespace {

constexpr int kCommandBytes = 4;
constexpr int kMaxFrameBytes = 65544;

constexpr uint8_t kWearLeftInCase = 0x01;
constexpr uint8_t kWearRightInCase = 0x02;
constexpr uint8_t kWearLeftWorn = 0x04;
constexpr uint8_t kWearRightWorn = 0x08;

uint8_t xorChecksum(const QByteArray& buf, qsizetype count) {
    uint8_t acc = 0;
    for (qsizetype i = 0; i < count; ++i) {
        acc ^= static_cast<uint8_t>(buf[i]);
    }
    return acc;
}

void updateBatterySlot(BatteryReading& slot, uint8_t rawPct, bool charging) {
    if (rawPct <= 100) {
        slot.level = static_cast<int>(rawPct);
        slot.charging = charging;
        slot.stale = false;
    } else {
        slot.charging = false;
        slot.stale = true;
    }
}

QString decodeEarWear(uint8_t flags, uint8_t inCaseBit, uint8_t wornBit) {
    if (flags & wornBit) return QStringLiteral("worn");
    if (flags & inCaseBit) return QStringLiteral("in_case");
    return QStringLiteral("removed");
}

std::optional<QList<MultiConnectDevice>> parseMultiConnectDevices(const QByteArray& payload) {
    if (payload.size() < 2 || static_cast<uint8_t>(payload[0]) != 0) {
        return std::nullopt;
    }
    const int count = static_cast<uint8_t>(payload[1]);
    int offset = 2;
    QList<MultiConnectDevice> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        if (offset + 11 > payload.size()) return std::nullopt;
        MultiConnectDevice dev;
        dev.mac = payload.mid(offset, 6);
        dev.raw3 = payload.mid(offset + 6, 3);
        dev.status = static_cast<uint8_t>(payload[offset + 9]);
        const int nameLen = static_cast<uint8_t>(payload[offset + 10]);
        if (offset + 11 + nameLen > payload.size()) return std::nullopt;
        dev.name = QString::fromUtf8(payload.mid(offset + 11, nameLen));
        out.append(dev);
        offset += 11 + nameLen;
    }
    return out;
}

std::optional<QJsonObject> parseTelemetryPayload(const QByteArray& payload) {
    const qsizetype start = payload.indexOf('{');
    const qsizetype end = payload.lastIndexOf('}');
    if (start < 0 || end <= start) return std::nullopt;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(payload.mid(start, end - start + 1), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    return doc.object();
}

}  // namespace

QString noiseModeLabel(NoiseMode mode) {
    switch (mode) {
        case NoiseMode::Anc: return trKey("mode_anc");
        case NoiseMode::Off: return trKey("mode_off");
        case NoiseMode::Transparency: return trKey("mode_trans");
    }
    return QStringLiteral("—");
}

QString noiseModeSlug(NoiseMode mode) {
    switch (mode) {
        case NoiseMode::Anc: return QStringLiteral("anc");
        case NoiseMode::Off: return QStringLiteral("off");
        case NoiseMode::Transparency: return QStringLiteral("transparency");
    }
    return QStringLiteral("off");
}

std::optional<NoiseMode> parseNoiseModeStr(const QString& text) {
    const QString k = text.trimmed().toLower();
    if (k == u"anc" || k == u"nc" || k == u"on" || k == u"noise" || k == u"0" ||
        k == u"шумодав" || k == u"шумоподавление") {
        return NoiseMode::Anc;
    }
    if (k == u"off" || k == u"normal" || k == u"1" || k == u"выкл" || k == u"выключено") {
        return NoiseMode::Off;
    }
    if (k == u"trans" || k == u"transparency" || k == u"transparent" || k == u"ambient" ||
        k == u"2" || k == u"прозрачность") {
        return NoiseMode::Transparency;
    }
    return std::nullopt;
}

const std::vector<PresetEntry>& ancLevelPresets() {
    static const std::vector<PresetEntry> kList = {
        {3, "anc_adaptive", "adaptive"},
        {1, "anc_mild", "mild"},
        {0, "anc_standard", "standard"},
        {2, "anc_deep", "deep"},
        {5, "anc_plane", "plane"},
        {6, "anc_metro", "metro"},
        {7, "anc_bus", "bus"},
    };
    return kList;
}

QString ancLevelLabel(int code) {
    for (const auto& p : ancLevelPresets()) {
        if (p.code == code) return trKey(p.trKeyName);
    }
    return {};
}

QString ancLevelSlug(int code) {
    for (const auto& p : ancLevelPresets()) {
        if (p.code == code) return QString::fromLatin1(p.slug);
    }
    return {};
}

std::optional<int> parseAncLevelStr(const QString& text) {
    const QString k = text.trimmed().toLower();
    if (k == u"adaptive" || k == u"auto" || k == u"smart" || k == u"3" ||
        k == u"адаптив" || k == u"адаптивное" || k == u"адаптивная") {
        return 3;
    }
    if (k == u"mild" || k == u"low" || k == u"light" || k == u"1" ||
        k == u"слабое" || k == u"легкое" || k == u"лёгкое") {
        return 1;
    }
    if (k == u"standard" || k == u"medium" || k == u"normal" || k == u"0" ||
        k == u"стандарт" || k == u"стандартное" || k == u"среднее") {
        return 0;
    }
    if (k == u"deep" || k == u"high" || k == u"strong" || k == u"max" || k == u"2" ||
        k == u"сильное" || k == u"глубокое") {
        return 2;
    }
    if (k == u"plane" || k == u"airplane" || k == u"5" || k == u"самолет" || k == u"самолёт") {
        return 5;
    }
    if (k == u"metro" || k == u"subway" || k == u"train" || k == u"6" || k == u"метро") {
        return 6;
    }
    if (k == u"bus" || k == u"transit" || k == u"7" || k == u"автобус" || k == u"транспорт") {
        return 7;
    }
    return std::nullopt;
}

const std::vector<PresetEntry>& transparencyPresets() {
    static const std::vector<PresetEntry> kList = {
        {1, "trans_standard", "standard"},
        {2, "trans_voice", "voice"},
    };
    return kList;
}

QString transparencyLabel(int code) {
    for (const auto& p : transparencyPresets()) {
        if (p.code == code) return trKey(p.trKeyName);
    }
    return {};
}

QString transparencySlug(int code) {
    for (const auto& p : transparencyPresets()) {
        if (p.code == code) return QString::fromLatin1(p.slug);
    }
    return {};
}

std::optional<int> parseTransparencyStr(const QString& text) {
    const QString k = text.trimmed().toLower();
    if (k == u"standard" || k == u"normal" || k == u"1" || k == u"стандарт" || k == u"стандартная") {
        return 1;
    }
    if (k == u"voice" || k == u"vocal" || k == u"speech" || k == u"2" ||
        k == u"голос" || k == u"вокал") {
        return 2;
    }
    return std::nullopt;
}

const std::vector<PresetEntry>& audioEffectPresets() {
    static const std::vector<PresetEntry> kList = {
        {0x00, "eq_standard", "standard"},
        {0x01, "eq_vocal", "vocal"},
        {0x02, "eq_bass", "bass"},
        {0x03, "eq_treble", "treble"},
        {0x05, "eq_audiobook", "audiobook"},
        {0x0B, "eq_master", "master"},
    };
    return kList;
}

QString audioEffectLabel(int code) {
    for (const auto& p : audioEffectPresets()) {
        if (p.code == code) return trKey(p.trKeyName);
    }
    return {};
}

const std::vector<PresetEntry>& noiseCyclePresets() {
    static const std::vector<PresetEntry> kList = {
        {0x0B, "cycle_all", "all"},
        {0x0A, "cycle_anc_trans", "anc_trans"},
        {0x08, "cycle_anc_off", "anc_off"},
        {0x09, "cycle_trans_off", "trans_off"},
    };
    return kList;
}

QString noiseCycleLabel(int code) {
    for (const auto& p : noiseCyclePresets()) {
        if (p.code == code) return trKey(p.trKeyName);
    }
    return {};
}

WireProfile selectProfile(const QString& deviceName) {
    QString norm;
    for (QChar ch : deviceName) {
        if (ch.isLetterOrNumber()) norm.append(ch.toLower());
    }

    auto parseEntry = [](const QJsonObject& o) -> WireProfile {
        return {
            o.value(QStringLiteral("label")).toString(QStringLiteral("Custom GAIA Profile")),
            static_cast<uint8_t>(o.value(QStringLiteral("gaia_version")).toInt(4)),
            QByteArray::fromHex(o.value(QStringLiteral("noise_query_hex")).toString().toLatin1()),
            QByteArray::fromHex(o.value(QStringLiteral("noise_suffix_hex")).toString().toLatin1()),
        };
    };

    QString cfgBase = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (cfgBase.isEmpty()) cfgBase = QDir::homePath() + QStringLiteral("/.config");
    const QStringList sources = {
        cfgBase + QStringLiteral("/kalm/models.json"),
        QStringLiteral(":/models.json"),
    };

    WireProfile fallback{QStringLiteral("vivo TWS 4/5 Family (GAIA v4)"), 4, QByteArray("\x00", 1), QByteArray("\x03\x01", 2)};
    for (const QString& src : sources) {
        QFile f(src);
        if (!f.exists() || f.size() > 1024 * 1024 || !f.open(QIODevice::ReadOnly)) continue;
        const auto doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) continue;
        const QJsonObject root = doc.object();
        if (root.contains(QStringLiteral("default")) && root.value(QStringLiteral("default")).isObject()) {
            fallback = parseEntry(root.value(QStringLiteral("default")).toObject());
        }
        for (const QJsonValue& v : root.value(QStringLiteral("models")).toArray()) {
            if (!v.isObject()) continue;
            const QJsonObject m = v.toObject();
            QString pat;
            for (QChar ch : m.value(QStringLiteral("match")).toString()) {
                if (ch.isLetterOrNumber()) pat.append(ch.toLower());
            }
            if (!pat.isEmpty() && norm.contains(pat)) {
                return parseEntry(m);
            }
        }
    }
    return fallback;
}

QString MultiConnectDevice::macStr() const {
    QStringList parts;
    for (char b : mac) {
        parts.append(QStringLiteral("%1").arg(static_cast<uint8_t>(b), 2, 16, QLatin1Char('0')).toUpper());
    }
    return parts.join(u':');
}

QString MultiConnectDevice::statusLabel() const {
    if (status == 2) return trKey("mp_connected_active");
    if (status == 1) return trKey("mp_connected");
    return trKey("mp_disconnected");
}

QJsonObject EarbudState::toJson() const {
    auto optInt = [](const std::optional<int>& v) -> QJsonValue {
        return v.has_value() ? QJsonValue(*v) : QJsonValue(QJsonValue::Null);
    };
    auto optBool = [](const std::optional<bool>& v) -> QJsonValue {
        return v.has_value() ? QJsonValue(*v) : QJsonValue(QJsonValue::Null);
    };

    QJsonObject batt{
        {QStringLiteral("left"), optInt(leftBattery.level)},
        {QStringLiteral("left_charging"), leftBattery.charging},
        {QStringLiteral("left_stale"), leftBattery.stale},
        {QStringLiteral("right"), optInt(rightBattery.level)},
        {QStringLiteral("right_charging"), rightBattery.charging},
        {QStringLiteral("right_stale"), rightBattery.stale},
        {QStringLiteral("case"), optInt(caseBattery.level)},
        {QStringLiteral("case_charging"), caseBattery.charging},
        {QStringLiteral("case_stale"), caseBattery.stale},
    };

    QJsonObject wear{
        {QStringLiteral("left"), leftWear.isEmpty() ? QJsonValue() : QJsonValue(leftWear)},
        {QStringLiteral("right"), rightWear.isEmpty() ? QJsonValue() : QJsonValue(rightWear)},
        {QStringLiteral("auto_pause"), optBool(wearDetection)},
    };

    QJsonObject nc{
        {QStringLiteral("left"), optInt(noiseCycleLeft)},
        {QStringLiteral("right"), optInt(noiseCycleRight)},
        {QStringLiteral("label"), noiseCycleLeft ? QJsonValue(noiseCycleLabel(*noiseCycleLeft)) : QJsonValue()},
    };

    QJsonArray devs;
    for (const auto& d : pairedDevices) {
        devs.append(QJsonObject{
            {QStringLiteral("mac"), d.macStr()},
            {QStringLiteral("name"), d.name},
            {QStringLiteral("status"), static_cast<int>(d.status)},
            {QStringLiteral("status_label"), d.statusLabel()},
            {QStringLiteral("connected"), d.connected()},
            {QStringLiteral("active"), d.active()},
        });
    }

    QString fullModeLabel;
    if (noiseMode) {
        fullModeLabel = noiseModeLabel(*noiseMode);
        if (protocol == DeviceProtocol::VivoGaia || protocol == DeviceProtocol::HuaweiSpp) {
            if (*noiseMode == NoiseMode::Anc && noiseEffect) {
                const QString sub = ancLevelLabel(*noiseEffect);
                if (!sub.isEmpty()) fullModeLabel += QStringLiteral(" (%1)").arg(sub);
            } else if (*noiseMode == NoiseMode::Transparency && transparencyEffect) {
                const QString sub = transparencyLabel(*transparencyEffect);
                if (!sub.isEmpty()) fullModeLabel += QStringLiteral(" (%1)").arg(sub);
            }
        }
    }

    QString protoStr = QStringLiteral("vivo_gaia");
    if (protocol == DeviceProtocol::RealmeOppoTl) protoStr = QStringLiteral("realme_tl");
    else if (protocol == DeviceProtocol::HuaweiSpp) protoStr = QStringLiteral("huawei_spp");

    return QJsonObject{
        {QStringLiteral("connected"), connected},
        {QStringLiteral("protocol"), protoStr},
        {QStringLiteral("device_name"), deviceName},
        {QStringLiteral("device_mac"), deviceMac},
        {QStringLiteral("firmware"), firmware.isEmpty() ? QJsonValue() : QJsonValue(firmware)},
        {QStringLiteral("noise_mode"), noiseMode ? QJsonValue(noiseModeSlug(*noiseMode)) : QJsonValue()},
        {QStringLiteral("noise_mode_label"), fullModeLabel.isEmpty() ? QJsonValue() : QJsonValue(fullModeLabel)},
        {QStringLiteral("anc_level"), optInt(noiseEffect)},
        {QStringLiteral("anc_level_slug"), noiseEffect ? QJsonValue(ancLevelSlug(*noiseEffect)) : QJsonValue()},
        {QStringLiteral("anc_level_label"), noiseEffect ? QJsonValue(ancLevelLabel(*noiseEffect)) : QJsonValue()},
        {QStringLiteral("transparency_effect"), optInt(transparencyEffect)},
        {QStringLiteral("transparency_effect_slug"), transparencyEffect ? QJsonValue(transparencySlug(*transparencyEffect)) : QJsonValue()},
        {QStringLiteral("transparency_effect_label"), transparencyEffect ? QJsonValue(transparencyLabel(*transparencyEffect)) : QJsonValue()},
        {QStringLiteral("battery"), batt},
        {QStringLiteral("wear"), wear},
        {QStringLiteral("audio_effect"), optInt(audioEffect)},
        {QStringLiteral("audio_effect_label"),
         audioEffect ? QJsonValue(protocol == DeviceProtocol::HuaweiSpp
                                      ? huawei::eqPresetLabel(*audioEffect)
                                      : audioEffectLabel(*audioEffect))
                     : QJsonValue()},
        {QStringLiteral("low_latency"), optBool(lowLatency)},
        {QStringLiteral("noise_cycle"), nc},
        {QStringLiteral("paired_devices"), devs},
    };
}

QByteArray encodeFrame(uint8_t version, uint16_t vendor, uint16_t command,
                       const QByteArray& payload, uint8_t flags) {
    const bool extended = (flags & kFlagLengthExtension) || (payload.size() > 254);
    if (extended) flags |= kFlagLengthExtension;
    const int headerSize = extended ? 5 : 4;
    QByteArray out(headerSize + kCommandBytes + payload.size(), Qt::Uninitialized);
    out[0] = static_cast<char>(kPreamble);
    out[1] = static_cast<char>(version);
    out[2] = static_cast<char>(flags);
    if (extended) {
        out[3] = static_cast<char>((payload.size() >> 8) & 0xFF);
        out[4] = static_cast<char>(payload.size() & 0xFF);
    } else {
        out[3] = static_cast<char>(payload.size() & 0xFF);
    }
    out[headerSize] = static_cast<char>((vendor >> 8) & 0xFF);
    out[headerSize + 1] = static_cast<char>(vendor & 0xFF);
    out[headerSize + 2] = static_cast<char>((command >> 8) & 0xFF);
    out[headerSize + 3] = static_cast<char>(command & 0xFF);
    if (!payload.isEmpty()) {
        memcpy(out.data() + headerSize + kCommandBytes, payload.constData(), static_cast<size_t>(payload.size()));
    }
    if (flags & kFlagChecksum) {
        out.append(static_cast<char>(xorChecksum(out, out.size())));
    }
    return out;
}

QList<Frame> FrameDecoder::feed(const QByteArray& chunk) {
    if (!chunk.isEmpty()) m_buf.append(chunk);
    if (m_buf.size() > 131072) {
        m_buf.remove(0, m_buf.size() - kMaxFrameBytes);
    }
    QList<Frame> frames;
    while (true) {
        const qsizetype idx = m_buf.indexOf(static_cast<char>(kPreamble));
        if (idx < 0) {
            m_buf.clear();
            break;
        }
        if (idx > 0) m_buf.remove(0, idx);
        if (m_buf.size() < 4) break;

        const uint8_t ver = static_cast<uint8_t>(m_buf[1]);
        const uint8_t flags = static_cast<uint8_t>(m_buf[2]);
        if ((ver != 3 && ver != 4) || (flags & ~(kFlagChecksum | kFlagLengthExtension)) != 0) {
            m_buf.remove(0, 1);
            continue;
        }

        const bool extended = (flags & kFlagLengthExtension) != 0;
        const int headerSize = extended ? 5 : 4;
        if (m_buf.size() < headerSize + kCommandBytes) break;

        const uint16_t vendor = (static_cast<uint16_t>(static_cast<uint8_t>(m_buf[headerSize])) << 8) |
                                static_cast<uint8_t>(m_buf[headerSize + 1]);
        if (vendor != kGaiaVendor && vendor != kVivoVendor) {
            m_buf.remove(0, 1);
            continue;
        }

        const int payloadLen = extended
                                   ? ((static_cast<int>(static_cast<uint8_t>(m_buf[3])) << 8) |
                                      static_cast<uint8_t>(m_buf[4]))
                                   : static_cast<int>(static_cast<uint8_t>(m_buf[3]));
        const int checksumLen = (flags & kFlagChecksum) ? 1 : 0;
        const int totalLen = headerSize + kCommandBytes + payloadLen + checksumLen;
        if (totalLen > kMaxFrameBytes) {
            m_buf.remove(0, 1);
            continue;
        }
        if (m_buf.size() < totalLen) break;

        const QByteArray raw = m_buf.left(totalLen);
        m_buf.remove(0, totalLen);

        if (checksumLen && xorChecksum(raw, raw.size() - 1) != static_cast<uint8_t>(raw.back())) {
            continue;
        }

        const uint16_t command = (static_cast<uint16_t>(static_cast<uint8_t>(raw[headerSize + 2])) << 8) |
                                 static_cast<uint8_t>(raw[headerSize + 3]);
        Frame f;
        f.version = ver;
        f.flags = flags;
        f.vendor = vendor;
        f.command = command;
        f.payload = raw.mid(headerSize + kCommandBytes, payloadLen);
        f.raw = raw;
        frames.append(std::move(f));
    }
    return frames;
}

QByteArray buildHandshake() { return encodeFrame(4, kGaiaVendor, kCmdHandshake); }

QList<QByteArray> buildRegisterNotifications() {
    QList<QByteArray> list;
    for (uint16_t cmd : kRegisterNotifications) {
        list.append(encodeFrame(4, kVivoVendor, cmd));
    }
    return list;
}

QByteArray buildQueryBattery() { return encodeFrame(4, kVivoVendor, kCmdQueryBattery); }

QByteArray buildQueryNoiseMode(const WireProfile& profile) {
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdQueryNoiseMode, profile.noiseQueryPayload);
}

QByteArray buildSetNoiseMode(NoiseMode mode, const WireProfile& profile,
                             std::optional<int> ancLevel,
                             std::optional<int> transEffect) {
    QByteArray p;
    p.append(static_cast<char>(static_cast<uint8_t>(mode)));
    QByteArray suffix = profile.noiseSetSuffix;
    if (suffix.size() >= 2) {
        if (ancLevel.has_value()) suffix[0] = static_cast<char>(*ancLevel & 0xFF);
        if (transEffect.has_value()) suffix[1] = static_cast<char>(*transEffect & 0xFF);
    } else if (suffix.size() == 1 && ancLevel.has_value()) {
        suffix[0] = static_cast<char>(*ancLevel & 0xFF);
    }
    p.append(suffix);
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdSetNoiseMode, p);
}

QByteArray buildQueryWearState(const WireProfile& profile) {
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdQueryWearState);
}

QByteArray buildQueryWearDetection(const WireProfile& profile) {
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdQueryWearDetection);
}

QByteArray buildSetWearDetection(bool enabled, const WireProfile& profile) {
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdSetWearDetection, QByteArray(1, enabled ? '\x01' : '\x00'));
}

QByteArray buildQueryAudioEffect(const WireProfile& profile) {
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdQueryAudioEffect);
}

QByteArray buildSetAudioEffect(int effectId, const WireProfile& profile) {
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdSetAudioEffect, QByteArray(1, static_cast<char>(effectId & 0xFF)));
}

QByteArray buildQueryLowLatency(const WireProfile& profile) {
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdQueryLowLatency);
}

QByteArray buildSetLowLatency(bool enabled, const WireProfile& profile) {
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdSetLowLatency, QByteArray(1, enabled ? '\x01' : '\x00'));
}

QByteArray buildQueryNoiseCycle(const WireProfile& profile) {
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdQueryNoiseCycle, QByteArray(1, static_cast<char>(kNoiseCycleSubcmd)));
}

QByteArray buildSetNoiseCycle(int leftCode, int rightCode, const WireProfile& profile) {
    QByteArray p(3, '\x00');
    p[0] = static_cast<char>(kNoiseCycleSubcmd);
    p[1] = static_cast<char>(leftCode & 0xFF);
    p[2] = static_cast<char>(rightCode & 0xFF);
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdSetNoiseCycle, p);
}

QByteArray buildQueryMultiConnect(const WireProfile& profile) {
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdQueryMultiConnect);
}

QByteArray buildSetMultiConnect(const QList<MultiConnectDevice>& devices,
                                const QString& targetMac, bool connect,
                                const WireProfile& profile) {
    const QString normTarget = targetMac.trimmed().toUpper();
    QByteArray payload;
    for (const auto& d : devices) {
        payload.append(d.mac.left(6));
        if (d.macStr().toUpper() == normTarget) {
            payload.append(static_cast<char>(connect ? 0x02 : 0x00));
        } else {
            payload.append(static_cast<char>(d.status));
        }
    }
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdSetMultiConnect, payload);
}

QByteArray buildFindDevice(bool ring, const WireProfile& profile) {
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdSetFindDevice, QByteArray(1, ring ? '\x01' : '\x00'));
}

QByteArray buildHostTimeResponse(const WireProfile& profile, const QDateTime& dt) {
    const QDate d = dt.date();
    const QTime t = dt.time();
    QByteArray p(7, '\x00');
    p[0] = static_cast<char>((d.year() / 100) & 0xFF);
    p[1] = static_cast<char>((d.year() % 100) & 0xFF);
    p[2] = static_cast<char>(d.month() & 0xFF);
    p[3] = static_cast<char>(d.day() & 0xFF);
    p[4] = static_cast<char>(t.hour() & 0xFF);
    p[5] = static_cast<char>(t.minute() & 0xFF);
    p[6] = static_cast<char>(t.second() & 0xFF);
    return encodeFrame(profile.gaiaVersion, kVivoVendor, kCmdHostTimeResponse, p);
}

QString applyFrameToState(const Frame& frame, EarbudState& state) {
    const QByteArray& p = frame.payload;
    if (frame.vendor == kGaiaVendor && frame.command == kCmdHandshakeResp) {
        return QStringLiteral("handshake");
    }
    if (frame.vendor != kVivoVendor) return QStringLiteral("unknown");

    const uint16_t cmd = frame.command;
    if (cmd == kCmdAckNoiseMode || cmd == kCmdReportNoiseMode) {
        if (p.size() >= 2 && static_cast<uint8_t>(p[0]) == 0) {
            const uint8_t m = static_cast<uint8_t>(p[1]);
            if (m <= 2) {
                state.noiseMode = static_cast<NoiseMode>(m);
                if (p.size() > 2) state.noiseEffect = static_cast<int>(static_cast<uint8_t>(p[2]));
                if (p.size() > 3) state.transparencyEffect = static_cast<int>(static_cast<uint8_t>(p[3]));
                return QStringLiteral("noise_mode");
            }
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kCmdReportBattery) {
        if (p.size() >= 5 && static_cast<uint8_t>(p[0]) == 0) {
            const uint8_t chg = static_cast<uint8_t>(p[4]);
            updateBatterySlot(state.leftBattery, static_cast<uint8_t>(p[1]), (chg & 0x01) != 0);
            updateBatterySlot(state.rightBattery, static_cast<uint8_t>(p[2]), (chg & 0x02) != 0);
            updateBatterySlot(state.caseBattery, static_cast<uint8_t>(p[3]), (chg & 0x04) != 0);
            return QStringLiteral("battery");
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kCmdReportWearState) {
        if (p.size() >= 2 && static_cast<uint8_t>(p[0]) == 0) {
            const uint8_t flags = static_cast<uint8_t>(p[1]);
            state.leftWear = decodeEarWear(flags, kWearLeftInCase, kWearLeftWorn);
            state.rightWear = decodeEarWear(flags, kWearRightInCase, kWearRightWorn);
            return QStringLiteral("wear_state");
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kCmdAckWearDetection || cmd == kCmdReportWearDetection) {
        if (p.size() >= 2 && static_cast<uint8_t>(p[0]) == 0) {
            state.wearDetection = (static_cast<uint8_t>(p[1]) != 0);
            return QStringLiteral("wear_detection");
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kCmdAckAudioEffect || cmd == kCmdReportAudioEffect) {
        if (p.size() >= 2 && static_cast<uint8_t>(p[0]) == 0) {
            state.audioEffect = static_cast<int>(static_cast<uint8_t>(p[1]));
            return QStringLiteral("audio_effect");
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kCmdAckLowLatency || cmd == kCmdReportLowLatency) {
        if (p.size() >= 2 && static_cast<uint8_t>(p[0]) == 0 &&
            (static_cast<uint8_t>(p[1]) == 0 || static_cast<uint8_t>(p[1]) == 1)) {
            state.lowLatency = (static_cast<uint8_t>(p[1]) == 1);
            return QStringLiteral("low_latency");
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kCmdAckNoiseCycle || cmd == kCmdReportNoiseCycle) {
        if (p.size() >= 4 && static_cast<uint8_t>(p[0]) == 0 &&
            static_cast<uint8_t>(p[1]) == kNoiseCycleSubcmd) {
            state.noiseCycleLeft = static_cast<int>(static_cast<uint8_t>(p[2]));
            state.noiseCycleRight = static_cast<int>(static_cast<uint8_t>(p[3]));
            return QStringLiteral("noise_cycle");
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kCmdReportMultiConnect) {
        if (auto parsed = parseMultiConnectDevices(p)) {
            state.pairedDevices = std::move(*parsed);
            return QStringLiteral("multi_connect");
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kCmdReportTelemetry) {
        if (auto obj = parseTelemetryPayload(p)) {
            const QString ver = obj->value(QStringLiteral("V")).toString();
            if (ver.contains(u'.')) {
                const QString clean = ver.section(u'_', 0, 0);
                if (clean != u"6.55.35") state.firmware = clean;
            }
            const QString batt = obj->value(QStringLiteral("C")).toString();
            if (batt.contains(u'_')) {
                bool okL = false, okR = false;
                const int lb = batt.section(u'_', 0, 0).toInt(&okL);
                const int rb = batt.section(u'_', 1, 1).toInt(&okR);
                if (okL && !state.leftBattery.level && lb > 0 && lb <= 100) {
                    state.leftBattery.level = lb;
                    state.leftBattery.stale = false;
                }
                if (okR && !state.rightBattery.level && rb > 0 && rb <= 100) {
                    state.rightBattery.level = rb;
                    state.rightBattery.stale = false;
                }
            }
            return QStringLiteral("telemetry");
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kCmdPeerTimeRequest) return QStringLiteral("time_request");
    if (cmd == kCmdAckFindDevice) return QStringLiteral("find_device");
    return QStringLiteral("other");
}

// ---- OPPO / OnePlus / realme Link "TL" (OPPOv1Wrapper 0xAA) Implementation ----

namespace {

std::optional<NoiseMode> decodeRealmeNoiseValue(uint8_t val) {
    if (val == 0x01) return NoiseMode::Off;
    if (val == 0x02) return NoiseMode::Transparency;
    if (val == 0x08 || val == 0x04 || val == 0x10 || val == 0x20) return NoiseMode::Anc;
    return std::nullopt;
}

bool parseRealmeBatteryTable(const QByteArray& p, int countOffset, EarbudState& state) {
    if (countOffset >= p.size()) return false;
    const int count = static_cast<uint8_t>(p[countOffset]);
    bool seenL = false, seenR = false, seenC = false;
    for (int i = 0; i < count; ++i) {
        const int off = countOffset + 1 + i * 2;
        if (off + 2 > p.size()) break;
        const uint8_t compId = static_cast<uint8_t>(p[off]);
        const uint8_t raw = static_cast<uint8_t>(p[off + 1]);
        const int lvl = raw & 0x7F;
        const bool chg = (raw & 0x80) != 0;

        if (compId == 1 && lvl <= 100) {
            state.leftBattery = BatteryReading{lvl, chg, false};
            seenL = true;
        } else if (compId == 2 && lvl <= 100) {
            state.rightBattery = BatteryReading{lvl, chg, false};
            seenR = true;
        } else if (compId == 3) {
            // Case inactive normalization: 03 00 (level=0, !charging) means case closed/unavailable
            if (lvl > 0 && lvl <= 100) {
                state.caseBattery = BatteryReading{lvl, chg, false};
                seenC = true;
            } else if (lvl == 0 && chg) {
                state.caseBattery = BatteryReading{0, true, false};
                seenC = true;
            }
        }
    }
    if (!seenL) state.leftBattery = BatteryReading{std::nullopt, false, true};
    if (!seenR) state.rightBattery = BatteryReading{std::nullopt, false, true};
    if (!seenC) state.caseBattery = BatteryReading{std::nullopt, false, true};
    return true;
}

QString parseRealmeEventBody(int offset, const QByteArray& p, EarbudState& state) {
    if (offset >= p.size()) return QStringLiteral("ignored");
    const uint8_t ev = static_cast<uint8_t>(p[offset]);
    const int dataOff = offset + 1;
    const int rem = static_cast<int>(p.size()) - dataOff;

    if (ev == 1 && rem >= 1) {
        return parseRealmeBatteryTable(p, dataOff, state) ? QStringLiteral("battery") : QStringLiteral("ignored");
    }

    if (ev == 3 && rem >= 2) {
        // NotificationCommandManager.k(case 3):
        // rem == 2 -> [type, noiseValue] (action implicitly 1)
        // rem >= 3 -> [action, type, noiseValue] (action 1 = active ANC mode; action 2 = cycle config)
        uint8_t action = 1;
        uint8_t noiseVal = 0;
        if (rem == 2) {
            noiseVal = static_cast<uint8_t>(p[dataOff + 1]);
        } else {
            action = static_cast<uint8_t>(p[dataOff]);
            noiseVal = static_cast<uint8_t>(p[dataOff + 2]);
        }
        if (action == 1) {
            if (auto m = decodeRealmeNoiseValue(noiseVal)) {
                state.noiseMode = *m;
                return QStringLiteral("noise_mode");
            }
        }
        return QStringLiteral("ignored");
    }

    return QStringLiteral("event_refresh");
}

}  // namespace

QByteArray buildRealmeOppoPacket(uint16_t cmd, uint8_t seq, const QByteArray& payload) {
    QByteArray inner(5 + payload.size(), Qt::Uninitialized);
    inner[0] = static_cast<char>(cmd & 0xFF);
    inner[1] = static_cast<char>((cmd >> 8) & 0xFF);
    inner[2] = static_cast<char>(seq);
    inner[3] = static_cast<char>(payload.size() & 0xFF);
    inner[4] = static_cast<char>((payload.size() >> 8) & 0xFF);
    if (!payload.isEmpty()) {
        std::memcpy(inner.data() + 5, payload.constData(), static_cast<size_t>(payload.size()));
    }

    const int linkLen = static_cast<int>(inner.size()) + 2;
    QByteArray out;
    out.reserve(4 + inner.size());
    out.append(static_cast<char>(0xAA));
    if (linkLen <= 127) {
        out.append(static_cast<char>(linkLen & 0x7F));
    } else {
        out.append(static_cast<char>((linkLen & 0x7F) | 0x80));
        out.append(static_cast<char>((linkLen >> 7) & 0x7F));
    }
    out.append('\x00');
    out.append('\x00');
    out.append(inner);
    return out;
}

QList<RealmeOppoFrame> RealmeOppoDecoder::feed(const QByteArray& chunk) {
    if (!chunk.isEmpty()) m_buf.append(chunk);
    if (m_buf.size() > 131072) {
        m_buf.remove(0, m_buf.size() - kMaxFrameBytes);
    }
    QList<RealmeOppoFrame> frames;
    while (true) {
        const qsizetype idx = m_buf.indexOf(static_cast<char>(0xAA));
        if (idx < 0) {
            m_buf.clear();
            break;
        }
        if (idx > 0) m_buf.remove(0, idx);
        if (m_buf.size() < 4) break;

        int linkLen = 0;
        int lenBytes = 0;
        if ((static_cast<uint8_t>(m_buf[1]) & 0x80) == 0) {
            linkLen = static_cast<uint8_t>(m_buf[1]) & 0x7F;
            lenBytes = 1;
        } else {
            if (m_buf.size() < 5) break;
            linkLen = (static_cast<uint8_t>(m_buf[1]) & 0x7F) |
                      ((static_cast<int>(static_cast<uint8_t>(m_buf[2])) & 0x7F) << 7);
            lenBytes = 2;
        }

        const qsizetype totalLen = 1 + lenBytes + linkLen;
        if (totalLen < 9 || totalLen > kMaxFrameBytes) {
            m_buf.remove(0, 1);
            continue;
        }
        if (m_buf.size() < totalLen) break;

        const QByteArray raw = m_buf.left(totalLen);
        m_buf.remove(0, totalLen);

        const uint8_t fsnFlags = static_cast<uint8_t>(raw[1 + lenBytes]) & 0x03;
        const qsizetype innerOffset = 1 + lenBytes + ((fsnFlags == 0) ? 2 : 3);
        const qsizetype innerLen = totalLen - innerOffset;
        if (innerLen < 5) continue;

        const uint16_t cmd = static_cast<uint8_t>(raw[innerOffset]) |
                             (static_cast<uint16_t>(static_cast<uint8_t>(raw[innerOffset + 1])) << 8);
        const uint8_t seq = static_cast<uint8_t>(raw[innerOffset + 2]);
        const uint16_t dlen = static_cast<uint8_t>(raw[innerOffset + 3]) |
                              (static_cast<uint16_t>(static_cast<uint8_t>(raw[innerOffset + 4])) << 8);
        const qsizetype avail = std::min<qsizetype>(dlen, innerLen - 5);

        RealmeOppoFrame f;
        f.command = cmd;
        f.seq = seq;
        f.payload = raw.mid(innerOffset + 5, avail);
        f.raw = raw;
        frames.append(std::move(f));
    }
    return frames;
}

QList<RealmeOppoFrame> parseRealmeOppoFrames(const QByteArray& data) {
    RealmeOppoDecoder dec;
    return dec.feed(data);
}

QByteArray buildRealmeSetNoise(NoiseMode mode, uint8_t seq) {
    uint8_t noiseStatus = 0x01;
    if (mode == NoiseMode::Anc) noiseStatus = 0x08;
    else if (mode == NoiseMode::Transparency) noiseStatus = 0x02;
    QByteArray payload(3, '\x01');
    payload[0] = '\x01';
    payload[1] = '\x01';
    payload[2] = static_cast<char>(noiseStatus);
    return buildRealmeOppoPacket(kTlCmdSetNoise, seq, payload);
}

QString applyRealmeFrameToState(const RealmeOppoFrame& frame, EarbudState& state) {
    const QByteArray& p = frame.payload;
    const uint16_t cmd = frame.command;

    if (cmd == kTlRespGetCapabilities) {
        return QStringLiteral("handshake");
    }

    if (cmd == kTlRespGetFirmware) {
        // Payload: [status=0x00, count, "1,2,1.1.0.054,2,2,1.1.0.054,3,2,022"]
        if (p.size() > 2 && static_cast<uint8_t>(p[0]) == 0) {
            const QString csv = QString::fromUtf8(p.mid(2));
            const QStringList parts = csv.split(u',');
            for (const QString& tok : parts) {
                if (tok.contains(u'.') && tok.size() >= 3) {
                    state.firmware = tok.trimmed();
                    return QStringLiteral("firmware");
                }
            }
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kTlRespGetBattery) {
        if (p.size() >= 2 && static_cast<uint8_t>(p[0]) == 0) {
            return parseRealmeBatteryTable(p, 1, state) ? QStringLiteral("battery") : QStringLiteral("ignored");
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kTlRespGetNoise) {
        // Valid NoiseReductionInfo requires >= 4 bytes: [status=0x00, action=0x01, type, noiseValue].
        // A 2-byte [00 02] reply is an unsupported-command stub and must NEVER overwrite noiseMode!
        if (p.size() >= 4 && static_cast<uint8_t>(p[0]) == 0 && static_cast<uint8_t>(p[1]) == 1) {
            if (auto m = decodeRealmeNoiseValue(static_cast<uint8_t>(p[3]))) {
                state.noiseMode = *m;
                return QStringLiteral("noise_mode");
            }
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kTlRespQueryNotify) {
        // 0x8202: response to 0x0202 [eventId] -> [status=0x00, eventId, ...body]
        if (p.size() >= 2 && static_cast<uint8_t>(p[0]) == 0) {
            return parseRealmeEventBody(1, p, state);
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kTlCmdPushNotify) {
        // 0x0204: live push notification from earbuds -> [eventId, ...body]
        return parseRealmeEventBody(0, p, state);
    }

    if (cmd == kTlRespGetNotifyCaps || cmd == kTlRespRegNotify) {
        return QStringLiteral("ignored");
    }

    if (cmd == kTlRespSetNoise) {
        if (!p.isEmpty() && static_cast<uint8_t>(p[0]) == 0) {
            return QStringLiteral("noise_ack");
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kTlRespGetSwitches) {
        // Payload: [00, count, (featId, status) * count]; featId 7 = Game Mode
        if (p.size() >= 2 && static_cast<uint8_t>(p[0]) == 0) {
            const int count = static_cast<uint8_t>(p[1]);
            for (int i = 0; i < count; ++i) {
                const int off = 2 + i * 2;
                if (off + 2 > p.size()) break;
                if (static_cast<uint8_t>(p[off]) == 7) {
                    state.lowLatency = (static_cast<uint8_t>(p[off + 1]) != 0);
                    return QStringLiteral("low_latency");
                }
            }
        }
        return QStringLiteral("ignored");
    }

    if (cmd == kTlCmdNotification || cmd == kTlRespNotification) {
        // Push event: event 0x03 = ANC changed on earbud stem; also trigger snapshot refresh
        if (p.size() >= 2 && static_cast<uint8_t>(p[0]) == 0x03) {
            if (auto m = decodeRealmeNoiseValue(static_cast<uint8_t>(p.back()))) {
                state.noiseMode = *m;
            }
        }
        return QStringLiteral("event_refresh");
    }

    return QStringLiteral("event_refresh");
}

}  // namespace kalm
