#include "TexturesPanel.h"

#include "ui/ScopedPanel.h"

#include <core/assets/AssetRegistry.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <string_view>
#include <utility>

namespace
{
    // Preview box height: uniform for every texture, aspect preserved inside.
    constexpr float kPreviewBoxHeight = 192.0f;

    bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty())
            return true;
        const auto it = std::search(
            haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b)
            { return std::tolower(static_cast<unsigned char>(a))
                  == std::tolower(static_cast<unsigned char>(b)); });
        return it != haystack.end();
    }

    // Source textures only: the registry also holds the physical cooked
    // artifacts (asset://...png.stex from the .cooked scan), which are build
    // outputs, not authorable sources.
    bool IsSourceTexture(const AssetRecord& record)
    {
        if (record.Type != AssetType::Texture)
            return false;
        constexpr std::string_view cooked = ".stex";
        const std::string_view path = record.Path;
        return path.size() < cooked.size()
            || path.substr(path.size() - cooked.size()) != cooked;
    }

    // "asset://textures/dev/foo.png" -> {"textures/dev", "foo.png"}
    std::pair<std::string_view, std::string_view> SplitFolder(std::string_view virtualPath)
    {
        constexpr std::string_view scheme = "asset://";
        if (virtualPath.starts_with(scheme))
            virtualPath.remove_prefix(scheme.size());
        const std::size_t slash = virtualPath.rfind('/');
        if (slash == std::string_view::npos)
            return { std::string_view{}, virtualPath };
        return { virtualPath.substr(0, slash), virtualPath.substr(slash + 1) };
    }
}

TexturesPanel::TexturesPanel(const AssetRegistry& registry,
                             std::vector<std::string> contentRoots,
                             RecookFn recook,
                             std::unique_ptr<ImGuiTextureBinding> preview,
                             CreateMaterialFn createMaterial)
    : Registry(registry)
    , ContentRoots(std::move(contentRoots))
    , Recook(std::move(recook))
    , Preview(std::move(preview))
    , CreateMaterial(std::move(createMaterial))
{
}

void TexturesPanel::ReleasePreviewResources()
{
    if (Preview)
        Preview->Release();
}

void TexturesPanel::SelectTexture(const std::string& virtualPath)
{
    Selected = virtualPath;
    Draft = TextureImportSettings{};
    SourceMissing = false;
    Status.clear();
    SetVisible(true);

    const auto source = ResolveTextureSource(ContentRoots, virtualPath);
    if (!source)
    {
        SourceMissing = true;
        Status = "no source file under any content root (cooked-only asset?)";
        return;
    }

    std::string error;
    Draft = LoadTextureImportSettingsFor(*source, &error);
    if (!error.empty())
        Status = "sidecar unreadable, showing defaults: " + error;
}

void TexturesPanel::ApplyDraft()
{
    const auto source = ResolveTextureSource(ContentRoots, Selected);
    if (!source)
    {
        Status = "no source file under any content root";
        return;
    }

    std::string error;
    if (!SaveTextureImportSettingsFor(*source, Draft, &error))
    {
        Status = "save failed: " + error;
        return;
    }
    if (Recook && !Recook(*source, &error))
    {
        Status = "settings saved, recook failed: " + error;
        return;
    }

    // Ground truth from the artifact that was just written, not from hope.
    Status = "cooked: " + DescribeCookedTextureState(ReadCookedTextureState(*source));
}

