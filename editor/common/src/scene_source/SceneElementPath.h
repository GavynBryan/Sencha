#pragma once

#include <core/identity/Id.h>
#include <core/identity/StrongId.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Stable identity of one placed scene instance within its containing scene.
// Same 64-bit editor-minted discipline as PersistentEntityId; distinct type
// because the two name different things -- an instance is a placement of a
// whole source, an entity id is one entity.
using SceneInstanceId = StrongId<struct SceneInstanceIdTag, std::uint64_t>;

//=============================================================================
// SceneElementPath
//
// The address of one element reached through nested scene instances: zero or
// more instance ids walking inward, ending at a source entity id. Overrides
// key on it, placements record minted ids against it, and the editor's
// projection index joins on it. The text form is the ids as 16-digit lowercase
// hex joined by '/', which is what the .sscene file stores and what object
// keys in override records use.
//=============================================================================
struct SceneElementPath
{
    std::vector<std::uint64_t> Elements;

    [[nodiscard]] bool IsEmpty() const { return Elements.empty(); }
    friend bool operator==(const SceneElementPath&, const SceneElementPath&) = default;

    [[nodiscard]] std::string ToString() const
    {
        std::string out;
        for (std::size_t i = 0; i < Elements.size(); ++i)
        {
            if (i > 0)
                out.push_back('/');
            out += PersistentEntityIdToString(PersistentEntityId{ Elements[i] });
        }
        return out;
    }

    // Strict inverse: one or more 16-hex-digit segments. Anything else --
    // empty text, a malformed segment, a trailing separator -- is nullopt;
    // a bad path in authored content should fail loudly at parse time.
    [[nodiscard]] static std::optional<SceneElementPath> FromString(std::string_view text)
    {
        SceneElementPath path;
        while (!text.empty())
        {
            const std::size_t cut = text.find('/');
            const std::string_view segment =
                cut == std::string_view::npos ? text : text.substr(0, cut);
            const std::optional<PersistentEntityId> id =
                PersistentEntityIdFromString(segment);
            if (!id.has_value())
                return std::nullopt;
            path.Elements.push_back(id->Value);
            if (cut == std::string_view::npos)
                break;
            text.remove_prefix(cut + 1);
            if (text.empty())
                return std::nullopt; // trailing separator
        }
        if (path.Elements.empty())
            return std::nullopt;
        return path;
    }
};
