from pathlib import Path
import re


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def replace_exact(text, old, new, expected=1, label="replacement"):
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{label}: expected {expected}, found {count}")
    return text.replace(old, new)


# ComponentTraits now exposes only the registry-scoped resource owner to hooks.
write("engine/include/ecs/ComponentTraits.h", """#pragma once

#include <ecs/EntityId.h>

class ResourceStore;

// ComponentTraits<T> is the opt-in specialization point for component lifetime edges.
// Hooks receive the registry-scoped resource owner, not entity storage. This keeps
// component lifetime work unable to perform structural ECS mutation.
template <typename T>
struct ComponentTraits
{
};

template <typename T>
concept ComponentHasOnAdd =
    requires(T& component, ResourceStore& resources, EntityId entity)
    {
        ComponentTraits<T>::OnAdd(component, resources, entity);
    };

template <typename T>
concept ComponentHasOnRemove =
    requires(const T& component, ResourceStore& resources, EntityId entity)
    {
        ComponentTraits<T>::OnRemove(component, resources, entity);
    };
""")

# World binds to one stable resource owner only for lifecycle dispatch.
path = "engine/include/ecs/World.h"
text = read(path)
text = replace_exact(
    text,
    "class CommandBuffer;\nclass World;\ntemplate <typename... Accessors> class Query;",
    "class CommandBuffer;\nclass ResourceStore;\nclass World;\ntemplate <typename... Accessors> class Query;",
    label="World forward declarations")
text = replace_exact(
    text,
    "    void (*OnAdd)(void*, World&, EntityId) = nullptr;\n    void (*OnRemove)(const void*, World&, EntityId) = nullptr;",
    "    void (*OnAdd)(void*, ResourceStore&, EntityId) = nullptr;\n    void (*OnRemove)(const void*, ResourceStore&, EntityId) = nullptr;",
    label="ComponentMeta lifecycle signatures")
text = replace_exact(
    text,
    "// Owns: entity registry, archetype table, archetype graph, resources,\n// and the query-scope guard.",
    "// Owns the entity registry, component metadata, archetype storage, change epochs,\n// structural versioning, and query guards. Registry-scoped resources live outside it.",
    label="World ownership comment")
text = replace_exact(
    text,
    "    World() = default;\n    ~World()",
    "    World() = default;\n    explicit World(ResourceStore& lifecycleResources)\n        : LifecycleResources(&lifecycleResources)\n    {\n    }\n\n    ~World()",
    label="World resource constructor")
registration_anchor = """        static_assert(std::is_trivially_copyable_v<T>,
                      "Components must be trivially copyable: archetype chunks "
                      "relocate rows with memcpy.");
        assert(!EntityCreated
               && "Component registration after entity creation is forbidden (v1).");
"""
registration_replacement = registration_anchor + """
        if constexpr (!std::is_empty_v<T>
                      && (ComponentHasOnAdd<T> || ComponentHasOnRemove<T>))
        {
            assert(LifecycleResources != nullptr
                   && "Lifecycle component registration requires a ResourceStore");
        }
"""
text = replace_exact(text, registration_anchor, registration_replacement,
                     label="lifecycle binding assertion")
text = replace_exact(
    text,
    "meta.OnAdd = [](void* ptr, World& world, EntityId entity) {\n"
    "                ComponentTraits<T>::OnAdd(*static_cast<T*>(ptr), world, entity);\n"
    "            };",
    "meta.OnAdd = [](void* ptr, ResourceStore& resources, EntityId entity) {\n"
    "                ComponentTraits<T>::OnAdd(*static_cast<T*>(ptr), resources, entity);\n"
    "            };",
    label="OnAdd erasure")
text = replace_exact(
    text,
    "meta.OnRemove = [](const void* ptr, World& world, EntityId entity) {\n"
    "                ComponentTraits<T>::OnRemove(*static_cast<const T*>(ptr), world, entity);\n"
    "            };",
    "meta.OnRemove = [](const void* ptr, ResourceStore& resources, EntityId entity) {\n"
    "                ComponentTraits<T>::OnRemove(*static_cast<const T*>(ptr), resources, entity);\n"
    "            };",
    label="OnRemove erasure")
