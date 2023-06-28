// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "allprojectsfind.h"

namespace ProjectExplorer {

class Project;

namespace Internal {

class CurrentProjectFind : public AllProjectsFind
{
    Q_OBJECT

public:
    CurrentProjectFind();

    QString id() const override;
    QString displayName() const override;

    bool isEnabled() const override;

    void writeSettings(QSettings *settings) override;
    void readSettings(QSettings *settings) override;

protected:
    QString label() const override;

private:
    TextEditor::FileContainerProvider fileContainerProvider() const override;
    void handleProjectChanged();
    void setupSearch(Core::SearchResult *search) override;
};

} // namespace Internal
} // namespace ProjectExplorer
