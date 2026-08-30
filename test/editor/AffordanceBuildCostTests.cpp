#include "WorkspaceFixture.h"

#include "authoring/EditorComponentAdapter.h"

#include "document/EntityNameComponent.h"
#include "document/EditorScene.h"

#include <ecs/ComponentTypeId.h>
#include <world/serialization/SceneSerializer.h>

#include <memory>
#include <span>
#include "document/DocumentSerialization.h"

// Viewport affordances are rebuilt several times a frame (each viewport, the
// overlay, hover, hit-testing), so the cost of one build is multiplied by a
// number nobody controls from the call site. These bound that cost in terms of
// what actually authors affordances rather than what happens to be registered.
namespace
{

class AffordanceBuildCostTest : public WorkspaceTest
{
protected:
    void AddBrushes(int count)
    {
        for (int i = 0; i < count; ++i)
            (void)AddBrush(Vec3d{ static_cast<float>(i) * 2.0f, 0, 0 });
    }

    [[nodiscard]] EditorAffordanceService& Affordances() { return *Workspace.Affordances; }

    // An adapter that records how many entities the affordance pass offered it.
    // Counting is what this file was always about: the pass must ask one
    // question per visible entity per affordance author, and the cost of a
    // build is that count. A wall clock answers the same question by measuring
    // the machine, which is why it answered differently on a CI runner than on
    // any developer's desk.
    class Probe final : public IEditorComponentAdapter
    {
    public:
        Probe(ComponentTypeId type, bool authors) : Type_(type), Authors_(authors) {}

        ComponentTypeId Type() const override { return Type_; }
        bool AuthorsViewportAffordances() const override { return Authors_; }
        void BuildViewport(const EditorComponentContext&,
                           ViewportAffordanceOutput&) const override
        {
            ++Offered;
        }

        mutable int Offered = 0;

    private:
        ComponentTypeId Type_;
        bool Authors_ = false;
    };

    // Registered against a component every brush carries, so every visible
    // brush is one offer.
    [[nodiscard]] Probe& AddProbe(ComponentTypeId type, bool authors)
    {
        auto probe = std::make_unique<Probe>(type, authors);
        Probe& ref = *probe;
        EXPECT_TRUE(Affordances().Registry().Register(std::move(probe)));
        return ref;
    }
};

TEST_F(AffordanceBuildCostTest, WorkScalesWithAffordanceAuthorsNotRegisteredComponentTypes)
{
    // A handful of adapters are registered against every serializable component
    // type, and most of them author inspector rows rather than viewport
    // affordances. Walking the registered types -- or even every adapter -- per
    // entity would make the inner loop an order of magnitude wider than the
    // work that can result.
    const std::size_t adapters = Affordances().Registry().Entries().size();
    const std::size_t authors = Affordances().Registry().ViewportEntries().size();
    const std::size_t serializers = EditorSceneSerializers().Entries().size();
    EXPECT_GT(serializers, adapters);
    EXPECT_LT(authors, adapters) << "every adapter is in the per-entity loop";
    EXPECT_LE(authors, 2u) << "affordance authors grew; re-check the per-entity inner loop";
}

TEST_F(AffordanceBuildCostTest, BuildingIsProportionalToVisibleEntities)
{
    // Two probes on components every brush carries: one that authors viewport
    // affordances and one that only draws inspector rows.
    Probe& author = AddProbe(ResolveComponentTypeId<BrushComponent>(), true);
    Probe& inspectorOnly =
        AddProbe(ResolveComponentTypeId<EntityNameComponent>(), false);

    constexpr int kBrushes = 20;
    AddBrushes(kBrushes);

    ViewportAffordanceOutput output;
    Affordances().Build(output);
    EXPECT_EQ(author.Offered, kBrushes) << "one offer per visible entity, no more";

    // Linear in builds, because the pass holds nothing back between them.
    output = {};
    Affordances().Build(output);
    EXPECT_EQ(author.Offered, 2 * kBrushes);

    // Proportional to what is visible, not to what exists: hidden entities are
    // skipped before any adapter is consulted.
    constexpr int kHidden = 5;
    const std::span<const EntityId> entities = Scene().GetAllEntities();
    ASSERT_GE(entities.size(), static_cast<std::size_t>(kHidden));
    for (int i = 0; i < kHidden; ++i)
        Scene().SetEntityVisible(entities[static_cast<std::size_t>(i)], false);

    const int before = author.Offered;
    output = {};
    Affordances().Build(output);
    EXPECT_EQ(author.Offered - before, kBrushes - kHidden);

    // And the claim the wall clock could never actually check: an adapter that
    // authors no affordances is not in the per-entity loop at all. It is on
    // every one of these entities and was never asked once.
    EXPECT_EQ(inspectorOnly.Offered, 0)
        << "the affordance pass walked an inspector-only adapter";
}

TEST_F(AffordanceBuildCostTest, HasEditTargetsIsCountedLikeAnyOtherBuild)
{
    // It answers a yes/no question by building the full output, so it costs a
    // build; the counter makes that visible rather than hidden behind a bool.
    const std::uint64_t before = Affordances().BuildCount();
    (void)Affordances().HasEditTargets();
    EXPECT_EQ(Affordances().BuildCount(), before + 1);
}

}
