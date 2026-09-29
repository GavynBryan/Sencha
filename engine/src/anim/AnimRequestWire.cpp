#include <anim/AnimRequestWire.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <array>

namespace
{
    std::array<AnimRequestWireRecord*, kAnimRequestCapacity> RecordsOf(AnimRequestSetWire& wire)
    {
        return { &wire.R0, &wire.R1, &wire.R2, &wire.R3, &wire.R4, &wire.R5, &wire.R6, &wire.R7 };
    }

    std::array<const AnimRequestWireRecord*, kAnimRequestCapacity> RecordsOf(const AnimRequestSetWire& wire)
    {
        return { &wire.R0, &wire.R1, &wire.R2, &wire.R3, &wire.R4, &wire.R5, &wire.R6, &wire.R7 };
    }

    const GameplayTagRegistry* TagsOf(const ReplicationWireContext& context)
    {
        return context.Entities != nullptr ? context.Entities->TryGetResource<GameplayTagRegistry>() : nullptr;
    }

    std::uint32_t TagToWire(const GameplayTagRegistry* tags, std::uint32_t id)
    {
        return tags != nullptr ? tags->WireKey(GameplayTagId{ id }) : 0u;
    }

    std::uint32_t TagFromWire(const GameplayTagRegistry* tags, std::uint32_t key)
    {
        return tags != nullptr && key != 0 ? tags->FindByWireKey(key).Value : 0u;
    }
}

void ReplicationCodec<AnimRequestSet>::ToWire(const ReplicationWireContext& context, const AnimRequestSet& set,
                                              AnimRequestSetWire& wire)
{
    const GameplayTagRegistry* tags = TagsOf(context);
    const auto records = RecordsOf(wire);
    for (std::size_t i = 0; i < kAnimRequestCapacity; ++i)
    {
        AnimRequestWireRecord& out = *records[i];
        out = AnimRequestWireRecord{};
        const AnimRequest& request = set.Records[i];
        // A prediction is this machine's guess: the image a delta lands on is the
        // authority's alone, so a guess the delta does not touch cannot survive it.
        if (!request.Occupied || request.Predicted)
            continue;
        out.Ticks.Values[0] = context.WireEntity(request.Id.Source);
        out.Ticks.Values[1] = request.StartTick;
        out.Ticks.Values[2] = request.CancelTick;
        out.Ticks.Values[3] = request.TailUntilTick;
        out.Ticks.Values[4] = request.AnchorSectionStartTick;
        out.Ticks.Values[5] = request.Command;
        out.Words.Values[0] = request.Id.Sequence;
        out.Words.Values[1] = TagToWire(tags, request.Intent.Value);
        out.Words.Values[2] = TagToWire(tags, request.SourceTag.Value);
        out.Words.Values[3] = request.FixedTicks;
        for (std::size_t p = 0; p < kAnimRequestParams; ++p)
        {
            const bool tag = (request.TagParams & (1u << p)) != 0;
            out.Words.Values[4 + p] = tag ? TagToWire(tags, request.Params[p]) : request.Params[p];
        }
        out.Words.Values[8] = static_cast<std::uint32_t>(request.Layers)
                            | static_cast<std::uint32_t>(request.Lifetime) << 8
                            | static_cast<std::uint32_t>(request.CancelReason) << 16
                            | static_cast<std::uint32_t>(request.AnchorSection) << 24;
        out.Words.Values[9] = 1u | static_cast<std::uint32_t>(request.TagParams) << 8;
    }
    wire.NextSequence = set.NextSequence;
    wire.RigTiming = set.RigTiming;
}

void ReplicationCodec<AnimRequestSet>::FromWire(const ReplicationWireContext& context, const AnimRequestSetWire& wire,
                                                AnimRequestSet& set)
{
    const GameplayTagRegistry* tags = TagsOf(context);
    const auto records = RecordsOf(wire);
    for (std::size_t i = 0; i < kAnimRequestCapacity; ++i)
    {
        const AnimRequestWireRecord& in = *records[i];
        AnimRequest& request = set.Records[i];
        request = AnimRequest{};
        if ((in.Words.Values[9] & 1u) == 0)
            continue;
        request.Occupied = true;
        request.Id.Source = context.LocalEntity(in.Ticks.Values[0]);
        request.StartTick = in.Ticks.Values[1];
        request.CancelTick = in.Ticks.Values[2];
        request.TailUntilTick = in.Ticks.Values[3];
        request.AnchorSectionStartTick = in.Ticks.Values[4];
        request.Command = in.Ticks.Values[5];
        request.Id.Sequence = in.Words.Values[0];
        request.Intent = GameplayTagId{ TagFromWire(tags, in.Words.Values[1]) };
        request.SourceTag = GameplayTagId{ TagFromWire(tags, in.Words.Values[2]) };
        request.FixedTicks = in.Words.Values[3];
        request.TagParams = static_cast<std::uint8_t>(in.Words.Values[9] >> 8);
        for (std::size_t p = 0; p < kAnimRequestParams; ++p)
        {
            const bool tag = (request.TagParams & (1u << p)) != 0;
            request.Params[p] = tag ? TagFromWire(tags, in.Words.Values[4 + p]) : in.Words.Values[4 + p];
        }
        const std::uint32_t shape = in.Words.Values[8];
        request.Layers = static_cast<std::uint8_t>(shape);
        request.Lifetime = static_cast<AnimRequestLifetime>(static_cast<std::uint8_t>(shape >> 8));
        request.CancelReason = static_cast<AnimCancelReason>(static_cast<std::uint8_t>(shape >> 16));
        request.AnchorSection = static_cast<std::uint8_t>(shape >> 24);
    }
    set.NextSequence = wire.NextSequence;
    set.RigTiming = wire.RigTiming;
}
