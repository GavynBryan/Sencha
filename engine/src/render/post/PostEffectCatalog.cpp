#include <render/post/PostEffectCatalog.h>

#include <algorithm>

PostEffectId PostEffectCatalog::Register(const PostEffectDesc& desc)
{
    const auto refuse = [&](std::string_view why) {
        Refusals_.push_back("post effect '" + std::string(desc.Name) + "' refused: " + std::string(why));
        return PostEffectId{};
    };
    if (desc.Name.empty())
        return refuse("it has no name");
    if (desc.FragmentSpirv.empty())
        return refuse("it has no shader");
    for (const PostEffectType& type : Types)
    {
        if (type.Name == desc.Name)
            return refuse("the name is taken");
        if (type.Domain == desc.Domain && type.Order == desc.Order)
            return refuse("'" + type.Name + "' already runs at that order in that domain");
    }

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
