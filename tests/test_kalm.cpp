// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.hpp"
#include "huawei.hpp"
#include "probe.hpp"
#include "protocol.hpp"
#include "registry.hpp"
#include "session.hpp"
#include "transport.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

#ifdef __GLIBC__
#include <malloc.h>
#endif
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define KALM_CHECK(cond)                                                                      \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED at " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
            std::abort();                                                                     \
        }                                                                                     \
    } while (0)

int main() {
    using namespace kalm;

    // 1. Protocol & ANC degrees encoding + models.json + realme Buds T300 (0xAA) codec
    KALM_CHECK(maskMacForReport(QStringLiteral("1c:ba:b8:0f:5a:0f")) == u"1C:BA:B8:XX:XX:XX");
    KALM_CHECK(buildRealmeOppoPacket(0x0106, 0, {}).toHex() == "aa0700000601000000");
    KALM_CHECK(buildRealmeSetNoise(NoiseMode::Anc, 0).toHex() == "aa0a00000404000300010108");
    KALM_CHECK(buildRealmeSetNoise(NoiseMode::Off, 0).toHex() == "aa0a00000404000300010101");
    KALM_CHECK(buildRealmeSetNoise(NoiseMode::Transparency, 0).toHex() == "aa0a00000404000300010102");

    // Verify real captured realme Buds T300 frames from Issue #1 + AGENTS.md + NotificationCommandManager
    {
        RealmeOppoDecoder rdec;
        const QByteArray t300Stream =
            buildRealmeOppoPacket(0x8105, 7, QByteArray::fromHex("0003312c322c312e312e302e3035342c322c322c312e312e302e3035342c332c322c303232")) +
            buildRealmeOppoPacket(0x8106, 8, QByteArray::fromHex("000301d5025a0300")) +  // L=85% charging(0x80|85=0xD5), R=90%, Case=0%(inactive->stale)
            buildRealmeOppoPacket(0x8202, 9, QByteArray::fromHex("00030101")) +          // QueryNotify(ev=3): Off (0x01)
            buildRealmeOppoPacket(0x810B, 10, QByteArray::fromHex("0002"));              // 2-byte unsupported stub [00 02] MUST NOT overwrite Off -> Transparency!

        QList<RealmeOppoFrame> rframes;
        for (qsizetype i = 0; i < t300Stream.size(); i += 11) {
            rframes.append(rdec.feed(t300Stream.mid(i, 11)));
        }
        KALM_CHECK(rframes.size() == 4);

        EarbudState rst;
        rst.connected = true;
        rst.protocol = DeviceProtocol::RealmeOppoTl;
        for (const auto& rf : rframes) applyRealmeFrameToState(rf, rst);

        KALM_CHECK(rst.firmware == u"1.1.0.054");
        KALM_CHECK(rst.leftBattery.level == 85 && rst.leftBattery.charging && !rst.leftBattery.stale);
        KALM_CHECK(rst.rightBattery.level == 90 && !rst.rightBattery.charging && !rst.rightBattery.stale);
        KALM_CHECK(!rst.caseBattery.level.has_value() && rst.caseBattery.stale);
        KALM_CHECK(rst.noiseMode == NoiseMode::Off);

        // Live stem-squeeze push notification (0x0204, ev=3, ANC=0x08)
        const auto pushFrames = rdec.feed(buildRealmeOppoPacket(0x0204, 1, QByteArray::fromHex("03010108")));
        KALM_CHECK(pushFrames.size() == 1);
        applyRealmeFrameToState(pushFrames[0], rst);
        KALM_CHECK(rst.noiseMode == NoiseMode::Anc);
    }

    const WireProfile air3 = selectProfile(QStringLiteral("vivo TWS Air3 Pro"));
    KALM_CHECK(air3.gaiaVersion == 3);
    KALM_CHECK(air3.noiseSetSuffix.toHex() == "0400");

    const WireProfile v4 = selectProfile(QStringLiteral("vivo TWS 5 Hi-Fi"));
    KALM_CHECK(buildHandshake().toHex() == "ff040000000a0300");
    KALM_CHECK(buildSetNoiseMode(NoiseMode::Anc, v4).toHex() == "ff040003001b0130000301");
    KALM_CHECK(buildSetNoiseMode(NoiseMode::Off, v4).toHex() == "ff040003001b0130010301");
    KALM_CHECK(buildSetNoiseMode(NoiseMode::Transparency, v4).toHex() == "ff040003001b0130020301");

    // Explicit ANC degrees & Transparency effects extracted from vivo APK
    KALM_CHECK(buildSetNoiseMode(NoiseMode::Anc, v4, 1, 1).toHex() == "ff040003001b0130000101");
    KALM_CHECK(buildSetNoiseMode(NoiseMode::Anc, v4, 0, 1).toHex() == "ff040003001b0130000001");
    KALM_CHECK(buildSetNoiseMode(NoiseMode::Anc, v4, 2, 1).toHex() == "ff040003001b0130000201");
    KALM_CHECK(buildSetNoiseMode(NoiseMode::Anc, v4, 5, 2).toHex() == "ff040003001b0130000502");
    KALM_CHECK(buildSetNoiseMode(NoiseMode::Transparency, v4, 2, 2).toHex() == "ff040003001b0130020202");

    KALM_CHECK(parseAncLevelStr(QStringLiteral("mild")) == 1);
    KALM_CHECK(parseAncLevelStr(QStringLiteral("deep")) == 2);
    KALM_CHECK(parseAncLevelStr(QStringLiteral("adaptive")) == 3);
    KALM_CHECK(parseAncLevelStr(QStringLiteral("metro")) == 6);
    KALM_CHECK(parseTransparencyStr(QStringLiteral("voice")) == 2);

    // 2. Live stream decoding
    const QByteArray rawStream = QByteArray::fromHex(
        "ff030005001b820700395bff00"
        "ff030002001b820d000c"
        "ff04020112001b8224"
        "0001117b226576656e744964223a22413130327c3130303937222c22706172616d73223a7b"
        "2274797065223a2230222c22505f4c223a2234222c22505f52223a2233222c22425f48223a"
        "2231222c22425f50223a2231222c22415f4d223a2231222c22415f4c223a2230222c225253"
        "53495f31223a222d3532222c22525353495f32223a2230222c22525353495f50223a222d37"
        "32222c224d5f41326470223a2231222c224d5f53636f223a2230222c2250223a22345f3422"
        "2c2242223a22315f31222c2241223a2231222c2252535349223a222d35355f305f305f2d37"
        "30227d2c2256223a22322e31302e35355f322e31302e3535222c2243223a2235375f393122"
        "2c2244223a22383836333432227d00"
        "ff030004001b823000000202");

    FrameDecoder dec;
    QList<Frame> frames;
    for (qsizetype i = 0; i < rawStream.size(); i += 17) {
        frames.append(dec.feed(rawStream.mid(i, 17)));
    }
    KALM_CHECK(frames.size() == 4);

    EarbudState st;
    st.connected = true;
    for (const auto& f : frames) applyFrameToState(f, st);

    KALM_CHECK(st.leftBattery.level == 57);
    KALM_CHECK(st.rightBattery.level == 91);
    KALM_CHECK(st.firmware == u"2.10.55");
    KALM_CHECK(st.noiseMode == NoiseMode::Anc);
    KALM_CHECK(st.noiseEffect == 2);
    KALM_CHECK(st.transparencyEffect == 2);

    // 3. i18n tests (Default English + Russian)
    initI18n();
    KALM_CHECK(currentLanguage() == u"en");
    KALM_CHECK(ancLevelLabel(2) == u"Deep");
    KALM_CHECK(transparencyLabel(2) == u"Vocal enhancement");

    initI18n(QStringLiteral("ru"));
    KALM_CHECK(currentLanguage() == u"ru");
    KALM_CHECK(ancLevelLabel(2) == u"Сильное");
    KALM_CHECK(transparencyLabel(2) == u"Усиление голоса");

    // Path-traversal / malformed language codes must safely fall back to English
    initI18n(QStringLiteral("../../etc/passwd"));
    KALM_CHECK(currentLanguage() == u"en");
    initI18n(QStringLiteral("ru"));

    // 4. Registry tests
    QTemporaryDir tmp;
    KALM_CHECK(tmp.isValid());
    const QString cfgPath = tmp.path() + QStringLiteral("/devices.json");
    DeviceRegistry reg(cfgPath);
    KALM_CHECK(reg.language() == u"en");
    reg.setLanguage(QStringLiteral("ru"));
    KALM_CHECK(reg.language() == u"ru");

    const auto d1 = reg.add(QStringLiteral("1c:ba:b8:0f:5a:0f"), QStringLiteral("vivo TWS 5 Hi-Fi"), {}, true);
    const auto d2 = reg.add(QStringLiteral("AA-BB-CC-11-22-33"), QStringLiteral("vivo TWS 3e"), QStringLiteral("Спорт"), false);
    KALM_CHECK(d1.mac == u"1C:BA:B8:0F:5A:0F");
    KALM_CHECK(d2.mac == u"AA:BB:CC:11:22:33");
    KALM_CHECK(reg.activeMac() == u"1C:BA:B8:0F:5A:0F");

    // 4b. HUAWEI FreeBuds (SPP) codec, state mapping and capabilities
    {
        namespace hw = kalm::huawei;
        KALM_CHECK(hw::matchesName(QStringLiteral("HUAWEI FreeBuds 6i")));
        KALM_CHECK(hw::matchesName(QStringLiteral("FreeBuds Pro 3")));
        KALM_CHECK(!hw::matchesName(QStringLiteral("vivo TWS 5 Hi-Fi")));
        KALM_CHECK(!hw::matchesName(QStringLiteral("HUAWEI MateBook")));
        KALM_CHECK(hw::matchesName(QStringLiteral("HUAWEI FreeLace Pro 2")));

        KALM_CHECK(hw::crc16Xmodem(QByteArray::fromHex("5a0009000108010002000300")).toHex() == "fbb9");
        KALM_CHECK(hw::buildQueryBattery().toHex() == "5a0009000108010002000300fbb9");
        KALM_CHECK(hw::buildSetAnc(hw::kModeAnc).toHex() == "5a0007002b04010201ffffec");
        KALM_CHECK(hw::buildSetAnc(hw::kModeNormal).toHex() == "5a0007002b0401020000d22d");
        KALM_CHECK(hw::buildSetAnc(hw::kModeAnc, 2).toHex() == "5a0007002b0401020102c15e");
        KALM_CHECK(hw::buildSetEqualizerPreset(2).toHex() == "5a0006002b490101021f79");

        const auto profile = hw::profileFor(QStringLiteral("HUAWEI FreeBuds 6i"));
        KALM_CHECK(profile.sppChannel == 1);
        KALM_CHECK(hw::ancLevels(profile).size() == 4);
        KALM_CHECK(hw::eqPresets(profile).size() == 4);

        hw::Decoder decoder;
        EarbudState hwSt;
        hwSt.protocol = DeviceProtocol::HuaweiSpp;
        const auto batteryPackets =
            decoder.feed(QByteArray::fromHex("5a001000012701011902034b190103030000000678"));
        KALM_CHECK(batteryPackets.size() == 1);
        KALM_CHECK(batteryPackets.first().command.toHex() == "0127");
        KALM_CHECK(hw::applyPacket(batteryPackets.first(), hwSt, profile).stateChanged);
        KALM_CHECK(hwSt.leftBattery.level == 75);
        KALM_CHECK(hwSt.rightBattery.level == 25);
        KALM_CHECK(hwSt.caseBattery.level == 1);
        KALM_CHECK(!hwSt.leftBattery.stale);

        const auto ancPackets = decoder.feed(QByteArray::fromHex("5a0007002b2a010203015043"));
        KALM_CHECK(ancPackets.size() == 1);
        hw::applyPacket(ancPackets.first(), hwSt, profile);
        KALM_CHECK(hwSt.noiseMode == NoiseMode::Anc);
        KALM_CHECK(hwSt.noiseEffect == 3);

        const auto eqPackets = decoder.feed(QByteArray::fromHex("5a000f002b4a0101010201020304010203099f7b"));
        KALM_CHECK(eqPackets.size() == 1);
        KALM_CHECK(hw::eqCurrentPreset(eqPackets.first()) == 2);
        hw::applyPacket(eqPackets.first(), hwSt, profile);
        KALM_CHECK(hwSt.audioEffect == 2);
        KALM_CHECK(hwSt.toJson().value(QStringLiteral("protocol")).toString() == u"huawei_spp");

        const auto llPackets = decoder.feed(QByteArray::fromHex("5a0006002b6c020101fd41"));
        KALM_CHECK(llPackets.size() == 1);
        hw::applyPacket(llPackets.first(), hwSt, profile);
        KALM_CHECK(hwSt.lowLatency.value_or(false));

        const QByteArray dcRaw = hw::encode(
            QByteArray::fromHex("2b31"),
            {{2, QByteArray("\x01", 1)},
             {3, QByteArray("\x00", 1)},
             {4, QByteArray::fromHex("aabbcc112233")},
             {5, QByteArray("\x01", 1)},
             {9, QByteArray("Laptop")}});
        const auto dcPackets = decoder.feed(dcRaw);
        KALM_CHECK(dcPackets.size() == 1);
        const auto dcEffect = hw::applyPacket(dcPackets.first(), hwSt, profile);
        KALM_CHECK(dcEffect.dualConnectRow);
        KALM_CHECK(dcEffect.dcCount == 1);
        KALM_CHECK(dcEffect.dcDevice.macStr() == u"AA:BB:CC:11:22:33");
        KALM_CHECK(dcEffect.dcDevice.name == u"Laptop");
        KALM_CHECK(dcEffect.dcDevice.connected());

        // Wear notifications (2b25): param 1 = right earbud, param 2 = left.
        const auto wearIn = decoder.feed(
            QByteArray::fromHex("5a000f002b25010101020101030100040100e11b"));
        KALM_CHECK(wearIn.size() == 1);
        const auto wearEffect = hw::applyPacket(wearIn.first(), hwSt, profile);
        KALM_CHECK(wearEffect.wearUpdate);
        KALM_CHECK(hwSt.leftWear == u"worn");
        KALM_CHECK(hwSt.rightWear == u"worn");

        const auto leftOut = decoder.feed(
            QByteArray::fromHex("5a000f002b25010101020100030100040100597a"));
        KALM_CHECK(leftOut.size() == 1);
        hw::applyPacket(leftOut.first(), hwSt, profile);
        KALM_CHECK(hwSt.leftWear == u"in_case");
        KALM_CHECK(hwSt.rightWear == u"worn");

        // Garbage before a valid packet must be skipped by the resync logic.
        hw::Decoder noisy;
        const auto resynced = noisy.feed(QByteArray("garbage") +
                                         QByteArray::fromHex("5a0007002b2a010203015043"));
        KALM_CHECK(resynced.size() == 1);

        // SessionCapabilities per vendor
        DeviceRegistry capReg(tmp.path() + QStringLiteral("/caps_devices.json"));
        capReg.add(QStringLiteral("11:22:33:44:55:66"), QStringLiteral("HUAWEI FreeBuds 6i"), {}, true);
        KalmSession hwSess(capReg);
        const auto hwCaps = hwSess.capabilities();
        KALM_CHECK(hwCaps.protocol == DeviceProtocol::HuaweiSpp);
        KALM_CHECK(hwCaps.ancLevels && hwCaps.equalizer && hwCaps.gameMode && !hwCaps.noiseCycle && !hwCaps.findDevice && hwCaps.probe);

        capReg.add(QStringLiteral("22:33:44:55:66:77"), QStringLiteral("realme Buds T300"), {}, true);
        KalmSession rmSess(capReg);
        const auto rmCaps = rmSess.capabilities();
        KALM_CHECK(rmCaps.protocol == DeviceProtocol::RealmeOppoTl);
        KALM_CHECK(!rmCaps.ancLevels && !rmCaps.equalizer && !rmCaps.gameMode && !rmCaps.findDevice && rmCaps.probe);

        // Adversarial stream fuzzing & bounded buffer resync across all 3 decoders
        FrameDecoder advVivo;
        RealmeOppoDecoder advRealme;
        hw::Decoder advHw;
        const QByteArray poison(200000, '\x5a');
        (void)advVivo.feed(QByteArray::fromHex("ff0402ffff001b8230") + poison);
        (void)advRealme.feed(QByteArray::fromHex("aaff7f00000681") + poison);
        (void)advHw.feed(QByteArray::fromHex("5a1ffb002b2a01ff") + poison);

        KALM_CHECK(advVivo.feed(buildHandshake()).size() == 1);
        KALM_CHECK(advRealme.feed(buildRealmeOppoPacket(kTlCmdGetBattery, 1, {})).size() == 1);
        KALM_CHECK(advHw.feed(hw::buildQueryAnc()).size() == 1);
    }

    // 5. Heap & Handle/FD Leak Stress Test (10,000 decode/JSON cycles + 1,000 IPC roundtrips)
    auto countOpenHandles = []() -> long {
#ifdef _WIN32
        DWORD count = 0;
        ::GetProcessHandleCount(::GetCurrentProcess(), &count);
        return static_cast<long>(count);
#else
        return static_cast<long>(QDir(QStringLiteral("/proc/self/fd")).entryList(QDir::NoDotAndDotDot).size());
#endif
    };

    qputenv("XDG_RUNTIME_DIR", tmp.path().toUtf8());
    KalmSession testSession(reg);
    IpcServer srv(testSession);
    KALM_CHECK(srv.start());

    for (int w = 0; w < 100; ++w) {
        initI18n((w % 2 == 0) ? QStringLiteral("en") : QStringLiteral("ru"));
        FrameDecoder warmDec;
        EarbudState warmSt;
        for (const auto& f : warmDec.feed(rawStream)) applyFrameToState(f, warmSt);
        (void)warmSt.toJson();
        (void)ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("ping")}}, 1000);
        (void)ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("status")}}, 1000);
    }

    // Concurrent i18n + IPC thread-safety stress check
    {
        std::thread writer([&]() {
            for (int i = 0; i < 200; ++i) {
                initI18n((i % 2 == 0) ? QStringLiteral("en") : QStringLiteral("ru"));
            }
        });
        std::thread reader([&]() {
            for (int i = 0; i < 400; ++i) {
                const QString lbl = ancLevelLabel(2);
                KALM_CHECK(!lbl.isEmpty());
                const QVariantMap m = i18nVariantMap();
                KALM_CHECK(!m.isEmpty());
                const auto resp = ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("status")}}, 1000);
                KALM_CHECK(resp.has_value());
            }
        });
        writer.join();
        reader.join();
    }

    const long fdsBefore = countOpenHandles();
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
    const size_t h0 = mallinfo2().uordblks;
