// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.hpp"
#include "i18n.hpp"
#include "probe.hpp"
#include "protocol.hpp"
#include "registry.hpp"
#include "session.hpp"
#include "transport.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <csignal>
#include <cstdio>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <QSocketNotifier>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifndef _WIN32
int g_sigPair[2] = {-1, -1};

void handleUnixSignal(int) {
    if (g_sigPair[0] >= 0) {
        const char b = 1;
        const ssize_t unused = ::write(g_sigPair[0], &b, 1);
        (void)unused;
    }
}
#endif

void notifyDesktop(const QString& title, const QString& body) {
#ifdef _WIN32
    kalm::ipcRequest(
        QJsonObject{{QStringLiteral("cmd"), QStringLiteral("notify")},
                    {QStringLiteral("title"), title},
                    {QStringLiteral("body"), body}},
        500);
#else
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("notify-send"));
    if (exe.isEmpty()) return;
    QProcess::startDetached(exe, {
                                     QStringLiteral("-a"),
                                     QStringLiteral("Kalm"),
                                     QStringLiteral("-i"),
                                     QStringLiteral("audio-headset"),
                                     QStringLiteral("-h"),
                                     QStringLiteral("string:x-canonical-private-synchronous:kalm"),
                                     title,
                                     body,
                                 });
#endif
}

QString formatBattCli(const QJsonValue& levelVal, bool charging, bool stale) {
    if (levelVal.isNull() || levelVal.isUndefined()) return QStringLiteral("—");
    const int lvl = levelVal.toInt();
    const QString suffix = charging
                               ? QStringLiteral(" ⚡")
                               : (stale ? QStringLiteral(" (%1)").arg(kalm::trKey("batt_stale_short")) : QString{});
    return QStringLiteral("%1%%2").arg(lvl).arg(suffix);
}

QString wearCliLabel(const QString& w) {
    if (w == u"worn") return kalm::trKey("wear_worn").toLower();
    if (w == u"in_case") return kalm::trKey("wear_in_case").toLower();
    if (w == u"removed") return kalm::trKey("wear_removed").toLower();
    return QStringLiteral("—");
}

