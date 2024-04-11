// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppcodemodelsettings.h"

#include "compileroptionsbuilder.h"
#include "cppeditorconstants.h"
#include "cppeditortr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectpanelfactory.h>
#include <projectexplorer/projectsettingswidget.h>

#include <utils/algorithm.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/qtcprocess.h>
#include <utils/qtcassert.h>
#include <utils/store.h>

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QPair>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

using namespace ProjectExplorer;
using namespace Utils;

namespace CppEditor {

static Key pchUsageKey() { return Constants::CPPEDITOR_MODEL_MANAGER_PCH_USAGE; }
static Key interpretAmbiguousHeadersAsCHeadersKey()
    { return Constants::CPPEDITOR_INTERPRET_AMBIGIUOUS_HEADERS_AS_C_HEADERS; }
static Key skipIndexingBigFilesKey() { return Constants::CPPEDITOR_SKIP_INDEXING_BIG_FILES; }
static Key ignoreFilesKey() { return Constants::CPPEDITOR_IGNORE_FILES; }
static Key ignorePatternKey() { return Constants::CPPEDITOR_IGNORE_PATTERN; }
static Key useBuiltinPreprocessorKey() { return Constants::CPPEDITOR_USE_BUILTIN_PREPROCESSOR; }
static Key indexerFileSizeLimitKey() { return Constants::CPPEDITOR_INDEXER_FILE_SIZE_LIMIT; }
static Key useGlobalSettingsKey() { return "useGlobalSettings"; }

bool operator==(const CppEditor::CppCodeModelSettings::Data &s1,
                const CppEditor::CppCodeModelSettings::Data &s2)
{
    return s1.pchUsage == s2.pchUsage
           && s1.interpretAmbigiousHeadersAsC == s2.interpretAmbigiousHeadersAsC
           && s1.skipIndexingBigFiles == s2.skipIndexingBigFiles
           && s1.useBuiltinPreprocessor == s2.useBuiltinPreprocessor
           && s1.indexerFileSizeLimitInMb == s2.indexerFileSizeLimitInMb
           && s1.categorizeFindReferences == s2.categorizeFindReferences
           && s1.ignoreFiles == s2.ignoreFiles && s1.ignorePattern == s2.ignorePattern;
}

Store CppCodeModelSettings::Data::toMap() const
{
    Store store;
    store.insert(pchUsageKey(), pchUsage);
    store.insert(interpretAmbiguousHeadersAsCHeadersKey(), interpretAmbigiousHeadersAsC);
    store.insert(skipIndexingBigFilesKey(), skipIndexingBigFiles);
    store.insert(ignoreFilesKey(), ignoreFiles);
    store.insert(ignorePatternKey(), ignorePattern);
    store.insert(useBuiltinPreprocessorKey(), useBuiltinPreprocessor);
    store.insert(indexerFileSizeLimitKey(), indexerFileSizeLimitInMb);
    return store;
}

void CppCodeModelSettings::Data::fromMap(const Utils::Store &store)
{
    const CppCodeModelSettings::Data def;
    pchUsage = static_cast<PCHUsage>(store.value(pchUsageKey(), def.pchUsage).toInt());
    interpretAmbigiousHeadersAsC = store
                                       .value(interpretAmbiguousHeadersAsCHeadersKey(),
                                              def.interpretAmbigiousHeadersAsC)
                                       .toBool();
    skipIndexingBigFiles = store.value(skipIndexingBigFilesKey(), def.skipIndexingBigFiles).toBool();
    ignoreFiles = store.value(ignoreFilesKey(), def.ignoreFiles).toBool();
    ignorePattern = store.value(ignorePatternKey(), def.ignorePattern).toString();
    useBuiltinPreprocessor
        = store.value(useBuiltinPreprocessorKey(), def.useBuiltinPreprocessor).toBool();
    indexerFileSizeLimitInMb
        = store.value(indexerFileSizeLimitKey(), def.indexerFileSizeLimitInMb).toInt();
}

void CppCodeModelSettings::fromSettings(QtcSettings *s)
{
    m_data.fromMap(storeFromSettings(Constants::CPPEDITOR_SETTINGSGROUP, s));
}

void CppCodeModelSettings::toSettings(QtcSettings *s)
{
    storeToSettingsWithDefault(Constants::CPPEDITOR_SETTINGSGROUP, s, m_data.toMap(), Data().toMap());
}

CppCodeModelSettings &CppCodeModelSettings::globalInstance()
{
    static CppCodeModelSettings theCppCodeModelSettings(Core::ICore::settings());
    return theCppCodeModelSettings;
}

CppCodeModelSettings CppCodeModelSettings::settingsForProject(const ProjectExplorer::Project *project)
{
    return {CppCodeModelProjectSettings(const_cast<ProjectExplorer::Project *>(project)).data()};
}

CppCodeModelSettings CppCodeModelSettings::settingsForProject(const Utils::FilePath &projectFile)
{
    return settingsForProject(ProjectManager::projectWithProjectFilePath(projectFile));
}

CppCodeModelSettings CppCodeModelSettings::settingsForFile(const Utils::FilePath &file)
{
    return settingsForProject(ProjectManager::projectForFile(file));
}

void CppCodeModelSettings::setGlobalData(const Data &data)
{
    if (globalInstance().m_data == data)
        return;

    globalInstance().m_data = data;
    globalInstance().toSettings(Core::ICore::settings());
    emit globalInstance().changed(nullptr);
}

CppCodeModelSettings::PCHUsage CppCodeModelSettings::pchUsage(const Project *project)
{
    return CppCodeModelSettings::settingsForProject(project).pchUsage();
}

UsePrecompiledHeaders CppCodeModelSettings::usePrecompiledHeaders() const
{
    return pchUsage() == CppCodeModelSettings::PchUse_None ? UsePrecompiledHeaders::No
                                                           : UsePrecompiledHeaders::Yes;
}

UsePrecompiledHeaders CppCodeModelSettings::usePrecompiledHeaders(const Project *project)
{
    return CppCodeModelSettings::settingsForProject(project).usePrecompiledHeaders();
}

int CppCodeModelSettings::effectiveIndexerFileSizeLimitInMb() const
{
    return skipIndexingBigFiles() ? indexerFileSizeLimitInMb() : -1;
}

bool CppCodeModelSettings::categorizeFindReferences()
{
    return globalInstance().m_data.categorizeFindReferences;
}

void CppCodeModelSettings::setCategorizeFindReferences(bool categorize)
{
    globalInstance().m_data.categorizeFindReferences = categorize;
}

CppCodeModelProjectSettings::CppCodeModelProjectSettings(ProjectExplorer::Project *project)
    : m_project(project)
{
    loadSettings();
}

CppCodeModelSettings::Data CppCodeModelProjectSettings::data() const
{
    return m_useGlobalSettings ? CppCodeModelSettings::globalInstance().data() : m_customSettings;
}

void CppCodeModelProjectSettings::setData(const CppCodeModelSettings::Data &data)
{
    m_customSettings = data;
    saveSettings();
    emit CppCodeModelSettings::globalInstance().changed(m_project);
}

void CppCodeModelProjectSettings::setUseGlobalSettings(bool useGlobal)
{
    m_useGlobalSettings = useGlobal;
    saveSettings();
    emit CppCodeModelSettings::globalInstance().changed(m_project);
}

void CppCodeModelProjectSettings::loadSettings()
{
    if (!m_project)
        return;
    const Store data = storeFromVariant(m_project->namedSettings(Constants::CPPEDITOR_SETTINGSGROUP));
    m_useGlobalSettings = data.value(useGlobalSettingsKey(), true).toBool();
    m_customSettings.fromMap(data);
}

void CppCodeModelProjectSettings::saveSettings()
{
    if (!m_project)
        return;
    Store data = m_customSettings.toMap();
    data.insert(useGlobalSettingsKey(), m_useGlobalSettings);
    m_project->setNamedSettings(Constants::CPPEDITOR_SETTINGSGROUP, variantFromStore(data));
}

namespace Internal {

class CppCodeModelSettingsWidget final : public Core::IOptionsPageWidget
{
    Q_OBJECT
public:
    CppCodeModelSettingsWidget(const CppCodeModelSettings::Data &data);

