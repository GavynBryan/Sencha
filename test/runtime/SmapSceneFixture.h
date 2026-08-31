#pragma once

// The shared on-disk cooked-scene fixture: writes SmapContents through the
// real writer into a unique temp .smap and registers its Scene record, so
// scene-asset, zone-scene, and spawn tests all exercise the same bytes a cook
// produces. Each test still authors its own contents -- what a scene holds is
// the thing under test; only the write-register-cleanup plumbing is shared.

#include <core/assets/AssetRegistry.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

class TempSmapScene
{
public:
    // `serializers` is the set the file is WRITTEN with, which is what lets a
    // skew test cook against a different schema than it loads with.
    TempSmapScene(AssetRegistry& registry,
                  const ComponentSerializerRegistry& serializers,
                  const SmapContents& contents,
                  std::string_view name)
    {
        std::vector<std::byte> bytes;
        SmapError error;
        EXPECT_TRUE(WriteSmap(contents, serializers, bytes, &error))
            << error.Message;

        static int counter = 0;
        File = std::filesystem::temp_directory_path()
            / ("sencha_smap_scene_" + std::string(name) + "_"
               + std::to_string(++counter) + ".smap");
        std::ofstream out(File, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));

        Path = "asset://levels/" + std::string(name) + ".smap";
        EXPECT_TRUE(registry.Register(AssetRecord{
            .Type = AssetType::Scene,
            .SourceKind = AssetSourceKind::File,
            .Path = Path,
            .FilePath = File.generic_string(),
        }));
    }

    ~TempSmapScene()
    {
        std::error_code ec;
        std::filesystem::remove(File, ec);
    }

    std::string Path;
    std::filesystem::path File;
};
