/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "androidrunsupport.h"
#include "androidrunner.h"

using namespace ProjectExplorer;

namespace Android {
namespace Internal {

AndroidRunSupport::AndroidRunSupport(AndroidRunConfiguration *runConfig,
                                     RunControl *runControl)
    : QObject(runControl),
      m_runControl(runControl),
      m_runner(new AndroidRunner(this, runConfig, runControl->runMode()))
{
    connect(m_runControl, SIGNAL(finished()),
            m_runner, SLOT(stop()));

    connect(m_runner, SIGNAL(remoteProcessFinished(QString)),
            SLOT(handleRemoteProcessFinished(QString)));

    connect(m_runner, SIGNAL(remoteErrorOutput(QByteArray)),
            SLOT(handleRemoteErrorOutput(QByteArray)));
    connect(m_runner, SIGNAL(remoteOutput(QByteArray)),
            SLOT(handleRemoteOutput(QByteArray)));
}

void AndroidRunSupport::handleRemoteProcessFinished(const QString &errorMsg)
{
    if (m_runControl)
        m_runControl->appendMessage(errorMsg, Utils::NormalMessageFormat);
}

void AndroidRunSupport::handleRemoteOutput(const QByteArray &output)
{
    if (m_runControl)
        m_runControl->appendMessage(QString::fromUtf8(output), Utils::StdOutFormatSameLine);
}

void AndroidRunSupport::handleRemoteErrorOutput(const QByteArray &output)
{
    if (m_runControl)
        m_runControl->appendMessage(QString::fromUtf8(output), Utils::StdErrFormatSameLine);
}

} // namespace Internal
} // namespace Android
