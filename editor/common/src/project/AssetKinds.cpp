#include "project/AssetKinds.h"

#include <array>
#include <cctype>

namespace
{
constexpr std::array<std::string_view, 8> kCategories = { "Materials", "Textures", "Scenes", "Data",
                                                          "Models",    "UI",       "Scripts", "Other" };

struct KindRow
{
    std::string_view Extension;
    std::string_view Category;
    std::string_view Label;
    IconId Icon;
};

constexpr KindRow kKinds[] = {
    { ".smat", "Materials", "Material", IconId::Material },
    { ".png", "Textures", "Texture", IconId::Texture },
    { ".sscene", "Scenes", "Scene", IconId::Box },
    { ".sworld", "Scenes", "World", IconId::World },
    { ".sdata", "Data", "Data", IconId::Data },
    { ".glb", "Models", "Model", IconId::Model },
    { ".gltf", "Models", "Model", IconId::Model },
    { ".blend", "Models", "Blender Model", IconId::Model },
    { ".smesh", "Models", "Mesh", IconId::Model },
    { ".skmesh", "Models", "Skinned Mesh", IconId::Model },
    { ".rml", "UI", "UI Document", IconId::Document },
    { ".rcss", "UI", "UI Style", IconId::Document },
    { ".t", "Scripts", "Script", IconId::Document },
};

// "movement.profile" -> "Movement Profile".
std::string Titled(std::string_view name)
{
    std::string out;
    bool boundary = true;
    for (const char ch : name)
    {
        if (ch == '.' || ch == '_')
        {
            out += ' ';
            boundary = true;
            continue;
        }
        out += boundary ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
        boundary = false;
    }
    return out;
}
} // namespace

AssetKind KindOf(const ProjectFile& file)
{
    for (const KindRow& row : kKinds)
        if (row.Extension == file.Extension)
            return { .Category = row.Category,
                     .Label = file.Subtype.empty() ? std::string(row.Label) : Titled(file.Subtype),
                     .Icon = row.Icon };
    return { .Category = "Other", .Label = "File", .Icon = IconId::File };
}

std::span<const std::string_view> AssetCategories()
{
    return kCategories;
}
