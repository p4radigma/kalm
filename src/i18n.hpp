// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

namespace kalm {

struct LanguageOption {
    QString code;   // "en", "ru", "auto", or custom "<xx>"
    QString label;  // "English", "Русский", "System locale", ...
};

// Initializes active translation map.
// Default (when langSpec is empty or "en") is English ("en").
// Pass "auto" to auto-detect from system locale, or any language code ("ru", "de", ...).
void initI18n(const QString& langSpec = QStringLiteral("en"));

QString currentLanguage();
QString trKey(const char* key);
QString trKey(const QString& key);
QVariantMap i18nVariantMap();
QList<LanguageOption> availableLanguages();

}  // namespace kalm
