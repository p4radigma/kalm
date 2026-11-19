// SPDX-License-Identifier: GPL-3.0-or-later
#include "registry.hpp"

#include "i18n.hpp"
#include "transport.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <stdexcept>

namespace kalm {

QString normalizeMac(const QString& raw) {
    QString cleaned = raw.trimmed().toUpper();
    cleaned.replace(u'-', u':');
    static const QRegularExpression kMacRe(QStringLiteral("^[0-9A-F]{2}(:[0-9A-F]{2}){5}$"));
    if (!kMacRe.match(cleaned).hasMatch()) {
        throw std::invalid_argument(trKey("err_invalid_mac").arg(raw).toStdString());
    }
    return cleaned;
}

DeviceRegistry::DeviceRegistry(const QString& customPath) {
    if (!customPath.isEmpty()) {
        m_path = customPath;
    } else {
        QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.config");
        m_path = base + QStringLiteral("/kalm/devices.json");
    }
    load();
}

void DeviceRegistry::load() {
    QMutexLocker lock(&m_mutex);
    QFile f(m_path);
    if (!f.exists() || f.size() > 2 * 1024 * 1024 || !f.open(QIODevice::ReadOnly)) {
        m_activeMac.clear();
        m_autoSwitch = true;
        m_language = QStringLiteral("en");
        m_initialized = false;
        m_devices.clear();
        return;
    }
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const QJsonObject root = doc.object();

    QList<SavedEarbud> list;
    const QJsonArray arr = root.value(QStringLiteral("devices")).toArray();
    for (const QJsonValue& val : arr) {
        if (!val.isObject()) continue;
        const QJsonObject o = val.toObject();
        try {
            const QString m = normalizeMac(o.value(QStringLiteral("mac")).toString());
            QString nm = o.value(QStringLiteral("name")).toString();
            if (nm.isEmpty()) nm = m;
            const QString al = o.value(QStringLiteral("alias")).toString();
            list.append(SavedEarbud{m, nm, al});
        } catch (...) {
        }
    }

    QString act;
    const QString rawAct = root.value(QStringLiteral("active_mac")).toString();
    if (!rawAct.isEmpty()) {
        try {
            act = normalizeMac(rawAct);
        } catch (...) {
        }
    }
    bool foundActive = false;
    for (const auto& d : list) {
        if (d.mac == act) {
            foundActive = true;
            break;
        }
    }
    if (!act.isEmpty() && !foundActive) {
        act = list.isEmpty() ? QString{} : list.first().mac;
    }

    m_devices = std::move(list);
    m_activeMac = act;
    m_autoSwitch = root.value(QStringLiteral("auto_switch")).toBool(true);
    const QString savedLang = root.value(QStringLiteral("language")).toString().trimmed();
    m_language = savedLang.isEmpty() ? QStringLiteral("en") : savedLang;
    m_initialized = root.value(QStringLiteral("initialized")).toBool(true);
}

void DeviceRegistry::saveUnlocked() {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QJsonArray arr;
    for (const auto& d : m_devices) {
        arr.append(QJsonObject{
            {QStringLiteral("mac"), d.mac},
            {QStringLiteral("name"), d.name},
            {QStringLiteral("alias"), d.alias},
        });
    }
    QJsonObject root{
        {QStringLiteral("active_mac"), m_activeMac.isEmpty() ? QJsonValue() : QJsonValue(m_activeMac)},
        {QStringLiteral("auto_switch"), m_autoSwitch},
        {QStringLiteral("language"), m_language},
        {QStringLiteral("initialized"), m_initialized},
        {QStringLiteral("devices"), arr},
    };
    QSaveFile sf(m_path);
    if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sf.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        sf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        sf.commit();
    }
}

void DeviceRegistry::ensureInitialImport() {
    {
        QMutexLocker lock(&m_mutex);
        if (m_initialized) return;
        m_initialized = true;
    }
    const auto supportedDevs = listBluezDevices(true, false);
    QMutexLocker lock(&m_mutex);
    for (const auto& bdev : supportedDevs) {
        addUnlocked(bdev.mac, bdev.name, {});
        if (bdev.connected && m_activeMac.isEmpty()) {
            m_activeMac = bdev.mac;
        }
    }
    if (m_activeMac.isEmpty() && !m_devices.isEmpty()) {
        m_activeMac = m_devices.first().mac;
    }
    saveUnlocked();
}

QList<SavedEarbud> DeviceRegistry::devices() const {
    QMutexLocker lock(&m_mutex);
    return m_devices;
}

