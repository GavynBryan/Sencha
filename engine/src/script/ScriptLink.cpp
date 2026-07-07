#include <script/ScriptLink.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <script/ScriptComponentSerializer.h>
#include <script/ScriptHostSurface.h>
#include <script/ScriptIntrinsics.h>
#include <script/WorldScriptHost.h>
#include <world/transform/TransformComponents.h>

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

namespace
{
    // A native-component field resolved to storage, at the granularity a script
    // may bind it: the whole group ("local.position", 3 slots) or one axis
    // ("local.position.y", 1 slot). Expanded from the shared host surface.
    struct HostFieldLayout
    {
        std::string Path;     // "local.position" or "local.position.y"
        std::uint16_t Offset; // bytes from the component start
        ScriptScalarKind Scalar;
        std::uint8_t SlotCount;
    };

    struct HostComponentLayout
    {
        std::string_view Name;
        ComponentTypeId Type;
        std::vector<HostFieldLayout> Fields;
    };

    // The byte offsets the shared surface declares must match the real struct.
    // The runtime reads component storage by raw offset, so a layout change has
    // to fail here rather than corrupt a field.
    static_assert(std::is_standard_layout_v<Transform3f>,
                  "script host offsets assume a standard-layout Transform3f");
    static_assert(offsetof(LocalTransform, Value) + offsetof(Transform3f, Position) == 0,
                  "Transform position offset changed; update ScriptHostSurface");
    static_assert(offsetof(LocalTransform, Value) + offsetof(Transform3f, Scale) == 28,
                  "Transform scale offset changed; update ScriptHostSurface");

    // The native component type behind a host-surface name. Small and closed:
    // mapping a script-visible name to a C++ component type is inherently code,
    // not data.
    ComponentTypeId HostComponentType(std::string_view name)
    {
        if (name == "Transform")
        {
            return ResolveComponentTypeId<LocalTransform>();
        }
        return ComponentTypeId{};
    }

    // Expands the shared host surface into resolvable bind paths: each group
    // field plus its per-axis leaves, so a script binding either granularity
    // resolves to the same storage.
    const std::vector<HostComponentLayout>& HostComponents()
    {
        static const std::vector<HostComponentLayout> table = [] {
            static constexpr std::string_view kAxes[] = { "x", "y", "z", "w" };
            std::vector<HostComponentLayout> out;
            for (const ScriptHostComponent& component : ScriptHostComponents())
            {
                HostComponentLayout layout;
                layout.Name = component.Name;
                layout.Type = HostComponentType(component.Name);
                for (const ScriptHostField& field : component.Fields)
                {
                    const auto scalarSize = static_cast<std::uint16_t>(
                        ScriptScalarSize(static_cast<std::uint8_t>(field.Scalar)));
                    layout.Fields.push_back({ std::string(field.BindPath), field.ByteOffset,
                                              field.Scalar, field.SlotCount });
                    for (std::uint8_t axis = 0; axis < field.SlotCount && axis < 4; ++axis)
                    {
                        layout.Fields.push_back(
                            { std::string(field.BindPath) + "." + std::string(kAxes[axis]),
                              static_cast<std::uint16_t>(field.ByteOffset + axis * scalarSize),
                              field.Scalar, 1 });
                    }
                }
                out.push_back(std::move(layout));
            }
            return out;
        }();
        return table;
    }

    const HostComponentLayout* FindHostComponent(std::string_view name)
    {
        for (const HostComponentLayout& c : HostComponents())
        {
            if (c.Name == name)
            {
                return &c;
            }
        }
        return nullptr;
    }
}

