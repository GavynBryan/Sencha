#include "EntitySnapshot.h"

#include "brush/BrushModifierSerialization.h"

#include <core/hash/ContentHash.h>
#include <core/json/JsonStringify.h>

#include <string>

std::uint64_t DigestOf(const EntitySnapshot& snapshot)
{
    std::string text = JsonStringify(snapshot.Components);
    if (snapshot.Brush)
        text += JsonStringify(BrushRecordToJson(*snapshot.Brush));
    text += snapshot.ParentId.IsValid() ? PersistentEntityIdToString(snapshot.ParentId) : std::string("-");
    text += snapshot.Hidden ? 'h' : '-';
    text += snapshot.Locked ? 'l' : '-';
    return HashBytes64(text);
}