void printStatusHuman(const QJsonObject& data) {
    QTextStream out(stdout);
    if (!data.value(QStringLiteral("connected")).toBool()) {
        out << kalm::trKey("status_disconnected") << "\n";
        return;
    }

    const QString name = data.value(QStringLiteral("device_name")).toString(QStringLiteral("Kalm"));
    const QString mac = data.value(QStringLiteral("device_mac")).toString();
    const QString fw = data.value(QStringLiteral("firmware")).toString(QStringLiteral("—"));

    const QString protoStr = data.value(QStringLiteral("protocol")).toString(QStringLiteral("vivo_gaia"));
    const bool hasSubModes = (protoStr == u"vivo_gaia" || protoStr == u"huawei_spp");
    QString mode = QStringLiteral("—");
    if (auto nm = kalm::parseNoiseModeStr(data.value(QStringLiteral("noise_mode")).toString())) {
        mode = kalm::noiseModeLabel(*nm);
        if (hasSubModes && *nm == kalm::NoiseMode::Anc && !data.value(QStringLiteral("anc_level")).isNull()) {
            QString sub = data.value(QStringLiteral("anc_level_label")).toString();
            if (sub.isEmpty()) sub = kalm::ancLevelLabel(data.value(QStringLiteral("anc_level")).toInt());
            if (!sub.isEmpty()) mode += QStringLiteral(" (%1)").arg(sub);
        } else if (hasSubModes && *nm == kalm::NoiseMode::Transparency && !data.value(QStringLiteral("transparency_effect")).isNull()) {
            QString sub = data.value(QStringLiteral("transparency_effect_label")).toString();
            if (sub.isEmpty()) sub = kalm::transparencyLabel(data.value(QStringLiteral("transparency_effect")).toInt());
            if (!sub.isEmpty()) mode += QStringLiteral(" (%1)").arg(sub);
        }
    }

    const QJsonObject batt = data.value(QStringLiteral("battery")).toObject();
    const QJsonObject wear = data.value(QStringLiteral("wear")).toObject();

    const QString leftB = formatBattCli(batt.value(QStringLiteral("left")),
                                        batt.value(QStringLiteral("left_charging")).toBool(),
                                        batt.value(QStringLiteral("left_stale")).toBool());
    const QString rightB = formatBattCli(batt.value(QStringLiteral("right")),
                                         batt.value(QStringLiteral("right_charging")).toBool(),
                                         batt.value(QStringLiteral("right_stale")).toBool());
    const QString caseB = formatBattCli(batt.value(QStringLiteral("case")),
                                        batt.value(QStringLiteral("case_charging")).toBool(),
                                        batt.value(QStringLiteral("case_stale")).toBool());

    const QString lShort = kalm::trKey("batt_left").left(1);
    const QString rShort = kalm::trKey("batt_right").left(1);

    auto padLabel = [](const QString& s) { return s.leftJustified(16, u' '); };

    out << padLabel(kalm::trKey("cli_earbuds")) << " " << name << " (" << mac << ")\n";
    out << padLabel(kalm::trKey("cli_firmware")) << " " << (fw.isEmpty() ? QStringLiteral("—") : fw) << "\n";
    out << padLabel(kalm::trKey("cli_noise")) << " " << mode << "\n";
    out << padLabel(kalm::trKey("cli_battery")) << " " << lShort << ": " << leftB << "   " << rShort << ": " << rightB
        << "   " << kalm::trKey("batt_case") << ": " << caseB << "\n";
    out << padLabel(kalm::trKey("cli_position")) << " " << lShort << ": "
        << wearCliLabel(wear.value(QStringLiteral("left")).toString()) << "   " << rShort << ": "
        << wearCliLabel(wear.value(QStringLiteral("right")).toString()) << "\n";

    if (!data.value(QStringLiteral("audio_effect")).isNull()) {
        QString eq = data.value(QStringLiteral("audio_effect_label")).toString();
        if (eq.isEmpty()) eq = kalm::audioEffectLabel(data.value(QStringLiteral("audio_effect")).toInt());
        if (!eq.isEmpty()) out << padLabel(kalm::trKey("cli_sound")) << " " << eq << "\n";
    }

    if (!data.value(QStringLiteral("low_latency")).isNull()) {
        out << padLabel(kalm::trKey("cli_game")) << " "
            << (data.value(QStringLiteral("low_latency")).toBool() ? kalm::trKey("cli_enabled")
                                                                   : kalm::trKey("cli_disabled"))
            << "\n";
    }

    const QJsonObject ncObj = data.value(QStringLiteral("noise_cycle")).toObject();
    if (!ncObj.value(QStringLiteral("left")).isNull()) {
        const QString nc = kalm::noiseCycleLabel(ncObj.value(QStringLiteral("left")).toInt());
        if (!nc.isEmpty()) out << padLabel(kalm::trKey("cli_long_press")) << " " << nc << "\n";
    }

    const QJsonArray devs = data.value(QStringLiteral("paired_devices")).toArray();
    if (!devs.isEmpty()) {
        out << kalm::trKey("cli_multipoint") << "\n";
        for (const QJsonValue& v : devs) {
            const QJsonObject d = v.toObject();
            const QString mark = d.value(QStringLiteral("connected")).toBool() ? QStringLiteral("●") : QStringLiteral("○");
            const int stCode = d.value(QStringLiteral("status")).toInt();
            const QString stLabel = (stCode == 2)
                                        ? kalm::trKey("mp_connected_active")
                                        : ((stCode == 1) ? kalm::trKey("mp_connected") : kalm::trKey("mp_disconnected"));
            out << "  " << mark << " " << d.value(QStringLiteral("name")).toString()
                << " (" << d.value(QStringLiteral("mac")).toString() << ") — "
                << stLabel << "\n";
        }
    }
}

