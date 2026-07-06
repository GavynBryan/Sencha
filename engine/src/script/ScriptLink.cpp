#include <script/ScriptLink.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/transform/TransformComponents.h>

#include <array>
#include <format>
#include <string_view>

namespace
{
    // Native components a script may name, with their field layout expressed
    // the way the compiler emits bind paths. Kept explicit (not reflected)
    // because Transform3f has no TypeSchema recursion; this is the engine's
    // authoritative statement of what "Transform" means to a script.
    struct HostFieldLayout
    {
        std::string_view Path;   // e.g. "local.position" or "local.position.y"
        std::uint16_t Offset;    // bytes from the component start
        ScriptScalarKind Scalar;
        std::uint8_t SlotCount;
    };

    struct HostComponentLayout
    {
        std::string_view Name;
        ComponentTypeId Type;
        std::vector<HostFieldLayout> Fields;
    };

    // LocalTransform.Value is a Transform3f: position (f32 x3) at 0, rotation
    // (f32 x4) at 12, scale (f32 x3) at 28. Offsets verified against the
    // struct layout.
    const std::vector<HostComponentLayout>& HostComponents()
    {
        static const std::vector<HostComponentLayout> table = [] {
            std::vector<HostComponentLayout> t;
            HostComponentLayout transform;
            transform.Name = "Transform";
            transform.Type = ResolveComponentTypeId<LocalTransform>();
            transform.Fields = {
                { "local.position", 0, ScriptScalarKind::Float, 3 },
                { "local.position.x", 0, ScriptScalarKind::Float, 1 },
                { "local.position.y", 4, ScriptScalarKind::Float, 1 },
                { "local.position.z", 8, ScriptScalarKind::Float, 1 },
                { "local.scale", 28, ScriptScalarKind::Float, 3 },
                { "local.scale.x", 28, ScriptScalarKind::Float, 1 },
                { "local.scale.y", 32, ScriptScalarKind::Float, 1 },
                { "local.scale.z", 36, ScriptScalarKind::Float, 1 },
            };
            t.push_back(std::move(transform));
            return t;
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

    result.ModuleIndex = static_cast<std::uint32_t>(runtime.Modules.size());
    runtime.Modules.push_back(std::move(linked));
    result.Ok = true;
    return result;
}
