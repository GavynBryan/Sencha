// What order a frame's nodes run in, and what happens to the ones that cannot.
//
// The ordering the editor and the game rely on is written as prose today --
// a comment above the viewport loop, a comment in AddMeshRenderFeature, an
// invariant in the docs -- so nothing tests it. Stated as dependencies it is a
// scheduling rule over plain values, which needs no device.
//
// The degrade behaviour is the half worth the most coverage: a view whose
// shadow work did not run must not render as though it had, and neither must
// anything downstream of it.

#include <gtest/gtest.h>

#include <render/FrameComposition.h>

#include <string>
#include <vector>

namespace
{

// Where every record body appends its name, so a test asserts one sequence
// rather than per-node flags.
struct Trace
{
    std::vector<std::string> Ran;
};

// A work body. One per node, held in a named local: the composition stores the
// pointer without owning it.
struct WorkBody
{
    Trace* Log = nullptr;
    std::string Name;

    static void Run(void* self, const RenderFrame&)
    {
        auto& body = *static_cast<WorkBody*>(self);
        body.Log->Ran.push_back(body.Name);
    }
};

// Views identify themselves from the view they are handed back, which is also
// how a host finds the viewport a view belongs to.
struct ViewBody
{
    Trace* Log = nullptr;
    const FrameView* Last = nullptr;

    static void Run(void* self, const RenderFrame&, const FrameView& view)
    {
        auto& body = *static_cast<ViewBody*>(self);
        body.Log->Ran.emplace_back(view.Name);
        body.Last = &view;
    }
};

RenderTargetId SomeTarget()
{
    return RenderTargetId{ 1, 1 };
}

[[nodiscard]] bool Reports(const FrameComposition& composition,
                           std::string_view name,
                           FrameCompositionProblem problem)
{
    for (const FrameCompositionDiagnostic& fault : composition.Diagnostics())
        if (fault.Problem == problem && composition.NameOf(fault.Node) == name)
            return true;
    return false;
}

std::vector<std::string> Names(FrameComposition& composition)
{
    std::vector<std::string> names;
    for (const FrameNodeId id : composition.Resolve())
        names.emplace_back(composition.NameOf(id));
    return names;
}

} // namespace

TEST(FrameComposition, IndependentNodesKeepTheOrderTheyWereDeclaredIn)
{
    Trace log;
    WorkBody first{ &log, "first" };
    WorkBody second{ &log, "second" };
    WorkBody third{ &log, "third" };

    FrameComposition composition;
    composition.Setup(nullptr);
    composition.AddWork({ .Name = "first", .Record = { &WorkBody::Run, &first } });
    composition.AddWork({ .Name = "second", .Record = { &WorkBody::Run, &second } });
    composition.AddWork({ .Name = "third", .Record = { &WorkBody::Run, &third } });

    EXPECT_EQ(Names(composition), (std::vector<std::string>{ "first", "second", "third" }));
    EXPECT_TRUE(composition.Diagnostics().empty());
}

TEST(FrameComposition, ADependentRunsAfterItsProducerNoMatterWhenItWasDeclared)
{
    Trace log;
    WorkBody shadows{ &log, "shadows" };
    ViewBody viewport{ &log };

    FrameComposition composition;
    composition.Setup(nullptr);
    const DependencyPointId ready = composition.DeclarePoint("ShadowAtlasReady");

    // The view is declared first on purpose: order comes from the dependency,
    // not from the order a host happened to walk its panels in.
    const DependencyPointId depends[] = { ready };
    composition.AddView({ .View = { .Name = "viewport", .Target = SomeTarget() },
                          .Record = { &ViewBody::Run, &viewport },
                          .DependsOn = depends });
    composition.AddWork({ .Name = "shadows",
                          .Record = { &WorkBody::Run, &shadows },
                          .Produces = ready });

    const RenderFrame frame{};
    composition.Execute(frame);

    EXPECT_EQ(log.Ran, (std::vector<std::string>{ "shadows", "viewport" }));
}

