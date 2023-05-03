// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "locatorfilter.h"

#include "clientrequesttask.h"
#include "currentdocumentsymbolsrequesttask.h"
#include "documentsymbolcache.h"
#include "languageclient_global.h"
#include "languageclientmanager.h"
#include "languageclienttr.h"

#include <coreplugin/editormanager/editormanager.h>

#include <extensionsystem/pluginmanager.h>

#include <languageserverprotocol/lsptypes.h>

#include <texteditor/textdocument.h>

#include <utils/asynctask.h>
#include <utils/fuzzymatcher.h>

#include <QFutureWatcher>
#include <QRegularExpression>

using namespace Core;
using namespace LanguageServerProtocol;
using namespace Utils;

namespace LanguageClient {

void filterResults(QPromise<void> &promise, const LocatorStorage &storage, Client *client,
                   const QList<SymbolInformation> &results, const QList<SymbolKind> &filter)
{
    const auto doFilter = [&](const SymbolInformation &info) {
        return filter.contains(SymbolKind(info.kind()));
    };
    if (promise.isCanceled())
        return;
    const QList<SymbolInformation> filteredResults = filter.isEmpty() ? results
        : Utils::filtered(results, doFilter);
    const auto generateEntry = [client](const SymbolInformation &info) {
        LocatorFilterEntry entry;
        entry.displayName = info.name();
        if (std::optional<QString> container = info.containerName())
            entry.extraInfo = container.value_or(QString());
        entry.displayIcon = symbolIcon(info.kind());
        entry.linkForEditor = info.location().toLink(client->hostPathMapper());
        return entry;
    };
    storage.reportOutput(Utils::transform(filteredResults, generateEntry));
}

LocatorMatcherTask locatorMatcher(Client *client, int maxResultCount,
                                  const QList<SymbolKind> &filter)
{
    using namespace Tasking;

    TreeStorage<LocatorStorage> storage;
    TreeStorage<QList<SymbolInformation>> resultStorage;

    const auto onQuerySetup = [=](WorkspaceSymbolRequestTask &request) {
        request.setClient(client);
        WorkspaceSymbolParams params;
        params.setQuery(storage->input());
        if (maxResultCount > 0)
            params.setLimit(maxResultCount);
        request.setParams(params);
    };
    const auto onQueryDone = [resultStorage](const WorkspaceSymbolRequestTask &request) {
        const std::optional<LanguageClientArray<SymbolInformation>> result
            = request.response().result();
        if (result.has_value())
            *resultStorage = result->toList();
    };

    const auto onFilterSetup = [=](Async<void> &async) {
        const QList<SymbolInformation> results = *resultStorage;
        if (results.isEmpty())
            return TaskAction::StopWithDone;
        async.setFutureSynchronizer(ExtensionSystem::PluginManager::futureSynchronizer());
        async.setConcurrentCallData(filterResults, *storage, client, results, filter);
        return TaskAction::Continue;
    };

    const Group root {
        Storage(resultStorage),
        SymbolRequest(onQuerySetup, onQueryDone),
        AsyncTask<void>(onFilterSetup)
    };
    return {root, storage};
}

LocatorMatcherTask allSymbolsMatcher(Client *client, int maxResultCount)
{
    return locatorMatcher(client, maxResultCount, {});
}

LocatorMatcherTask classMatcher(Client *client, int maxResultCount)
{
    return locatorMatcher(client, maxResultCount, {SymbolKind::Class, SymbolKind::Struct});
}

LocatorMatcherTask functionMatcher(Client *client, int maxResultCount)
{
    return locatorMatcher(client, maxResultCount,
                          {SymbolKind::Method, SymbolKind::Function, SymbolKind::Constructor});
}

static void filterCurrentResults(QPromise<void> &promise, const LocatorStorage &storage,
                                 const CurrentDocumentSymbolsData &currentSymbolsData)
{
    Q_UNUSED(promise)
    const auto docSymbolModifier = [](LocatorFilterEntry &entry, const DocumentSymbol &info,
                                      const LocatorFilterEntry &parent) {
        Q_UNUSED(parent)
        entry.displayName = info.name();
        if (std::optional<QString> detail = info.detail())
            entry.extraInfo = *detail;
    };
    // TODO: Pass promise into currentSymbols
    storage.reportOutput(LanguageClient::currentDocumentSymbols(storage.input(), currentSymbolsData,
                                                                docSymbolModifier));
}

LocatorMatcherTask currentDocumentMatcher()
{
    using namespace Tasking;

    TreeStorage<LocatorStorage> storage;
    TreeStorage<CurrentDocumentSymbolsData> resultStorage;

    const auto onQuerySetup = [=](CurrentDocumentSymbolsRequestTask &request) {
        Q_UNUSED(request)
    };
    const auto onQueryDone = [resultStorage](const CurrentDocumentSymbolsRequestTask &request) {
        *resultStorage = request.currentDocumentSymbolsData();
    };

    const auto onFilterSetup = [=](Async<void> &async) {
        async.setFutureSynchronizer(ExtensionSystem::PluginManager::futureSynchronizer());
        async.setConcurrentCallData(filterCurrentResults, *storage, *resultStorage);
    };

    const Group root {
        Storage(resultStorage),
        CurrentDocumentSymbolsRequest(onQuerySetup, onQueryDone),
        AsyncTask<void>(onFilterSetup)
    };
    return {root, storage};
}

using MatcherCreator = std::function<Core::LocatorMatcherTask(Client *, int)>;

static MatcherCreator creatorForType(MatcherType type)
{
    switch (type) {
    case MatcherType::AllSymbols: return &allSymbolsMatcher;
    case MatcherType::Classes: return &classMatcher;
    case MatcherType::Functions: return &functionMatcher;
    case MatcherType::CurrentDocumentSymbols: QTC_CHECK(false); return {};
    }
    return {};
}

LocatorMatcherTasks languageClientMatchers(MatcherType type, const QList<Client *> &clients,
                                           int maxResultCount)
{
    if (type == MatcherType::CurrentDocumentSymbols)
        return {currentDocumentMatcher()};
    const MatcherCreator creator = creatorForType(type);
    if (!creator)
        return {};
    LocatorMatcherTasks matchers;
    for (Client *client : clients)
        matchers << creator(client, maxResultCount);
    return matchers;
}

DocumentLocatorFilter::DocumentLocatorFilter()
{
    setId(Constants::LANGUAGECLIENT_DOCUMENT_FILTER_ID);
    setDisplayName(Tr::tr(Constants::LANGUAGECLIENT_DOCUMENT_FILTER_DISPLAY_NAME));
    setDescription(Tr::tr(Constants::LANGUAGECLIENT_DOCUMENT_FILTER_DESCRIPTION));
    setDefaultShortcutString(".");
    setDefaultIncludedByDefault(false);
    setPriority(ILocatorFilter::Low);
    connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
            this, &DocumentLocatorFilter::updateCurrentClient);
    connect(LanguageClientManager::instance(), &LanguageClientManager::clientInitialized,
            this, &DocumentLocatorFilter::updateCurrentClient);
}