    CppCodeModelSettings::Data data() const;

signals:
    void settingsDataChanged();

private:
    void apply() final { CppCodeModelSettings::globalInstance().setGlobalData(data()); }

    QCheckBox *m_interpretAmbiguousHeadersAsCHeaders;
    QCheckBox *m_ignorePchCheckBox;
    QCheckBox *m_useBuiltinPreprocessorCheckBox;
    QCheckBox *m_skipIndexingBigFilesCheckBox;
    QSpinBox *m_bigFilesLimitSpinBox;
    QCheckBox *m_ignoreFilesCheckBox;
    QPlainTextEdit *m_ignorePatternTextEdit;
};

CppCodeModelSettingsWidget::CppCodeModelSettingsWidget(const CppCodeModelSettings::Data &data)
{
    m_interpretAmbiguousHeadersAsCHeaders
        = new QCheckBox(Tr::tr("Interpret ambiguous headers as C headers"));

    m_skipIndexingBigFilesCheckBox = new QCheckBox(Tr::tr("Do not index files greater than"));
    m_skipIndexingBigFilesCheckBox->setChecked(data.skipIndexingBigFiles);

    m_bigFilesLimitSpinBox = new QSpinBox;
    m_bigFilesLimitSpinBox->setSuffix(Tr::tr("MB"));
    m_bigFilesLimitSpinBox->setRange(1, 500);
    m_bigFilesLimitSpinBox->setValue(data.indexerFileSizeLimitInMb);

    m_ignoreFilesCheckBox = new QCheckBox(Tr::tr("Ignore files"));
    m_ignoreFilesCheckBox->setToolTip(
        "<html><head/><body><p>"
        + Tr::tr("Ignore files that match these wildcard patterns, one wildcard per line.")
        + "</p></body></html>");

    m_ignoreFilesCheckBox->setChecked(data.ignoreFiles);
    m_ignorePatternTextEdit = new QPlainTextEdit(data.ignorePattern);
    m_ignorePatternTextEdit->setToolTip(m_ignoreFilesCheckBox->toolTip());
    m_ignorePatternTextEdit->setEnabled(m_ignoreFilesCheckBox->isChecked());

    connect(m_ignoreFilesCheckBox, &QCheckBox::stateChanged, this, [this] {
        m_ignorePatternTextEdit->setEnabled(m_ignoreFilesCheckBox->isChecked());
    });

    m_ignorePchCheckBox = new QCheckBox(Tr::tr("Ignore precompiled headers"));
    m_ignorePchCheckBox->setToolTip(Tr::tr(
        "<html><head/><body><p>When precompiled headers are not ignored, the parsing for code "
        "completion and semantic highlighting will process the precompiled header before "
        "processing any file.</p></body></html>"));

    m_useBuiltinPreprocessorCheckBox = new QCheckBox(Tr::tr("Use built-in preprocessor to show "
                                                            "pre-processed files"));
    m_useBuiltinPreprocessorCheckBox->setToolTip
        (Tr::tr("Uncheck this to invoke the actual compiler "
                "to show a pre-processed source file in the editor."));

    m_interpretAmbiguousHeadersAsCHeaders->setChecked(data.interpretAmbigiousHeadersAsC);
    m_ignorePchCheckBox->setChecked(data.pchUsage == CppCodeModelSettings::PchUse_None);
    m_useBuiltinPreprocessorCheckBox->setChecked(data.useBuiltinPreprocessor);

    using namespace Layouting;

    Column {
        Group {
            title(Tr::tr("General")),
            Column {
                   m_interpretAmbiguousHeadersAsCHeaders,
                   m_ignorePchCheckBox,
                   m_useBuiltinPreprocessorCheckBox,
                   Row { m_skipIndexingBigFilesCheckBox, m_bigFilesLimitSpinBox, st },
                   Row { Column { m_ignoreFilesCheckBox, st }, m_ignorePatternTextEdit },
                   }
        },
        st
    }.attachTo(this);

    for (const QCheckBox *const b : {m_interpretAmbiguousHeadersAsCHeaders,
                                     m_ignorePchCheckBox,
                                     m_useBuiltinPreprocessorCheckBox,
                                     m_skipIndexingBigFilesCheckBox,
                                     m_ignoreFilesCheckBox}) {
        connect(b, &QCheckBox::toggled, this, &CppCodeModelSettingsWidget::settingsDataChanged);
    }
    connect(m_bigFilesLimitSpinBox, &QSpinBox::valueChanged,
            this, &CppCodeModelSettingsWidget::settingsDataChanged);

    const auto timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, &CppCodeModelSettingsWidget::settingsDataChanged);
    connect(m_ignorePatternTextEdit, &QPlainTextEdit::textChanged,
            timer, qOverload<>(&QTimer::start));
}

CppCodeModelSettings::Data CppCodeModelSettingsWidget::data() const
{
    CppCodeModelSettings::Data data;
    data.interpretAmbigiousHeadersAsC = m_interpretAmbiguousHeadersAsCHeaders->isChecked();
    data.skipIndexingBigFiles = m_skipIndexingBigFilesCheckBox->isChecked();
    data.useBuiltinPreprocessor = m_useBuiltinPreprocessorCheckBox->isChecked();
    data.ignoreFiles = m_ignoreFilesCheckBox->isChecked();
    data.ignorePattern = m_ignorePatternTextEdit->toPlainText();
    data.indexerFileSizeLimitInMb = m_bigFilesLimitSpinBox->value();
    data.pchUsage = m_ignorePchCheckBox->isChecked() ? CppCodeModelSettings::PchUse_None
                                                     : CppCodeModelSettings::PchUse_BuildSystem;
    return data;
}

class CppCodeModelSettingsPage final : public Core::IOptionsPage
{
public:
    CppCodeModelSettingsPage()
    {
        setId(Constants::CPP_CODE_MODEL_SETTINGS_ID);
        setDisplayName(Tr::tr("Code Model"));
        setCategory(Constants::CPP_SETTINGS_CATEGORY);
        setDisplayCategory(Tr::tr("C++"));
        setCategoryIconPath(":/projectexplorer/images/settingscategory_cpp.png");
        setWidgetCreator(
            [] { return new CppCodeModelSettingsWidget(CppCodeModelSettings::globalInstance().data()); });
    }
};

void setupCppCodeModelSettingsPage()
{
    static CppCodeModelSettingsPage theCppCodeModelSettingsPage;
}

class CppCodeModelProjectSettingsWidget : public ProjectSettingsWidget
{
public:
    CppCodeModelProjectSettingsWidget(const CppCodeModelProjectSettings &settings)
        : m_settings(settings), m_widget(settings.data())
    {
        setGlobalSettingsId(Constants::CPP_CODE_MODEL_SETTINGS_ID);
        const auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(&m_widget);

        setUseGlobalSettings(m_settings.useGlobalSettings());
        m_widget.setEnabled(!useGlobalSettings());
        connect(this, &ProjectSettingsWidget::useGlobalSettingsChanged, this,
                [this](bool checked) {
                    m_widget.setEnabled(!checked);
                    m_settings.setUseGlobalSettings(checked);
                    if (!checked)
                        m_settings.setData(m_widget.data());
                });

        connect(&m_widget, &CppCodeModelSettingsWidget::settingsDataChanged,
                this, [this] { m_settings.setData(m_widget.data()); });
    }

private:
    CppCodeModelProjectSettings m_settings;
    CppCodeModelSettingsWidget m_widget;
};

class CppCodeModelProjectSettingsPanelFactory final : public ProjectPanelFactory
{
public:
    CppCodeModelProjectSettingsPanelFactory()
    {
        setPriority(100);
        setDisplayName(Tr::tr("C++ Code Model"));
        setCreateWidgetFunction([](Project *project) {
            return new CppCodeModelProjectSettingsWidget(project);
        });
    }
};
void setupCppCodeModelProjectSettingsPanel()
{
    static CppCodeModelProjectSettingsPanelFactory factory;
}

} // namespace Internal
} // namespace CppEditor

#include <cppcodemodelsettings.moc>