TEST(FrameComposition, ManyViewsOnOneProducerRunInDeclarationOrderAfterIt)
{
    Trace log;
    WorkBody shadows{ &log, "shadows" };
    ViewBody views{ &log };

    FrameComposition composition;
    composition.Setup(nullptr);
    const DependencyPointId ready = composition.DeclarePoint("ShadowAtlasReady");
    const DependencyPointId depends[] = { ready };

    composition.AddWork({ .Name = "shadows",
                          .Record = { &WorkBody::Run, &shadows },
                          .Produces = ready });
    for (const char* name : { "top", "front", "side", "perspective" })
        composition.AddView({ .View = { .Name = name, .Target = SomeTarget() },
                              .Record = { &ViewBody::Run, &views },
                              .DependsOn = depends });

    const RenderFrame frame{};
    composition.Execute(frame);

    EXPECT_EQ(log.Ran, (std::vector<std::string>{ "shadows", "top", "front", "side",
                                                  "perspective" }));
}

TEST(FrameComposition, TheViewHandedToARecordBodyIsTheOneThatWasDeclared)
{
    Trace log;
    ViewBody body{ &log };
    int panel = 0;

    CameraRenderData camera;
    camera.Position = Vec3d(3.0f, 4.0f, 5.0f);

    FrameComposition composition;
    composition.Setup(nullptr);
    composition.AddView({ .View = { .Name = "viewport",
                                    .Target = RenderTargetId{ 7, 2 },
                                    .Camera = camera,
                                    .User = &panel },
                          .Record = { &ViewBody::Run, &body } });

    const RenderFrame frame{};
    composition.Execute(frame);

    ASSERT_NE(body.Last, nullptr);
    EXPECT_EQ(body.Last->Target, (RenderTargetId{ 7, 2 }));
    EXPECT_EQ(body.Last->User, &panel);
    // The camera the host built once, not one each renderer rebuilt.
    EXPECT_EQ(body.Last->Camera.Position, Vec3d(3.0f, 4.0f, 5.0f));
}

TEST(FrameComposition, AViewWaitingOnAPointNobodyProducesDoesNotRun)
{
    Trace log;
    ViewBody body{ &log };

    FrameComposition composition;
    composition.Setup(nullptr);
    const DependencyPointId ready = composition.DeclarePoint("ShadowAtlasReady");
    const DependencyPointId depends[] = { ready };
    composition.AddView({ .View = { .Name = "viewport", .Target = SomeTarget() },
                          .Record = { &ViewBody::Run, &body },
                          .DependsOn = depends });

    const RenderFrame frame{};
    composition.Execute(frame);

    EXPECT_TRUE(log.Ran.empty());
    EXPECT_TRUE(Reports(composition, "viewport", FrameCompositionProblem::MissingProducer));
}

TEST(FrameComposition, AViewWithNoTargetDoesNotRun)
{
    Trace log;
    ViewBody body{ &log };

    FrameComposition composition;
    composition.Setup(nullptr);
    composition.AddView({ .View = { .Name = "viewport" },
                          .Record = { &ViewBody::Run, &body } });

    const RenderFrame frame{};
    composition.Execute(frame);

    EXPECT_TRUE(log.Ran.empty());
    EXPECT_TRUE(Reports(composition, "viewport", FrameCompositionProblem::InvalidTarget));
}

