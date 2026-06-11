#include <ecs/CommandBuffer.h>
#include <ecs/World.h>

#include <cassert>
#include <vector>

void CommandBuffer::Flush()
{
    assert(!W->InQueryScope()
           && "CommandBuffer::Flush called while a query is active.");

    for (size_t i = 0; i < Commands.size();)
    {
        Command& cmd = Commands[i];

        if (cmd.Kind == CommandKind::AddComponent && cmd.Payload.OnAddHook == nullptr)
        {
            const ComponentId id = cmd.Payload.Id;
            const size_t size = cmd.Payload.Size;
            const size_t align = cmd.Payload.Align;

            size_t end = i + 1;
            while (end < Commands.size()
                   && Commands[end].Kind == CommandKind::AddComponent
                   && Commands[end].Payload.Id == id
                   && Commands[end].Payload.Size == size
                   && Commands[end].Payload.Align == align
                   && Commands[end].Payload.OnAddHook == nullptr)
            {
                ++end;
            }

            if (end - i > 1)
            {
                std::vector<ComponentBatchItem> items;
                items.reserve(end - i);
                for (size_t j = i; j < end; ++j)
                {
                    if (!W->IsAlive(Commands[j].Entity)) continue;
                    items.push_back(ComponentBatchItem{
                        Commands[j].Entity,
                        PayloadData(Commands[j].Payload)
                    });
                }
                if (!items.empty())
                    W->AddComponentsRawBatch(id, items.data(), items.size(), size, align);
                i = end;
                continue;
            }
        }

        if (cmd.Kind == CommandKind::RemoveComponent && cmd.Payload.OnRemoveHook == nullptr)
        {
            const ComponentId id = cmd.Payload.Id;

            size_t end = i + 1;
            while (end < Commands.size()
                   && Commands[end].Kind == CommandKind::RemoveComponent
                   && Commands[end].Payload.Id == id
                   && Commands[end].Payload.OnRemoveHook == nullptr)
            {
                ++end;
            }

            if (end - i > 1)
            {
                std::vector<EntityId> entities;
                entities.reserve(end - i);
                for (size_t j = i; j < end; ++j)
                {
                    if (!W->IsAlive(Commands[j].Entity)) continue;
                    entities.push_back(Commands[j].Entity);
                }
                if (!entities.empty())
                    W->RemoveComponentsRawBatch(id, entities.data(), entities.size());
                i = end;
                continue;
            }
        }

        switch (cmd.Kind)
        {
        case CommandKind::AddComponent:
        {
            if (!W->IsAlive(cmd.Entity)) break;
            W->AddComponentRaw(
                cmd.Entity,
                cmd.Payload.Id,
                PayloadData(cmd.Payload),
                cmd.Payload.Size,
                cmd.Payload.Align,
                cmd.Payload.OnAddHook);
            break;
        }
        case CommandKind::RemoveComponent:
        {
            if (!W->IsAlive(cmd.Entity)) break;
            W->RemoveComponentRaw(
                cmd.Entity,
                cmd.Payload.Id,
                cmd.Payload.OnRemoveHook);
            break;
        }
        case CommandKind::DestroyEntity:
        {
            if (!W->IsAlive(cmd.Entity)) break;
            W->DestroyEntity(cmd.Entity);
            break;
        }
        case CommandKind::CreateEntity:
        {
            W->CreateEntity();
            break;
        }
        }

        ++i;
    }

    Commands.clear();
    PayloadArena.clear();
}
