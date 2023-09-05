// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtcsettings.h"
#include "store.h"

namespace Utils {

/*!
    \class Utils::QtcSettings
    \inheaderfile utils/qtcsettings.h
    \inmodule QtCreator

    \brief The QtcSettings class is an extension of the QSettings class.

    Use Utils::QtcSettings::setValueWithDefault() to write values with a
    default.
*/

/*!
    \fn template<typename T> void setValueWithDefault(const QString &key, const T &val, const T &defaultValue)

    Sets the value of setting \a key to \a val. If \a val is the same as the \a
    defaultValue, the settings key is removed instead. This makes sure that
    settings are only written if actually necessary, namely when the user
    changed them from the default. It also makes a new default value for a
    setting in a new version of the application take effect, if the user did
    not change the setting before.

    \sa QSettings::setValue()
*/

QVariant QtcSettings::value(const Key &key, const QVariant &def) const
{
    return QSettings::value(stringFromKey(key), def);
}

void QtcSettings::setValue(const Key &key, const QVariant &value)
{
    QSettings::setValue(stringFromKey(key), mapEntryFromStoreEntry(value));
}

} // namespace Utils