LocatorMatcherTasks DocumentLocatorFilter::matchers()
{
    return {currentDocumentMatcher()};
}

void DocumentLocatorFilter::updateCurrentClient()
{
    resetSymbols();
    disconnect(m_resetSymbolsConnection);

    TextEditor::TextDocument *document = TextEditor::TextDocument::currentTextDocument();
    if (Client *client = LanguageClientManager::clientForDocument(document);
            client && (client->locatorsEnabled() || m_forced)) {

        setEnabled(!m_forced);
        if (m_symbolCache != client->documentSymbolCache()) {
            disconnect(m_updateSymbolsConnection);
            m_symbolCache = client->documentSymbolCache();
            m_updateSymbolsConnection = connect(m_symbolCache, &DocumentSymbolCache::gotSymbols,
                                                this, &DocumentLocatorFilter::updateSymbols);
        }
        m_resetSymbolsConnection = connect(document, &IDocument::contentsChanged,
                                           this, &DocumentLocatorFilter::resetSymbols);
        m_currentUri = client->hostPathToServerUri(document->filePath());
        m_pathMapper = client->hostPathMapper();
    } else {
        disconnect(m_updateSymbolsConnection);
        m_symbolCache.clear();
        m_currentUri.clear();
        setEnabled(false);
        m_pathMapper = DocumentUri::PathMapper();
    }
}

