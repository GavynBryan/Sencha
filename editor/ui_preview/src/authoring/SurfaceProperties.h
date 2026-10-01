#pragma once

#include "properties/PropertySource.h"

#include <graphics/RenderExtent.h>

#include <functional>
#include <optional>

class SourceReloadRoots;
class UiPreviewSession;
struct DocumentEntry;

//=============================================================================
// SurfaceProperties
//
// The surface the open document is laid out on -- its size in pixels and how
// many of them a dp is, both live -- and what is known about the document:
// the model it binds, whether it is cooked, whether it has a preview model,
// and how much of the library is being watched for saves.
//=============================================================================
class SurfaceProperties final : public IPropertySource
{
public:
    SurfaceProperties(UiPreviewSession& session, std::function<const DocumentEntry*()> document,
                      const SourceReloadRoots* watch)
        : Session(session)
        , Document(std::move(document))
        , Watch(watch)
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override { return Apply(key, value); }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;

private:
    PropertyEditResult Apply(std::string_view key, std::string_view value);

    UiPreviewSession& Session;
    std::function<const DocumentEntry*()> Document;
    const SourceReloadRoots* Watch = nullptr;
    // What the surface was when an edit began, put back if it is cancelled.
    struct Before
    {
        RenderExtent Size;
        float Scale = 1.0f;
    };
    std::optional<Before> Editing;
};