dispatch_count = text.count(", *this, entity);")
if dispatch_count != 5:
    raise SystemExit(f"lifecycle dispatch: expected 5 World dispatches, found {dispatch_count}")
text = text.replace(", *this, entity);", ", LifecycleResourceStore(), entity);")
text = replace_exact(
    text,
    "    mutable uint32_t QueryDepth = 0;",
    "    ResourceStore* LifecycleResources = nullptr;\n\n"
    "    mutable uint32_t QueryDepth = 0;",
    label="World lifecycle resource member")
text = replace_exact(
    text,
    "        Resources = std::move(other.Resources);\n"
    "        QueryDepth = other.QueryDepth;",
    "        Resources = std::move(other.Resources);\n"
    "        LifecycleResources = other.LifecycleResources;\n"
    "        QueryDepth = other.QueryDepth;",
    label="World move resource binding")
text = replace_exact(
    text,
    "        other.Resources.clear();\n"
    "        other.QueryDepth = 0;",
    "        other.Resources.clear();\n"
    "        other.LifecycleResources = nullptr;\n"
    "        other.QueryDepth = 0;",
    label="World moved-from binding")
helper_anchor = """    void InvokeRemoveHooks(EntityId entity,
                           const Archetype& archetype,
                           EntityLocation location)
"""
helper = """    ResourceStore& LifecycleResourceStore()
    {
        assert(LifecycleResources != nullptr
               && "Lifecycle dispatch requires a ResourceStore");
        return *LifecycleResources;
    }

"""
text = replace_exact(text, helper_anchor, helper + helper_anchor,
                     label="lifecycle resource accessor")
write(path, text)

# Registry member order now mechanically enforces lifecycle teardown.
path = "engine/include/world/registry/Registry.h"
text = read(path)
text = replace_exact(
    text,
    "    Registry()\n    {\n        Components.AdvanceFrame();\n    }",
    "    Registry()\n        : Components(Resources)\n    {\n        Components.AdvanceFrame();\n    }",
    label="default Registry binding")
text = replace_exact(
    text,
    "        : Id(id)\n        , Kind(kind)\n        , Zone(zone)",
    "        : Id(id)\n        , Kind(kind)\n        , Zone(zone)\n        , Components(Resources)",
    label="typed Registry binding")
text = replace_exact(
    text,
    "    ~Registry()\n    {\n        Components.ClearEntities();\n    }",
    "    ~Registry() = default;",
    label="Registry destructor")
write(path, text)

# Production lifecycle hooks consume ResourceStore.
path = "engine/include/render/StaticMeshComponent.h"
text = read(path)
text = replace_exact(text, "#include <ecs/World.h>", "#include <core/ResourceStore.h>",
                     label="StaticMesh ResourceStore include")
text = text.replace("World& world", "ResourceStore& resources")
text = text.replace("world.TryGetResource<StaticMeshComponentAssets>()",
                    "resources.TryGet<StaticMeshComponentAssets>()")
write(path, text)

path = "engine/include/audio/AudioSourceComponent.h"
text = read(path)
text = replace_exact(text, "#include <ecs/World.h>", "#include <core/ResourceStore.h>",
                     label="AudioSource ResourceStore include")
text = text.replace("World& world", "ResourceStore& resources")
text = text.replace("world.TryGetResource<AudioSourceRuntime>()",
                    "resources.TryGet<AudioSourceRuntime>()")
write(path, text)

path = "engine/include/audio/AudioCaptionComponent.h"
text = read(path)
text = replace_exact(text, "#include <ecs/World.h>", "#include <core/ResourceStore.h>",
                     label="AudioCaption ResourceStore include")