void DocumentLocatorFilter::updateSymbols(const DocumentUri &uri,
                                          const DocumentSymbolsResult &symbols)
{
    if (uri != m_currentUri)
        return;
    QMutexLocker locker(&m_mutex);
    m_currentSymbols = symbols;
    emit symbolsUpToDate(QPrivateSignal());
}

void DocumentLocatorFilter::resetSymbols()
{
    QMutexLocker locker(&m_mutex);
    m_currentSymbols.reset();
}

static LocatorFilterEntry entryForSymbolInfo(const SymbolInformation &info,
                                             DocumentUri::PathMapper pathMapper)
{
    LocatorFilterEntry entry;
    entry.displayName = info.name();
    if (std::optional<QString> container = info.containerName())
        entry.extraInfo = container.value_or(QString());
    entry.displayIcon = symbolIcon(info.kind());
    entry.linkForEditor = info.location().toLink(pathMapper);
    return entry;
}

LocatorFilterEntries entriesForSymbolsInfo(const QList<SymbolInformation> &infoList,
    const QRegularExpression &regexp, const DocumentUri::PathMapper &pathMapper)
{
    QTC_ASSERT(pathMapper, return {});
    LocatorFilterEntries entries;
    for (const SymbolInformation &info : infoList) {
        if (regexp.match(info.name()).hasMatch())
            entries << LanguageClient::entryForSymbolInfo(info, pathMapper);
    }
    return entries;
}

LocatorFilterEntries entriesForDocSymbols(const QList<DocumentSymbol> &infoList,
    const QRegularExpression &regexp, const FilePath &filePath,
    const DocSymbolModifier &docSymbolModifier, const LocatorFilterEntry &parent = {})
{
    LocatorFilterEntries entries;
    for (const DocumentSymbol &info : infoList) {
        const QList<DocumentSymbol> children = info.children().value_or(QList<DocumentSymbol>());
        const bool hasMatch = regexp.match(info.name()).hasMatch();
        LocatorFilterEntry entry;
        if (hasMatch) {
            entry.displayIcon = LanguageClient::symbolIcon(info.kind());
            const Position &pos = info.range().start();
            entry.linkForEditor = {filePath, pos.line() + 1, pos.character()};
            docSymbolModifier(entry, info, parent);
            entries << entry;
        } else {
            entry = parent;
        }
        entries << entriesForDocSymbols(children, regexp, filePath, docSymbolModifier, entry);
    }
    return entries;
}

Core::LocatorFilterEntries currentDocumentSymbols(const QString &input,
                                                  const CurrentDocumentSymbolsData &currentSymbolsData,
                                                  const DocSymbolModifier &docSymbolModifier)
{
    const FuzzyMatcher::CaseSensitivity caseSensitivity
        = ILocatorFilter::caseSensitivity(input) == Qt::CaseSensitive
              ? FuzzyMatcher::CaseSensitivity::CaseSensitive
              : FuzzyMatcher::CaseSensitivity::CaseInsensitive;
    const QRegularExpression regExp = FuzzyMatcher::createRegExp(input, caseSensitivity);
    if (!regExp.isValid())
        return {};

    if (auto list = std::get_if<QList<DocumentSymbol>>(&currentSymbolsData.m_symbols))
        return entriesForDocSymbols(*list, regExp, currentSymbolsData.m_filePath, docSymbolModifier);
    else if (auto list = std::get_if<QList<SymbolInformation>>(&currentSymbolsData.m_symbols))
        return entriesForSymbolsInfo(*list, regExp, currentSymbolsData.m_pathMapper);
    return {};
}

void DocumentLocatorFilter::prepareSearch(const QString &/*entry*/)
{
    QMutexLocker locker(&m_mutex);
    m_currentFilePath = m_pathMapper ? m_currentUri.toFilePath(m_pathMapper) : FilePath();
    if (m_symbolCache && !m_currentSymbols.has_value()) {
        locker.unlock();
        m_symbolCache->requestSymbols(m_currentUri, Schedule::Now);
    }
}