ScriptLinkResult LinkScriptModule(World& world, std::shared_ptr<const ScriptModule> module)
{
    ScriptLinkResult result;
    auto fail = [&result](std::string message) -> ScriptLinkResult& {
        result.Ok = false;
        result.Error = std::move(message);
        return result;
    };

    if (module == nullptr)
    {
        return fail("cannot link a null module");
    }

    if (!world.HasResource<ScriptRuntime>())
    {
        world.AddResource<ScriptRuntime>();
    }
    ScriptRuntime& runtime = world.GetResource<ScriptRuntime>();

    ScriptLinkedModule linked;
    linked.Module = module;

    // 1. Register the module's script-defined components. RegisterComponentRaw
    // is idempotent per ComponentTypeId, so two modules declaring the same
    // component (identical schema) share one registration.
    for (const ScriptComponentDef& component : module->Components)
    {
        const std::string_view name = module->GetString(component.Name);
        const ScriptComponentLayout layout = ComputeScriptComponentLayout(component);
        const ComponentTypeId typeId =
            MakeComponentTypeId(std::string("script.") + std::string(name));
        world.RegisterComponentRaw(name, typeId, layout.Size, layout.Alignment, /*isTag*/ false);
    }

    // Register a serializer per script component so authored field values
    // round-trip through scene save/load (idempotent on the identity triple, so
    // a second zone or a hot reload re-registering the same module is a no-op).
    RegisterScriptComponentSerializers(*module);

    // 2. Tag binds.
    GameplayTagRegistry* tagRegistry = world.TryGetResource<GameplayTagRegistry>();
    linked.Tags.reserve(module->TagBinds.size());
    for (const std::uint32_t nameIdx : module->TagBinds)
    {
        const std::string_view name = module->GetString(nameIdx);
        if (tagRegistry == nullptr)
        {
            return fail(std::format("script references tag '{}' but the world has no "
                                    "GameplayTagRegistry", name));
        }
        // Registering is idempotent and creates missing parents; a script that
        // names a tag effectively declares it.
        const auto id = tagRegistry->RegisterTag(name);
        if (!id)
        {
            return fail(std::format("could not register tag '{}'", name));
        }
        linked.Tags.push_back(*id);
    }

    // 3. Component binds (has(), commands). For script components the module
    // schema index is recorded so commands.add can marshal the record image.
    linked.Components.reserve(module->ComponentBinds.size());
    linked.ComponentSchemaIndex.reserve(module->ComponentBinds.size());
    for (const std::uint32_t nameIdx : module->ComponentBinds)
    {
        const std::string_view name = module->GetString(nameIdx);
        ComponentId id = InvalidComponentId;
        std::int32_t schemaIndex = -1;
        if (const HostComponentLayout* host = FindHostComponent(name))
        {
            id = world.GetComponentIdByType(host->Type);
        }
        else
        {
            id = world.GetComponentIdByType(
                MakeComponentTypeId(std::string("script.") + std::string(name)));
            for (std::size_t i = 0; i < module->Components.size(); ++i)
            {
                if (module->GetString(module->Components[i].Name) == name)
                {
                    schemaIndex = static_cast<std::int32_t>(i);
                    break;
                }
            }
        }
        if (id == InvalidComponentId)
        {
            return fail(std::format("script references component '{}' which is not "
                                    "registered in this world", name));
        }
        linked.Components.push_back(id);
        linked.ComponentSchemaIndex.push_back(schemaIndex);
    }

    // 4. Field binds.
    linked.Fields.reserve(module->FieldBinds.size());
    for (const ScriptFieldBind& bind : module->FieldBinds)
    {
        const std::string_view componentName = module->GetString(bind.ComponentName);
        const std::string_view path = module->GetString(bind.FieldPath);
        ResolvedFieldBind resolved;

        if (const HostComponentLayout* host = FindHostComponent(componentName))
        {
            const ComponentId id = world.GetComponentIdByType(host->Type);
            if (id == InvalidComponentId)
            {
                return fail(std::format("script uses '{}' but component '{}' is not "
                                        "registered in this world", path, componentName));
            }
            const HostFieldLayout* field = nullptr;
            for (const HostFieldLayout& candidate : host->Fields)
            {
                if (candidate.Path == path)
                {
                    field = &candidate;
                    break;
                }
            }
            if (field == nullptr)
            {
                return fail(std::format("component '{}' has no field '{}'",
                                        componentName, path));
            }
            resolved.Component = id;
            resolved.Offset = field->Offset;
            resolved.Scalar = static_cast<std::uint8_t>(field->Scalar);
            resolved.SlotCount = field->SlotCount;
            resolved.Ok = true;
        }
        else
        {
            // Script component: resolve the leaf group from the module schema.
            const ComponentTypeId typeId = MakeComponentTypeId(
                std::string("script.") + std::string(componentName));
            const ComponentId id = world.GetComponentIdByType(typeId);
            if (id == InvalidComponentId)
            {
                return fail(std::format("script component '{}' is not registered",
                                        componentName));
            }
            const ScriptComponentDef* schema = nullptr;
            for (const ScriptComponentDef& c : module->Components)
            {
                if (module->GetString(c.Name) == componentName)
                {
                    schema = &c;
                    break;
                }
            }
            if (schema == nullptr)
            {
                return fail(std::format("script component '{}' has no schema in this module",
                                        componentName));
            }
            // The bind path names either a leaf ("target.x") or a group
            // ("target", matching leaves "target.x/.y/.z"). Find the matching
            // leaves in offset order.
            const std::string exact(path);
            const std::string prefix = exact + ".";
            std::uint16_t minOffset = 0xFFFF;
            std::uint8_t count = 0;
            std::uint8_t scalar = 0;
            for (const ScriptComponentField& leaf : schema->Fields)
            {
                const std::string_view leafPath = module->GetString(leaf.Name);
                if (leafPath == exact || leafPath.starts_with(prefix))
                {
                    minOffset = std::min(minOffset, leaf.ByteOffset);
                    count = static_cast<std::uint8_t>(count + std::max<std::uint8_t>(leaf.ArrayCount, 1));
                    scalar = leaf.Scalar;
                }
            }
            if (count == 0)
            {
                return fail(std::format("script component '{}' has no field '{}'",
                                        componentName, path));
            }
            resolved.Component = id;
            resolved.Offset = minOffset;
            resolved.Scalar = scalar;
            resolved.SlotCount = count;
            resolved.Ok = true;
        }
        linked.Fields.push_back(resolved);
    }

    // 5. Host imports. Resolve each dotted name once to an intrinsic id (the VM
    // executes those inline) or a host op id (WorldScriptHost dispatches on it),
    // so neither path compares names at call time.
    linked.Imports.reserve(module->HostImports.size());
    for (const ScriptHostImport& import : module->HostImports)
    {
        const std::string_view name = module->GetString(import.Name);
        const std::int32_t intrinsic = FindScriptIntrinsic(name);
        if (intrinsic >= 0)
        {
            linked.Imports.push_back({ ScriptImportKind::Intrinsic, intrinsic });
        }
        else
        {
            linked.Imports.push_back(
                { ScriptImportKind::Host, static_cast<std::int32_t>(ResolveScriptHostOp(name)) });
        }
    }

    // 6. Per-declaration `requires` sets: resolve each named component (script or
    // host) to this world's ComponentId. The tick gates a declaration on them.
    linked.DeclarationRequired.reserve(module->Declarations.size());
    for (const ScriptDeclaration& decl : module->Declarations)
    {
        std::vector<ComponentId> required;
        required.reserve(decl.RequiredComponents.size());
        for (const std::uint32_t nameIdx : decl.RequiredComponents)
        {
            const std::string_view name = module->GetString(nameIdx);
            ComponentId id = InvalidComponentId;
            if (const HostComponentLayout* host = FindHostComponent(name))
            {
                id = world.GetComponentIdByType(host->Type);
            }
            else
            {
                id = world.GetComponentIdByType(
                    MakeComponentTypeId(std::string("script.") + std::string(name)));
            }
            if (id == InvalidComponentId)
            {
                return fail(std::format("declaration '{}' requires component '{}' which is not "
                                        "registered in this world",
                                        module->GetString(decl.Name), name));
            }
            required.push_back(id);
        }
        linked.DeclarationRequired.push_back(std::move(required));
    }

    result.ModuleIndex = static_cast<std::uint32_t>(runtime.Modules.size());
    runtime.Modules.push_back(std::move(linked));
    // Keep ModuleHandles parallel; the scene-link step overwrites the slot
    // with the asset handle that loaded the module.
    runtime.ModuleHandles.emplace_back();
    result.Ok = true;
    return result;
}
