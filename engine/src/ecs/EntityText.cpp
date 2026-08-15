#include <ecs/EntityText.h>

#include <cstdio>

void AppendEntityText(std::string& out, EntityId entity)
{
    char buffer[48];
    const int length = std::snprintf(buffer, sizeof(buffer), "%u:%u",
                                     entity.Index, entity.Generation);
    if (length > 0)
        out.append(buffer, static_cast<std::size_t>(length));
}
