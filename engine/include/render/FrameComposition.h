#pragma once

#include <graphics/RenderTargetId.h>
#include <graphics/RenderFeature.h>
#include <render/extract/Camera.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

class LoggingProvider;

//=============================================================================
// FrameComposition
//
// What a frame is made of, declared rather than hand-sequenced: a set of views
// and subsystem work, the dependencies between them, and the order that
// satisfies those dependencies.
//
// The edges exist today as prose. The editor runs shadow arbitration once
// before its viewport loop and says so in a comment; the game gets the same
// ordering from feature registration order plus a comment in
// AddMeshRenderFeature plus invariant 6 in the docs. A second view host
// therefore has to re-derive the order by reading those comments, which is how
// the editor and the material preview came to hold two copies of one shape.
//
// The boundary, ratified 2026-08-19: composition views are *externally
// declared* frame views. A specialised render subsystem may own internal views
// and internal work -- ShadowResidency owns its budgets, priorities, steal
// hysteresis, and per-slot scheduling -- and publishes a dependency point
// meaning "my product is ready this frame" instead of contributing one node per
// shadow map. Contributing them would move dynamic scheduling policy up into a
// static description, which is the opposite of what this type is for. The rule
// is about who owns the scheduling, not about which attachments a node writes.
//
// What this does NOT do: acquire targets, open rendering scopes, or place
// barriers. A node's record body does its own, because barrier granularity is
// pass policy -- the shadow atlas is transitioned once around all its views,
// the swapchain carries a cross-frame depth barrier no attachment declaration
// can express, and a bloom plane is the next sub-pass's sampled source. The
// composition decides *when* a body runs, not *what* it records.
//=============================================================================

// Within-frame identities. Deliberately not Handle<Tag>: a generation guards a
// slot recycled across frames, and neither of these outlives the composition
// that issued it -- Clear() invalidates every one at once.
struct FrameNodeId
{
    std::uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    friend bool operator==(FrameNodeId, FrameNodeId) = default;
};

// A named point in the frame that one node produces and others wait on.
struct DependencyPointId
{
    std::uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    friend bool operator==(DependencyPointId, DependencyPointId) = default;
};

struct FrameView;

// -- Record bodies -----------------------------------------------------------
//
// A plain function pointer plus the object it runs on, rather than a
// std::function: the composition is cleared and rebuilt every frame, and
// assigning a std::function whose capture exceeds its small-buffer size
// allocates on every one of those assignments. The bound object must outlive
// the frame, which it does -- a composition is built and executed inside a
// single OnDraw.
//
// Bind one with a capture-less lambda, which converts to the function pointer:
//
//   .Record = { [](void* self, const RenderFrame& frame, const FrameView& view)
//               { static_cast<EditorRenderFeature*>(self)->RecordView(frame, view); },
//               this }

struct FrameWorkRecord
{
    void (*Fn)(void* self, const RenderFrame& frame) = nullptr;
    void* Self = nullptr;
};

struct FrameViewRecord
{
    void (*Fn)(void* self, const RenderFrame& frame, const FrameView& view) = nullptr;
    void* Self = nullptr;
};

// -- Nodes -------------------------------------------------------------------

// A declared frame view: where it renders, what it renders from, and whatever
// the host needs to identify it again. This is the value handed back to the
// record body, so the camera is built once by whoever declares the view rather
// than rebuilt by each renderer inside it.
struct FrameView
{
    // Diagnostics only, and stored by reference: it must have static storage
    // duration. Views of one kind may share a name; it identifies the shape,
    // not the instance.
    std::string_view Name{};
    RenderTargetId Target{};
    CameraRenderData Camera{};
    // Opaque host payload. The composition stores and returns it and never
    // dereferences it -- it is how a host finds the viewport, panel, or
    // capture face a view belongs to.
    void* User = nullptr;
};

// Every member carries a default so a caller may name only the fields that
// matter to it: the build treats a missing field initializer as an error, and
// without these that would force each of these structs to be spelled out in
// full at every call site.
struct FrameViewDesc
{
    FrameView View{};
    FrameViewRecord Record{};
    // The point this view publishes once recorded, if anything waits on it.
    DependencyPointId Produces{};
    // Copied on Add; the caller's array need not outlive the call.
    std::span<const DependencyPointId> DependsOn{};
};