#endif

    for (int iter = 0; iter < 10000; ++iter) {
        FrameDecoder loopDec;
        EarbudState loopSt;
        loopSt.connected = true;
        for (const auto& f : loopDec.feed(rawStream)) {
            applyFrameToState(f, loopSt);
        }
        const auto json = loopSt.toJson();
        KALM_CHECK(json.value(QStringLiteral("connected")).toBool());
    }
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
    const size_t h1 = mallinfo2().uordblks;
#endif

    for (int iter = 0; iter < 1000; ++iter) {
        initI18n((iter % 2 == 0) ? QStringLiteral("en") : QStringLiteral("ru"));
    }
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
    const size_t h2 = mallinfo2().uordblks;
#endif

    for (int i = 0; i < 500; ++i) {
        const auto pong = ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("ping")}}, 1000);
        KALM_CHECK(pong.has_value() && pong->value(QStringLiteral("ok")).toBool());
        const auto stResp = ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("status")}}, 1000);
        KALM_CHECK(stResp.has_value() && stResp->value(QStringLiteral("ok")).toBool());
    }
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
    const size_t h3 = mallinfo2().uordblks;
#endif
    const long fdsDuring = countOpenHandles();

    srv.stop();
    const long fdsAfterStop = countOpenHandles();

#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
    const ssize_t dDecode = static_cast<ssize_t>(h1) - static_cast<ssize_t>(h0);
    const ssize_t dI18n = static_cast<ssize_t>(h2) - static_cast<ssize_t>(h1);
    const ssize_t dIpc = static_cast<ssize_t>(h3) - static_cast<ssize_t>(h2);
    std::cout << "Leak check: FDs=" << fdsBefore << "->" << fdsDuring << " (after stop=" << fdsAfterStop
              << "), 10000 decode=" << dDecode << "B, 1000 i18n=" << dI18n << "B, 1000 IPC=" << dIpc << "B\n";
    KALM_CHECK(fdsDuring == fdsBefore);
    KALM_CHECK(fdsAfterStop < fdsBefore);
    KALM_CHECK(dDecode <= 65536);
    KALM_CHECK(dI18n <= 65536);
    KALM_CHECK(dIpc <= 65536);
#else
    std::cout << "Handle/FD check: before=" << fdsBefore << " during=" << fdsDuring
              << " afterStop=" << fdsAfterStop << "\n";
    KALM_CHECK(fdsDuring - fdsBefore < 20);
#endif

    std::cout << "All C++ tests passed.\n";
    return 0;
}
