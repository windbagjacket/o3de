/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Atom/RPI.Edit/Common/AssetUtils.h>
#include <AtomToolsFramework/Document/AtomToolsDocument.h>
#include <AtomToolsFramework/Document/AtomToolsDocumentNotificationBus.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzToolsFramework/SourceControl/SourceControlAPI.h>

namespace AtomToolsFramework
{
    void AtomToolsDocument::Reflect(AZ::ReflectContext* context)
    {
        if (auto serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<AtomToolsDocument>()
                ->Version(0);
        }

        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<AtomToolsDocumentRequestBus>("AtomToolsDocumentRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Editor")
                ->Attribute(AZ::Script::Attributes::Module, "atomtools")
                ->Event("GetAbsolutePath", &AtomToolsDocumentRequestBus::Events::GetAbsolutePath)
                ->Event("Open", &AtomToolsDocumentRequestBus::Events::Open)
                ->Event("Reopen", &AtomToolsDocumentRequestBus::Events::Reopen)
                ->Event("Close", &AtomToolsDocumentRequestBus::Events::Close)
                ->Event("Save", &AtomToolsDocumentRequestBus::Events::Save)
                ->Event("SaveAsChild", &AtomToolsDocumentRequestBus::Events::SaveAsChild)
                ->Event("SaveAsCopy", &AtomToolsDocumentRequestBus::Events::SaveAsCopy)
                ->Event("IsOpen", &AtomToolsDocumentRequestBus::Events::IsOpen)
                ->Event("IsModified", &AtomToolsDocumentRequestBus::Events::IsModified)
                ->Event("IsSavable", &AtomToolsDocumentRequestBus::Events::IsSavable)
                ->Event("CanUndo", &AtomToolsDocumentRequestBus::Events::CanUndo)
                ->Event("CanRedo", &AtomToolsDocumentRequestBus::Events::CanRedo)
                ->Event("Undo", &AtomToolsDocumentRequestBus::Events::Undo)
                ->Event("Redo", &AtomToolsDocumentRequestBus::Events::Redo)
                ->Event("BeginEdit", &AtomToolsDocumentRequestBus::Events::BeginEdit)
                ->Event("EndEdit", &AtomToolsDocumentRequestBus::Events::EndEdit)
                ;
        }
    }

    AtomToolsDocument::AtomToolsDocument(const AZ::Crc32& toolId)
        : m_toolId(toolId)
    {
        AtomToolsDocumentRequestBus::Handler::BusConnect(m_id);
        AtomToolsDocumentNotificationBus::Event(m_toolId, &AtomToolsDocumentNotificationBus::Events::OnDocumentCreated, m_id);
    }

    AtomToolsDocument::~AtomToolsDocument()
    {
        AtomToolsDocumentNotificationBus::Event(m_toolId, &AtomToolsDocumentNotificationBus::Events::OnDocumentDestroyed, m_id);
        AtomToolsDocumentRequestBus::Handler::BusDisconnect();
        AzToolsFramework::AssetSystemBus::Handler::BusDisconnect();
    }

    const AZ::Uuid& AtomToolsDocument::GetId() const
    {
        return m_id;
    }

    AZStd::string_view AtomToolsDocument::GetAbsolutePath() const
    {
        return m_absolutePath;
    }

    AZStd::vector<DocumentObjectInfo> AtomToolsDocument::GetObjectInfo() const
    {
        AZ_Warning("AtomToolsDocument", false, "%s not implemented.", __FUNCTION__);
        return AZStd::vector<DocumentObjectInfo>();
    }

    bool AtomToolsDocument::Open(AZStd::string_view loadPath)
    {
        Clear();

        m_absolutePath = loadPath;
        if (!AzFramework::StringFunc::Path::Normalize(m_absolutePath))
        {
            AZ_Error("AtomToolsDocument", false, "Document path could not be normalized: '%s'.", m_absolutePath.c_str());
            return OpenFailed();
        }

        if (AzFramework::StringFunc::Path::IsRelative(m_absolutePath.c_str()))
        {
            AZ_Error("AtomToolsDocument", false, "Document path must be absolute: '%s'.", m_absolutePath.c_str());
            return OpenFailed();
        }

        return true;
    }

    bool AtomToolsDocument::Reopen()
    {
        if (!ReopenRecordState())
        {
            return false;
        }

        const auto loadPath = m_absolutePath;
        if (!Open(loadPath))
        {
            return false;
        }

        if (!ReopenRestoreState())
        {
            return false;
        }

        AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
            m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentModified, m_id);
        AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
            m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentUndoStateChanged, m_id);
        return true;
    }

    bool AtomToolsDocument::Save()
    {
        m_savePathNormalized = m_absolutePath;
        if (!AzFramework::StringFunc::Path::Normalize(m_savePathNormalized))
        {
            AZ_Error("AtomToolsDocument", false, "Document save path could not be normalized: '%s'.", m_savePathNormalized.c_str());
            return SaveFailed();
        }

        if (!IsOpen())
        {
            AZ_Error("AtomToolsDocument", false, "Document is not open to be saved: '%s'.", m_absolutePath.c_str());
            return SaveFailed();
        }

        if (!IsSavable())
        {
            AZ_Error("AtomToolsDocument", false, "Document type can not be saved: '%s'.", m_absolutePath.c_str());
            return SaveFailed();
        }

        return true;
    }

    bool AtomToolsDocument::SaveAsCopy(AZStd::string_view savePath)
    {
        m_savePathNormalized = savePath;
        if (!AzFramework::StringFunc::Path::Normalize(m_savePathNormalized))
        {
            AZ_Error("AtomToolsDocument", false, "Document save path could not be normalized: '%s'.", m_savePathNormalized.c_str());
            return SaveFailed();
        }

        if (!IsOpen())
        {
            AZ_Error("AtomToolsDocument", false, "Document is not open to be saved: '%s'.", m_absolutePath.c_str());
            return SaveFailed();
        }

        if (!IsSavable())
        {
            AZ_Error("AtomToolsDocument", false, "Document type can not be saved: '%s'.", m_absolutePath.c_str());
            return SaveFailed();
        }

        return true;
    }

    bool AtomToolsDocument::SaveAsChild(AZStd::string_view savePath)
    {
        m_savePathNormalized = savePath;
        if (!AzFramework::StringFunc::Path::Normalize(m_savePathNormalized))
        {
            AZ_Error("AtomToolsDocument", false, "Document save path could not be normalized: '%s'.", m_savePathNormalized.c_str());
            return SaveFailed();
        }

        if (!IsOpen())
        {
            AZ_Error("AtomToolsDocument", false, "Document is not open to be saved: '%s'.", m_absolutePath.c_str());
            return SaveFailed();
        }

        if (m_absolutePath == m_savePathNormalized || m_sourceDependencies.find(m_savePathNormalized) != m_sourceDependencies.end())
        {
            AZ_Error("AtomToolsDocument", false, "Document can not be saved over a dependancy: '%s'.", m_savePathNormalized.c_str());
            return SaveFailed();
        }

        return true;
    }

    bool AtomToolsDocument::Close()
    {
        if (!IsOpen())
        {
            AZ_Error("AtomToolsDocument", false, "Document is not open.");
            return false;
        }

        AZ_TracePrintf("AtomToolsDocument", "Document closed: '%s'.\n", m_absolutePath.c_str());

        AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
            m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentClosed, m_id);

        // Clearing after notification so paths are still available
        Clear();
        return true;
    }

    bool AtomToolsDocument::IsOpen() const
    {
        return !m_id.IsNull() && !m_absolutePath.empty();
    }

    bool AtomToolsDocument::IsModified() const
    {
        return false;
    }

    bool AtomToolsDocument::IsSavable() const
    {
        return false;
    }

    bool AtomToolsDocument::CanUndo() const
    {
        // Undo will only be allowed if something has been recorded and we're not at the beginning of history
        return IsOpen() && !m_undoHistory.empty() && m_undoHistoryIndex > 0;
    }

    bool AtomToolsDocument::CanRedo() const
    {
        // Redo will only be allowed if something has been recorded and we're not at the end of history
        return IsOpen() && !m_undoHistory.empty() && m_undoHistoryIndex < m_undoHistory.size();
    }

    bool AtomToolsDocument::Undo()
    {
        if (CanUndo())
        {
            // The history index is one beyond the last executed command. Decrement the index then execute undo.
            m_undoHistory[--m_undoHistoryIndex].first();
            AZ_TracePrintf("AtomToolsDocument", "Document undo: '%s'.\n", m_absolutePath.c_str());
            AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
                m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentUndoStateChanged, m_id);
            return true;
        }
        return false;
    }

    bool AtomToolsDocument::Redo()
    {
        if (CanRedo())
        {
            // Execute the current redo command then move the history index to the next position.
            m_undoHistory[m_undoHistoryIndex++].second();
            AZ_TracePrintf("AtomToolsDocument", "Document redo: '%s'.\n", m_absolutePath.c_str());
            AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
                m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentUndoStateChanged, m_id);
            return true;
        }
        return false;
    }

    bool AtomToolsDocument::BeginEdit()
    {
        AZ_Warning("AtomToolsDocument", false, "%s not implemented.", __FUNCTION__);
        return false;
    }

    bool AtomToolsDocument::EndEdit()
    {
        AZ_Warning("AtomToolsDocument", false, "%s not implemented.", __FUNCTION__);
        return false;
    }

    void AtomToolsDocument::Clear()
    {
        AzToolsFramework::AssetSystemBus::Handler::BusDisconnect();

        m_absolutePath.clear();
        m_sourceDependencies.clear();
        m_ignoreSourceFileChangeToSelf = {};
        m_undoHistory.clear();
        m_undoHistoryIndex = {};
    }

    bool AtomToolsDocument::OpenSucceeded()
    {
        AZ_TracePrintf("AtomToolsDocument", "Document opened: '%s'.\n", m_absolutePath.c_str());
        AzToolsFramework::AssetSystemBus::Handler::BusConnect();
        AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
            m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentOpened, m_id);
        return true;
    }

    bool AtomToolsDocument::OpenFailed()
    {
        AZ_TracePrintf("AtomToolsDocument", "Document could not opened: '%s'.\n", m_absolutePath.c_str());
        Clear();
        return false;
    }

    bool AtomToolsDocument::SaveSucceeded()
    {
        m_ignoreSourceFileChangeToSelf = true;

        AZ_TracePrintf("AtomToolsDocument", "Document saved: '%s'.\n", m_savePathNormalized.c_str());

        // Auto add or checkout saved file
        AzToolsFramework::SourceControlCommandBus::Broadcast(
            &AzToolsFramework::SourceControlCommandBus::Events::RequestEdit, m_savePathNormalized.c_str(), true,
            [](bool, const AzToolsFramework::SourceControlFileInfo&) {});

        AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
            m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentSaved, m_id);
        return true;
    }

    bool AtomToolsDocument::SaveFailed()
    {
        AZ_TracePrintf("AtomToolsDocument", "Document not saved: '%s'.\n", m_savePathNormalized.c_str());
        return false;
    }

    bool AtomToolsDocument::ReopenRecordState()
    {
        m_undoHistoryBeforeReopen = m_undoHistory;
        m_undoHistoryIndexBeforeReopen = m_undoHistoryIndex;
        return true;
    }

    bool AtomToolsDocument::ReopenRestoreState()
    {
        m_undoHistory = m_undoHistoryBeforeReopen;
        m_undoHistoryIndex = m_undoHistoryIndexBeforeReopen;
        m_undoHistoryBeforeReopen = {};
        m_undoHistoryIndexBeforeReopen = {};
        return true;
    }

    void AtomToolsDocument::AddUndoRedoHistory(const UndoRedoFunction& undoCommand, const UndoRedoFunction& redoCommand)
    {
        // Wipe any state beyond the current history index
        m_undoHistory.erase(m_undoHistory.begin() + m_undoHistoryIndex, m_undoHistory.end());

        // Add undo and redo operations using functions that capture state and restore it when executed
        m_undoHistory.emplace_back(undoCommand, redoCommand);

        // Assign the index to the end of history
        m_undoHistoryIndex = aznumeric_cast<int>(m_undoHistory.size());
        AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
            m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentUndoStateChanged, m_id);
    }

    void AtomToolsDocument::SourceFileChanged(AZStd::string relativePath, AZStd::string scanFolder, [[maybe_unused]] AZ::Uuid sourceUUID)
    {
        const auto sourcePath = AZ::RPI::AssetUtils::ResolvePathReference(scanFolder, relativePath);

        if (m_absolutePath == sourcePath)
        {
            // ignore notifications caused by saving the open document
            if (!m_ignoreSourceFileChangeToSelf)
            {
                AZ_TracePrintf("AtomToolsDocument", "Document changed externally: '%s'.\n", m_absolutePath.c_str());
                AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
                    m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentExternallyModified, m_id);
            }
            m_ignoreSourceFileChangeToSelf = false;
        }
        else if (m_sourceDependencies.find(sourcePath) != m_sourceDependencies.end())
        {
            AZ_TracePrintf("AtomToolsDocument", "Document dependency changed: '%s'.\n", m_absolutePath.c_str());
            AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
                m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentDependencyModified, m_id);
        }
    }

} // namespace AtomToolsFramework
