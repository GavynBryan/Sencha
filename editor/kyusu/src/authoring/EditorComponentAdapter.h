#pragma once

#include "overlay/EditorOverlayState.h"
#include "render/EditorLinePipeline.h"
#include "render/EditorWideLinePipeline.h"

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/Ray3d.h>
#include <math/geometry/3d/Transform3d.h>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "selection/SelectableRef.h"

class CommandStack;
class EditorDocument;
class EditorScene;
class SelectionService;
class WorldDocument;
struct GridSettings;
struct EditorViewport;

template <typename T>
struct IValueEditTransaction
{
    virtual void Preview(const T& value) = 0;
    virtual void Commit(const T& value) = 0;
    virtual void Cancel() = 0;
    virtual ~IValueEditTransaction() = default;
};

struct AabbEditTarget
{
    uint64_t Key = 0;
    Transform3f LocalToWorld;
    Aabb3d Value;
    std::function<std::unique_ptr<IValueEditTransaction<Aabb3d>>()> BeginEdit;
};

struct RectangleEditValue
{
    // Offset from the rectangle's center at the start of the edit, expressed
    // in its local right/up basis.
    Vec2d CenterOffset;
    Vec2d HalfExtents;
};

struct RectEditTarget
{
    uint64_t Key = 0;
    Transform3f LocalToWorld;
    Vec2d HalfExtents;
    std::function<std::unique_ptr<IValueEditTransaction<RectangleEditValue>>()> BeginEdit;
};

struct EditorPickProxy
{
    enum class Kind : uint8_t { Rectangle };
    Kind Shape = Kind::Rectangle;
    EntityId Entity;
    Transform3f LocalToWorld;
    Vec2d HalfExtents;
};

struct ViewportAffordanceOutput
{
    std::vector<EditorLineSegment> Lines;
    std::vector<EditorLineVertex> FillTriangles;
    std::vector<LabelRequest> Labels;
    std::vector<EditorPickProxy> PickProxies;
    std::vector<AabbEditTarget> Aabbs;
    std::vector<RectEditTarget> Rectangles;
};

struct EditorComponentContext
{
    WorldDocument& World;
    EditorDocument& Document;
    EditorScene& Scene;
    CommandStack& Commands;
    EntityId Entity;
    Transform3f Transform;
    bool Selected = false;
};

struct EditorComponentInspectorContext
{
    WorldDocument& World;
    EditorDocument& Document;
    EditorScene& Scene;
    SelectionService& Selection;
    CommandStack& Commands;
    EntityId Entity;
};

class IEditorComponentAdapter
{
public:
    [[nodiscard]] virtual ComponentTypeId Type() const = 0;
    virtual void BuildViewport(const EditorComponentContext& context,
                               ViewportAffordanceOutput& output) const = 0;
    virtual bool DrawInspector(EditorComponentInspectorContext&) const { return false; }
    [[nodiscard]] virtual bool AllowEntityScale() const { return true; }
    virtual ~IEditorComponentAdapter() = default;
};

class IComponentSerializer;

class EditorComponentAdapterRegistry
{
public:
    bool Register(std::unique_ptr<IEditorComponentAdapter> adapter);
    [[nodiscard]] const IEditorComponentAdapter* Find(ComponentTypeId type) const;

    // Registered adapters paired with the serializer that answers "does this
    // entity have the component". Iterating these (rather than every registered
    // component type) keeps affordance work proportional to what actually
    // authors affordances, which is a handful of types at most.
    struct Entry
    {
        const IEditorComponentAdapter* Adapter;
        const IComponentSerializer*    Serializer;
    };
    [[nodiscard]] std::span<const Entry> Entries() const { ResolveEntries(); return Resolved; }

private:
    // Rebuilt lazily when the adapter set or the serializer registry changes;
    // both are bring-up-time facts, so this settles after the first build.
    void ResolveEntries() const;

    std::unordered_map<ComponentTypeId, std::unique_ptr<IEditorComponentAdapter>> Adapters;
    mutable std::vector<Entry> Resolved;
    mutable std::size_t ResolvedSerializerCount = 0;
};

class EditorAffordanceService
{
public:
    EditorAffordanceService(WorldDocument& world, SelectionService& selection,
                            CommandStack& commands, GridSettings& grid);

    [[nodiscard]] EditorComponentAdapterRegistry& Registry() { return Adapters; }
    void Build(ViewportAffordanceOutput& output) const;
    [[nodiscard]] std::optional<std::pair<SelectableRef, float>> Pick(
        const Ray3d& ray, const EditorScene& scene) const;
    [[nodiscard]] bool AllowsScaleForSelection() const;
    [[nodiscard]] bool HasEditTargets() const;

    // How many times Build has run. Affordances are rebuilt from several places
    // per frame (each viewport, the overlay, hit-testing); this exists so a test
    // can bound that rather than guess at it.
    [[nodiscard]] std::uint64_t BuildCount() const { return Builds; }

private:
    void AppendZoneBoundsTarget(ViewportAffordanceOutput& output) const;

    mutable std::uint64_t Builds = 0;

    WorldDocument& World;
    SelectionService& Selection;
    CommandStack& Commands;
    GridSettings& Grid;
    EditorComponentAdapterRegistry Adapters;
};