text = text.replace("World& world", "ResourceStore& resources")
text = text.replace("world.TryGetResource<AudioSourceRuntime>()",
                    "resources.TryGet<AudioSourceRuntime>()")
write(path, text)

# Register lifecycle bindings on the sole registry resource owner.
path = "engine/src/zone/DefaultZoneBuilder.cpp"
text = read(path)
old = """    registry.Resources.Register<ActiveCameraService>();
    // Storage traits, not raw RegisterComponent: LocalTransform's traits also
"""
new = """    registry.Resources.Register<ActiveCameraService>();
    registry.Resources.Register<StaticMeshComponentAssets>(meshes, materialSets);
    registry.Resources.Register<AudioSourceRuntime>(audioClips, audio, captions);
    // Storage traits, not raw RegisterComponent: LocalTransform's traits also
"""
text = replace_exact(text, old, new, label="DefaultZoneBuilder lifecycle resources")
text = replace_exact(
    text,
    "    registry.Components.AddResource<StaticMeshComponentAssets>(meshes, materialSets);\n"
    "    registry.Components.AddResource<AudioSourceRuntime>(audioClips, audio, captions);\n",
    "",
    label="remove World lifecycle registrations")
write(path, text)

path = "editor/kyusu/src/document/EditorDocument.cpp"
text = read(path)
text = replace_exact(
    text,
    "    World& world = Registry_.Components;\n"
    "    if (!world.HasResource<StaticMeshComponentAssets>())\n"
    "        world.AddResource<StaticMeshComponentAssets>(&assets.StaticMeshes, &assets.MaterialSets);",
    "    if (!Registry_.Resources.Has<StaticMeshComponentAssets>())\n"
    "        Registry_.Resources.Register<StaticMeshComponentAssets>(\n"
    "            &assets.StaticMeshes, &assets.MaterialSets);",
    label="Editor lifecycle resource registration")
write(path, text)

for path in ["engine/src/audio/AudioSystem.cpp", "engine/src/audio/CaptionSystem.cpp"]:
    text = read(path)
    text = text.replace("registry.Components.TryGetResource<AudioSourceRuntime>()",
                        "registry.Resources.TryGet<AudioSourceRuntime>()")
    write(path, text)

# Runtime audio tests register bindings on Registry::Resources.
for path in ["test/runtime/AudioRuntimeTests.cpp", "test/runtime/CaptionSystemTests.cpp"]:
    text = read(path)
    text = text.replace(".Components.AddResource<AudioSourceRuntime>",
                        ".Resources.Register<AudioSourceRuntime>")
    text = text.replace(".Components.TryGetResource<AudioSourceRuntime>",
                        ".Resources.TryGet<AudioSourceRuntime>")
    write(path, text)