QString battSummaryShort(const QJsonObject& data) {
    const QJsonObject b = data.value(QStringLiteral("battery")).toObject();
    QStringList parts;
    const QString lShort = kalm::trKey("batt_left").left(1);
    const QString rShort = kalm::trKey("batt_right").left(1);
    if (!b.value(QStringLiteral("left")).isNull()) {
        parts.append(QStringLiteral("%1 %2%").arg(lShort).arg(b.value(QStringLiteral("left")).toInt()));
    }
    if (!b.value(QStringLiteral("right")).isNull()) {
        parts.append(QStringLiteral("%1 %2%").arg(rShort).arg(b.value(QStringLiteral("right")).toInt()));
    }
    if (!b.value(QStringLiteral("case")).isNull() && !b.value(QStringLiteral("case_stale")).toBool()) {
        parts.append(QStringLiteral("%1 %2%").arg(kalm::trKey("batt_case")).arg(b.value(QStringLiteral("case")).toInt()));
    }
    return parts.join(QStringLiteral(" · "));
}

bool isHuaweiCliTarget(kalm::DeviceRegistry& reg, const QString& query) {
    if (!query.trimmed().isEmpty()) {
        if (auto resolved = reg.resolveQuery(query)) {
            return kalm::huawei::matchesName(resolved->name);
        }
        return false;
    }
    if (auto active = reg.getActive()) {
        return kalm::huawei::matchesName(active->name);
    }
    return false;
}

std::optional<int> parseEqPreset(const QString& raw) {
    const QString k = raw.trimmed().toLower();
    if (k == u"0" || k == u"standard" || k == u"стандартный") return 0x00;
    if (k == u"1" || k == u"vocal" || k == u"voice" || k == u"вокал") return 0x01;
    if (k == u"2" || k == u"bass" || k == u"бас") return 0x02;
    if (k == u"3" || k == u"treble" || k == u"highs" || k == u"высокие") return 0x03;
    if (k == u"5" || k == u"audiobook" || k == u"книги") return 0x05;
    if (k == u"11" || k == u"master" || k == u"hifi" || k == u"мастер") return 0x0B;
    return std::nullopt;
}

void printHelp() {
    QTextStream out(stdout);
    out << "Kalm — " << kalm::trKey("app_subtitle") << "\n\n"
        << "  kalm [--minimized] [--lang <en|ru|auto>] [-d <MAC|name>]\n"
        << "  kalm status [--json] [-d <MAC|name>]\n"
        << "  kalm anc [adaptive|mild|standard|deep|plane|metro|bus] [--notify]\n"
        << "  kalm trans [standard|voice] [--notify]\n"
        << "  kalm off [--notify]\n"
        << "  kalm toggle [--modes anc,trans,off] [--notify]\n"
        << "  kalm eq <standard|vocal|bass|treble|audiobook|master>\n"
        << "  kalm game <on|off>\n"
        << "  kalm lang <en|ru|auto>\n"
        << "  kalm probe [-d <MAC|name>] [--listen <sec>] [--output <file>]\n"
        << "  kalm devices [list|scan|add <MAC> [--alias <name>]|use <target>|rename <target> <name>|remove <target>]\n";
}