TEST(FrameComposition, SkippingAProducerSkipsEverythingDownstreamOfIt)
{
    Trace log;
    ViewBody views{ &log };
    WorkBody post{ &log, "post" };

    FrameComposition composition;
    composition.Setup(nullptr);
    const DependencyPointId rendered = composition.DeclarePoint("ViewportRendered");
    const DependencyPointId composited = composition.DeclarePoint("ViewportComposited");
    const DependencyPointId onRendered[] = { rendered };
    const DependencyPointId onComposited[] = { composited };

    // The view cannot run -- no target -- so neither the effect that samples it
    // nor anything after that may run either.
    composition.AddView({ .View = { .Name = "viewport" },
                          .Record = { &ViewBody::Run, &views },
                          .Produces = rendered });
    composition.AddWork({ .Name = "post",
                          .Record = { &WorkBody::Run, &post },
                          .Produces = composited,
                          .DependsOn = onRendered });
    composition.AddView({ .View = { .Name = "overlay", .Target = SomeTarget() },
                          .Record = { &ViewBody::Run, &views },
                          .DependsOn = onComposited });

    const RenderFrame frame{};
    composition.Execute(frame);

    EXPECT_TRUE(log.Ran.empty());
    EXPECT_TRUE(Reports(composition, "viewport", FrameCompositionProblem::InvalidTarget));
    EXPECT_TRUE(Reports(composition, "post", FrameCompositionProblem::SkippedDependency));
    EXPECT_TRUE(Reports(composition, "overlay", FrameCompositionProblem::SkippedDependency));
}

TEST(FrameComposition, UnrelatedNodesStillRunWhenOneBranchIsSkipped)
{
    Trace log;
    ViewBody views{ &log };
    WorkBody healthy{ &log, "healthy" };

    FrameComposition composition;
    composition.Setup(nullptr);
    const DependencyPointId missing = composition.DeclarePoint("NeverProduced");
    const DependencyPointId onMissing[] = { missing };

    composition.AddView({ .View = { .Name = "broken", .Target = SomeTarget() },
                          .Record = { &ViewBody::Run, &views },
                          .DependsOn = onMissing });
    composition.AddWork({ .Name = "healthy", .Record = { &WorkBody::Run, &healthy } });

    const RenderFrame frame{};
    composition.Execute(frame);

    EXPECT_EQ(log.Ran, (std::vector<std::string>{ "healthy" }));
}

TEST(FrameComposition, ACycleRunsNothingInIt)
{
    Trace log;
    WorkBody left{ &log, "left" };
    WorkBody right{ &log, "right" };
    WorkBody downstream{ &log, "downstream" };

    FrameComposition composition;
    composition.Setup(nullptr);
    const DependencyPointId a = composition.DeclarePoint("A");
    const DependencyPointId b = composition.DeclarePoint("B");
    const DependencyPointId onA[] = { a };
    const DependencyPointId onB[] = { b };

    composition.AddWork({ .Name = "left", .Record = { &WorkBody::Run, &left },
                          .Produces = a, .DependsOn = onB });
    composition.AddWork({ .Name = "right", .Record = { &WorkBody::Run, &right },
                          .Produces = b, .DependsOn = onA });
    composition.AddWork({ .Name = "downstream", .Record = { &WorkBody::Run, &downstream },
                          .DependsOn = onA });

    const RenderFrame frame{};
    composition.Execute(frame);

    EXPECT_TRUE(log.Ran.empty());
    EXPECT_TRUE(Reports(composition, "left", FrameCompositionProblem::Cycle));
    EXPECT_TRUE(Reports(composition, "right", FrameCompositionProblem::Cycle));
    EXPECT_TRUE(Reports(composition, "downstream", FrameCompositionProblem::Cycle));
}

TEST(FrameComposition, TwoNodesProducingOnePointLeaveTheFirstOneHoldingIt)
{
    Trace log;
    WorkBody first{ &log, "first" };
    WorkBody second{ &log, "second" };
    ViewBody views{ &log };

    FrameComposition composition;
    composition.Setup(nullptr);
    const DependencyPointId ready = composition.DeclarePoint("ShadowAtlasReady");
    const DependencyPointId depends[] = { ready };

    composition.AddWork({ .Name = "first", .Record = { &WorkBody::Run, &first },
                          .Produces = ready });
    composition.AddWork({ .Name = "second", .Record = { &WorkBody::Run, &second },
                          .Produces = ready });
    composition.AddView({ .View = { .Name = "viewport", .Target = SomeTarget() },
                          .Record = { &ViewBody::Run, &views },
                          .DependsOn = depends });

    const RenderFrame frame{};
    composition.Execute(frame);

    // The duplicate is the node that loses, and the dependent still runs behind
    // the producer that kept the point.
    EXPECT_EQ(log.Ran, (std::vector<std::string>{ "first", "viewport" }));
    EXPECT_TRUE(Reports(composition, "second", FrameCompositionProblem::DuplicateProducer));
}

