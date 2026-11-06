#include <render/post/PostEffectCatalog.h>

#include <algorithm>

PostEffectId PostEffectCatalog::Register(const PostEffectDesc& desc)
{
    const bool taken = std::ranges::any_of(Types, [&](const PostEffectType& type) {
        return type.Name == desc.Name || (type.Domain == desc.Domain && type.Order == desc.Order);
    });
    if (taken || desc.Name.empty() || desc.FragmentSpirv.empty())
        return {};

    Types.push_back(PostEffectType{ .Name = std::string(desc.Name),
                                    .FragmentSpirv = { desc.FragmentSpirv.begin(), desc.FragmentSpirv.end() },
                                    .Domain = desc.Domain,
                                    .Order = desc.Order });
    return PostEffectId{ static_cast<std::uint32_t>(Types.size()) };
}

PostEffectId PostEffectCatalog::Find(std::string_view name) const
{
    const auto found = std::ranges::find_if(Types, [&](const PostEffectType& type) { return type.Name == name; });
    return found != Types.end() ? PostEffectId{ static_cast<std::uint32_t>(found - Types.begin()) + 1 } : PostEffectId{};
}

const PostEffectType* PostEffectCatalog::Get(PostEffectId id) const
{
    return id.IsValid() && id.Value <= Types.size() ? &Types[id.Value - 1] : nullptr;
}