int launchGui(const QString& targetDevice, bool startMinimized, const QString& langOverride, int argc, char** argv) {
    if (targetDevice.isEmpty()) {
        const QString token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
        const auto existing = kalm::ipcRequest(
            QJsonObject{{QStringLiteral("cmd"), QStringLiteral("show")},
                        {QStringLiteral("token"), token}},
            500);
        if (existing && existing->value(QStringLiteral("ok")).toBool()) {
            return 0;
        }
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
#ifdef _WIN32
        qputenv("QT_QUICK_CONTROLS_STYLE", "FluentWinUI3");
#else
        qputenv("QT_QUICK_CONTROLS_STYLE", "org.kde.desktop");
#endif
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Kalm"));
    app.setApplicationDisplayName(QStringLiteral("Kalm"));
#ifdef KALM_DISPLAY_VERSION
    app.setApplicationVersion(QStringLiteral(KALM_DISPLAY_VERSION));
#else
    app.setApplicationVersion(QStringLiteral("0.5.1"));
#endif
    app.setDesktopFileName(QStringLiteral("io.github.kalm"));
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(kalm::makeKalmIcon(false, std::nullopt));

    kalm::DeviceRegistry registry;
    kalm::initI18n(!langOverride.isEmpty() ? langOverride : registry.language());
    registry.ensureInitialImport();

    QString overrideMac;
    if (!targetDevice.isEmpty()) {
        if (auto resolved = registry.resolveQuery(targetDevice)) {
            registry.setActive(resolved->mac);
            overrideMac = resolved->mac;
        }
    }

    kalm::KalmBackend* backendPtr = nullptr;
    kalm::KalmSession session(
        registry,
        overrideMac,
        [&](const kalm::EarbudState& st) {
            if (backendPtr) backendPtr->pushStateFromWorker(st);
        },
        [&]() {
            if (backendPtr) backendPtr->pushDevicesRefreshFromWorker();
        },
        [&](const QString& err) {
            if (backendPtr) backendPtr->pushErrorFromWorker(err);
        });

    kalm::KalmBackend backend(session, registry);
    backendPtr = &backend;

    kalm::KalmTrayController tray(session, backend);
    kalm::IpcServer ipc(
        session,
        [&](const QString& token) { backend.pushShowFromWorker(token); },
        [&](const QString& title, const QString& body) { tray.showToast(title, body); });
    ipc.start();
    session.start();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
#ifdef _WIN32
    engine.load(QUrl(QStringLiteral("qrc:/qml/MainWin.qml")));
#else
    const QUrl qmlUrl = !qEnvironmentVariableIsEmpty("KALM_WIN_UI")
                            ? QUrl(QStringLiteral("qrc:/qml/MainWin.qml"))
                            : QUrl(QStringLiteral("qrc:/qml/Main.qml"));
    engine.load(qmlUrl);
#endif
    if (engine.rootObjects().isEmpty()) {
        std::cerr << "Failed to load QML interface.\n";
        ipc.stop();
        session.stop();
        return 1;
    }

    if (auto* win = qobject_cast<QWindow*>(engine.rootObjects().first())) {
        backend.bindWindow(win);
        tray.bindWindow(win);
        win->setVisible(!startMinimized);
    }

    QTimer btTimer;
    btTimer.setInterval(6000);
    QObject::connect(&btTimer, &QTimer::timeout, &backend, &kalm::KalmBackend::refreshSavedDevicesAsync);
    btTimer.start();

#ifndef _WIN32
    kalm::UniqueFd sigWriteFd;
    kalm::UniqueFd sigReadFd;
    std::unique_ptr<QSocketNotifier> sigNotifier;
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, g_sigPair) == 0) {
        sigWriteFd.reset(g_sigPair[0]);
        sigReadFd.reset(g_sigPair[1]);
        sigNotifier = std::make_unique<QSocketNotifier>(sigReadFd.get(), QSocketNotifier::Read);
        QObject::connect(sigNotifier.get(), &QSocketNotifier::activated, &backend, [&]() {
            sigNotifier->setEnabled(false);
            char tmp = 0;
            const ssize_t unused = ::read(sigReadFd.get(), &tmp, 1);
            (void)unused;
            backend.quitApp();
        });
        std::signal(SIGINT, handleUnixSignal);
        std::signal(SIGTERM, handleUnixSignal);
    }
#endif

    const int rc = app.exec();
#ifndef _WIN32
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    g_sigPair[0] = -1;
    g_sigPair[1] = -1;
    sigNotifier.reset();
#endif

    backendPtr = nullptr;
    tray.hide();
    ipc.stop();
    session.stop();
    return rc;
}

