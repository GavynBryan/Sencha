#include <presentation/PresentationOwner.h>

#include <ecs/World.h>

#include <tuple>

bool PresentationOwnerPrecedes(const PresentationOwner& a, const PresentationOwner& b)
{
    return std::tuple(a.Writer.Value, a.Holder.Entity.Index, a.Holder.Entity.Generation, a.Holder.Key)
         < std::tuple(b.Writer.Value, b.Holder.Entity.Index, b.Holder.Entity.Generation, b.Holder.Key);
}

PresentationWriterId PresentationWriters::Register(std::string_view debugName)
{
    Names.emplace_back(debugName);
    return PresentationWriterId{ static_cast<std::uint32_t>(Names.size()) };
}

std::string_view PresentationWriters::NameOf(PresentationWriterId writer) const
{
    return writer.IsValid() && writer.Value <= Names.size() ? std::string_view(Names[writer.Value - 1])
                                                            : std::string_view();
}

PresentationWriterId RegisterPresentationWriter(World& world, std::string_view debugName)
{
    if (!world.HasResource<PresentationWriters>())
        world.AddResource<PresentationWriters>();
    return world.GetResource<PresentationWriters>().Register(debugName);
}