// Subsystem work that is not a declared view: it owns whatever internal views
// and targets it needs and exposes only the point it produces.
struct FrameWorkDesc
{
    std::string_view Name{};
    FrameWorkRecord Record{};
    DependencyPointId Produces{};
    std::span<const DependencyPointId> DependsOn{};
};

// -- Diagnostics -------------------------------------------------------------

enum class FrameCompositionProblem : std::uint8_t
{
    // Two nodes claim to produce the same dependency point. The first one
    // added keeps it.
    DuplicateProducer,
    // Depends on a point no node produced this frame.
    MissingProducer,
    // A view with no target to render into.
    InvalidTarget,
    // Nothing to run.
    NoRecordBody,
    // Depends on a node that was itself skipped. Reported separately from the
    // original fault so the cascade is readable.
    SkippedDependency,
    // In, or downstream of, a dependency cycle. Not separated further: telling
    // a cycle member from its downstream needs a strongly-connected-component
    // pass, and both outcomes are the same -- the node does not run.
    Cycle,
};

// Both fields are always written: unlike the descriptors above, this is never
// partially filled in by a caller, so neither gets a default that would have to
// stand for "no problem" without meaning it.
struct FrameCompositionDiagnostic
{
    FrameNodeId Node;
    FrameCompositionProblem Problem;
};

class FrameComposition
{
public:
    // Logging is optional so the whole type is reachable in a headless test;
    // when absent, Diagnostics() is the only report.
    void Setup(LoggingProvider* logging);

    // Start a frame. Keeps the storage it has -- steady-state framing does not
    // allocate -- and keeps declared dependency points, whose ids stay stable
    // for the composition's life.
    void Clear();

    // Interns a name and returns its id; the same name always returns the same
    // id. Names are stored by reference and must have static storage duration.
    [[nodiscard]] DependencyPointId DeclarePoint(std::string_view name);

    FrameNodeId AddWork(const FrameWorkDesc& work);
    FrameNodeId AddView(const FrameViewDesc& view);

    // The nodes that will run, in the order they will run. Nodes that cannot
    // run are absent, and each one's reason is in Diagnostics(). Idempotent
    // within a frame; adding a node afterwards invalidates it.
    [[nodiscard]] std::span<const FrameNodeId> Resolve();

    // Resolve, then run each scheduled node's record body in order.
    void Execute(const RenderFrame& frame);

    [[nodiscard]] std::string_view NameOf(FrameNodeId id) const;
    [[nodiscard]] std::span<const FrameCompositionDiagnostic> Diagnostics() const
    {
        return Faults;
    }

private:
    enum class NodeKind : std::uint8_t
    {
        Work,
        View,
    };

    enum class NodeState : std::uint8_t
    {
        Pending,
        Scheduled,
        Skipped,
    };

    struct Node
    {
        std::string_view Name;
        NodeKind Kind = NodeKind::Work;
        DependencyPointId Produces;
        // Range into Dependencies, which is flat so a node is fixed-size and
        // adding one cannot invalidate another's dependency list.
        std::uint32_t DependencyBegin = 0;
        std::uint32_t DependencyCount = 0;
        FrameWorkRecord Work;
        FrameViewRecord ViewRecord;
        FrameView View;
    };

    FrameNodeId AddNode(Node&& node, std::span<const DependencyPointId> dependsOn);
    void Skip(std::uint32_t index, FrameCompositionProblem problem);
    // True when every dependency's producer has already been scheduled.
    [[nodiscard]] bool DependenciesScheduled(const Node& node) const;
    void Report(const FrameCompositionDiagnostic& fault);

    LoggingProvider* Logging = nullptr;

    std::vector<Node> Nodes;
    std::vector<DependencyPointId> Dependencies;
    // Point ids are index + 1 into this; it survives Clear().
    std::vector<std::string_view> Points;

    // Resolve state, all sized to the node count and reused.
    std::vector<NodeState> States;
    // Producer node index + 1 per point, 0 for none.
    std::vector<std::uint32_t> ProducerOf;
    std::vector<FrameNodeId> Order;
    std::vector<FrameCompositionDiagnostic> Faults;
    // Hashes of the (problem, name) pairs already logged, so a composition that
    // is wrong every frame reports itself once rather than at frame rate.
    std::vector<std::uint64_t> Reported;

    bool Resolved = false;
};
