// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial

#pragma once

#include <extensionsystem/iplugin.h>

namespace ProjectExplorer { class Project; }

namespace Axivion::Internal {

class AxivionSettings;
class AxivionProjectSettings;

class AxivionPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Axivion.json")

public:
    AxivionPlugin();
    ~AxivionPlugin() final;

    static AxivionPlugin *instance();
    static AxivionSettings *settings();
    static AxivionProjectSettings *projectSettings(ProjectExplorer::Project *project);

    static bool handleCertificateIssue();

signals:
    void settingsChanged();

private:
    bool initialize(const QStringList &arguments, QString *errorMessage) final;
    void extensionsInitialized() final {}
};

} // Axivion::Internal