# Lifecycle contract tests now prove resource-only hooks and isolation.
write("test/ecs/ComponentTeardownTests.cpp", """#include <gtest/gtest.h>

#include <core/ResourceStore.h>
#include <ecs/CommandBuffer.h>
#include <ecs/Ecs.h>
#include <world/registry/Registry.h>
#include <zone/ZoneRuntime.h>

struct ComponentTeardownState
{
    int* AddCount = nullptr;
    int* RemoveCount = nullptr;
    int* LastValue = nullptr;
};

struct ComponentTeardownProbe
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(ComponentTeardownProbe, "test.component_teardown_probe");

template <>
struct ComponentTraits<ComponentTeardownProbe>
{
    static void OnAdd(ComponentTeardownProbe& component,
                      ResourceStore& resources,
                      EntityId)
    {
        ComponentTeardownState& state = resources.Get<ComponentTeardownState>();
        if (state.AddCount != nullptr)
            ++*state.AddCount;
        if (state.LastValue != nullptr)
            *state.LastValue = component.Value;
    }

    static void OnRemove(const ComponentTeardownProbe& component,
                         ResourceStore& resources,
                         EntityId)
    {
        ComponentTeardownState& state = resources.Get<ComponentTeardownState>();
        if (state.RemoveCount != nullptr)
            ++*state.RemoveCount;
        if (state.LastValue != nullptr)
            *state.LastValue = component.Value;
    }
};

namespace
{
    void PrepareWorld(World& world,
                      ResourceStore& resources,
                      int& removeCount,
                      int& lastValue,
                      int* addCount = nullptr)
    {
        resources.Register<ComponentTeardownState>(ComponentTeardownState{
            .AddCount = addCount,
            .RemoveCount = &removeCount,
            .LastValue = &lastValue,
        });
        world.RegisterComponent<ComponentTeardownProbe>();
    }

    EntityId AddProbe(World& world, int value)
    {
        const EntityId entity = world.CreateEntity();
        world.AddComponent(entity, ComponentTeardownProbe{ value });
        return entity;
    }
}

TEST(ComponentTeardown, DestroyEntityRunsRemoveHookExactlyOnce)
{
    int removeCount = 0;
    int lastValue = 0;
    ResourceStore resources;
    World world(resources);
    PrepareWorld(world, resources, removeCount, lastValue);

    const EntityId entity = AddProbe(world, 17);
    world.DestroyEntity(entity);

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 17);
    EXPECT_FALSE(world.IsAlive(entity));

    world.ClearEntities();
    EXPECT_EQ(removeCount, 1);
}

TEST(ComponentTeardown, CommandBufferDestroyRunsRemoveHook)
{
    int removeCount = 0;
    int lastValue = 0;
    ResourceStore resources;
    World world(resources);
    PrepareWorld(world, resources, removeCount, lastValue);

    const EntityId entity = AddProbe(world, 23);
    CommandBuffer commands(world);
    commands.DestroyEntity(entity);
    commands.Flush();

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 23);
    EXPECT_FALSE(world.IsAlive(entity));
}

TEST(ComponentTeardown, RawMutationUsesRegisteredLifecycleMetadata)
{
    int addCount = 0;
    int removeCount = 0;
    int lastValue = 0;
    ResourceStore resources;
    World world(resources);
    PrepareWorld(world, resources, removeCount, lastValue, &addCount);

    const EntityId entity = world.CreateEntity();
    const ComponentId component = world.GetComponentId<ComponentTeardownProbe>();
    const ComponentTeardownProbe value{ 29 };

    world.AddComponentRaw(entity, component, &value);
    EXPECT_EQ(addCount, 1);
    EXPECT_EQ(lastValue, 29);

    world.RemoveComponentRaw(entity, component);
    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 29);
}

TEST(ComponentTeardown, CommandBufferUsesRegisteredLifecycleMetadata)
{
    int addCount = 0;
    int removeCount = 0;
    int lastValue = 0;
    ResourceStore resources;
    World world(resources);
    PrepareWorld(world, resources, removeCount, lastValue, &addCount);

    const EntityId entity = world.CreateEntity();
    CommandBuffer commands(world);
    commands.AddComponent(entity, ComponentTeardownProbe{ 31 });
    commands.Flush();

    EXPECT_EQ(addCount, 1);
    EXPECT_EQ(lastValue, 31);

    commands.RemoveComponent<ComponentTeardownProbe>(entity);
    commands.Flush();

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 31);
}

TEST(ComponentTeardown, ClearEntitiesRunsEveryHookAndPreservesSetup)
{
    int removeCount = 0;
    int lastValue = 0;
    ResourceStore resources;
    World world(resources);
    PrepareWorld(world, resources, removeCount, lastValue);

    AddProbe(world, 1);
    AddProbe(world, 2);
    AddProbe(world, 3);

    world.ClearEntities();

    EXPECT_EQ(removeCount, 3);
    EXPECT_EQ(world.EntityCount(), 0u);
    EXPECT_TRUE(world.IsRegistered<ComponentTeardownProbe>());
    EXPECT_TRUE(resources.Has<ComponentTeardownState>());

    world.ClearEntities();
    EXPECT_EQ(removeCount, 3);
}

TEST(ComponentTeardown, WorldDestructorRunsHooksBeforeBoundResourcesDie)
{
    int removeCount = 0;
    int lastValue = 0;
    ResourceStore resources;
    {
        World world(resources);
        PrepareWorld(world, resources, removeCount, lastValue);
        AddProbe(world, 37);
    }

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 37);
    EXPECT_TRUE(resources.Has<ComponentTeardownState>());
}

TEST(ComponentTeardown, RegistryDestructorRunsHooksBeforeRegistryResourcesDie)
{
    int removeCount = 0;
    int lastValue = 0;
    {
        Registry registry(RegistryId{ 2, 1 }, RegistryKind::Zone, ZoneId{ 1 });
        PrepareWorld(registry.Components, registry.Resources, removeCount, lastValue);
        AddProbe(registry.Components, 41);
    }

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 41);
}

TEST(ComponentTeardown, RegistriesUseIsolatedLifecycleResources)
{
    int leftRemoves = 0;
    int rightRemoves = 0;
    int leftValue = 0;
    int rightValue = 0;

    Registry left(RegistryId{ 2, 1 }, RegistryKind::Zone, ZoneId{ 1 });
    Registry right(RegistryId{ 3, 1 }, RegistryKind::Zone, ZoneId{ 2 });
    PrepareWorld(left.Components, left.Resources, leftRemoves, leftValue);
    PrepareWorld(right.Components, right.Resources, rightRemoves, rightValue);

    const EntityId leftEntity = AddProbe(left.Components, 11);
    const EntityId rightEntity = AddProbe(right.Components, 22);

    left.Components.DestroyEntity(leftEntity);
    EXPECT_EQ(leftRemoves, 1);
    EXPECT_EQ(leftValue, 11);
    EXPECT_EQ(rightRemoves, 0);

    right.Components.DestroyEntity(rightEntity);
    EXPECT_EQ(rightRemoves, 1);
    EXPECT_EQ(rightValue, 22);
}

TEST(ComponentTeardown, DestroyZoneRunsHooks)
{
    int removeCount = 0;
    int lastValue = 0;
    ZoneRuntime runtime;
    Registry& zone = runtime.CreateZone(ZoneId{ 1 });
    PrepareWorld(zone.Components, zone.Resources, removeCount, lastValue);
    AddProbe(zone.Components, 43);

    EXPECT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 43);
}

TEST(ComponentTeardown, ZoneRuntimeClearCoversGlobalActiveAndDormantRegistries)
{
    int removeCount = 0;
    int lastValue = 0;
    ZoneRuntime runtime;

    PrepareWorld(runtime.Global().Components, runtime.Global().Resources,
                 removeCount, lastValue);
    AddProbe(runtime.Global().Components, 1);

    Registry& active = runtime.CreateZone(ZoneId{ 1 });
    PrepareWorld(active.Components, active.Resources, removeCount, lastValue);
    AddProbe(active.Components, 2);
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Logic = true });

    Registry& dormant = runtime.CreateZone(ZoneId{ 2 });
    PrepareWorld(dormant.Components, dormant.Resources, removeCount, lastValue);
    AddProbe(dormant.Components, 3);

    runtime.ClearEntities();

    EXPECT_EQ(removeCount, 3);
    EXPECT_EQ(runtime.Global().Components.EntityCount(), 0u);
    EXPECT_EQ(active.Components.EntityCount(), 0u);
    EXPECT_EQ(dormant.Components.EntityCount(), 0u);
}
""")

