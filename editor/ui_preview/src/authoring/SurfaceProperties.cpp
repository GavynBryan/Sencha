#include "authoring/SurfaceProperties.h"

#include "authoring/DocumentLibrary.h"
#include "authoring/UiPreviewSession.h"
#include "properties/SettingProperties.h"

#include <assets/hotreload/SourceReloadRoots.h>

#include <cmath>
#include <format>

namespace
{
constexpr double kMinPixels = 64.0;
constexpr double kMaxPixels = 8192.0;
constexpr double kMinScale = 0.5;
constexpr double kMaxScale = 4.0;
} // namespace

std::vector<PropertyNode> SurfaceProperties::Nodes() const
{
    const RenderExtent size = Session.Resolution();
    std::vector<PropertyNode> nodes{
        NumberProperty("width", "Width", size.Width, kMinPixels, kMaxPixels, 1.0,
                       "Surface width in pixels: what the document is laid out and rendered at"),
        NumberProperty("height", "Height", size.Height, kMinPixels, kMaxPixels, 1.0,
                       "Surface height in pixels"),
        NumberProperty("scale", "Scale", Session.DisplayScale(), kMinScale, kMaxScale, 0.05,
                       "Display scale: how many surface pixels one dp is"),
    };
    if (const DocumentEntry* document = Document())
    {
        nodes.push_back(NoteProperty("model", "Model", document->ModelName.empty() ? "None, a static document"
                                                                                   : document->ModelName));
        nodes.push_back(NoteProperty("cooked", "Cooked", document->Cooked ? "Yes" : "No, the layer cannot open it"));
        nodes.push_back(NoteProperty("preview_model", "Preview model",
                                     document->HasPreviewModel ? "Beside the document" : "None yet"));
    }
    if (Watch != nullptr)
        nodes.push_back(NoteProperty("watching", "Watching",
                                     std::format("{} roots, {} files", Watch->RootCount(), Watch->WatchedFileCount())));
    return nodes;
}

bool SurfaceProperties::BeginEdit(std::string_view key)
{
    if (key != "width" && key != "height" && key != "scale")
        return false;
    Editing = Before{ Session.Resolution(), Session.DisplayScale() };
    return true;
}

PropertyEditResult SurfaceProperties::CommitEdit(std::string_view key, std::string_view value)
{
    const PropertyEditResult result = Apply(key, value);
    Editing.reset();
    return result;
}

void SurfaceProperties::CancelEdit(std::string_view)
{
    if (!Editing)
        return;
    Session.SetResolution(Editing->Size);
    Session.SetDisplayScale(Editing->Scale);
    Editing.reset();
}

PropertyEditResult SurfaceProperties::Apply(std::string_view key, std::string_view value)
{
    if (key == "scale")
    {
        const std::optional<double> scale = ParseNumber(value, kMinScale, kMaxScale);
        if (!scale)
            return PropertyEditResult::Refused;
        if (static_cast<float>(*scale) == Session.DisplayScale())
            return PropertyEditResult::Unchanged;
        Session.SetDisplayScale(static_cast<float>(*scale));
        return PropertyEditResult::Applied;
    }
    if (key != "width" && key != "height")
        return PropertyEditResult::Gone;
    const std::optional<double> pixels = ParseNumber(value, kMinPixels, kMaxPixels);
    if (!pixels)
        return PropertyEditResult::Refused;
    RenderExtent size = Session.Resolution();
    (key == "width" ? size.Width : size.Height) = static_cast<std::uint32_t>(std::lround(*pixels));
    if (size == Session.Resolution())
        return PropertyEditResult::Unchanged;
    Session.SetResolution(size);
    return PropertyEditResult::Applied;
}
