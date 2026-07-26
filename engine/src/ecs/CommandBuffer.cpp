#include <ecs/CommandBuffer.h>
#include <ecs/World.h>

#include <cassert>
#include <vector>

void CommandBuffer::Flush()
{
    assert(!W->InQueryScope()
           && "CommandBuffer::Flush called while a query is active.");

    // Entities created during this flush, in the order their CreateEntity commands
    // were recorded, so a command addressing a PendingEntity can name one. A
    // handle can only be obtained from an earlier CreateEntity call and commands
    // execute in record order, so the creation an ordinal names has always run by
    // the time something addresses it.
    std::vector<EntityId> created;
    const auto resolve = [&created](const Command& command)
    {
        if (command.PendingOrdinal == PendingEntity::InvalidOrdinal)
            return command.Entity;

        assert(command.PendingOrdinal < created.size()
               && "a command addressed a creation that has not run yet");
        return created[command.PendingOrdinal];
    };

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
                    const EntityId entity = resolve(Commands[j]);
                    if (!W->IsAlive(entity)) continue;
                    items.push_back(ComponentBatchItem{
                        entity,
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
                    const EntityId entity = resolve(Commands[j]);
                    if (!W->IsAlive(entity)) continue;
                    entities.push_back(entity);
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
            const EntityId entity = resolve(cmd);
            if (!W->IsAlive(entity)) break;
            W->AddComponentRaw(
                entity,
                cmd.Payload.Id,
                PayloadData(cmd.Payload),
                cmd.Payload.Size,
                cmd.Payload.Align,
                cmd.Payload.OnAddHook);
            break;
        }
        case CommandKind::RemoveComponent:
        {
            const EntityId entity = resolve(cmd);
            if (!W->IsAlive(entity)) break;
            W->RemoveComponentRaw(
                entity,
                cmd.Payload.Id,
                cmd.Payload.OnRemoveHook);
            break;
        }
        case CommandKind::DestroyEntity:
        {
            const EntityId entity = resolve(cmd);
            if (!W->IsAlive(entity)) break;
            W->DestroyEntity(entity);
            break;
        }
        case CommandKind::CreateEntity:
        {
            assert(cmd.PendingOrdinal == created.size()
                   && "creation ordinals must match their execution order");
            created.push_back(W->CreateEntity(cmd.Partition));
            break;
        }
        }

        ++i;
    }

    Commands.clear();
    PayloadArena.clear();
    EndRecording();
}