# Adapt the broad ECS fixture to a bound ResourceStore and remove the now-impossible
# structural mutation hook test.
path = "test/ecs/EcsTests.cpp"
text = read(path)
text = replace_exact(text, "#include <ecs/Ecs.h>\n", "#include <core/ResourceStore.h>\n#include <ecs/Ecs.h>\n",
                     label="EcsTests ResourceStore include")
text = replace_exact(text, "struct HookAddsMass { int Id = 0; };\n", "",
                     label="remove structural hook component")
old_tracked = """template <>
struct ComponentTraits<Tracked>
{
    static void OnAdd(Tracked& /*c*/, World& /*w*/, EntityId /*e*/)
    {
        ++g_OnAddCount;
    }
    static void OnRemove(const Tracked& /*c*/, World& /*w*/, EntityId /*e*/)
    {
        ++g_OnRemoveCount;
    }
};

template <>
struct ComponentTraits<HookAddsMass>
{
    static void OnAdd(HookAddsMass& /*c*/, World& w, EntityId e)
    {
        w.AddComponent<Mass>(e, { 10.f });
    }
};
"""
new_tracked = """template <>
struct ComponentTraits<Tracked>
{
    static void OnAdd(Tracked&, ResourceStore&, EntityId)
    {
        ++g_OnAddCount;
    }

    static void OnRemove(const Tracked&, ResourceStore&, EntityId)
    {
        ++g_OnRemoveCount;
    }
};
"""
text = replace_exact(text, old_tracked, new_tracked, label="EcsTests hook signatures")
text = replace_exact(text,
                     'SENCHA_DECLARE_COMPONENT_TYPE(HookAddsMass, "test.hook_adds_mass");\n',
                     "",
                     label="remove hook component identity")
