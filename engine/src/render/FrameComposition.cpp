#include <render/FrameComposition.h>

#include <core/hash/Fnv1a.h>
#include <core/logging/LoggingProvider.h>

#include <algorithm>

namespace
{

// The tail of the one diagnostic line, so the reason reads as a sentence about
// the node rather than as an enumerator name the reader has to go look up.
std::string_view ProblemText(FrameCompositionProblem problem)
{
    switch (problem)
    {
        case FrameCompositionProblem::DuplicateProducer:
            return "a dependency point it produces is already produced by an earlier node";
        case FrameCompositionProblem::MissingProducer:
            return "it depends on a point no node produced this frame";
        case FrameCompositionProblem::InvalidTarget:
            return "it is a view with no render target";
        case FrameCompositionProblem::NoRecordBody:
            return "it has no record body";
        case FrameCompositionProblem::SkippedDependency:
            return "a node it depends on was skipped";
        case FrameCompositionProblem::Cycle:
            return "it is in, or downstream of, a dependency cycle";
    }
    return "unknown";
}

} // namespace

void FrameComposition::Setup(LoggingProvider* logging)
{
    Logging = logging;
}

void FrameComposition::Clear()
{
    // clear(), never shrink: the frame after this one wants the same storage.
    // Points survive so their ids stay stable for the composition's life.
    Nodes.clear();
    Dependencies.clear();
    Order.clear();
    Faults.clear();
    Resolved = false;
}

DependencyPointId FrameComposition::DeclarePoint(std::string_view name)
{
    const auto it = std::find(Points.begin(), Points.end(), name);
    if (it != Points.end())
        return DependencyPointId{ static_cast<std::uint32_t>(it - Points.begin()) + 1u };
    Points.push_back(name);
    return DependencyPointId{ static_cast<std::uint32_t>(Points.size()) };
}

FrameNodeId FrameComposition::AddNode(Node&& node,
                                      std::span<const DependencyPointId> dependsOn)
{
    node.DependencyBegin = static_cast<std::uint32_t>(Dependencies.size());
    node.DependencyCount = static_cast<std::uint32_t>(dependsOn.size());
    Dependencies.insert(Dependencies.end(), dependsOn.begin(), dependsOn.end());
    Nodes.push_back(std::move(node));
    Resolved = false;
    return FrameNodeId{ static_cast<std::uint32_t>(Nodes.size()) };
}

FrameNodeId FrameComposition::AddWork(const FrameWorkDesc& work)
{
    Node node;
    node.Name = work.Name;
    node.Kind = NodeKind::Work;
    node.Produces = work.Produces;
    node.Work = work.Record;
    return AddNode(std::move(node), work.DependsOn);
}

FrameNodeId FrameComposition::AddView(const FrameViewDesc& view)
{
    Node node;
    node.Name = view.View.Name;
    node.Kind = NodeKind::View;
    node.Produces = view.Produces;
    node.ViewRecord = view.Record;
    node.View = view.View;
    return AddNode(std::move(node), view.DependsOn);
}

std::string_view FrameComposition::NameOf(FrameNodeId id) const
{
    if (!id.IsValid() || id.Value > Nodes.size())
        return {};
    return Nodes[id.Value - 1].Name;
}

void FrameComposition::Report(const FrameCompositionDiagnostic& fault)
{
    Faults.push_back(fault);
    if (Logging == nullptr)
        return;

    // Deduplicate on (problem, name) rather than node index: a composition that
    // is wrong is wrong every frame, and node indices shift as viewports open
    // and close, so indexing would let the same fault re-report itself.
    const std::string_view name = NameOf(fault.Node);
    std::uint64_t key = kFnv1aOffsetBasis;
    HashFnv1aByte(key, static_cast<std::uint8_t>(fault.Problem));
    HashFnv1aBytes(key, name.data(), name.size());
    if (std::find(Reported.begin(), Reported.end(), key) != Reported.end())
        return;
    Reported.push_back(key);

    Logging->GetLogger<FrameComposition>().Warn(
        "Frame node '{}' will not run: {}", name, ProblemText(fault.Problem));
}

void FrameComposition::Skip(std::uint32_t index, FrameCompositionProblem problem)
{
    States[index] = NodeState::Skipped;
    Report({ .Node = FrameNodeId{ index + 1u }, .Problem = problem });
}

