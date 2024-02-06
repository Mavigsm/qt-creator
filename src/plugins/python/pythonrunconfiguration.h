// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

namespace Python::Internal {

void setupPythonRunConfiguration();
void setupPythonRunWorker();
void setupPythonDebugWorker();
void setupPythonOutputParser();

} // Python::Internal