text = replace_exact(text,
                     "        world.RegisterComponent<Tracked>();\n"
                     "        world.RegisterComponent<HookAddsMass>();",
                     "        world.RegisterComponent<Tracked>();",
                     label="remove hook component registration")
text = replace_exact(text,
                     "    World world;\n};",
                     "    ResourceStore resources;\n    World world{ resources };\n};",
                     label="bind EcsTest world")
old_test = """TEST_F(EcsTest, LifecycleHook_StructuralMutationAssertsInDebug)
{
    EntityId e = world.CreateEntity();
#ifdef NDEBUG
    GTEST_SKIP() << "Assertion only fires in debug builds.";
#else
    EXPECT_DEATH(world.AddComponent<HookAddsMass>(e, { 1 }),
                 "lifecycle hook is active");
#endif
}
"""
new_test = """TEST_F(EcsTest, HookedComponentRegistrationRequiresResourceStore)
{
#ifdef NDEBUG
    GTEST_SKIP() << "Assertion only fires in debug builds.";
#else
    World unbound;
    EXPECT_DEATH(unbound.RegisterComponent<Tracked>(),
                 "requires a ResourceStore");
#endif
}
"""
text = replace_exact(text, old_test, new_test, label="resource binding contract test")
write(path, text)

# Documentation records the final hook boundary.
path = "docs/ecs/component-traits.md"
text = read(path)
text = text.replace("World&", "ResourceStore&")
text = text.replace("the `World`", "the registry resource store")
text = text.replace("World resource", "registry resource")
write(path, text)

# Audit the mechanical contract before committing.
remaining_old_hooks = []
for root in [Path("engine"), Path("test"), Path("editor"), Path("template"), Path("example")]:
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix not in {".h", ".cpp"}:
            continue
        try:
            content = path.read_text()
        except UnicodeDecodeError:
            continue
        if re.search(r"static\s+void\s+On(?:Add|Remove)[^{;]*World&", content, re.DOTALL):
            remaining_old_hooks.append(str(path))
if remaining_old_hooks:
    raise SystemExit("old World lifecycle signatures remain: " + ", ".join(remaining_old_hooks))

for old in [
    "Components.AddResource<StaticMeshComponentAssets>",
    "Components.AddResource<AudioSourceRuntime>",
    "Components.TryGetResource<AudioSourceRuntime>",
]:
    hits = []
    for path in Path(".").rglob("*"):
        if not path.is_file() or path.suffix not in {".h", ".cpp"}:
            continue
        try:
            content = path.read_text()
        except UnicodeDecodeError:
            continue
        if old in content:
            hits.append(str(path))
    if hits:
        raise SystemExit(f"old lifecycle resource path remains for {old}: {hits}")