bool FrameComposition::DependenciesScheduled(const Node& node) const
{
    for (std::uint32_t i = 0; i < node.DependencyCount; ++i)
    {
        const DependencyPointId point = Dependencies[node.DependencyBegin + i];
        const std::uint32_t producer = ProducerOf[point.Value];
        if (States[producer - 1] != NodeState::Scheduled)
            return false;
    }
    return true;
}

std::span<const FrameNodeId> FrameComposition::Resolve()
{
    if (Resolved)
        return Order;

    const std::uint32_t count = static_cast<std::uint32_t>(Nodes.size());
    States.assign(count, NodeState::Pending);
    ProducerOf.assign(Points.size() + 1, 0u);
    Order.clear();
    Faults.clear();

    // 1. Who produces what. First writer keeps the point, so the resolution of
    //    a duplicate does not depend on which node happened to be added last.
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const DependencyPointId produces = Nodes[i].Produces;
        if (!produces.IsValid() || produces.Value > Points.size())
            continue;
        if (ProducerOf[produces.Value] != 0)
            Skip(i, FrameCompositionProblem::DuplicateProducer);
        else
            ProducerOf[produces.Value] = i + 1u;
    }

    // 2. Faults a node carries on its own, independent of any other node.
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (States[i] != NodeState::Pending)
            continue;
        const Node& node = Nodes[i];
        const bool hasBody = node.Kind == NodeKind::View ? node.ViewRecord.Fn != nullptr
                                                         : node.Work.Fn != nullptr;
        if (!hasBody)
        {
            Skip(i, FrameCompositionProblem::NoRecordBody);
            continue;
        }
        if (node.Kind == NodeKind::View && !node.View.Target.IsValid())
        {
            Skip(i, FrameCompositionProblem::InvalidTarget);
            continue;
        }
        for (std::uint32_t d = 0; d < node.DependencyCount; ++d)
        {
            const DependencyPointId point = Dependencies[node.DependencyBegin + d];
            if (!point.IsValid() || point.Value > Points.size()
                || ProducerOf[point.Value] == 0)
            {
                Skip(i, FrameCompositionProblem::MissingProducer);
                break;
            }
        }
    }

    // 3. Cascade: a node that depends on a skipped node cannot run either. A
    //    view that did not render must not be sampled as though it had, so the
    //    skip propagates rather than letting a dependent read stale contents.
    for (bool changed = true; changed;)
    {
        changed = false;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (States[i] != NodeState::Pending)
                continue;
            const Node& node = Nodes[i];
            for (std::uint32_t d = 0; d < node.DependencyCount; ++d)
            {
                const std::uint32_t producer =
                    ProducerOf[Dependencies[node.DependencyBegin + d].Value];
                if (States[producer - 1] == NodeState::Skipped)
                {
                    Skip(i, FrameCompositionProblem::SkippedDependency);
                    changed = true;
                    break;
                }
            }
        }
    }

    // 4. Topological order, lowest insertion index among the ready nodes first.
    //    Deterministic by construction: no container iteration order and no
    //    comparison beyond the index decide anything. The rescan makes this
    //    O(n^2) in the node count -- a frame declares single-digit nodes, and
    //    the alternative rules are harder to state than they are to run.
    Order.reserve(count);
    for (bool emitted = true; emitted;)
    {
        emitted = false;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (States[i] != NodeState::Pending || !DependenciesScheduled(Nodes[i]))
                continue;
            States[i] = NodeState::Scheduled;
            Order.push_back(FrameNodeId{ i + 1u });
            emitted = true;
            break;
        }
    }

    // 5. Whatever is still pending has every fault-free dependency satisfiable
    //    and still cannot run, which means it is waiting on itself.
    for (std::uint32_t i = 0; i < count; ++i)
        if (States[i] == NodeState::Pending)
            Skip(i, FrameCompositionProblem::Cycle);

    Resolved = true;
    return Order;
}

void FrameComposition::Execute(const RenderFrame& frame)
{
    for (const FrameNodeId id : Resolve())
    {
        const Node& node = Nodes[id.Value - 1];
        if (node.Kind == NodeKind::View)
            node.ViewRecord.Fn(node.ViewRecord.Self, frame, node.View);
        else
            node.Work.Fn(node.Work.Self, frame);
    }
}