QList<LocatorFilterEntry> DocumentLocatorFilter::matchesFor(
    QFutureInterface<LocatorFilterEntry> &future, const QString &entry)
{
    const auto docSymbolModifier = [](LocatorFilterEntry &entry, const DocumentSymbol &info,
                                      const LocatorFilterEntry &parent) {
        Q_UNUSED(parent)
        entry.displayName = info.name();
        if (std::optional<QString> detail = info.detail())
            entry.extraInfo = *detail;
    };
    return matchesForImpl(future, entry, docSymbolModifier);
}

QList<LocatorFilterEntry> DocumentLocatorFilter::matchesForImpl(
    QFutureInterface<LocatorFilterEntry> &future, const QString &entry,
    const DocSymbolModifier &docSymbolModifier)
{
    const FuzzyMatcher::CaseSensitivity caseSensitivity
        = ILocatorFilter::caseSensitivity(entry) == Qt::CaseSensitive
              ? FuzzyMatcher::CaseSensitivity::CaseSensitive
              : FuzzyMatcher::CaseSensitivity::CaseInsensitive;
    const QRegularExpression regExp = FuzzyMatcher::createRegExp(entry, caseSensitivity);
    if (!regExp.isValid())
        return {};

    QMutexLocker locker(&m_mutex);
    if (!m_symbolCache)
        return {};
    if (!m_currentSymbols.has_value()) {
        QEventLoop loop;
        connect(this, &DocumentLocatorFilter::symbolsUpToDate, &loop, [&] { loop.exit(1); });
        QFutureWatcher<LocatorFilterEntry> watcher;
        connect(&watcher, &QFutureWatcher<LocatorFilterEntry>::canceled, &loop, &QEventLoop::quit);
        watcher.setFuture(future.future());
        locker.unlock();
        if (!loop.exec())
            return {};
        locker.relock();
    }

    QTC_ASSERT(m_currentSymbols.has_value(), return {});

    if (auto list = std::get_if<QList<DocumentSymbol>>(&*m_currentSymbols))
        return entriesForDocSymbols(*list, regExp, m_currentFilePath, docSymbolModifier);
    else if (auto list = std::get_if<QList<SymbolInformation>>(&*m_currentSymbols))
        return entriesForSymbolsInfo(*list, regExp, m_pathMapper);

    return {};
}

WorkspaceLocatorFilter::WorkspaceLocatorFilter()
    : WorkspaceLocatorFilter(QVector<SymbolKind>())
{}

LocatorMatcherTasks WorkspaceLocatorFilter::matchers()
{
    return languageClientMatchers(MatcherType::AllSymbols,
        Utils::filtered(LanguageClientManager::clients(), &Client::locatorsEnabled));
}

WorkspaceLocatorFilter::WorkspaceLocatorFilter(const QVector<SymbolKind> &filter)
    : m_filterKinds(filter)
{
    setId(Constants::LANGUAGECLIENT_WORKSPACE_FILTER_ID);
    setDisplayName(Tr::tr(Constants::LANGUAGECLIENT_WORKSPACE_FILTER_DISPLAY_NAME));
    setDescription(Tr::tr(Constants::LANGUAGECLIENT_WORKSPACE_FILTER_DESCRIPTION));
    setDefaultShortcutString(":");
    setDefaultIncludedByDefault(false);
    setPriority(ILocatorFilter::Low);
}

void WorkspaceLocatorFilter::prepareSearch(const QString &entry)
{
    prepareSearchForClients(entry, Utils::filtered(LanguageClientManager::clients(),
                                                   &Client::locatorsEnabled));
}

void WorkspaceLocatorFilter::prepareSearchForClients(const QString &entry,
                                                     const QList<Client *> &clients)
{
    m_pendingRequests.clear();
    m_results.clear();

    if (clients.isEmpty())
        return;

    WorkspaceSymbolParams params;
    params.setQuery(entry);
    if (m_maxResultCount > 0)
        params.setLimit(m_maxResultCount);

    QMutexLocker locker(&m_mutex);
    for (auto client : std::as_const(clients)) {
        if (!client->reachable())
            continue;
        std::optional<std::variant<bool, WorkDoneProgressOptions>> capability
            = client->capabilities().workspaceSymbolProvider();
        if (!capability.has_value())
            continue;
        if (std::holds_alternative<bool>(*capability) && !std::get<bool>(*capability))
            continue;
        WorkspaceSymbolRequest request(params);
        request.setResponseCallback(
            [this, client](const WorkspaceSymbolRequest::Response &response) {
                handleResponse(client, response);
            });
        m_pendingRequests[client] = request.id();
        client->sendMessage(request);
    }
}

