#pragma once

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityNameComponent.h"

#include <core/logging/LoggingProvider.h>
#include <render/PointLightComponent.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>

// Prefab sources authored through real documents under a content root of the
// test's own: a door (a named brush with a named light parented under it) and
// a crate placing the door inside it, turned and moved, for nesting.
class PrefabSourceFixture
{
public:
    explicit PrefabSourceFixture(LoggingProvider& logging)
        : Logging(logging)
        , Root(std::filesystem::temp_directory_path()
               / ("sencha_prefabs_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
    {
        std::filesystem::remove_all(Root);
        std::filesystem::create_directories(Root / "props");
        WriteDoor(1.0f);

        EditorDocument crate(Logging);
        crate.SetContentRoots({ Root });
        Transform3f inner = Transform3f::Identity();
        inner.Position = Vec3d(0.5f, 0.25f, -1.0f);
        inner.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), 0.4f);
        EXPECT_TRUE(crate.PlaceSceneInstance(Door, inner).IsValid());
        const EntityId lid = crate.GetScene().CreateBrush(Vec3d(0.0f, 2.0f, 0.0f), Vec3d(0.5f, 0.1f, 0.5f));
        crate.GetScene().SetComponent(lid, EntityNameComponent{ InlineString<64>("Lid") });
        EXPECT_TRUE(crate.SaveAs((Root / "props/crate.sscene").generic_string()));
    }

    ~PrefabSourceFixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    PrefabSourceFixture(const PrefabSourceFixture&) = delete;
    PrefabSourceFixture& operator=(const PrefabSourceFixture&) = delete;

    // Rewrites the door with its body `height` tall, which changes the file.
    void WriteDoor(float height)
    {
        EditorDocument door(Logging);
        const EntityId body = door.GetScene().CreateBrush(Vec3d(0.0f, height, 0.0f), Vec3d(0.5f, height, 0.1f));
        door.GetScene().SetComponent(body, EntityNameComponent{ InlineString<64>("Body") });
        const EntityId light = door.GetScene().CreateEntity(Vec3d(0.0f, 2.0f * height, 0.3f));
        door.GetScene().SetComponent(light, EntityNameComponent{ InlineString<64>("Lamp") });
        door.GetScene().GetRegistry().Components.AddComponent(light, PointLightComponent{});
        EXPECT_TRUE(door.GetScene().SetParent(light, body));
        EXPECT_TRUE(door.SaveAs((Root / "props/door.sscene").generic_string()));
    }

    LoggingProvider& Logging;
    std::filesystem::path Root;
    const std::string Door = "asset://props/door.sscene";
    const std::string Crate = "asset://props/crate.sscene";
};
