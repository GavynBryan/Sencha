#pragma once

#include "commands/CommandStack.h"

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class DataDocument
{
public:
    static std::unique_ptr<DataDocument> Open(const std::filesystem::path& file,
                                              std::string virtualPath,
                                              const DataAssetTypeRegistry& types,
                                              const DataSchemaRegistry& schemas,
                                              std::string* error = nullptr);

    static std::unique_ptr<DataDocument> Create(const std::filesystem::path& file,
                                                std::string virtualPath,
                                                const DataAssetTypeRegistration& type,
                                                const DataSchema& schema);

    DataDocument(const DataDocument&) = delete;
    DataDocument& operator=(const DataDocument&) = delete;

    [[nodiscard]] const std::filesystem::path& FilePath() const { return File; }
    [[nodiscard]] const std::string& VirtualPath() const { return AssetPath; }
    [[nodiscard]] const std::string& Subtype() const { return TypeName; }
    [[nodiscard]] uint32_t Version() const { return FileVersion; }

    [[nodiscard]] const JsonValue& Root() const { return WorkingRoot; }
    [[nodiscard]] JsonValue CopyRoot() const { return WorkingRoot; }
    [[nodiscard]] const JsonValue* Data() const { return WorkingRoot.Find("data"); }

    void ReplaceRoot(JsonValue root);
    void Undo();
    void Redo();
    [[nodiscard]] bool CanUndo() const { return History.CanUndo(); }
    [[nodiscard]] bool CanRedo() const { return History.CanRedo(); }

    // Live edit transaction. A drag reports a change every frame; without a
    // scope around it each frame would land its own full-document command. The
    // baseline taken at Begin is what Commit and Cancel undo to, so one
    // interaction produces one undo entry.
    //
    // Every path that can interrupt an interaction (escape, switching tabs,
    // closing the document, saving, shutdown) must reach Commit or Cancel.
    void BeginEdit();
    void PreviewRoot(JsonValue root);
    void CommitEdit();
    void CancelEdit();
    [[nodiscard]] bool IsEditing() const { return Editing; }

    // Bumped on every content change, including previews. Derived views cache
    // against it instead of re-serializing the document to detect edits.
    [[nodiscard]] uint64_t Revision() const { return ContentRevision; }

    [[nodiscard]] bool IsDirty() const { return Dirty; }
    [[nodiscard]] bool IsExternallyModified() const;

    [[nodiscard]] bool Save(std::string* error = nullptr);
    [[nodiscard]] bool Reload(const DataAssetTypeRegistry& types,
                              const DataSchemaRegistry& schemas,
                              std::string* error = nullptr);

    void Validate(const DataAssetTypeRegistry& types,
                  const DataSchemaRegistry& schemas);
    [[nodiscard]] const std::vector<DataValidationError>& ValidationErrors() const
    {
        return Errors;
    }
    [[nodiscard]] bool IsSemanticallyValid() const { return Errors.empty(); }

private:
    class ReplaceRootCommand;

    DataDocument(std::filesystem::path file,
                 std::string virtualPath,
                 JsonValue root);

    void ApplyRoot(JsonValue root);
    void RefreshEnvelopeIdentity();
    void RefreshDirty();
    void RefreshTimestamp();

    std::filesystem::path File;
    std::string AssetPath;
    std::string TypeName;
    uint32_t FileVersion = 0;

    JsonValue WorkingRoot;
    std::string SavedText;
    std::filesystem::file_time_type LastWriteTime{};
    bool HasWriteTime = false;
    bool Dirty = false;

    JsonValue EditBaseline;
    bool Editing = false;
    uint64_t ContentRevision = 0;

    CommandStack History;
    std::vector<DataValidationError> Errors;
};

[[nodiscard]] JsonValue CreateDefaultDataValue(const DataFieldSchema& field);