int handleDevicesSubcmd(const QStringList& rest) {
    QTextStream out(stdout);
    kalm::DeviceRegistry reg;
    reg.ensureInitialImport();
    const QString action = rest.isEmpty() ? QStringLiteral("list") : rest.first();

    if (action == u"list") {
        const auto devs = reg.devices();
        if (devs.isEmpty()) {
            out << kalm::trKey("cli_no_saved") << "\n";
            return 0;
        }
        QMap<QString, kalm::BluezDevice> bluez;
        for (const auto& b : kalm::listBluezDevices(false, false)) bluez.insert(b.mac, b);
        const QString actMac = reg.activeMac();
        out << kalm::trKey("cli_saved_header") << "\n";
        for (const auto& d : devs) {
            const QString mark = (d.mac == actMac) ? QStringLiteral("★") : QStringLiteral(" ");
            const bool hasB = bluez.contains(d.mac);
            const QString btStatus = (hasB && bluez[d.mac].connected)
                                         ? kalm::trKey("status_connected")
                                         : (hasB ? kalm::trKey("status_paired") : kalm::trKey("status_not_in_bt"));
            const QString aliasNote =
                (!d.alias.isEmpty() && d.alias != d.name) ? QStringLiteral(" [%1]").arg(d.name) : QString{};
            out << "  " << mark << " " << d.displayName() << aliasNote << " — " << d.mac << " (" << btStatus << ")\n";
        }
        return 0;
    }

    if (action == u"scan") {
        const auto found = kalm::listBluezDevices(false, true);
        if (found.isEmpty()) {
            out << kalm::trKey("cli_no_bt") << "\n";
            return 0;
        }
        QSet<QString> saved;
        for (const auto& s : reg.devices()) saved.insert(s.mac);
        out << kalm::trKey("cli_bt_header") << "\n";
        for (const auto& b : found) {
            const QString tag = b.isHuaweiTws() ? QStringLiteral("HUAWEI FreeBuds")
                              : (b.isVivoTws() ? QStringLiteral("vivo/iQOO GAIA")
                              : (b.isRealmeOppoTws() ? QStringLiteral("realme/OPPO") : QStringLiteral("Bluetooth Audio")));
            const QString conn = b.connected ? kalm::trKey("status_connected") : kalm::trKey("status_disconnected");
            const QString sv = saved.contains(b.mac) ? QStringLiteral(" [%1]").arg(kalm::trKey("status_in_list")) : QString{};
            out << "  • " << b.name << " — " << b.mac << " (" << tag << ", " << conn << ")" << sv << "\n";
        }
        return 0;
    }

    if (action == u"add") {
        if (rest.size() < 2) throw std::runtime_error("Usage: kalm devices add <MAC> [--alias <name>]");
        const QString mac = rest[1];
        QString alias;
        for (qsizetype i = 2; i < rest.size(); ++i) {
            if (rest[i] == u"--alias" && i + 1 < rest.size()) {
                alias = rest[++i];
            }
        }
        const auto bdev = kalm::findBluezByMac(mac);
        const auto entry = reg.add(mac, bdev ? bdev->name : mac.toUpper(), alias, true);
        kalm::ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("reload_registry")}});
        out << kalm::trKey("cli_added").arg(entry.displayName(), entry.mac) << "\n";
        return 0;
    }

    if (action == u"remove") {
        if (rest.size() < 2) throw std::runtime_error("Usage: kalm devices remove <MAC|name>");
        const auto target = reg.resolveQuery(rest[1]);
        if (!target || !reg.remove(target->mac)) {
            throw std::runtime_error(kalm::trKey("err_device_not_saved").arg(rest[1]).toStdString());
        }
        kalm::ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("reload_registry")}});
        out << kalm::trKey("cli_removed").arg(target->displayName(), target->mac) << "\n";
        return 0;
    }

    if (action == u"use") {
        if (rest.size() < 2) throw std::runtime_error("Usage: kalm devices use <MAC|name>");
        const auto target = reg.resolveQuery(rest[1]);
        if (!target || !reg.setActive(target->mac)) {
            throw std::runtime_error(kalm::trKey("err_device_not_saved").arg(rest[1]).toStdString());
        }
        kalm::ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("select_device")},
                                     {QStringLiteral("mac"), target->mac}});
        out << kalm::trKey("cli_active").arg(target->displayName(), target->mac) << "\n";
        return 0;
    }

    if (action == u"rename") {
        if (rest.size() < 3) throw std::runtime_error("Usage: kalm devices rename <MAC|name> <new_name>");
        const auto target = reg.resolveQuery(rest[1]);
        if (!target || !reg.rename(target->mac, rest[2])) {
            throw std::runtime_error(kalm::trKey("err_device_not_saved").arg(rest[1]).toStdString());
        }
        kalm::ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("reload_registry")}});
        out << kalm::trKey("cli_renamed").arg(rest[2].isEmpty() ? target->name : rest[2], target->mac) << "\n";
        return 0;
    }

    throw std::runtime_error(QStringLiteral("Unknown devices action: %1").arg(action).toStdString());
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc > 1) {
        if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* fpOut = nullptr;
            FILE* fpErr = nullptr;
            freopen_s(&fpOut, "CONOUT$", "w", stdout);
            freopen_s(&fpErr, "CONOUT$", "w", stderr);
        }
        ::SetConsoleOutputCP(CP_UTF8);
    }
