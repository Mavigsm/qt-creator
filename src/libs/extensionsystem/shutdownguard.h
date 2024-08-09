// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "extensionsystem_global.h"

#include <QObject>

namespace ExtensionSystem {

EXTENSIONSYSTEM_EXPORT QObject *shutdownGuard();

} // namespace ExtensionSystem
