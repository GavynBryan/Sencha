#include <framework/gameplay_tags/GameplayTagRegistry.h>

#include <cctype>

namespace
{
    bool IsValidSegmentCharacter(char c)
    {
        const unsigned char ch = static_cast<unsigned char>(c);
        return std::isalnum(ch) || c == '_';
    }

    bool ValidateGameplayTagName(std::string_view name, GameplayTagError* error)
    {
        if (name.empty())
        {
            if (error)
            {
                error->Message = "Gameplay tag name must not be empty";
                error->Position = 0;
            }
            return false;
        }

        bool previousWasDot = true;
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            const char c = name[i];
            if (c == '.')
            {
                if (previousWasDot)
                {
                    if (error)
                    {
                        error->Message = "Gameplay tag names cannot contain empty segments";
                        error->Position = i;
                    }
                    return false;
                }

                previousWasDot = true;
                continue;
            }

            if (!IsValidSegmentCharacter(c))
            {
                if (error)
                {
                    error->Message = "Gameplay tag names may only contain letters, digits, '_' and '.'";
                    error->Position = i;
                }
                return false;
            }

            previousWasDot = false;
        }

        if (previousWasDot)
        {
            if (error)
            {
                error->Message = "Gameplay tag names cannot end with '.'";
                error->Position = name.size() - 1;
            }
            return false;
        }

        return true;
    }
}

GameplayTagRegistry::GameplayTagRegistry()
{
    Tags.push_back(TagRecord{});
}

std::optional<GameplayTagId> GameplayTagRegistry::RegisterTag(std::string_view name,
                                                              GameplayTagError* error)
{
    if (!ValidateGameplayTagName(name, error))
        return std::nullopt;

    auto existing = IdsByName.find(std::string(name));
    if (existing != IdsByName.end())
        return existing->second;

    return EnsureSegmentPath(name, error);
}

GameplayTagId GameplayTagRegistry::FindTag(std::string_view name) const
{
    auto it = IdsByName.find(std::string(name));
    return it == IdsByName.end() ? GameplayTagId{} : it->second;
}

std::string_view GameplayTagRegistry::GetName(GameplayTagId id) const
{
    if (!IsKnown(id))
        return {};

    return Tags[id.Value].Name;
}

GameplayTagId GameplayTagRegistry::GetParent(GameplayTagId id) const
{
    if (!IsKnown(id))
        return {};

    return Tags[id.Value].Parent;
}

bool GameplayTagRegistry::IsDescendantOf(GameplayTagId child, GameplayTagId ancestor) const
{
    if (!IsKnown(child) || !IsKnown(ancestor))
        return false;

    for (GameplayTagId current = child; current.IsValid(); current = GetParent(current))
    {
        if (current == ancestor)
            return true;
    }

    return false;
}

std::vector<GameplayTagId> GameplayTagRegistry::GetChildren(GameplayTagId id) const
{
    if (!IsKnown(id))
        return {};

    return Tags[id.Value].Children;
}

std::size_t GameplayTagRegistry::Size() const
{
    return Tags.size() - 1;
}

bool GameplayTagRegistry::IsKnown(GameplayTagId id) const
{
    return id.IsValid() && id.Value < Tags.size();
}

std::optional<GameplayTagId> GameplayTagRegistry::EnsureSegmentPath(std::string_view canonicalName,
                                                                    GameplayTagError*)
{
    GameplayTagId parent;
    GameplayTagId current;
    std::size_t segmentEnd = 0;

    while (segmentEnd != std::string_view::npos)
    {
        segmentEnd = canonicalName.find('.', segmentEnd + (segmentEnd == 0 ? 0 : 1));
        const std::string path(canonicalName.substr(0, segmentEnd));

        auto it = IdsByName.find(path);
        if (it != IdsByName.end())
        {
            current = it->second;
            parent = current;
            continue;
        }

        current = GameplayTagId{ static_cast<std::uint32_t>(Tags.size()) };
        Tags.push_back(TagRecord{
            .Name = path,
            .Parent = parent,
            .Children = {},
        });
        IdsByName.emplace(Tags.back().Name, current);

        if (parent.IsValid())
            Tags[parent.Value].Children.push_back(current);

        parent = current;
    }

    return current;
}