TEST(FrameComposition, ANodeWithNoRecordBodyDoesNotRunAndSaysSo)
{
    FrameComposition composition;
    composition.Setup(nullptr);
    composition.AddWork({ .Name = "empty" });

    EXPECT_TRUE(Names(composition).empty());
    EXPECT_TRUE(Reports(composition, "empty", FrameCompositionProblem::NoRecordBody));
}

TEST(FrameComposition, DeclaringAPointTwiceNamesTheSamePoint)
{
    FrameComposition composition;
    composition.Setup(nullptr);

    const DependencyPointId first = composition.DeclarePoint("ShadowAtlasReady");
    const DependencyPointId other = composition.DeclarePoint("ProbesReady");
    const DependencyPointId again = composition.DeclarePoint("ShadowAtlasReady");

    EXPECT_TRUE(first.IsValid());
    EXPECT_EQ(first, again);
    EXPECT_FALSE(first == other);
}

TEST(FrameComposition, ClearingTheFrameKeepsPointIdsUsable)
{
    Trace log;
    WorkBody shadows{ &log, "shadows" };
    ViewBody views{ &log };

    FrameComposition composition;
    composition.Setup(nullptr);
    const DependencyPointId ready = composition.DeclarePoint("ShadowAtlasReady");
    const DependencyPointId depends[] = { ready };

    // A host declares its points once and reuses the ids every frame; only the
    // nodes are rebuilt.
    for (int frameIndex = 0; frameIndex < 3; ++frameIndex)
    {
        composition.Clear();
        composition.AddWork({ .Name = "shadows", .Record = { &WorkBody::Run, &shadows },
                              .Produces = ready });
        composition.AddView({ .View = { .Name = "viewport", .Target = SomeTarget() },
                              .Record = { &ViewBody::Run, &views },
                              .DependsOn = depends });
        const RenderFrame frame{};
        composition.Execute(frame);
    }

    EXPECT_EQ(log.Ran, (std::vector<std::string>{ "shadows", "viewport", "shadows",
                                                  "viewport", "shadows", "viewport" }));
    EXPECT_TRUE(composition.Diagnostics().empty());
}

TEST(FrameComposition, ResolvingTwiceWithoutRebuildingGivesTheSameOrder)
{
    Trace log;
    WorkBody shadows{ &log, "shadows" };
    ViewBody views{ &log };

    FrameComposition composition;
    composition.Setup(nullptr);
    const DependencyPointId ready = composition.DeclarePoint("ShadowAtlasReady");
    const DependencyPointId depends[] = { ready };
    composition.AddView({ .View = { .Name = "viewport", .Target = SomeTarget() },
                          .Record = { &ViewBody::Run, &views },
                          .DependsOn = depends });
    composition.AddWork({ .Name = "shadows", .Record = { &WorkBody::Run, &shadows },
                          .Produces = ready });

    const std::vector<std::string> once = Names(composition);
    const std::vector<std::string> twice = Names(composition);

    EXPECT_EQ(once, (std::vector<std::string>{ "shadows", "viewport" }));
    EXPECT_EQ(once, twice);
    // Diagnostics are per-resolve state, not an accumulating log.
    EXPECT_TRUE(composition.Diagnostics().empty());
}

TEST(FrameComposition, AddingANodeAfterResolvingReschedules)
{
    Trace log;
    WorkBody early{ &log, "early" };
    WorkBody late{ &log, "late" };

    FrameComposition composition;
    composition.Setup(nullptr);
    composition.AddWork({ .Name = "early", .Record = { &WorkBody::Run, &early } });
    EXPECT_EQ(Names(composition), (std::vector<std::string>{ "early" }));

    composition.AddWork({ .Name = "late", .Record = { &WorkBody::Run, &late } });
    EXPECT_EQ(Names(composition), (std::vector<std::string>{ "early", "late" }));
}
