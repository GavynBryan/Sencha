#include "scene_source/SceneSourceDocument.h"

#include "scene_source/Json5Convert.h"
#include "scene_source/Json5Parser.h"
#include "scene_source/Json5Writer.h"

#include <core/assets/AssetPath.h>
#include <core/assets/AssetRefCodec.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace
{
    bool Fail(std::string* error, std::string message)
    {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    }

    // ── Reading ──────────────────────────────────────────────────────────────

    bool ReadId(const Json5Value& object, std::string_view key, bool required,
                std::uint64_t& out, std::string* error, std::string_view what)
    {
        out = 0;
        const Json5Value* value = object.Find(key);
        if (value == nullptr)
        {
            if (required)
                return Fail(error, std::string(what) + ": missing '" + std::string(key) + "'");
            return true;
        }
        if (!value->IsString())
            return Fail(error, std::string(what) + ": '" + std::string(key)
                + "' must be a 16-hex-digit id string");
        const std::optional<PersistentEntityId> parsed =
            PersistentEntityIdFromString(value->Text);
        if (!parsed.has_value())
            return Fail(error, std::string(what) + ": malformed id '" + value->Text + "'");
        if (!IsAuthoredPersistentEntityId(*parsed))
            return Fail(error, std::string(what) + ": id '" + value->Text
                + "' uses the reserved runtime namespace");
        out = parsed->Value;
        return true;
    }

    bool ReadVec(const Json5Value& transform, std::string_view key, std::size_t arity,
                 float* out, std::string* error, std::string_view what)
    {
        const Json5Value* value = transform.Find(key);
        if (value == nullptr)
            return true; // identity default
        if (!value->IsArray() || value->Elements.size() != arity)
            return Fail(error, std::string(what) + ": '" + std::string(key) + "' must be ["
                + std::to_string(arity) + " numbers]");
        for (std::size_t i = 0; i < arity; ++i)
        {
            if (!value->Elements[i].IsNumber())
                return Fail(error, std::string(what) + ": '" + std::string(key)
                    + "' must hold numbers");
            out[i] = static_cast<float>(value->Elements[i].Number);
        }
        return true;
    }

    bool ReadTransform(const Json5Value& record, Transform3f& out,
                       std::string* error, std::string_view what)
    {
        const Json5Value* transform = record.Find("transform");
        if (transform == nullptr)
            return true;
        if (!transform->IsObject())
            return Fail(error, std::string(what) + ": 'transform' must be an object");
        float position[3] = { 0.0f, 0.0f, 0.0f };
        float rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float scale[3] = { 1.0f, 1.0f, 1.0f };
        if (!ReadVec(*transform, "position", 3, position, error, what)
            || !ReadVec(*transform, "rotation", 4, rotation, error, what)
            || !ReadVec(*transform, "scale", 3, scale, error, what))
        {
            return false;
        }
        out.Position = Vec3d{ position[0], position[1], position[2] };
        out.Rotation = Quat<float>{ rotation[0], rotation[1], rotation[2], rotation[3] };
        out.Scale = Vec3d{ scale[0], scale[1], scale[2] };
        return true;
    }

    bool ReadPathKey(std::string_view text, SceneElementPath& out,
                     std::string* error, std::string_view what)
    {
        const std::optional<SceneElementPath> path = SceneElementPath::FromString(text);
        if (!path.has_value())
            return Fail(error, std::string(what) + ": malformed element path '"
                + std::string(text) + "'");
        out = *path;
        return true;
    }

    // An asset://...sscene reference, by path or by {id, path}.
    bool ReadSceneRef(const Json5Value* value, std::string& path, AssetId& id, std::string* error,
                      const std::string& what)
    {
        const std::optional<AssetRef> ref = value != nullptr ? AssetRefFromJson(Json5ToJson(*value)) : std::nullopt;
        if (!ref.has_value() || !ref->Path.starts_with(kAssetPathPrefix) || !ref->Path.ends_with(".sscene"))
            return Fail(error, what + " must be an asset://...sscene reference");
        path = ref->Path;
        id = ref->Id;
        return true;
    }

    Json5Value SceneRefToJson5(const std::string& path, AssetId id)
    {
        return Json5FromJson(AssetRefToJson(AssetRef{ AssetType::Scene, path, id }));
    }

    // { "<entity id>": value }, read with `read` per entry.
    template<typename T, typename Read>
    bool ReadIdKeyed(const Json5Value& value, std::string_view key,
                     std::vector<std::pair<PersistentEntityId, T>>& out, std::string* error, Read&& read)
    {
        if (!value.IsObject())
            return Fail(error, "'" + std::string(key) + "' must be an object keyed by entity id");
        for (const Json5Value::Member& member : value.Members)
        {
            const std::optional<PersistentEntityId> id = PersistentEntityIdFromString(member.first);
            if (!id.has_value() || !IsAuthoredPersistentEntityId(*id))
                return Fail(error, std::string(key) + ": '" + member.first + "' is not an authored entity id");
            T entry;
            if (!read(member.second, entry, std::string(key) + " " + member.first))
                return false;
            out.emplace_back(*id, std::move(entry));
        }
        return true;
    }

    bool ReadInstance(const Json5Value& record, SceneInstanceRecord& out, std::string* error)
    {
        if (!record.IsObject())
            return Fail(error, "instances: each record must be an object");

        std::uint64_t id = 0;
        if (!ReadId(record, "id", true, id, error, "instance"))
            return false;
        out.Id = SceneInstanceId{ id };
        const std::string what = "instance " + SceneIdText(id);

        std::uint64_t parent = 0;
        if (!ReadId(record, "parent", false, parent, error, what))
            return false;
        out.Parent = PersistentEntityId{ parent };

        const Json5Value* source = record.Find("source");
        const std::optional<AssetRef> sourceRef =
            source != nullptr ? AssetRefFromJson(Json5ToJson(*source)) : std::nullopt;
        if (source == nullptr)
            return Fail(error, what + ": missing 'source'");
        if (!sourceRef.has_value())
            return Fail(error, what + ": source must be an asset://...sscene reference");
        out.Source = sourceRef->Path;
        out.SourceId = sourceRef->Id;
        if (const Json5Value* name = record.Find("name"); name != nullptr && name->IsString())
            out.Name = name->Text;
        if (!out.Source.starts_with(kAssetPathPrefix) || !out.Source.ends_with(".sscene"))
            return Fail(error, what + ": source must be an asset://...sscene reference, got '"
                + out.Source + "'");

        if (!ReadTransform(record, out.Placement, error, what))
            return false;

        if (const Json5Value* ids = record.Find("entity_ids"))
        {
            if (!ids->IsObject())
                return Fail(error, what + ": 'entity_ids' must be an object");
            for (const Json5Value::Member& member : ids->Members)
            {
                SceneElementPath path;
                if (!ReadPathKey(member.first, path, error, what))
                    return false;
                if (!member.second.IsString())
                    return Fail(error, what + ": entity id for '" + member.first
                        + "' must be an id string");
                const std::optional<PersistentEntityId> minted =
                    PersistentEntityIdFromString(member.second.Text);
                if (!minted.has_value() || !IsAuthoredPersistentEntityId(*minted))
                    return Fail(error, what + ": malformed minted id '"
                        + member.second.Text + "'");
                out.EntityIds.emplace_back(std::move(path), *minted);
            }
        }

        const auto readPathObject =
            [&](std::string_view key,
                std::vector<std::pair<SceneElementPath, Json5Value>>& into) -> bool
        {
            const Json5Value* group = record.Find(key);
            if (group == nullptr)
                return true;
            if (!group->IsObject())
                return Fail(error, what + ": '" + std::string(key) + "' must be an object");
            for (const Json5Value::Member& member : group->Members)
            {
                SceneElementPath path;
                if (!ReadPathKey(member.first, path, error, what))
                    return false;
                if (!member.second.IsObject())
                    return Fail(error, what + ": override for '" + member.first
                        + "' must be an object of components");
                into.emplace_back(std::move(path), member.second);
            }
            return true;
        };
        if (!readPathObject("patch", out.Patches)
            || !readPathObject("add", out.AddedComponents))
        {
            return false;
        }

        if (const Json5Value* removes = record.Find("remove"))
        {
            if (!removes->IsObject())
                return Fail(error, what + ": 'remove' must be an object");
            for (const Json5Value::Member& member : removes->Members)
            {
                SceneElementPath path;
                if (!ReadPathKey(member.first, path, error, what))
                    return false;
                if (!member.second.IsArray())
                    return Fail(error, what + ": remove for '" + member.first
                        + "' must be an array of component names");
                std::vector<std::string> names;
                for (const Json5Value& name : member.second.Elements)
                {
                    if (!name.IsString())
                        return Fail(error, what + ": remove entries must be strings");
                    names.push_back(name.Text);
                }
                out.RemovedComponents.emplace_back(std::move(path), std::move(names));
            }
        }

        if (const Json5Value* added = record.Find("add_entities"))
        {
            if (!added->IsArray())
                return Fail(error, what + ": 'add_entities' must be an array");
            for (const Json5Value& element : added->Elements)
            {
                if (!element.IsObject())
                    return Fail(error, what + ": each added entity must be an object");
                SceneAddedEntity entity;
                std::uint64_t addedId = 0;
                if (!ReadId(element, "id", true, addedId, error, what))
                    return false;
                entity.Id = PersistentEntityId{ addedId };
                if (const Json5Value* parentPath = element.Find("parent_path"))
                {
                    if (!parentPath->IsString()
                        || !ReadPathKey(parentPath->Text, entity.ParentPath, error, what))
                    {
                        return false;
                    }
                }
                if (const Json5Value* components = element.Find("components"))
                {
                    if (!components->IsObject())
                        return Fail(error, what + ": added entity components must be an object");
                    entity.Components = *components;
                }
                else
                {
                    entity.Components = Json5Value::MakeObject();
                }
                out.AddedEntities.push_back(std::move(entity));
            }
        }

        if (const Json5Value* suppressed = record.Find("suppress"))
        {
            if (!suppressed->IsArray())
                return Fail(error, what + ": 'suppress' must be an array of paths");
            for (const Json5Value& element : suppressed->Elements)
            {
                SceneElementPath path;
                if (!element.IsString()
                    || !ReadPathKey(element.Text, path, error, what))
                {
                    return false;
                }
                out.Suppressed.push_back(std::move(path));
            }
        }

        out.LeadingComments = record.LeadingComments;
        return true;
    }

    // ── Validation over the whole document ───────────────────────────────────

    bool Validate(const SceneSourceDocument& document, std::string* error)
    {
        // Every stable id this document owns lives in one 64-bit space, and a
        // collision anywhere in it would make some path or parent reference
        // ambiguous, so uniqueness is checked across all of them together.
        std::unordered_set<std::uint64_t> owned;
        const auto claim = [&](std::uint64_t id, std::string_view what) -> bool
        {
            if (!owned.insert(id).second)
                return Fail(error, std::string(what) + ": id " + SceneIdText(id)
                    + " is already used elsewhere in the document");
            return true;
        };

        for (const SceneSourceEntity& entity : document.Entities)
            if (!claim(entity.Id.Value, "entities"))
                return false;
        for (const SceneInstanceRecord& instance : document.Instances)
        {
            if (!claim(instance.Id.Value, "instances"))
                return false;
            for (const auto& [path, minted] : instance.EntityIds)
                if (!claim(minted.Value, "instance " + SceneIdText(instance.Id.Value)))
                    return false;
            for (const SceneAddedEntity& added : instance.AddedEntities)
                if (!claim(added.Id.Value, "instance " + SceneIdText(instance.Id.Value)))
                    return false;
        }

        // Parents point at ids this document defines as parentable: local
        // entities and instances.
        std::unordered_map<std::uint64_t, std::uint64_t> parentOf;
        std::unordered_set<std::uint64_t> parentable;
        for (const SceneSourceEntity& entity : document.Entities)
            parentable.insert(entity.Id.Value);
        for (const SceneInstanceRecord& instance : document.Instances)
            parentable.insert(instance.Id.Value);

        const auto checkParent =
            [&](std::uint64_t child, std::uint64_t parent, std::string_view what) -> bool
        {
            if (parent == 0)
                return true;
            if (child == parent)
                return Fail(error, std::string(what) + " " + SceneIdText(child)
                    + ": parents itself");
            // An inherited parent is one only composition knows: it checks it.
            if (!parentable.contains(parent) && document.Extends.empty())
                return Fail(error, std::string(what) + " " + SceneIdText(child)
                    + ": parent " + SceneIdText(parent) + " does not exist");
            parentOf.emplace(child, parent);
            return true;
        };
        for (const SceneSourceEntity& entity : document.Entities)
            if (!checkParent(entity.Id.Value, entity.Parent.Value, "entity"))
                return false;
        for (const SceneInstanceRecord& instance : document.Instances)
            if (!checkParent(instance.Id.Value, instance.Parent.Value, "instance"))
                return false;

        // Cycle check over the combined parent graph, bounded per walk.
        for (const auto& [start, first] : parentOf)
        {
            std::uint64_t current = first;
            for (std::size_t steps = 0;
                 current != 0 && steps <= parentOf.size();
                 ++steps)
            {
                if (current == start)
                    return Fail(error, "parent cycle through " + SceneIdText(start));
                const auto next = parentOf.find(current);
                current = next == parentOf.end() ? 0 : next->second;
            }
        }

        // A component both added and removed on one target contradicts itself.
        for (const SceneInstanceRecord& instance : document.Instances)
            for (const auto& [removePath, names] : instance.RemovedComponents)
                for (const auto& [addPath, components] : instance.AddedComponents)
                {
                    if (!(removePath == addPath))
                        continue;
                    for (const std::string& name : names)
                        if (components.Find(name) != nullptr)
                            return Fail(error,
                                "instance " + SceneIdText(instance.Id.Value) + ": component '"
                                    + name + "' on '" + removePath.ToString()
                                    + "' is both added and removed");
                }

        return true;
    }

    // ── Writing ──────────────────────────────────────────────────────────────

    [[nodiscard]] Json5Value WriteVec(const float* values, std::size_t arity)
    {
        Json5Value array = Json5Value::MakeArray();
        for (std::size_t i = 0; i < arity; ++i)
            array.Elements.emplace_back(static_cast<double>(values[i]));
        return array;
    }

} // namespace