#endif
    QStringList rawArgs;
    for (int i = 1; i < argc; ++i) rawArgs.append(QString::fromLocal8Bit(argv[i]));

    bool startMinimized = false;
    bool jsonOut = false;
    bool notify = false;
    bool wantHelp = false;
    bool wantVersion = false;
    QString langOverride;
    QString deviceQuery;
    QString outputFile;
    int listenSeconds = 0;
    QString modesCsv = QStringLiteral("anc,trans,off");
    QStringList positional;

    for (qsizetype i = 0; i < rawArgs.size(); ++i) {
        const QString& a = rawArgs[i];
        if (a == u"-h" || a == u"--help") {
            wantHelp = true;
        } else if (a == u"-v" || a == u"--version") {
            wantVersion = true;
        } else if (a == u"--minimized") {
            startMinimized = true;
        } else if (a == u"--json") {
            jsonOut = true;
        } else if (a == u"--notify") {
            notify = true;
        } else if (a == u"--lang" && i + 1 < rawArgs.size()) {
            langOverride = rawArgs[++i];
        } else if ((a == u"-d" || a == u"--device" || a == u"--mac") && i + 1 < rawArgs.size()) {
            deviceQuery = rawArgs[++i];
        } else if ((a == u"-o" || a == u"--output") && i + 1 < rawArgs.size()) {
            outputFile = rawArgs[++i];
        } else if (a == u"--listen") {
            if (i + 1 < rawArgs.size() && !rawArgs[i + 1].startsWith(u'-')) {
                listenSeconds = std::max(1, rawArgs[++i].toInt());
            } else {
                listenSeconds = 15;
            }
        } else if (a == u"--modes" && i + 1 < rawArgs.size()) {
            modesCsv = rawArgs[++i];
        } else {
            positional.append(a);
        }
    }

    if (wantVersion) {
#ifdef KALM_DISPLAY_VERSION
        std::cout << "kalm " << KALM_DISPLAY_VERSION << "\n";
#else
        std::cout << "kalm 0.5.1\n";
#endif
        return 0;
    }

    const QString subcmd = (!wantHelp && positional.isEmpty()) ? QStringLiteral("gui")
                                                               : (positional.isEmpty() ? QString{} : positional.first());
    if (!wantHelp && subcmd == u"gui") {
        return launchGui(deviceQuery, startMinimized, langOverride, argc, argv);
    }

    QCoreApplication coreApp(argc, argv);
    kalm::DeviceRegistry reg;
    kalm::initI18n(!langOverride.isEmpty() ? langOverride : reg.language());

    if (wantHelp) {
        printHelp();
        return 0;
    }

    QTextStream out(stdout);
    QTextStream err(stderr);

    try {
        if (subcmd == u"lang") {
            if (positional.size() < 2) {
                out << reg.language() << "\n";
                return 0;
            }
            reg.setLanguage(positional[1]);
            kalm::initI18n(reg.language());
            out << kalm::trKey("lang_title") << ": " << reg.language() << "\n";
            return 0;
        }

        if (subcmd == u"devices") {
            return handleDevicesSubcmd(positional.mid(1));
        }

        if (subcmd == u"probe") {
            if (listenSeconds > 0) {
                err << "Listening for earbud events for " << listenSeconds
                    << "s (squeeze the stem or switch modes on phone now)...\n";
                err.flush();
            }
            const int ipcTimeout = 12000 + listenSeconds * 1000;
            const auto ipc = kalm::ipcRequest(
                QJsonObject{{QStringLiteral("cmd"), QStringLiteral("probe")},
                            {QStringLiteral("device"), deviceQuery},
                            {QStringLiteral("listen"), listenSeconds}},
                ipcTimeout);
            const QString rep = (ipc && ipc->value(QStringLiteral("ok")).toBool())
                                    ? ipc->value(QStringLiteral("report")).toString()
                                    : kalm::generateDiagnosticReport(reg, deviceQuery, listenSeconds);
            if (!outputFile.isEmpty()) {
                QFile f(outputFile);
                if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    throw std::runtime_error(QStringLiteral("Cannot write to '%1'").arg(outputFile).toStdString());
                }
                f.write(rep.toUtf8());
                out << "Saved diagnostic report to " << outputFile << "\n";
            } else {
                out << rep;
            }
            return 0;
        }

        if (subcmd == u"status") {
            std::optional<QJsonObject> ipc;
            if (deviceQuery.isEmpty()) {
                ipc = kalm::ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("status")}});
            }
            const QJsonObject state =
                (ipc && ipc->value(QStringLiteral("ok")).toBool() &&
                 ipc->value(QStringLiteral("state")).toObject().value(QStringLiteral("connected")).toBool())
                    ? ipc->value(QStringLiteral("state")).toObject()
                    : kalm::oneshotStatus(reg, deviceQuery);
            if (jsonOut) {
                out << QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Indented));
            } else {
                printStatusHuman(state);
            }
            return 0;
        }

        if (subcmd == u"anc" || subcmd == u"trans" || subcmd == u"off" || subcmd == u"mode") {
            const QString rawTarget = (subcmd == u"mode")
                                          ? (positional.size() >= 2 ? positional[1] : QString{})
                                          : subcmd;
            const auto mode = kalm::parseNoiseModeStr(rawTarget);
            if (!mode) {
                throw std::runtime_error(
                    QStringLiteral("Unknown mode '%1'. Available: anc, trans, off").arg(rawTarget).toStdString());
            }

            std::optional<int> ancLvl;
            std::optional<int> transEff;
            const qsizetype subArgIdx = (subcmd == u"mode") ? 2 : 1;
            if (positional.size() > subArgIdx) {
                const QString extra = positional[subArgIdx];
                if (*mode == kalm::NoiseMode::Anc) {
                    ancLvl = kalm::parseAncLevelStr(extra);
                    if (!ancLvl) {
                        throw std::runtime_error(
                            QStringLiteral("Unknown ANC level '%1'. Available: adaptive, mild, standard, deep, plane, metro, bus")
                                .arg(extra)
                                .toStdString());
                    }
                } else if (*mode == kalm::NoiseMode::Transparency) {
                    transEff = kalm::parseTransparencyStr(extra);
                    if (!transEff) {
                        throw std::runtime_error(
                            QStringLiteral("Unknown transparency mode '%1'. Available: standard, voice")
                                .arg(extra)
                                .toStdString());
                    }
                }
            }

            QJsonObject reqObj{{QStringLiteral("cmd"), QStringLiteral("set_noise")},
                               {QStringLiteral("mode"), kalm::noiseModeSlug(*mode)}};
            if (ancLvl) reqObj.insert(QStringLiteral("anc_level"), *ancLvl);
            if (transEff) reqObj.insert(QStringLiteral("trans_effect"), *transEff);

            QJsonObject state;
            const auto ipc = deviceQuery.isEmpty() ? kalm::ipcRequest(reqObj) : std::nullopt;
            if (ipc) {
                if (!ipc->value(QStringLiteral("ok")).toBool()) {
                    throw std::runtime_error(ipc->value(QStringLiteral("error")).toString().toStdString());
                }
                state = ipc->value(QStringLiteral("state")).toObject();
            } else {
                state = kalm::oneshotSetNoise(reg, *mode, ancLvl, transEff, deviceQuery);
            }
            const QString label = state.value(QStringLiteral("noise_mode_label")).toString(kalm::noiseModeLabel(*mode));
            out << kalm::trKey("cli_mode_set").arg(label) << "\n";
            if (notify) {
                const QString summary = battSummaryShort(state);
                notifyDesktop(kalm::trKey("cli_notify_mode").arg(label),
                              summary.isEmpty() ? state.value(QStringLiteral("device_name")).toString(QStringLiteral("Kalm"))
                                                : summary);
            }
            return 0;
        }

        if (subcmd == u"toggle") {
            QList<kalm::NoiseMode> modesList;
            QJsonArray modesJson;
            for (const QString& tok : modesCsv.split(u',', Qt::SkipEmptyParts)) {
                const auto m = kalm::parseNoiseModeStr(tok);
                if (!m) {
                    throw std::runtime_error(QStringLiteral("Unknown mode '%1'").arg(tok).toStdString());
                }
                modesList.append(*m);
                modesJson.append(kalm::noiseModeSlug(*m));
            }
            if (modesList.size() < 2) {
                throw std::runtime_error("Specify at least 2 modes for toggle (e.g. --modes anc,trans)");
            }
            QJsonObject state;
            const auto ipc = deviceQuery.isEmpty()
                                 ? kalm::ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("toggle_noise")},
                                                                {QStringLiteral("modes"), modesJson}})
                                 : std::nullopt;
            if (ipc) {
                if (!ipc->value(QStringLiteral("ok")).toBool()) {
                    throw std::runtime_error(ipc->value(QStringLiteral("error")).toString().toStdString());
                }
                state = ipc->value(QStringLiteral("state")).toObject();
            } else {
                state = kalm::oneshotToggleNoise(reg, modesList, deviceQuery);
            }
            const QString label = state.value(QStringLiteral("noise_mode_label")).toString(QStringLiteral("—"));
            out << kalm::trKey("cli_mode_toggled").arg(label) << "\n";
            if (notify) {
                const QString summary = battSummaryShort(state);
                notifyDesktop(kalm::trKey("cli_notify_mode").arg(label),
                              summary.isEmpty() ? state.value(QStringLiteral("device_name")).toString(QStringLiteral("Kalm"))
                                                : summary);
            }
            return 0;
        }

        if (subcmd == u"eq") {
            if (positional.size() < 2) {
                throw std::runtime_error("Usage: kalm eq <standard|vocal|bass|treble|audiobook|master>");
            }
            const auto eff = parseEqPreset(positional[1]);
            if (!eff) {
                throw std::runtime_error(
                    QStringLiteral("Unknown preset '%1'. Available: standard, vocal, bass, treble, audiobook, master")
                        .arg(positional[1])
                        .toStdString());
            }

            // vivo GAIA and HUAWEI SPP use different preset ids for the same
            // sound profile; translate the CLI slug for HUAWEI devices.
            int targetCode = *eff;
            const char* labelKey = nullptr;
            if (isHuaweiCliTarget(reg, deviceQuery)) {
                switch (*eff) {
                    case 0: targetCode = 1; labelKey = "eq_standard"; break;
                    case 1: targetCode = 9; labelKey = "eq_vocal"; break;
                    case 2: targetCode = 2; labelKey = "eq_bass"; break;
                    case 3: targetCode = 3; labelKey = "eq_treble"; break;
                    default:
                        throw std::runtime_error(
                            "This sound preset is not available on HUAWEI earbuds.");
                }
            }

            const auto ipc = deviceQuery.isEmpty()
                                 ? kalm::ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("set_eq")},
                                                                {QStringLiteral("effect"), targetCode}})
                                 : std::nullopt;
            if (!ipc || !ipc->value(QStringLiteral("ok")).toBool()) {
                kalm::oneshotSetEq(reg, targetCode, deviceQuery);
            }
            out << kalm::trKey("cli_sound") << " "
                << (labelKey ? kalm::trKey(labelKey) : kalm::audioEffectLabel(*eff)) << "\n";
            return 0;
        }

        if (subcmd == u"game") {
            if (positional.size() < 2 || (positional[1] != u"on" && positional[1] != u"off")) {
                throw std::runtime_error("Usage: kalm game <on|off>");
            }
            const bool en = (positional[1] == u"on");
            const auto ipc = deviceQuery.isEmpty()
                                 ? kalm::ipcRequest(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("set_game")},
                                                                {QStringLiteral("enabled"), en}})
                                 : std::nullopt;
            if (!ipc || !ipc->value(QStringLiteral("ok")).toBool()) {
                kalm::oneshotSetGame(reg, en, deviceQuery);
            }
            out << kalm::trKey("cli_game") << " " << (en ? kalm::trKey("cli_enabled") : kalm::trKey("cli_disabled")) << "\n";
            return 0;
        }

        throw std::runtime_error(QStringLiteral("Unknown command '%1'. See kalm --help").arg(subcmd).toStdString());
    } catch (const std::exception& ex) {
        err << kalm::trKey("err_prefix") << ": " << QString::fromUtf8(ex.what()) << "\n";
        return 1;
    }
}
