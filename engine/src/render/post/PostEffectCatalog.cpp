#include <render/post/PostEffectCatalog.h>

#include <algorithm>

PostEffectId PostEffectCatalog::Register(const PostEffectDesc& desc)
{
    const std::span<const PostEffectType> types = All();
    const auto clash = std::ranges::find_if(types, [&](const PostEffectType& type) {
        return type.Domain == desc.Domain && type.Order == desc.Order;
    });
    const std::string conflict = desc.Kind == PostEffectKind::Shader && desc.FragmentSpirv.empty()
        ? "it has no shader"
        : clash != types.end() ? "'" + clash->Name + "' already runs at that order in that domain"
                               : std::string{};
    return Add("post effect",
               PostEffectType{ .Name = std::string(desc.Name),
                               .FragmentSpirv = { desc.FragmentSpirv.begin(), desc.FragmentSpirv.end() },
                               .Domain = desc.Domain,
                               .Order = desc.Order,
                               .Kind = desc.Kind },
               conflict);
}
