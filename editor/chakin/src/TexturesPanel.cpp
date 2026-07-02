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
                             RecookFn recook)
    : Registry(registry)
    , ContentRoots(std::move(contentRoots))
    , Recook(std::move(recook))
{
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
    ImGui::SeparatorText("Import Settings");
    if (Selected.empty())
    {
        ImGui::TextDisabled("Select a texture.");
        return;
    }

    ImGui::TextUnformatted(Selected.c_str());

    const auto source = ResolveTextureSource(ContentRoots, Selected);
    // Ground truth every frame: what the cooked artifact actually is.
    const std::string cooked = source
        ? DescribeCookedTextureState(ReadCookedTextureState(*source))
        : std::string("no source file");
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
}

void TexturesPanel::OnDraw()
{
    if (!IsVisible())
        return;

    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "filter textures", FilterText, sizeof(FilterText));

    // List on top, details pinned below it.
    const float detailsHeight = ImGui::GetTextLineHeightWithSpacing() * 11.0f;
    if (ImGui::BeginChild("##texture_list",
                          ImVec2(0.0f, -detailsHeight)))
        DrawTextureList();
    ImGui::EndChild();

    DrawDetails();
}
