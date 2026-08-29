#include <assets/cook/SceneCookOutput.h>

#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetManifest.h>
#include <core/hash/ContentHash.h>
#include <core/identity/Id.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneFormat.h>

#include <cstdint>
#include <fstream>
#include <optional>
#include <unordered_set>
#include <vector>

namespace
{
    // The assembled scene's {version, entities, hierarchy} shape, compiled to
    // entity records: components keyed to their contract ids, the hierarchy's
    // positional relations to parent ordinals, and each entity's persistent_id
    // lifted into the record's identity field. Unknown component keys refuse
    // the cook -- an artifact this build cannot name is an artifact no runtime
    // of this build can load.
    bool CompileSceneEntities(const JsonValue& scene,
                              const ComponentSerializerRegistry& serializers,
                              SmapContents& contents,
                              std::string* error)
    {
        const JsonValue* version = scene.Find("version");
        const JsonValue* entities = scene.Find("entities");
        if (version == nullptr || !version->IsNumber()
            || static_cast<std::uint32_t>(version->AsNumber()) != SceneVersion
            || entities == nullptr || !entities->IsArray())
        {
            if (error)
                *error = "WriteCookedScene: assembled scene has an invalid "
                         "version or entity list";
            return false;
        }

        contents.Entities.reserve(entities->AsArray().size());
        for (const JsonValue& entityValue : entities->AsArray())
        {
            if (!entityValue.IsObject())
            {
                if (error)
                    *error = "WriteCookedScene: scene entity must be an object";
                return false;
            }

            SmapEntityRecord record;
            if (const JsonValue* components = entityValue.Find("components"))
            {
                if (!components->IsObject())
                {
                    if (error)
                        *error = "WriteCookedScene: scene components must be "
                                 "an object";
                    return false;
                }
                for (const auto& [key, payload] : components->AsObject())
                {
                    const IComponentSerializer* serializer =
                        serializers.FindByJsonKey(key);
                    if (serializer == nullptr)
                    {
                        if (error)
                            *error = "WriteCookedScene: scene references an "
                                     "unregistered component '" + key + "'";
                        return false;
                    }
                    record.Components.emplace_back(serializer->TypeId(), payload);

                    // Same lift and same tolerance as the JSON package
                    // builder: a malformed id string leaves the record's
                    // identity invalid here and fails the component's strict
                    // codec at import instead.
                    if (key == "persistent_id" && payload.IsObject())
                    {
                        if (const JsonValue* id = payload.Find("id");
                            id != nullptr && id->IsString())
                        {
                            if (const auto parsed =
                                    PersistentEntityIdFromString(id->AsString()))
                                record.Persistent = *parsed;
                        }
                    }
                }
            }
            contents.Entities.push_back(std::move(record));
        }

        const JsonValue* hierarchy = scene.Find("hierarchy");
        if (hierarchy != nullptr && !hierarchy->IsArray())
        {
            if (error)
                *error = "WriteCookedScene: scene hierarchy must be an array";
            return false;
        }
        if (hierarchy != nullptr)
        {
            for (const JsonValue& relation : hierarchy->AsArray())
            {
                const JsonValue* child =
                    relation.IsObject() ? relation.Find("child") : nullptr;
                const JsonValue* parent =
                    relation.IsObject() ? relation.Find("parent") : nullptr;
                if (child == nullptr || parent == nullptr
                    || !child->IsNumber() || !parent->IsNumber())
                {
                    if (error)
                        *error = "WriteCookedScene: scene hierarchy relation "
                                 "is invalid";
                    return false;
                }
                const auto childIndex =
                    static_cast<std::size_t>(child->AsNumber());
                const auto parentIndex =
                    static_cast<std::size_t>(parent->AsNumber());
                if (childIndex >= contents.Entities.size()
                    || parentIndex >= contents.Entities.size()
                    || childIndex == parentIndex
                    || contents.Entities[childIndex].Parent != UINT32_MAX)
                {
                    if (error)
                        *error = "WriteCookedScene: scene hierarchy relation "
                                 "is invalid";
                    return false;
                }
                contents.Entities[childIndex].Parent =
                    static_cast<std::uint32_t>(parentIndex);
            }
        }
        return true;
    }
} // namespace

