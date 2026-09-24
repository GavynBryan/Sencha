#pragma once

#include <core/serialization/Archive.h>

#include <optional>
#include <string>
#include <string_view>

//=============================================================================
// Stable-identity scene fields
//
// How an authored scene persists a stable 64-bit identity (DockId, LinkId,
// NavLinkId, ...): as its canonical text form. Loading accepts the invalid
// identity -- the text the formatter produces for a default-constructed id --
// so a scene with an unresolved reference survives save, load, copy, and undo
// and the editor can decorate and repair it. The strict FromString parsers
// reject that value on purpose; cooked formats use them, which is where an
// invalid identity becomes an error.
//=============================================================================

template <typename Id, typename ToString>
bool SaveStableIdField(IWriteArchive& archive, std::string_view key, Id id,
                       ToString toString)
{
    archive.Field(key, std::string_view(toString(id)));
    return archive.Ok();
}

template <typename Id, typename FromString, typename ToString>
bool LoadStableIdField(IReadArchive& archive, std::string_view key, Id& id,
                       FromString fromString, ToString toString)
{
    std::string text;
    archive.Field(key, text);
    if (!archive.Ok())
        return false;
    if (text == toString(Id{}))
    {
        id = Id{};
        return true;
    }
    const std::optional<Id> parsed = fromString(text);
    if (!parsed)
    {
        archive.MarkInvalidField(key);
        return false;
    }
    id = *parsed;
    return true;
}
