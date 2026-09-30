#include "BrushModifier.h"

#include <core/hash/Fnv1a.h>

#include <array>

namespace
{
    struct Rebase
    {
        Vec3d Delta;

        void operator()(MirrorModifier& mirror) const
        {
            if (mirror.Source != MirrorPlaneSource::Custom)
                return;
            // Points on the plane satisfy n.p + d = 0; the same points shifted by
            // delta satisfy n.(p + delta) + d' = 0, so d' = d - n.delta.
            mirror.CustomPlane.D -= mirror.CustomPlane.Normal.Dot(Delta);
        }

        void operator()(ArrayModifier&) const {}
    };

    const std::array<BrushModifierKindInfo, 2> kKinds = { {
        { "mirror", [] { return BrushModifier{ true, MirrorModifier{} }; } },
        { "array", [] { return BrushModifier{ true, ArrayModifier{} }; } },
    } };
    static_assert(kKinds.size() == std::variant_size_v<decltype(BrushModifier::Params)>,
                  "one kind row per variant alternative");
}

std::span<const BrushModifierKindInfo> BrushModifierKinds()
{
    return kKinds;
}

const BrushModifierKindInfo& BrushModifierKindOf(const BrushModifier& modifier)
{
    return kKinds[modifier.Params.index()];
}

void RebaseBrushModifiers(BrushModifierStack& stack, Vec3d delta)
{
    for (BrushModifier& modifier : stack)
        std::visit(Rebase{ delta }, modifier.Params);
}

namespace
{
    // One visitor per domain. A kind that touches a domain folds every field
    // that domain depends on; a kind that does not touch it folds nothing.
    struct TopologyFold
    {
        std::uint64_t& H;
        bool operator()(const MirrorModifier& mirror) const
        {
            HashFnv1aByte(H, 'M');
            HashFnv1aValue(H, mirror.Axis);
            HashFnv1aValue(H, mirror.Source);
            HashFnv1aValue(H, mirror.Offset);
            HashFnv1aValue(H, mirror.CustomPlane.Normal);
            HashFnv1aValue(H, mirror.CustomPlane.D);
            return true;
        }
        bool operator()(const ArrayModifier&) const { return false; } // mints nothing
    };
    struct PlacementFold
    {
        std::uint64_t& H;
        void operator()(const MirrorModifier& mirror) const { (void)TopologyFold{ H }(mirror); }
        void operator()(const ArrayModifier& array) const
        {
            HashFnv1aByte(H, 'A');
            HashFnv1aValue(H, array.Placement);
            HashFnv1aValue(H, array.Axis);
            HashFnv1aByte(H, array.Reverse ? 1 : 0);
            HashFnv1aValue(H, array.Count);
            HashFnv1aValue(H, array.Spacing);
            HashFnv1aValue(H, array.Offset);
        }
    };
}

BrushModifierSignatures BrushStackSignatures(const BrushModifierStack& stack)
{
    BrushModifierSignatures out{ kFnv1aOffsetBasis, kFnv1aOffsetBasis };
    for (const BrushModifier& modifier : stack)
    {
        if (!modifier.Enabled)
            continue;
        // Stack order matters for placement: a Mirror before an Array reflects
        // a wall and repeats the pair; after it, repeats the wall. Topology
        // folds only the kinds that mint meshes, in their own order, so adding
        // or retuning an Array leaves every baked mesh where it is.
        HashFnv1aValue(out.Placement, static_cast<std::uint8_t>(modifier.Params.index()));
        std::visit(PlacementFold{ out.Placement }, modifier.Params);
        std::uint64_t topology = kFnv1aOffsetBasis;
        if (std::visit(TopologyFold{ topology }, modifier.Params))
        {
            HashFnv1aValue(out.Topology, static_cast<std::uint8_t>(modifier.Params.index()));
            HashFnv1aValue(out.Topology, topology);
        }
    }
    return out;
}