QList<LocatorFilterEntry> WorkspaceLocatorFilter::matchesFor(
    QFutureInterface<LocatorFilterEntry> &future, const QString & /*entry*/)
{
    QMutexLocker locker(&m_mutex);
    if (!m_pendingRequests.isEmpty()) {
        QEventLoop loop;
        connect(this, &WorkspaceLocatorFilter::allRequestsFinished, &loop, [&] { loop.exit(1); });
        QFutureWatcher<LocatorFilterEntry> watcher;
        connect(&watcher,
                &QFutureWatcher<LocatorFilterEntry>::canceled,
                &loop,
                &QEventLoop::quit);
        watcher.setFuture(future.future());
        locker.unlock();
        if (!loop.exec())
            return {};

        locker.relock();
    }

    if (!m_filterKinds.isEmpty()) {
        m_results = Utils::filtered(m_results, [&](const SymbolInfoWithPathMapper &info) {
            return m_filterKinds.contains(SymbolKind(info.symbol.kind()));
        });
    }
    auto generateEntry = [](const SymbolInfoWithPathMapper &info) {
        return entryForSymbolInfo(info.symbol, info.mapper);
    };
    return Utils::transform(m_results, generateEntry).toList();
}

void WorkspaceLocatorFilter::handleResponse(Client *client,
                                            const WorkspaceSymbolRequest::Response &response)
{
    QMutexLocker locker(&m_mutex);
    m_pendingRequests.remove(client);
    auto result = response.result().value_or(LanguageClientArray<SymbolInformation>());
    if (!result.isNull())
        m_results.append(
            Utils::transform(result.toList(), [client](const SymbolInformation &info) {
                return SymbolInfoWithPathMapper{info, client->hostPathMapper()};
            }));
    if (m_pendingRequests.isEmpty())
        emit allRequestsFinished(QPrivateSignal());
}

WorkspaceClassLocatorFilter::WorkspaceClassLocatorFilter()
    : WorkspaceLocatorFilter({SymbolKind::Class, SymbolKind::Struct})
{
    setId(Constants::LANGUAGECLIENT_WORKSPACE_CLASS_FILTER_ID);
    setDisplayName(Tr::tr(Constants::LANGUAGECLIENT_WORKSPACE_CLASS_FILTER_DISPLAY_NAME));
    setDescription(Tr::tr(Constants::LANGUAGECLIENT_WORKSPACE_CLASS_FILTER_DESCRIPTION));
    setDefaultShortcutString("c");
}

LocatorMatcherTasks WorkspaceClassLocatorFilter::matchers()
{
    return languageClientMatchers(MatcherType::Classes,
        Utils::filtered(LanguageClientManager::clients(), &Client::locatorsEnabled));
}

WorkspaceMethodLocatorFilter::WorkspaceMethodLocatorFilter()
    : WorkspaceLocatorFilter({SymbolKind::Method, SymbolKind::Function, SymbolKind::Constructor})
{
    setId(Constants::LANGUAGECLIENT_WORKSPACE_METHOD_FILTER_ID);
    setDisplayName(Tr::tr(Constants::LANGUAGECLIENT_WORKSPACE_METHOD_FILTER_DISPLAY_NAME));
    setDescription(Tr::tr(Constants::LANGUAGECLIENT_WORKSPACE_METHOD_FILTER_DESCRIPTION));
    setDefaultShortcutString("m");
}

LocatorMatcherTasks WorkspaceMethodLocatorFilter::matchers()
{
    return languageClientMatchers(MatcherType::Functions,
        Utils::filtered(LanguageClientManager::clients(), &Client::locatorsEnabled));
}

} // namespace LanguageClient