bool WriteCookedScene(
    const JsonValue& cookedScene,
    std::span<const std::string> extraRefs,
    std::span<const SmapCollisionCell> collisionCells,
    const ComponentSerializerRegistry& serializers,
    const std::function<std::filesystem::path(std::string_view)>& physicalPathFor,
    const std::filesystem::path& idMapPath,
    const std::filesystem::path& cookedScenePath,
    std::string* error)
{
    // Scene refs first (encounter order), then the caller's extra refs, then one
    // level of .smat texture indirection. A single seen-set keeps the dependency
    // table free of duplicates while preserving first-seen order.
    std::vector<std::string> paths = CollectAssetPaths(cookedScene);
    std::unordered_set<std::string> seen(paths.begin(), paths.end());

    for (const std::string& ref : extraRefs)
    {
        if (seen.insert(ref).second)
            paths.push_back(ref);
    }

    // Walk .smat indirection over a growing list: a material pulled in by an
    // extra ref still gets its textures collected. Index-based because `paths`
    // grows inside the loop.
    for (std::size_t i = 0; i < paths.size(); ++i)
    {
        if (!paths[i].ends_with(".smat"))
            continue;

        std::string parseError;
        std::optional<JsonValue> smatJson =
            JsonParseFile(physicalPathFor(paths[i]), &parseError);
        if (!smatJson)
        {
            if (error)
                *error = "WriteCookedScene: " + parseError;
            return false;
        }

        for (std::string& ref : CollectAssetPaths(*smatJson))
        {
            if (seen.insert(ref).second)
                paths.push_back(std::move(ref));
        }
    }

    // A broken id map must never silently re-mint ids: renames would lose their
    // history. Fail; the committed map is the fix.
    AssetIdMap idMap;
    std::string idMapError;
    if (std::filesystem::exists(idMapPath)
        && !AssetIdMap::LoadFromFile(idMapPath.generic_string(), idMap, &idMapError))
    {
        if (error)
            *error = "WriteCookedScene: bad id map: " + idMapError;
        return false;
    }

    const auto pathIsLive = [&physicalPathFor](std::string_view assetPath) {
        std::error_code existsEc;
        return std::filesystem::exists(physicalPathFor(assetPath), existsEc);
    };

    SmapContents contents;
    contents.Dependencies.reserve(paths.size());
    for (const std::string& path : paths)
    {
        uint64_t contentHash = 0;
        (void)HashFileContents(physicalPathFor(path).generic_string(), contentHash);
        const AssetId id = idMap.EnsureId(path, contentHash, pathIsLive);

        // Authored scene sources (scene_instance provenance) earn stable ids
        // and stamp like every ref, but stay out of the dependency table: the
        // table is what a runtime preload warms, and a .sscene is authoring
        // data no runtime registry ever resolves.
        if (path.ends_with(".sscene"))
            continue;
        contents.Dependencies.push_back(SmapDependency{ id, path });
    }

    // Written when dirty, and also when the staged file does not exist yet:
    // the transaction registered it at Seed, and a registered-but-unwritten
    // artifact fails the commit. A scene with no asset refs otherwise leaves
    // exactly that hole.
    std::error_code idMapExistsEc;
    if ((idMap.IsDirty() || !std::filesystem::exists(idMapPath, idMapExistsEc))
        && !idMap.SaveToFile(idMapPath.generic_string()))
    {
        if (error)
            *error = "WriteCookedScene: could not write '" + idMapPath.generic_string() + "'";
        return false;
    }

    // Refs the map knows become {"id","path"} objects before compilation; refs
    // it does not know stay plain paths, so the cooked output is never less
    // resolvable than its input.
    if (!CompileSceneEntities(StampAssetRefIds(cookedScene, idMap), serializers,
                              contents, error))
        return false;

    contents.Collision.assign(collisionCells.begin(), collisionCells.end());

    std::vector<std::byte> bytes;
    SmapError smapError;
    if (!WriteSmap(contents, serializers, bytes, &smapError))
    {
        if (error)
            *error = "WriteCookedScene: " + smapError.Message;
        return false;
    }

    std::ofstream out(cookedScenePath, std::ios::binary | std::ios::trunc);
    if (out.is_open())
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    if (!out.good())
    {
        if (error)
            *error = "WriteCookedScene: could not write '"
                + cookedScenePath.generic_string() + "'";
        return false;
    }

    return true;
}