Json5Value TransformToJson5(const Transform3f& transform)
{
    Json5Value object = Json5Value::MakeObject();
    const float position[3] = { transform.Position.X, transform.Position.Y,
                                transform.Position.Z };
    const float rotation[4] = { transform.Rotation.X, transform.Rotation.Y,
                                transform.Rotation.Z, transform.Rotation.W };
    const float scale[3] = { transform.Scale.X, transform.Scale.Y,
                             transform.Scale.Z };
    object.Members.emplace_back("position", WriteVec(position, 3));
    object.Members.emplace_back("rotation", WriteVec(rotation, 4));
    object.Members.emplace_back("scale", WriteVec(scale, 3));
    return object;
}

std::optional<SceneSourceDocument> ParseSceneSource(std::string_view text,
                                                    std::string* error)
{
    Json5ParseError parseError;
    std::vector<std::string> endComments;
    const std::optional<Json5Value> parsed = Json5Parse(text, &parseError, &endComments);
    if (!parsed.has_value())
    {
        (void)Fail(error, "line " + std::to_string(parseError.Line) + ", column "
            + std::to_string(parseError.Column) + ": " + parseError.Message);
        return std::nullopt;
    }
    if (!parsed->IsObject())
    {
        (void)Fail(error, "scene source must be an object");
        return std::nullopt;
    }

    const Json5Value* version = parsed->Find("format_version");
    if (version == nullptr || !version->IsNumber())
    {
        // The retired shape: a "version" number over a positional entity array
        // whose records have no ids. Name the cutover rather than reporting a
        // baffling missing-member error.
        if (parsed->Find("version") != nullptr && parsed->Find("entities") != nullptr)
        {
            (void)Fail(error,
                "this is a retired .level.json scene; the authored format is now "
                ".sscene and there is no compatibility loader -- convert the file");
            return std::nullopt;
        }
        (void)Fail(error, "missing 'format_version'");
        return std::nullopt;
    }
    if (version->Number != 1.0 && version->Number != static_cast<double>(SceneSourceDocument::Version))
    {
        (void)Fail(error, "unsupported format_version "
            + std::to_string(version->Number));
        return std::nullopt;
    }

    SceneSourceDocument document;
    document.FormatVersion = static_cast<std::uint32_t>(version->Number);
    document.LeadingComments = parsed->LeadingComments;
    document.TrailingComments = parsed->TrailingComments;
    document.EndComments = std::move(endComments);

    for (const Json5Value::Member& member : parsed->Members)
    {
        const std::string& key = member.first;
        const Json5Value& value = member.second;
        if (key == "format_version")
        {
            continue;
        }
        if (key == "settings")
        {
            if (!value.IsObject())
            {
                (void)Fail(error, "'settings' must be an object");
                return std::nullopt;
            }
            document.Settings = value;
        }
        else if (key == "brush_meshes")
        {
            document.BrushMeshes = value;
        }
        else if (key == "entities")
        {
            if (!value.IsArray())
            {
                (void)Fail(error, "'entities' must be an array");
                return std::nullopt;
            }
            for (const Json5Value& record : value.Elements)
            {
                if (!record.IsObject())
                {
                    (void)Fail(error, "entities: each record must be an object");
                    return std::nullopt;
                }
                SceneSourceEntity entity;
                std::uint64_t id = 0;
                if (!ReadId(record, "id", true, id, error, "entity"))
                    return std::nullopt;
                entity.Id = PersistentEntityId{ id };
                std::uint64_t parent = 0;
                if (!ReadId(record, "parent", false, parent, error,
                            "entity " + SceneIdText(id)))
                    return std::nullopt;
                entity.Parent = PersistentEntityId{ parent };
                if (const Json5Value* hidden = record.Find("hidden"))
                    entity.Hidden = hidden->IsBool() && hidden->Boolean;
                if (const Json5Value* locked = record.Find("locked"))
                    entity.Locked = locked->IsBool() && locked->Boolean;
                if (const Json5Value* components = record.Find("components"))
                {
                    if (!components->IsObject())
                    {
                        (void)Fail(error, "entity " + SceneIdText(id)
                            + ": 'components' must be an object");
                        return std::nullopt;
                    }
                    entity.Components = *components;
                }
                else
                {
                    entity.Components = Json5Value::MakeObject();
                }
                entity.LeadingComments = record.LeadingComments;
                document.Entities.push_back(std::move(entity));
            }
        }
        else if (key == "extends" || key == "includes" || key == "abstract" || key == "patch" || key == "remove")
        {
            if (document.FormatVersion < 2)
            {
                (void)Fail(error, "'" + key + "' needs format_version 2");
                return std::nullopt;
            }
            if (key == "extends" && !ReadSceneRef(&value, document.Extends, document.ExtendsId, error, "'extends'"))
                return std::nullopt;
            if (key == "abstract")
            {
                if (!value.IsBool())
                {
                    (void)Fail(error, "'abstract' must be true or false");
                    return std::nullopt;
                }
                document.Abstract = value.Boolean;
            }
            if (key == "includes")
            {
                if (!value.IsArray())
                {
                    (void)Fail(error, "'includes' must be an array");
                    return std::nullopt;
                }
                for (std::size_t i = 0; i < value.Elements.size(); ++i)
                {
                    const Json5Value& entry = value.Elements[i];
                    const std::string what = "includes[" + std::to_string(i) + "]";
                    SceneFragmentInclude include;
                    std::uint64_t onto = 0;
                    if (!entry.IsObject()
                        || !ReadSceneRef(entry.Find("fragment"), include.Fragment, include.FragmentId, error,
                                         what + " 'fragment'")
                        || !ReadId(entry, "onto", false, onto, error, what))
                    {
                        if (!entry.IsObject())
                            (void)Fail(error, what + " must be an object");
                        return std::nullopt;
                    }
                    include.Onto = PersistentEntityId{ onto };
                    document.Includes.push_back(std::move(include));
                }
            }
            if (key == "patch"
                && !ReadIdKeyed<Json5Value>(value, key, document.InheritedPatches, error,
                                            [&](const Json5Value& entry, Json5Value& out, const std::string& what) {
                                                if (!entry.IsObject())
                                                    return Fail(error, what + ": a patch is an object of components");
                                                out = entry;
                                                return true;
                                            }))
                return std::nullopt;
            if (key == "remove"
                && !ReadIdKeyed<std::vector<std::string>>(
                    value, key, document.InheritedRemovals, error,
                    [&](const Json5Value& entry, std::vector<std::string>& out, const std::string& what) {
                        if (!entry.IsArray())
                            return Fail(error, what + ": a list of component names");
                        for (const Json5Value& name : entry.Elements)
                        {
                            if (!name.IsString())
                                return Fail(error, what + ": a list of component names");
                            out.push_back(name.Text);
                        }
                        return true;
                    }))
                return std::nullopt;
        }
        else if (key == "instances")
        {
            if (!value.IsArray())
            {
                (void)Fail(error, "'instances' must be an array");
                return std::nullopt;
            }
            for (const Json5Value& record : value.Elements)
            {
                SceneInstanceRecord instance;
                if (!ReadInstance(record, instance, error))
                    return std::nullopt;
                document.Instances.push_back(std::move(instance));
            }
        }
        else
        {
            document.UnknownRoot.push_back(member);
        }
    }

    if (!Validate(document, error))
        return std::nullopt;
    return document;
}

