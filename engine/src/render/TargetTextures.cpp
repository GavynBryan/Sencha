#include <render/TargetTextures.h>

std::optional<std::string_view> ParseTargetTextureRef(std::string_view path)
{
    if (!path.starts_with(kTargetTextureScheme) || path.size() == kTargetTextureScheme.size())
        return std::nullopt;
    return path.substr(kTargetTextureScheme.size());
}

TargetTextureId TargetTextureTable::Intern(std::string_view name)
{
    if (name.empty())
        return {};
    if (const TargetTextureId found = Find(name); found.IsValid())
        return found;
    Entries.push_back({ .Name = std::string(name) });
    return { static_cast<std::uint32_t>(Entries.size()) };
}

TargetTextureId TargetTextureTable::Find(std::string_view name) const
{
    for (std::size_t i = 0; i < Entries.size(); ++i)
        if (Entries[i].Name == name)
            return { static_cast<std::uint32_t>(i + 1) };
    return {};
}

std::string_view TargetTextureTable::NameOf(TargetTextureId id) const
{
    return id.IsValid() && id.Value <= Entries.size() ? std::string_view(Entries[id.Value - 1].Name)
                                                      : std::string_view{};
}

std::uint32_t TargetTextureTable::EncodeSlot(TargetTextureId id)
{
    return id.IsValid() ? (kSlotFlag | id.Value) : kNoTexture;
}

bool TargetTextureTable::IsTargetSlot(std::uint32_t slot)
{
    return slot != kNoTexture && (slot & kSlotFlag) != 0;
}

std::uint32_t TargetTextureTable::Resolve(std::uint32_t slot) const
{
    if (!IsTargetSlot(slot))
        return slot;
    const std::uint32_t index = slot & ~kSlotFlag;
    if (index == 0 || index > Entries.size())
        return Unpublished;
    const std::uint32_t published = Entries[index - 1].Published;
    return published != kNoTexture ? published : Unpublished;
}

void TargetTextureTable::Publish(TargetTextureId id, std::uint32_t bindlessIndex)
{
    if (id.IsValid() && id.Value <= Entries.size())
        Entries[id.Value - 1].Published = bindlessIndex;
}

void TargetTextureTable::Withdraw(TargetTextureId id)
{
    Publish(id, kNoTexture);
}

bool TargetTextureTable::IsPublished(TargetTextureId id) const
{
    return id.IsValid() && id.Value <= Entries.size() && Entries[id.Value - 1].Published != kNoTexture;
}