QString DeviceRegistry::activeMac() const {
    QMutexLocker lock(&m_mutex);
    return m_activeMac;
}

bool DeviceRegistry::autoSwitch() const {
    QMutexLocker lock(&m_mutex);
    return m_autoSwitch;
}

void DeviceRegistry::setAutoSwitch(bool enabled) {
    QMutexLocker lock(&m_mutex);
    m_autoSwitch = enabled;
    saveUnlocked();
}

QString DeviceRegistry::language() const {
    QMutexLocker lock(&m_mutex);
    return m_language;
}

void DeviceRegistry::setLanguage(const QString& lang) {
    QMutexLocker lock(&m_mutex);
    const QString c = lang.trimmed().toLower();
    m_language = c.isEmpty() ? QStringLiteral("en") : c;
    saveUnlocked();
}

std::optional<SavedEarbud> DeviceRegistry::get(const QString& mac) const {
    const QString norm = normalizeMac(mac);
    QMutexLocker lock(&m_mutex);
    for (const auto& d : m_devices) {
        if (d.mac == norm) return d;
    }
    return std::nullopt;
}

std::optional<SavedEarbud> DeviceRegistry::getActive() const {
    QMutexLocker lock(&m_mutex);
    if (m_activeMac.isEmpty()) return std::nullopt;
    for (const auto& d : m_devices) {
        if (d.mac == m_activeMac) return d;
    }
    return std::nullopt;
}

SavedEarbud DeviceRegistry::addUnlocked(const QString& mac, const QString& name, const QString& alias) {
    const QString norm = normalizeMac(mac);
    for (auto& d : m_devices) {
        if (d.mac == norm) {
            if (!name.trimmed().isEmpty()) d.name = name.trimmed();
            if (!alias.trimmed().isEmpty()) d.alias = alias.trimmed();
            return d;
        }
    }
    SavedEarbud entry{norm, name.trimmed().isEmpty() ? norm : name.trimmed(), alias.trimmed()};
    m_devices.append(entry);
    if (m_activeMac.isEmpty()) m_activeMac = norm;
    return entry;
}

SavedEarbud DeviceRegistry::add(const QString& mac, const QString& name,
                                const QString& alias, bool makeActive) {
    QMutexLocker lock(&m_mutex);
    m_initialized = true;
    SavedEarbud entry = addUnlocked(mac, name, alias);
    if (makeActive) m_activeMac = entry.mac;
    saveUnlocked();
    return entry;
}

bool DeviceRegistry::remove(const QString& mac) {
    const QString norm = normalizeMac(mac);
    QMutexLocker lock(&m_mutex);
    qsizetype removed = 0;
    for (qsizetype i = m_devices.size() - 1; i >= 0; --i) {
        if (m_devices[i].mac == norm) {
            m_devices.removeAt(i);
            ++removed;
        }
    }
    if (removed == 0) return false;
    if (m_activeMac == norm) {
        m_activeMac = m_devices.isEmpty() ? QString{} : m_devices.first().mac;
    }
    saveUnlocked();
    return true;
}

bool DeviceRegistry::rename(const QString& mac, const QString& alias) {
    const QString norm = normalizeMac(mac);
    QMutexLocker lock(&m_mutex);
    for (auto& d : m_devices) {
        if (d.mac == norm) {
            d.alias = alias.trimmed();
            saveUnlocked();
            return true;
        }
    }
    return false;
}

bool DeviceRegistry::setActive(const QString& mac) {
    const QString norm = normalizeMac(mac);
    QMutexLocker lock(&m_mutex);
    for (const auto& d : m_devices) {
        if (d.mac == norm) {
            m_activeMac = norm;
            saveUnlocked();
            return true;
        }
    }
    return false;
}

std::optional<SavedEarbud> DeviceRegistry::resolveQuery(const QString& query) const {
    const QString q = query.trimmed();
    if (q.isEmpty()) return std::nullopt;
    try {
        const QString norm = normalizeMac(q);
        if (auto found = get(norm)) return found;
        return SavedEarbud{norm, norm, {}};
    } catch (...) {
    }

    const QString low = q.toLower();
    QMutexLocker lock(&m_mutex);
    for (const auto& d : m_devices) {
        if (d.displayName().toLower() == low || d.name.toLower() == low) return d;
    }
    for (const auto& d : m_devices) {
        if (d.displayName().toLower().contains(low) || d.name.toLower().contains(low)) return d;
    }
    return std::nullopt;
}

}  // namespace kalm