void TexturesPanel::DrawRow(const char* label, const std::string& virtualPath)
{
    ImGui::PushID(virtualPath.c_str());
    if (ImGui::Selectable(label, Selected == virtualPath))
        SelectTexture(virtualPath);
    if (ImGui::BeginPopupContextItem("texture_context"))
    {
        if (ImGui::MenuItem("Create Material", nullptr, false, CreateMaterial != nullptr))
            CreateMaterial(virtualPath);
        ImGui::SetItemTooltip("New .smat beside this texture, named after it "
                              "(a \"T-\" prefix becomes \"M-\"), with this texture "
                              "as its base color.");
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void TexturesPanel::DrawTextureList()
{
    const std::string_view filter = FilterText;

    if (!filter.empty())
    {
        for (const auto& [path, record] : Registry.Records())
        {
            if (!IsSourceTexture(record) || !ContainsCaseInsensitive(record.Path, filter))
                continue;
            DrawRow(record.Path.c_str(), record.Path);
        }
        return;
    }

    // Folder-grouped; the registry map iterates in path order, so folders are
    // contiguous runs.
    std::string_view openFolder;
    bool folderVisible = true;
    bool folderStarted = false;
    for (const auto& [path, record] : Registry.Records())
    {
        if (!IsSourceTexture(record))
            continue;
        const auto [folder, leaf] = SplitFolder(record.Path);
        if (!folderStarted || folder != openFolder)
        {
            if (folderStarted && folderVisible && !openFolder.empty())
                ImGui::TreePop();
            openFolder = folder;
            folderStarted = true;
            folderVisible = folder.empty()
                ? true
                : ImGui::TreeNodeEx(std::string(folder).c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen
                                        | ImGuiTreeNodeFlags_SpanAvailWidth);
        }
        if (!folderVisible)
            continue;
        DrawRow(std::string(leaf).c_str(), record.Path);
    }
    if (folderStarted && folderVisible && !openFolder.empty())
        ImGui::TreePop();
}

void TexturesPanel::DrawDetails()
{
    // Import settings with the preview box beside them; a column too narrow
    // for both stacks the preview underneath instead.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool sideBySide = avail >= kPreviewBoxHeight * 2.0f;
    bool nearestFilter = false;
    if (sideBySide)
    {
        if (ImGui::BeginChild("##import_settings",
                              ImVec2(avail - kPreviewBoxHeight - style.ItemSpacing.x, 0.0f)))
            nearestFilter = DrawImportSettings();
        ImGui::EndChild();
        ImGui::SameLine();
        DrawPreview(nearestFilter);
    }
    else
    {
        const float previewHeight = kPreviewBoxHeight + style.ItemSpacing.y;
        if (ImGui::BeginChild("##import_settings", ImVec2(0.0f, -previewHeight)))
            nearestFilter = DrawImportSettings();
        ImGui::EndChild();
        DrawPreview(nearestFilter);
    }
}

bool TexturesPanel::DrawImportSettings()
{
    ImGui::SeparatorText("Import Settings");
    if (Selected.empty())
    {
        ImGui::TextDisabled("Select a texture.");
        return false;
    }

    ImGui::TextUnformatted(Selected.c_str());

    const auto source = ResolveTextureSource(ContentRoots, Selected);
    // Ground truth every frame: what the cooked artifact actually is.
    const CookedTextureState cookedState =
        source ? ReadCookedTextureState(*source) : CookedTextureState{};
    const std::string cooked =
        source ? DescribeCookedTextureState(cookedState) : std::string("no source file");
    ImGui::TextDisabled("cooked: %s", cooked.c_str());

    ImGui::BeginDisabled(SourceMissing);

    static constexpr const char* kUsages[] = {
        "auto (from name)", "base_color", "normal", "orm", "emissive", "linear_data"
    };
    int usage = static_cast<int>(Draft.Usage);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##usage", &usage, kUsages, 6))
        Draft.Usage = static_cast<TextureUsage>(usage);

    static constexpr const char* kFilters[] = { "linear", "nearest" };
    int filter = static_cast<int>(Draft.Filter);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("Filter", &filter, kFilters, 2))
        Draft.Filter = static_cast<TextureFilter>(filter);
    ImGui::SetItemTooltip("nearest = point sampling (pixel art)");

    ImGui::Checkbox("Block compress (BC)", &Draft.Compress);
    ImGui::SetItemTooltip("Off keeps uncompressed RGBA8: BC blocks smear crisp pixel-art texels.");
    ImGui::SameLine();
    ImGui::Checkbox("Mips", &Draft.GenerateMips);

    if (ImGui::Button("Apply"))
        ApplyDraft();
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
        SelectTexture(Selected);

    ImGui::EndDisabled();

    if (!Status.empty())
        ImGui::TextWrapped("%s", Status.c_str());

    return cookedState.Filter == TextureFilter::Nearest;
}

void TexturesPanel::DrawPreview(bool nearestFilter)
{
    if (Preview == nullptr)
        return;
    Preview->SetTexture(Selected, nearestFilter);

    // Uniform presentation: every texture gets the same square box, scaled
    // to fit with its aspect kept (nearest-filtered textures stay crisp when
    // the fit upscales them).
    const float width = ImGui::GetContentRegionAvail().x;
    if (width < 16.0f)
        return;
    const ImVec2 box(std::min(width, kPreviewBoxHeight), kPreviewBoxHeight);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + box.x, origin.y + box.y),
                        ImGui::GetColorU32(ImGuiCol_FrameBg));

    const ImTextureID texture = Preview->TextureId();
    const VkExtent2D extent = Preview->Extent();
    if (texture != 0 && extent.width > 0 && extent.height > 0)
    {
        const float scale = std::min(box.x / static_cast<float>(extent.width),
                                     box.y / static_cast<float>(extent.height));
        const ImVec2 size(extent.width * scale, extent.height * scale);
        const ImVec2 corner(origin.x + (box.x - size.x) * 0.5f,
                            origin.y + (box.y - size.y) * 0.5f);
        draw->AddImage(texture, corner, ImVec2(corner.x + size.x, corner.y + size.y));
    }
    ImGui::Dummy(box);
}

void TexturesPanel::OnDraw()
{
    if (!IsVisible())
        return;

    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    // Texture list on the left, import settings + preview on the right,
    // split by a draggable divider.
    const float panelLeft = ImGui::GetCursorScreenPos().x;
    const float panelWidth = ImGui::GetContentRegionAvail().x;
    const float panelHeight = std::max(ImGui::GetContentRegionAvail().y, 1.0f);
    constexpr float kMinColumnWidth = 160.0f;
    const float listWidth = std::clamp(panelWidth * ListFraction, kMinColumnWidth,
                                       std::max(kMinColumnWidth, panelWidth - kMinColumnWidth));

    if (ImGui::BeginChild("##list_column", ImVec2(listWidth, 0.0f)))
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "filter textures", FilterText, sizeof(FilterText));
        if (ImGui::BeginChild("##texture_list"))
            DrawTextureList();
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton("##column_splitter", ImVec2(6.0f, panelHeight));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive() && panelWidth > 0.0f)
        ListFraction = std::clamp((ImGui::GetMousePos().x - panelLeft) / panelWidth, 0.1f, 0.9f);
    ImGui::SameLine(0.0f, 0.0f);

    if (ImGui::BeginChild("##details_column"))
        DrawDetails();
    ImGui::EndChild();
}