std::string WriteSceneSource(const SceneSourceDocument& document)
{
    Json5Value root = Json5Value::MakeObject();
    root.LeadingComments = document.LeadingComments;
    root.TrailingComments = document.TrailingComments;

    root.Members.emplace_back("format_version", Json5Value(static_cast<double>(document.FormatVersion)));
    if (document.Abstract)
        root.Members.emplace_back("abstract", Json5Value(true));
    if (!document.Extends.empty())
        root.Members.emplace_back("extends", SceneRefToJson5(document.Extends, document.ExtendsId));
    if (!document.Includes.empty())
    {
        Json5Value includes = Json5Value::MakeArray();
        for (const SceneFragmentInclude& include : document.Includes)
        {
            Json5Value entry = Json5Value::MakeObject();
            entry.Members.emplace_back("fragment", SceneRefToJson5(include.Fragment, include.FragmentId));
            if (include.Onto.IsValid())
                entry.Members.emplace_back("onto", Json5Value(SceneIdText(include.Onto.Value)));
            includes.Elements.push_back(std::move(entry));
        }
        root.Members.emplace_back("includes", std::move(includes));
    }

    if (document.Settings.IsObject() && !document.Settings.Members.empty())
        root.Members.emplace_back("settings", document.Settings);

    Json5Value entities = Json5Value::MakeArray();
    for (const SceneSourceEntity& entity : document.Entities)
    {
        Json5Value record = Json5Value::MakeObject();
        record.LeadingComments = entity.LeadingComments;
        record.Members.emplace_back("id", Json5Value(SceneIdText(entity.Id.Value)));
        if (entity.Parent.IsValid())
            record.Members.emplace_back("parent", Json5Value(SceneIdText(entity.Parent.Value)));
        if (entity.Hidden)
            record.Members.emplace_back("hidden", Json5Value(true));
        if (entity.Locked)
            record.Members.emplace_back("locked", Json5Value(true));
        record.Members.emplace_back("components", entity.Components);
        entities.Elements.push_back(std::move(record));
    }
    root.Members.emplace_back("entities", std::move(entities));

    if (!document.InheritedPatches.empty())
    {
        Json5Value patches = Json5Value::MakeObject();
        for (const auto& [id, patch] : document.InheritedPatches)
            patches.Members.emplace_back(SceneIdText(id.Value), patch);
        root.Members.emplace_back("patch", std::move(patches));
    }
    if (!document.InheritedRemovals.empty())
    {
        Json5Value removals = Json5Value::MakeObject();
        for (const auto& [id, names] : document.InheritedRemovals)
        {
            Json5Value list = Json5Value::MakeArray();
            for (const std::string& name : names)
                list.Elements.emplace_back(std::string(name));
            removals.Members.emplace_back(SceneIdText(id.Value), std::move(list));
        }
        root.Members.emplace_back("remove", std::move(removals));
    }

    if (!document.Instances.empty())
    {
        Json5Value instances = Json5Value::MakeArray();
        for (const SceneInstanceRecord& instance : document.Instances)
        {
            Json5Value record = Json5Value::MakeObject();
            record.LeadingComments = instance.LeadingComments;
            record.Members.emplace_back("id", Json5Value(SceneIdText(instance.Id.Value)));
            if (instance.Parent.IsValid())
                record.Members.emplace_back("parent",
                    Json5Value(SceneIdText(instance.Parent.Value)));
            record.Members.emplace_back(
                "source", Json5FromJson(AssetRefToJson(AssetRef{ AssetType::Scene, instance.Source, instance.SourceId })));
            if (!instance.Name.empty())
                record.Members.emplace_back("name", Json5Value(instance.Name));
            record.Members.emplace_back("transform", TransformToJson5(instance.Placement));

            if (!instance.EntityIds.empty())
            {
                Json5Value ids = Json5Value::MakeObject();
                for (const auto& [path, minted] : instance.EntityIds)
                    ids.Members.emplace_back(path.ToString(),
                        Json5Value(SceneIdText(minted.Value)));
                record.Members.emplace_back("entity_ids", std::move(ids));
            }
            const auto writePathObject =
                [&](const char* key,
                    const std::vector<std::pair<SceneElementPath, Json5Value>>& group)
            {
                if (group.empty())
                    return;
                Json5Value object = Json5Value::MakeObject();
                for (const auto& [path, value] : group)
                    object.Members.emplace_back(path.ToString(), value);
                record.Members.emplace_back(key, std::move(object));
            };
            writePathObject("patch", instance.Patches);
            writePathObject("add", instance.AddedComponents);
            if (!instance.RemovedComponents.empty())
            {
                Json5Value removes = Json5Value::MakeObject();
                for (const auto& [path, names] : instance.RemovedComponents)
                {
                    Json5Value list = Json5Value::MakeArray();
                    for (const std::string& name : names)
                        list.Elements.emplace_back(std::string(name));
                    removes.Members.emplace_back(path.ToString(), std::move(list));
                }
                record.Members.emplace_back("remove", std::move(removes));
            }
            if (!instance.AddedEntities.empty())
            {
                Json5Value added = Json5Value::MakeArray();
                for (const SceneAddedEntity& entity : instance.AddedEntities)
                {
                    Json5Value element = Json5Value::MakeObject();
                    element.Members.emplace_back("id", Json5Value(SceneIdText(entity.Id.Value)));
                    if (!entity.ParentPath.IsEmpty())
                        element.Members.emplace_back("parent_path",
                            Json5Value(entity.ParentPath.ToString()));
                    element.Members.emplace_back("components", entity.Components);
                    added.Elements.push_back(std::move(element));
                }
                record.Members.emplace_back("add_entities", std::move(added));
            }
            if (!instance.Suppressed.empty())
            {
                Json5Value suppressed = Json5Value::MakeArray();
                for (const SceneElementPath& path : instance.Suppressed)
                    suppressed.Elements.emplace_back(path.ToString());
                record.Members.emplace_back("suppress", std::move(suppressed));
            }
            instances.Elements.push_back(std::move(record));
        }
        root.Members.emplace_back("instances", std::move(instances));
    }

    if (document.BrushMeshes.IsObject() && !document.BrushMeshes.Members.empty())
        root.Members.emplace_back("brush_meshes", document.BrushMeshes);

    for (const Json5Value::Member& member : document.UnknownRoot)
        root.Members.push_back(member);

    return Json5Write(root, document.EndComments);
}

namespace
{
    void CollectMemberNulls(const Json5Value& value, const std::string& path, std::vector<std::string>& out)
    {
        for (const Json5Value::Member& member : value.IsObject() ? value.Members : std::vector<Json5Value::Member>{})
        {
            const std::string at = path + "." + member.first;
            if (member.second.IsNull())
                out.push_back(at);
            else
                CollectMemberNulls(member.second, at, out);
        }
    }
}

std::vector<std::string> UpgradeSceneSourceFormat(SceneSourceDocument& document)
{
    std::vector<std::string> nulls;
    if (document.FormatVersion >= SceneSourceDocument::Version)
        return nulls;
    for (const SceneInstanceRecord& instance : document.Instances)
        for (const auto& [path, patch] : instance.Patches)
            CollectMemberNulls(patch, SceneIdText(instance.Id.Value) + ": " + path.ToString(), nulls);
    document.FormatVersion = SceneSourceDocument::Version;
    return nulls;
}
